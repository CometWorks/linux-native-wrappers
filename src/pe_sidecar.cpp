#include "pe_sidecar.h"

#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint64_t kPageSize = 4096;
constexpr uint32_t kMaxImageSize = 0x80000000U;
constexpr unsigned kMaxChainDepth = 32;

struct Range {
    uint32_t begin;
    uint32_t end;
    std::string name;
};

struct Section {
    uint32_t rva;
    uint32_t extent;
    uint32_t raw_offset;
    uint32_t raw_size;
    uint32_t flags;
    std::string name;
};

struct RuntimeFunction {
    uint32_t begin;
    uint32_t end;
    uint32_t unwind;
};

enum class OpKind { Push, Alloc, Frame, Save, Machine };

struct Operation {
    uint8_t code_offset;
    OpKind kind;
    uint32_t first;
    uint32_t second;
};

struct UnwindInfo {
    std::vector<Operation> inherited;
    std::vector<Operation> operations;
    std::vector<Operation> all;
    uint8_t prolog_size;
};

struct FunctionCfi {
    uint32_t begin;
    uint32_t end;
    std::vector<Operation> inherited;
    std::vector<Operation> operations;
};

struct Export {
    std::string name;
    uint32_t rva;
};

struct ParsedPe {
    std::vector<uint8_t> source;
    std::vector<uint8_t> image;
    std::vector<RuntimeFunction> functions;
    std::vector<FunctionCfi> cfi;
    std::vector<Export> exports;
    std::vector<Range> executable;
    uint64_t fingerprint;
};

struct Symbol {
    std::string name;
    uint64_t value;
    uint64_t size;
    unsigned char info;
    uint16_t section;
    // Excluded from the DT_HASH chains when false: the symbol stays visible to
    // debuggers and dladdr, but dlsym cannot resolve it by name. PE export
    // names must not be dlsym-able, or the .NET runtime's default DllImport
    // probing can bind P/Invokes straight to the raw Microsoft-ABI PE code,
    // bypassing the SysV wrapper stubs.
    bool lookup = true;
};

struct CfiRange {
    uint32_t begin;
    uint32_t end;
    const std::vector<Operation> *inherited;
    const std::vector<Operation> *operations;
};

uint64_t align_up(uint64_t value, uint64_t alignment)
{
    if (value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
        throw std::runtime_error("ELF layout overflow");
    return (value + alignment - 1) & ~(alignment - 1);
}

std::string hex_value(uint64_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

uint16_t u16(const std::vector<uint8_t> &data, uint64_t offset)
{
    if (offset > data.size() || data.size() - offset < 2)
        throw std::runtime_error("truncated 16-bit field at file offset " + hex_value(offset));
    return static_cast<uint16_t>(data[offset] | static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t u32(const std::vector<uint8_t> &data, uint64_t offset)
{
    if (offset > data.size() || data.size() - offset < 4)
        throw std::runtime_error("truncated 32-bit field at file offset " + hex_value(offset));
    return static_cast<uint32_t>(data[offset]) |
           static_cast<uint32_t>(data[offset + 1]) << 8 |
           static_cast<uint32_t>(data[offset + 2]) << 16 |
           static_cast<uint32_t>(data[offset + 3]) << 24;
}

std::vector<uint8_t> read_file(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        throw std::runtime_error("cannot open " + std::string(path) + ": " + std::strerror(errno));

    struct stat st {};
    if (fstat(fd, &st) < 0) {
        int saved = errno;
        close(fd);
        throw std::runtime_error("cannot stat " + std::string(path) + ": " + std::strerror(saved));
    }
    if (st.st_size < 0 || static_cast<uint64_t>(st.st_size) > std::numeric_limits<size_t>::max()) {
        close(fd);
        throw std::runtime_error("input file is too large");
    }

    std::vector<uint8_t> data(static_cast<size_t>(st.st_size));
    size_t done = 0;
    while (done < data.size()) {
        ssize_t count = read(fd, data.data() + done, data.size() - done);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            int saved = count < 0 ? errno : EIO;
            close(fd);
            throw std::runtime_error("cannot read " + std::string(path) + ": " + std::strerror(saved));
        }
        done += static_cast<size_t>(count);
    }
    if (close(fd) < 0)
        throw std::runtime_error("cannot close " + std::string(path) + ": " + std::strerror(errno));
    return data;
}

bool contained(uint32_t rva, uint64_t size, const std::vector<Range> &ranges)
{
    uint64_t end = static_cast<uint64_t>(rva) + size;
    for (const Range &range : ranges)
        if (rva >= range.begin && end <= range.end)
            return true;
    return false;
}

void require_range(uint32_t rva, uint64_t size, uint32_t image_size,
                   const std::vector<Range> &ranges, const std::string &description)
{
    if (rva > image_size || size > static_cast<uint64_t>(image_size) - rva)
        throw std::runtime_error(description + " is outside SizeOfImage");
    if (size && !contained(rva, size, ranges))
        throw std::runtime_error(description + " is outside mapped PE data");
}

ParsedPe parse_pe(const char *path)
{
    ParsedPe result;
    result.source = read_file(path);
    const std::vector<uint8_t> &data = result.source;
    if (data.size() < 0x40 || data[0] != 'M' || data[1] != 'Z')
        throw std::runtime_error("missing or truncated DOS header");

    uint32_t pe = u32(data, 0x3c);
    if (pe > data.size() || data.size() - pe < 24 || u32(data, pe) != 0x00004550)
        throw std::runtime_error("missing or truncated PE signature");
    uint64_t coff = static_cast<uint64_t>(pe) + 4;
    if (u16(data, coff) != 0x8664)
        throw std::runtime_error("PE machine is not x86-64");
    uint16_t section_count = u16(data, coff + 2);
    uint16_t optional_size = u16(data, coff + 16);
    uint64_t optional = coff + 20;
    if (!section_count || section_count > 96)
        throw std::runtime_error("invalid section count " + std::to_string(section_count));
    if (optional_size < 112 || optional > data.size() || data.size() - optional < optional_size)
        throw std::runtime_error("truncated PE optional header");
    if (u16(data, optional) != 0x20b)
        throw std::runtime_error("optional header is not PE32+");

    uint32_t image_size = u32(data, optional + 56);
    uint32_t headers_size = u32(data, optional + 60);
    if (!image_size || image_size > kMaxImageSize)
        throw std::runtime_error("invalid SizeOfImage " + hex_value(image_size));
    if (!headers_size || headers_size > data.size() || headers_size > image_size)
        throw std::runtime_error("invalid SizeOfHeaders " + hex_value(headers_size));

    uint32_t directory_count = u32(data, optional + 108);
    uint32_t present_directories = std::min(directory_count, 16U);
    if (112ULL + static_cast<uint64_t>(present_directories) * 8 > optional_size)
        throw std::runtime_error("data directories exceed the optional header");

    uint64_t section_table = optional + optional_size;
    if (section_table > data.size() ||
        static_cast<uint64_t>(section_count) * 40 > data.size() - section_table)
        throw std::runtime_error("truncated section table");

    std::vector<Section> sections;
    std::vector<Range> occupied;
    for (uint16_t index = 0; index < section_count; ++index) {
        uint64_t offset = section_table + static_cast<uint64_t>(index) * 40;
        size_t name_length = 0;
        while (name_length < 8 && data[offset + name_length])
            ++name_length;
        std::string name(reinterpret_cast<const char *>(data.data() + offset), name_length);
        uint32_t virtual_size = u32(data, offset + 8);
        uint32_t rva = u32(data, offset + 12);
        uint32_t raw_size = u32(data, offset + 16);
        uint32_t raw_offset = u32(data, offset + 20);
        uint32_t extent = std::max(virtual_size, raw_size);
        if (raw_size && (raw_offset < headers_size || raw_offset > data.size() ||
                         raw_size > data.size() - raw_offset))
            throw std::runtime_error("section '" + name + "' has out-of-file raw data");
        if (extent && (rva < headers_size || rva > image_size || extent > image_size - rva))
            throw std::runtime_error("section '" + name + "' exceeds SizeOfImage");
        sections.push_back({rva, extent, raw_offset, raw_size, u32(data, offset + 36), name});
        if (extent)
            occupied.push_back({rva, static_cast<uint32_t>(rva + extent), name});
    }
    std::sort(occupied.begin(), occupied.end(), [](const Range &left, const Range &right) {
        return left.begin < right.begin;
    });
    for (size_t index = 1; index < occupied.size(); ++index)
        if (occupied[index].begin < occupied[index - 1].end)
            throw std::runtime_error("overlapping sections '" + occupied[index - 1].name +
                                     "' and '" + occupied[index].name + "'");

    result.image.resize(image_size);
    std::copy_n(data.begin(), headers_size, result.image.begin());
    for (const Section &section : sections)
        if (section.raw_size)
            std::copy_n(data.begin() + section.raw_offset, section.raw_size,
                        result.image.begin() + section.rva);

    std::vector<Range> mapped{{0, headers_size, "headers"}};
    for (const Section &section : sections) {
        if (section.extent)
            mapped.push_back({section.rva, static_cast<uint32_t>(section.rva + section.extent), section.name});
        if (section.extent && (section.flags & 0x20000000U))
            result.executable.push_back({section.rva,
                                         static_cast<uint32_t>(section.rva + section.extent), section.name});
    }

    auto directory = [&](unsigned index) -> std::pair<uint32_t, uint32_t> {
        if (index >= present_directories)
            return {0, 0};
        uint64_t offset = optional + 112 + static_cast<uint64_t>(index) * 8;
        return {u32(data, offset), u32(data, offset + 4)};
    };

    auto exception = directory(3);
    if (!!exception.first != !!exception.second)
        throw std::runtime_error("exception directory has only one of RVA and size");
    if (exception.second % 12)
        throw std::runtime_error("exception directory size is not a multiple of 12");
    if (exception.second)
        require_range(exception.first, exception.second, image_size, mapped, "exception directory");
    for (uint64_t offset = exception.first;
         offset < static_cast<uint64_t>(exception.first) + exception.second; offset += 12) {
        uint32_t begin = u32(result.image, offset);
        uint32_t end = u32(result.image, offset + 4);
        uint32_t unwind = u32(result.image, offset + 8);
        if (!begin || begin >= end)
            throw std::runtime_error("invalid RUNTIME_FUNCTION range at RVA " + hex_value(offset));
        require_range(begin, static_cast<uint64_t>(end) - begin, image_size, result.executable,
                      "RUNTIME_FUNCTION " + hex_value(begin));
        if (unwind & 3)
            throw std::runtime_error("unaligned UNWIND_INFO RVA " + hex_value(unwind));
        require_range(unwind, 4, image_size, mapped, "UNWIND_INFO " + hex_value(unwind));
        result.functions.push_back({begin, end, unwind});
    }
    std::sort(result.functions.begin(), result.functions.end(),
              [](const RuntimeFunction &left, const RuntimeFunction &right) {
                  return left.begin < right.begin;
              });
    for (size_t index = 1; index < result.functions.size(); ++index)
        if (result.functions[index].begin < result.functions[index - 1].end)
            throw std::runtime_error("overlapping RUNTIME_FUNCTION ranges at " +
                                     hex_value(result.functions[index - 1].begin) + " and " +
                                     hex_value(result.functions[index].begin));

    static const uint8_t register_map[16] = {0, 2, 1, 3, 7, 6, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15};
    auto nonvolatile = [](uint8_t reg) {
        return reg == 3 || reg == 5 || reg == 6 || reg == 7 || reg >= 12;
    };
    std::map<uint32_t, UnwindInfo> cache;
    std::vector<uint32_t> stack;

    auto resolve_unwind = [&](auto &&self, uint32_t rva) -> UnwindInfo {
        if (std::find(stack.begin(), stack.end(), rva) != stack.end())
            throw std::runtime_error("chained UNWIND_INFO cycle at RVA " + hex_value(rva));
        if (stack.size() >= kMaxChainDepth)
            throw std::runtime_error("chained UNWIND_INFO depth exceeds 32");
        auto cached = cache.find(rva);
        if (cached != cache.end())
            return cached->second;

        require_range(rva, 4, image_size, mapped, "UNWIND_INFO " + hex_value(rva));
        uint8_t version_flags = result.image[rva];
        uint8_t prolog_size = result.image[rva + 1];
        uint8_t code_count = result.image[rva + 2];
        uint8_t frame = result.image[rva + 3];
        uint8_t version = version_flags & 7;
        uint8_t flags = version_flags >> 3;
        uint8_t frame_register = frame & 15;
        uint32_t frame_offset = static_cast<uint32_t>(frame >> 4) * 16;
        if (version != 1)
            throw std::runtime_error("unsupported UNWIND_INFO version " + std::to_string(version) +
                                     " at RVA " + hex_value(rva));
        if ((flags & ~7U) || ((flags & 4) && (flags & 3)))
            throw std::runtime_error("invalid UNWIND_INFO flags " + hex_value(flags) +
                                     " at RVA " + hex_value(rva));
        if (!frame_register && frame_offset)
            throw std::runtime_error("frame offset without frame register at RVA " + hex_value(rva));
        if (frame_register && !nonvolatile(frame_register))
            throw std::runtime_error("invalid frame register " + std::to_string(frame_register) +
                                     " at RVA " + hex_value(rva));
        uint32_t padded_count = (static_cast<uint32_t>(code_count) + 1) & ~1U;
        require_range(rva, 4 + static_cast<uint64_t>(padded_count) * 2, image_size, mapped,
                      "UNWIND_INFO codes at RVA " + hex_value(rva));

        std::vector<Operation> operations;
        unsigned slot = 0;
        while (slot < code_count) {
            uint8_t code_offset = result.image[rva + 4 + slot * 2];
            uint8_t encoded = result.image[rva + 5 + slot * 2];
            uint8_t opcode = encoded & 15;
            uint8_t info = encoded >> 4;
            if (code_offset > prolog_size || (!code_offset && !(flags & 4)))
                throw std::runtime_error("invalid unwind code offset " + std::to_string(code_offset) +
                                         " at RVA " + hex_value(rva));
            unsigned operands = 0;
            Operation operation{code_offset, OpKind::Alloc, 0, 0};
            if (opcode == 0) {
                if (!nonvolatile(info))
                    throw std::runtime_error("PUSH_NONVOL uses volatile register at RVA " + hex_value(rva));
                operation = {code_offset, OpKind::Push, register_map[info], 0};
            } else if (opcode == 1) {
                uint32_t size;
                if (info == 0) {
                    operands = 1;
                    if (slot + operands >= code_count)
                        throw std::runtime_error("truncated ALLOC_LARGE at RVA " + hex_value(rva));
                    size = u16(result.image, rva + 6 + slot * 2) * 8U;
                } else if (info == 1) {
                    operands = 2;
                    if (slot + operands >= code_count)
                        throw std::runtime_error("truncated ALLOC_LARGE at RVA " + hex_value(rva));
                    size = u32(result.image, rva + 6 + slot * 2);
                } else {
                    throw std::runtime_error("invalid ALLOC_LARGE form " + std::to_string(info) +
                                             " at RVA " + hex_value(rva));
                }
                if (!size || (size & 7))
                    throw std::runtime_error("invalid ALLOC_LARGE size " + std::to_string(size) +
                                             " at RVA " + hex_value(rva));
                operation = {code_offset, OpKind::Alloc, size, 0};
            } else if (opcode == 2) {
                operation = {code_offset, OpKind::Alloc, static_cast<uint32_t>(info) * 8 + 8, 0};
            } else if (opcode == 3) {
                if (!frame_register)
                    throw std::runtime_error("invalid SET_FPREG at RVA " + hex_value(rva));
                operation = {code_offset, OpKind::Frame, register_map[frame_register], frame_offset};
            } else if (opcode == 4 || opcode == 5) {
                if (!nonvolatile(info))
                    throw std::runtime_error("SAVE_NONVOL uses volatile register at RVA " + hex_value(rva));
                operands = opcode == 4 ? 1 : 2;
                if (slot + operands >= code_count)
                    throw std::runtime_error("truncated SAVE_NONVOL at RVA " + hex_value(rva));
                uint32_t saved = opcode == 4
                    ? static_cast<uint32_t>(u16(result.image, rva + 6 + slot * 2)) * 8
                    : u32(result.image, rva + 6 + slot * 2);
                operation = {code_offset, OpKind::Save, register_map[info], saved};
            } else if (opcode == 8 || opcode == 9) {
                if (info < 6)
                    throw std::runtime_error("SAVE_XMM128 uses volatile register at RVA " + hex_value(rva));
                operands = opcode == 8 ? 1 : 2;
                if (slot + operands >= code_count)
                    throw std::runtime_error("truncated SAVE_XMM128 at RVA " + hex_value(rva));
                uint32_t saved = opcode == 8
                    ? static_cast<uint32_t>(u16(result.image, rva + 6 + slot * 2)) * 16
                    : u32(result.image, rva + 6 + slot * 2);
                operation = {code_offset, OpKind::Save, static_cast<uint32_t>(17 + info), saved};
            } else if (opcode == 10) {
                if (info > 1)
                    throw std::runtime_error("invalid PUSH_MACHFRAME form at RVA " + hex_value(rva));
                operation = {code_offset, OpKind::Machine, static_cast<uint32_t>(40 + info * 8), 0};
            } else {
                throw std::runtime_error("unsupported unwind opcode " + std::to_string(opcode) +
                                         " at RVA " + hex_value(rva));
            }
            operations.push_back(operation);
            slot += operands + 1;
        }
        for (size_t index = 1; index < operations.size(); ++index)
            if (operations[index - 1].code_offset < operations[index].code_offset)
                throw std::runtime_error("UNWIND_CODE entries are not descending at RVA " + hex_value(rva));
        std::reverse(operations.begin(), operations.end());

        uint32_t trailer = rva + 4 + padded_count * 2;
        std::vector<Operation> inherited;
        if (flags & 4) {
            require_range(trailer, 12, image_size, mapped,
                          "chained RUNTIME_FUNCTION at RVA " + hex_value(trailer));
            uint32_t chain_begin = u32(result.image, trailer);
            uint32_t chain_end = u32(result.image, trailer + 4);
            uint32_t chain_unwind = u32(result.image, trailer + 8);
            if (!chain_begin || chain_begin >= chain_end)
                throw std::runtime_error("invalid chained RUNTIME_FUNCTION at RVA " + hex_value(trailer));
            require_range(chain_begin, static_cast<uint64_t>(chain_end) - chain_begin, image_size,
                          result.executable, "chained RUNTIME_FUNCTION");
            if (chain_unwind & 3)
                throw std::runtime_error("unaligned chained UNWIND_INFO RVA " + hex_value(chain_unwind));
            require_range(chain_unwind, 4, image_size, mapped, "chained UNWIND_INFO");
            stack.push_back(rva);
            UnwindInfo parent = self(self, chain_unwind);
            stack.pop_back();
            inherited = std::move(parent.all);
        } else if (flags & 3) {
            require_range(trailer, 4, image_size, mapped,
                          "exception-handler trailer at RVA " + hex_value(trailer));
            uint32_t handler = u32(result.image, trailer);
            require_range(handler, 1, image_size, result.executable,
                          "exception handler " + hex_value(handler));
        }

        UnwindInfo resolved;
        resolved.inherited = std::move(inherited);
        resolved.operations = std::move(operations);
        resolved.all = resolved.inherited;
        resolved.all.insert(resolved.all.end(), resolved.operations.begin(), resolved.operations.end());
        resolved.prolog_size = prolog_size;
        cache.emplace(rva, resolved);
        return resolved;
    };

    for (const RuntimeFunction &function : result.functions) {
        UnwindInfo unwind = resolve_unwind(resolve_unwind, function.unwind);
        if (unwind.prolog_size > function.end - function.begin)
            throw std::runtime_error("prolog at RVA " + hex_value(function.unwind) +
                                     " exceeds function " + hex_value(function.begin));
        result.cfi.push_back({function.begin, function.end, std::move(unwind.inherited),
                              std::move(unwind.operations)});
    }

    auto export_directory = directory(0);
    if (!!export_directory.first != !!export_directory.second)
        throw std::runtime_error("export directory has only one of RVA and size");
    if (export_directory.second) {
        require_range(export_directory.first, export_directory.second, image_size, mapped, "export directory");
        require_range(export_directory.first, 40, image_size, mapped, "export directory header");
        uint32_t function_count = u32(result.image, export_directory.first + 20);
        uint32_t name_count = u32(result.image, export_directory.first + 24);
        uint32_t functions_rva = u32(result.image, export_directory.first + 28);
        uint32_t names_rva = u32(result.image, export_directory.first + 32);
        uint32_t ordinals_rva = u32(result.image, export_directory.first + 36);
        require_range(functions_rva, static_cast<uint64_t>(function_count) * 4, image_size, mapped,
                      "export address table");
        require_range(names_rva, static_cast<uint64_t>(name_count) * 4, image_size, mapped,
                      "export name table");
        require_range(ordinals_rva, static_cast<uint64_t>(name_count) * 2, image_size, mapped,
                      "export ordinal table");
        for (uint32_t index = 0; index < name_count; ++index) {
            uint32_t name_rva = u32(result.image, names_rva + static_cast<uint64_t>(index) * 4);
            require_range(name_rva, 1, image_size, mapped, "export name");
            uint32_t containing_end = 0;
            for (const Range &range : mapped)
                if (name_rva >= range.begin && name_rva < range.end) {
                    containing_end = range.end;
                    break;
                }
            uint32_t terminator = name_rva;
            while (terminator < containing_end && result.image[terminator])
                ++terminator;
            if (terminator == containing_end)
                throw std::runtime_error("unterminated export name at RVA " + hex_value(name_rva));
            bool ascii = terminator > name_rva;
            for (uint32_t pos = name_rva; pos < terminator; ++pos)
                ascii = ascii && result.image[pos] >= 0x20 && result.image[pos] < 0x7f && result.image[pos] != '@';
            uint16_t ordinal = u16(result.image, ordinals_rva + static_cast<uint64_t>(index) * 2);
            if (ordinal >= function_count)
                throw std::runtime_error("export ordinal exceeds address table");
            uint32_t target = u32(result.image, functions_rva + static_cast<uint64_t>(ordinal) * 4);
            if (!target)
                continue;
            if (target >= export_directory.first &&
                target < static_cast<uint64_t>(export_directory.first) + export_directory.second) {
                require_range(target, 1, image_size, mapped, "export forwarder");
                continue;
            }
            if (target >= image_size)
                throw std::runtime_error("export points outside SizeOfImage");
            if (ascii && contained(target, 1, result.executable))
                result.exports.push_back({std::string(reinterpret_cast<const char *>(result.image.data() + name_rva),
                                                      terminator - name_rva), target});
        }
    }

    result.fingerprint = 0xcbf29ce484222325ULL;
    for (uint8_t byte : result.source)
        result.fingerprint = (result.fingerprint ^ byte) * 0x100000001b3ULL;
    return result;
}

void append_u32(std::vector<uint8_t> &out, uint32_t value)
{
    out.push_back(value & 0xff);
    out.push_back((value >> 8) & 0xff);
    out.push_back((value >> 16) & 0xff);
    out.push_back((value >> 24) & 0xff);
}

void set_u32(std::vector<uint8_t> &out, size_t offset, uint32_t value)
{
    for (unsigned index = 0; index < 4; ++index)
        out[offset + index] = (value >> (index * 8)) & 0xff;
}

void append_uleb(std::vector<uint8_t> &out, uint64_t value)
{
    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        out.push_back(byte | (value ? 0x80 : 0));
    } while (value);
}

void append_sleb(std::vector<uint8_t> &out, int64_t value)
{
    bool more;
    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        more = !((value == 0 && !(byte & 0x40)) || (value == -1 && (byte & 0x40)));
        out.push_back(byte | (more ? 0x80 : 0));
    } while (more);
}

int32_t signed_32(int64_t value, const char *description)
{
    if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
        throw std::runtime_error(std::string(description) + " exceeds signed PC-relative range");
    return static_cast<int32_t>(value);
}

void append_advance(std::vector<uint8_t> &out, uint32_t delta)
{
    if (!delta)
        return;
    if (delta < 64) {
        out.push_back(static_cast<uint8_t>(0x40 | delta));
    } else if (delta <= 0xff) {
        out.push_back(0x02);
        out.push_back(delta);
    } else if (delta <= 0xffff) {
        out.push_back(0x03);
        out.push_back(delta & 0xff);
        out.push_back(delta >> 8);
    } else {
        out.push_back(0x04);
        append_u32(out, delta);
    }
}

void emit_offset(std::vector<uint8_t> &out, uint32_t reg, int64_t offset)
{
    if (offset % -8)
        throw std::runtime_error("unwind save offset is not eight-byte aligned");
    out.push_back(0x11); // DW_CFA_offset_extended_sf
    append_uleb(out, reg);
    append_sleb(out, offset / -8);
}

void apply_operation(std::vector<uint8_t> &out, const Operation &operation,
                     uint64_t &depth, bool &frame, uint64_t &bias)
{
    if (operation.kind == OpKind::Push) {
        depth += 8;
        if (!frame) {
            out.push_back(0x0e); // DW_CFA_def_cfa_offset
            append_uleb(out, depth + bias);
        }
        emit_offset(out, operation.first, -static_cast<int64_t>(depth + bias));
    } else if (operation.kind == OpKind::Alloc) {
        depth += operation.first;
        if (!frame) {
            out.push_back(0x0e);
            append_uleb(out, depth + bias);
        }
    } else if (operation.kind == OpKind::Frame) {
        if (depth + bias < operation.second)
            throw std::runtime_error("frame-register CFA offset is negative");
        frame = true;
        out.push_back(0x0c); // DW_CFA_def_cfa
        append_uleb(out, operation.first);
        append_uleb(out, depth + bias - operation.second);
    } else if (operation.kind == OpKind::Save) {
        emit_offset(out, operation.first,
                    static_cast<int64_t>(operation.second) - static_cast<int64_t>(depth + bias));
    } else {
        bias = 0;
        depth += operation.first;
        if (!frame) {
            out.push_back(0x0e);
            append_uleb(out, depth);
        }
        emit_offset(out, 16, -40);
    }
}

std::vector<CfiRange> make_cfi_ranges(const ParsedPe &pe)
{
    static const std::vector<Operation> empty;
    std::vector<CfiRange> ranges;
    for (const FunctionCfi &function : pe.cfi)
        ranges.push_back({function.begin, function.end, &function.inherited, &function.operations});

    for (const Range &section : pe.executable) {
        uint32_t position = section.begin;
        for (const RuntimeFunction &function : pe.functions) {
            if (function.end <= position || function.begin >= section.end)
                continue;
            if (function.begin > position)
                ranges.push_back({position, std::min(function.begin, section.end), &empty, &empty});
            position = std::max(position, function.end);
            if (position >= section.end)
                break;
        }
        if (position < section.end)
            ranges.push_back({position, section.end, &empty, &empty});
    }
    std::sort(ranges.begin(), ranges.end(), [](const CfiRange &left, const CfiRange &right) {
        return left.begin < right.begin;
    });
    return ranges;
}

std::vector<uint8_t> build_eh_frame(uint64_t eh_frame_va, uint64_t text_va,
                                    const std::vector<CfiRange> &ranges,
                                    std::vector<uint32_t> &fde_offsets)
{
    std::vector<uint8_t> out(4);
    append_u32(out, 0); // CIE id
    out.push_back(1);
    out.insert(out.end(), {'z', 'R', 0});
    append_uleb(out, 1);
    append_sleb(out, -8);
    append_uleb(out, 16);
    append_uleb(out, 1);
    out.push_back(0x1b); // DW_EH_PE_pcrel | DW_EH_PE_sdata4
    out.push_back(0x0c); // DW_CFA_def_cfa rsp, 8
    append_uleb(out, 7);
    append_uleb(out, 8);
    out.push_back(0x90); // DW_CFA_offset rip, -8
    append_uleb(out, 1);
    while (out.size() % 8)
        out.push_back(0); // DW_CFA_nop
    set_u32(out, 0, out.size() - 4);

    for (const CfiRange &range : ranges) {
        uint32_t fde_start = static_cast<uint32_t>(out.size());
        fde_offsets.push_back(fde_start);
        out.resize(out.size() + 4);
        append_u32(out, fde_start + 4); // Distance from this field to CIE at section start.
        uint64_t location_field = eh_frame_va + out.size();
        append_u32(out, static_cast<uint32_t>(signed_32(
                            static_cast<int64_t>(text_va + range.begin) - location_field,
                            "FDE initial location")));
        append_u32(out, range.end - range.begin);
        append_uleb(out, 0); // Empty FDE augmentation data.

        uint64_t depth = 0;
        bool frame = false;
        uint64_t bias = 8;
        for (const Operation &operation : *range.inherited)
            apply_operation(out, operation, depth, frame, bias);
        uint32_t position = 0;
        size_t index = 0;
        while (index < range.operations->size()) {
            uint32_t code_offset = (*range.operations)[index].code_offset;
            append_advance(out, code_offset - position);
            position = code_offset;
            while (index < range.operations->size() &&
                   (*range.operations)[index].code_offset == code_offset)
                apply_operation(out, (*range.operations)[index++], depth, frame, bias);
        }
        while (out.size() % 8)
            out.push_back(0);
        set_u32(out, fde_start, out.size() - fde_start - 4);
    }
    append_u32(out, 0);
    return out;
}

std::vector<uint8_t> build_eh_frame_hdr(uint64_t header_va, uint64_t eh_frame_va,
                                        uint64_t text_va, const std::vector<CfiRange> &ranges,
                                        const std::vector<uint32_t> &fde_offsets)
{
    std::vector<uint8_t> out{1, 0x1b, 0x03, 0x3b};
    append_u32(out, static_cast<uint32_t>(signed_32(
                        static_cast<int64_t>(eh_frame_va) - static_cast<int64_t>(header_va + 4),
                        ".eh_frame pointer")));
    append_u32(out, ranges.size());
    for (size_t index = 0; index < ranges.size(); ++index) {
        append_u32(out, static_cast<uint32_t>(signed_32(
                            static_cast<int64_t>(text_va + ranges[index].begin) -
                                static_cast<int64_t>(header_va),
                            ".eh_frame_hdr function address")));
        append_u32(out, static_cast<uint32_t>(signed_32(
                            static_cast<int64_t>(eh_frame_va + fde_offsets[index]) -
                                static_cast<int64_t>(header_va),
                            ".eh_frame_hdr FDE address")));
    }
    return out;
}

uint32_t elf_hash(const std::string &name)
{
    uint32_t hash = 0;
    for (unsigned char byte : name) {
        hash = (hash << 4) + byte;
        uint32_t high = hash & 0xf0000000U;
        if (high)
            hash ^= high >> 24;
        hash &= ~high;
    }
    return hash;
}

template <typename T>
void copy_object(std::vector<uint8_t> &output, uint64_t offset, const T &object)
{
    if (offset > output.size() || sizeof(object) > output.size() - offset)
        throw std::runtime_error("internal ELF layout error");
    std::memcpy(output.data() + offset, &object, sizeof(object));
}

void copy_bytes(std::vector<uint8_t> &output, uint64_t offset, const std::vector<uint8_t> &bytes)
{
    if (offset > output.size() || bytes.size() > output.size() - offset)
        throw std::runtime_error("internal ELF layout error");
    std::copy(bytes.begin(), bytes.end(), output.begin() + offset);
}

std::vector<uint8_t> build_elf(const ParsedPe &pe, const char *path)
{
    enum SectionIndex : uint16_t {
        Null, Text, Tail, Note, Dynsym, Dynstr, Hash, Rodata, EhFrameHdr, EhFrame, Dynamic, Shstrtab, Count
    };
    constexpr uint16_t phnum = 10;
    constexpr uint64_t text_offset = kPageSize;
    constexpr uint64_t text_va = 0;
    uint64_t text_end = text_offset + pe.image.size();
    // Keep the raw loader's writable zero page after SizeOfImage.
    uint64_t metadata_start = align_up(text_end, kPageSize) + kPageSize;
    uint64_t header_va = metadata_start - kPageSize;

    std::string dll_name = path;
    size_t slash = dll_name.find_last_of('/');
    if (slash != std::string::npos)
        dll_name.erase(0, slash + 1);
    for (char &character : dll_name)
        if (!((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '_' || character == '.' ||
              character == '-'))
            character = '_';
    if (dll_name.empty())
        dll_name = "pe";

    std::map<uint32_t, uint64_t> function_sizes;
    for (const RuntimeFunction &function : pe.functions)
        function_sizes[function.begin] = function.end - function.begin;
    std::set<std::string> used{"__lnw_pe_image_start", "__lnw_pe_image_end",
                               "__lnw_pe_source_size", "__lnw_pe_source_fingerprint",
                               "__lnw_sidecar_version"};
    std::vector<Symbol> symbols;
    symbols.push_back({"__lnw_pe_image_start", text_va, pe.image.size(),
                       ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), Text});
    symbols.push_back({"__lnw_pe_image_end", text_va + pe.image.size(), 0,
                       ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), Text});
    symbols.push_back({"__lnw_pe_source_size", 0, 8,
                       ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), Rodata});
    symbols.push_back({"__lnw_pe_source_fingerprint", 0, 8,
                       ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), Rodata});
    symbols.push_back({"__lnw_sidecar_version", 0, 8,
                       ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), Rodata});
    for (const Export &exported : pe.exports) {
        if (used.insert(exported.name).second) {
            symbols.push_back({exported.name, text_va + exported.rva,
                               function_sizes.count(exported.rva) ? function_sizes[exported.rva] : 0,
                               ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), Text, false});
        }
    }
    for (const RuntimeFunction &function : pe.functions) {
        std::string name = dll_name + "!sub_";
        std::ostringstream rva;
        rva << std::hex << function.begin;
        name += rva.str();
        if (used.insert(name).second)
            symbols.push_back({name, text_va + function.begin, function.end - function.begin,
                               ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), Text});
    }

    std::vector<CfiRange> cfi_ranges = make_cfi_ranges(pe);
    // ponytail: PE v1 epilogs have no scopes; add an instruction recognizer only if body-only CFI is insufficient.

    std::vector<uint8_t> dynstr{0};
    std::vector<uint32_t> name_offsets;
    for (const Symbol &symbol : symbols) {
        name_offsets.push_back(dynstr.size());
        dynstr.insert(dynstr.end(), symbol.name.begin(), symbol.name.end());
        dynstr.push_back(0);
    }

    uint64_t dynsym_offset = align_up(metadata_start, 8);
    uint64_t dynsym_size = static_cast<uint64_t>(symbols.size() + 1) * sizeof(Elf64_Sym);
    uint64_t dynstr_offset = dynsym_offset + dynsym_size;
    uint64_t hash_offset = align_up(dynstr_offset + dynstr.size(), 4);
    uint32_t chain_count = symbols.size() + 1;
    uint32_t bucket_count = std::max(1U, chain_count / 4);
    uint64_t hash_size = static_cast<uint64_t>(2 + bucket_count + chain_count) * 4;
    uint64_t rodata_offset = align_up(hash_offset + hash_size, 8);
    symbols[2].value = rodata_offset;
    symbols[3].value = rodata_offset + 8;
    symbols[4].value = rodata_offset + 16;
    uint64_t eh_frame_hdr_offset = align_up(rodata_offset + 24, 4);
    uint64_t eh_frame_hdr_size = 12 + static_cast<uint64_t>(cfi_ranges.size()) * 8;
    uint64_t eh_frame_offset = align_up(eh_frame_hdr_offset + eh_frame_hdr_size, 8);

    std::vector<uint32_t> fde_offsets;
    std::vector<uint8_t> eh_frame = build_eh_frame(eh_frame_offset, text_va, cfi_ranges, fde_offsets);
    std::vector<uint8_t> eh_frame_hdr = build_eh_frame_hdr(
        eh_frame_hdr_offset, eh_frame_offset, text_va, cfi_ranges, fde_offsets);
    uint64_t metadata_end = eh_frame_offset + eh_frame.size();
    uint64_t dynamic_offset = align_up(metadata_end, kPageSize);
    constexpr size_t dynamic_count = 6;
    uint64_t dynamic_size = dynamic_count * sizeof(Elf64_Dyn);

    std::vector<uint8_t> shstr{0};
    const char *section_names[Count] = {"", ".text", ".data", ".note.gnu.build-id", ".dynsym", ".dynstr",
                                         ".hash", ".rodata", ".eh_frame_hdr", ".eh_frame",
                                         ".dynamic", ".shstrtab"};
    uint32_t shname[Count]{};
    for (unsigned index = 1; index < Count; ++index) {
        shname[index] = shstr.size();
        shstr.insert(shstr.end(), section_names[index], section_names[index] + std::strlen(section_names[index]) + 1);
    }
    uint64_t shstr_offset = dynamic_offset + dynamic_size;
    uint64_t section_header_offset = align_up(shstr_offset + shstr.size(), 8);
    uint64_t file_size = section_header_offset + Count * sizeof(Elf64_Shdr);
    if (file_size > std::numeric_limits<size_t>::max())
        throw std::runtime_error("ELF output is too large");
    std::vector<uint8_t> output(static_cast<size_t>(file_size));

    uint64_t note_offset = align_up(sizeof(Elf64_Ehdr) + phnum * sizeof(Elf64_Phdr), 4);
    std::vector<uint8_t> note;
    append_u32(note, 4);
    append_u32(note, 20);
    append_u32(note, NT_GNU_BUILD_ID);
    note.insert(note.end(), {'G', 'N', 'U', 0});
    for (unsigned index = 0; index < 8; ++index)
        note.push_back((pe.fingerprint >> (index * 8)) & 0xff);
    uint64_t source_size = pe.source.size();
    for (unsigned index = 0; index < 8; ++index)
        note.push_back((source_size >> (index * 8)) & 0xff);
    uint32_t sidecar_version = PE_SIDECAR_VERSION;
    for (unsigned index = 0; index < 4; ++index)
        note.push_back((sidecar_version >> (index * 8)) & 0xff);
    if (note_offset + note.size() > text_offset)
        throw std::runtime_error("ELF headers exceed first page");

    Elf64_Ehdr ehdr{};
    std::memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_ident[EI_OSABI] = ELFOSABI_SYSV;
    ehdr.e_type = ET_DYN;
    ehdr.e_machine = EM_X86_64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_shoff = section_header_offset;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = phnum;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = Count;
    ehdr.e_shstrndx = Shstrtab;
    copy_object(output, 0, ehdr);

    std::vector<Elf64_Phdr> phdrs(phnum);
    phdrs[0] = {PT_PHDR, PF_R, sizeof(Elf64_Ehdr), header_va + sizeof(Elf64_Ehdr),
                header_va + sizeof(Elf64_Ehdr), phnum * sizeof(Elf64_Phdr),
                phnum * sizeof(Elf64_Phdr), 8};
    phdrs[1] = {PT_LOAD, PF_R | PF_X, text_offset, text_va, text_va,
                 pe.image.size(), pe.image.size(), kPageSize};
    phdrs[2] = {PT_LOAD, PF_R | PF_W, align_up(text_end, kPageSize),
                header_va - kPageSize, header_va - kPageSize, kPageSize, kPageSize, kPageSize};
    phdrs[3] = {PT_LOAD, PF_R, 0, header_va, header_va, text_offset, text_offset, kPageSize};
    phdrs[4] = {PT_LOAD, PF_R, metadata_start, metadata_start, metadata_start,
                 metadata_end - metadata_start, metadata_end - metadata_start, kPageSize};
    phdrs[5] = {PT_LOAD, PF_R | PF_W, dynamic_offset, dynamic_offset, dynamic_offset,
                 dynamic_size, dynamic_size, kPageSize};
    phdrs[6] = {PT_DYNAMIC, PF_R | PF_W, dynamic_offset, dynamic_offset, dynamic_offset,
                 dynamic_size, dynamic_size, 8};
    phdrs[7] = {PT_NOTE, PF_R, note_offset, header_va + note_offset, header_va + note_offset,
                 note.size(), note.size(), 4};
    phdrs[8] = {PT_GNU_EH_FRAME, PF_R, eh_frame_hdr_offset, eh_frame_hdr_offset,
                 eh_frame_hdr_offset, eh_frame_hdr.size(), eh_frame_hdr.size(), 4};
    phdrs[9] = {PT_GNU_STACK, PF_R | PF_W, 0, 0, 0, 0, 0, 16};
    for (size_t index = 0; index < phdrs.size(); ++index)
        copy_object(output, sizeof(Elf64_Ehdr) + index * sizeof(Elf64_Phdr), phdrs[index]);

    copy_bytes(output, note_offset, note);
    copy_bytes(output, text_offset, pe.image);
    copy_bytes(output, dynstr_offset, dynstr);

    Elf64_Sym null_symbol{};
    copy_object(output, dynsym_offset, null_symbol);
    for (size_t index = 0; index < symbols.size(); ++index) {
        Elf64_Sym symbol{};
        symbol.st_name = name_offsets[index];
        symbol.st_info = symbols[index].info;
        symbol.st_other = STV_DEFAULT;
        symbol.st_shndx = symbols[index].section;
        symbol.st_value = symbols[index].value;
        symbol.st_size = symbols[index].size;
        copy_object(output, dynsym_offset + (index + 1) * sizeof(Elf64_Sym), symbol);
    }

    std::vector<uint32_t> buckets(bucket_count);
    std::vector<uint32_t> chains(chain_count);
    for (uint32_t index = 1; index < chain_count; ++index) {
        if (!symbols[index - 1].lookup)
            continue;
        uint32_t bucket = elf_hash(symbols[index - 1].name) % bucket_count;
        if (!buckets[bucket]) {
            buckets[bucket] = index;
        } else {
            uint32_t current = buckets[bucket];
            while (chains[current])
                current = chains[current];
            chains[current] = index;
        }
    }
    std::vector<uint8_t> hash;
    append_u32(hash, bucket_count);
    append_u32(hash, chain_count);
    for (uint32_t value : buckets)
        append_u32(hash, value);
    for (uint32_t value : chains)
        append_u32(hash, value);
    copy_bytes(output, hash_offset, hash);

    std::memcpy(output.data() + rodata_offset, &source_size, sizeof(source_size));
    std::memcpy(output.data() + rodata_offset + 8, &pe.fingerprint, sizeof(pe.fingerprint));
    std::memcpy(output.data() + rodata_offset + 16, &PE_SIDECAR_VERSION, sizeof(PE_SIDECAR_VERSION));
    copy_bytes(output, eh_frame_hdr_offset, eh_frame_hdr);
    copy_bytes(output, eh_frame_offset, eh_frame);

    Elf64_Dyn dynamic_entries[dynamic_count]{};
    dynamic_entries[0].d_tag = DT_HASH;
    dynamic_entries[0].d_un.d_ptr = hash_offset;
    dynamic_entries[1].d_tag = DT_STRTAB;
    dynamic_entries[1].d_un.d_ptr = dynstr_offset;
    dynamic_entries[2].d_tag = DT_SYMTAB;
    dynamic_entries[2].d_un.d_ptr = dynsym_offset;
    dynamic_entries[3].d_tag = DT_STRSZ;
    dynamic_entries[3].d_un.d_val = dynstr.size();
    dynamic_entries[4].d_tag = DT_SYMENT;
    dynamic_entries[4].d_un.d_val = sizeof(Elf64_Sym);
    dynamic_entries[5].d_tag = DT_NULL;
    std::memcpy(output.data() + dynamic_offset, dynamic_entries, sizeof(dynamic_entries));

    copy_bytes(output, shstr_offset, shstr);
    Elf64_Shdr sections[Count]{};
    sections[Text] = {shname[Text], SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, text_va,
                       text_offset, pe.image.size(), 0, 0, kPageSize, 0};
    sections[Tail] = {shname[Tail], SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, header_va - kPageSize,
                      align_up(text_end, kPageSize), kPageSize, 0, 0, kPageSize, 0};
    sections[Note] = {shname[Note], SHT_NOTE, SHF_ALLOC, header_va + note_offset, note_offset,
                       note.size(), 0, 0, 4, 0};
    sections[Dynsym] = {shname[Dynsym], SHT_DYNSYM, SHF_ALLOC, dynsym_offset, dynsym_offset,
                        dynsym_size, Dynstr, 1, 8, sizeof(Elf64_Sym)};
    sections[Dynstr] = {shname[Dynstr], SHT_STRTAB, SHF_ALLOC, dynstr_offset, dynstr_offset,
                        dynstr.size(), 0, 0, 1, 0};
    sections[Hash] = {shname[Hash], SHT_HASH, SHF_ALLOC, hash_offset, hash_offset,
                      hash.size(), Dynsym, 0, 4, 4};
    sections[Rodata] = {shname[Rodata], SHT_PROGBITS, SHF_ALLOC, rodata_offset, rodata_offset,
                         24, 0, 0, 8, 0};
    sections[EhFrameHdr] = {shname[EhFrameHdr], SHT_PROGBITS, SHF_ALLOC, eh_frame_hdr_offset,
                            eh_frame_hdr_offset, eh_frame_hdr.size(), 0, 0, 4, 0};
    sections[EhFrame] = {shname[EhFrame], SHT_PROGBITS, SHF_ALLOC, eh_frame_offset,
                         eh_frame_offset, eh_frame.size(), 0, 0, 8, 0};
    sections[Dynamic] = {shname[Dynamic], SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE, dynamic_offset,
                         dynamic_offset, dynamic_size, Dynstr, 0, 8, sizeof(Elf64_Dyn)};
    sections[Shstrtab] = {shname[Shstrtab], SHT_STRTAB, 0, 0, shstr_offset,
                          shstr.size(), 0, 0, 1, 0};
    std::memcpy(output.data() + section_header_offset, sections, sizeof(sections));
    return output;
}

void write_output(int fd, const std::vector<uint8_t> &output)
{
    if (ftruncate(fd, 0) < 0)
        throw std::runtime_error("cannot truncate output: " + std::string(std::strerror(errno)));
    if (lseek(fd, 0, SEEK_SET) < 0)
        throw std::runtime_error("cannot seek output: " + std::string(std::strerror(errno)));
    size_t done = 0;
    while (done < output.size()) {
        ssize_t count = write(fd, output.data() + done, output.size() - done);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            throw std::runtime_error("cannot write output: " + std::string(std::strerror(count < 0 ? errno : EIO)));
        done += static_cast<size_t>(count);
    }
}

} // namespace

bool generate_pe_sidecar(const char *pe_path, int output_fd, std::string *error)
{
    try {
        const uint16_t endian = 1;
        if (!pe_path || !*pe_path)
            throw std::runtime_error("PE path is empty");
        if (output_fd < 0)
            throw std::runtime_error("output file descriptor is invalid");
        if (*reinterpret_cast<const uint8_t *>(&endian) != 1)
            throw std::runtime_error("only little-endian hosts are supported");
        struct stat input_stat {}, output_stat {};
        if (stat(pe_path, &input_stat) < 0)
            throw std::runtime_error("cannot stat " + std::string(pe_path) + ": " + std::strerror(errno));
        if (fstat(output_fd, &output_stat) < 0)
            throw std::runtime_error("cannot stat output: " + std::string(std::strerror(errno)));
        if (input_stat.st_dev == output_stat.st_dev && input_stat.st_ino == output_stat.st_ino)
            throw std::runtime_error("input and output must be different files");
        ParsedPe pe = parse_pe(pe_path);
        write_output(output_fd, build_elf(pe, pe_path));
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &exception) {
        if (error)
            *error = exception.what();
        return false;
    } catch (...) {
        if (error)
            *error = "unknown sidecar generation error";
        return false;
    }
}
