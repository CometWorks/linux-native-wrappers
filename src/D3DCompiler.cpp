#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include "dll_loader.h"

static void EnsureThreadInfo()
{
    if (!setup_nt_threadinfo(nullptr)) {
        fprintf(stderr, "D3DCompiler: Failed to initialize thread info\n");
        std::abort();
    }
    pe_ensure_tls_for_loaded_images();
}

static pe_image g_d3dcompiler_image;

// D3D_SHADER_MACRO layout (Windows): two pointers (Name, Definition), null-terminated array
struct D3D_SHADER_MACRO {
    const char *Name;
    const char *Definition;
};

// Windows x64 ABI function pointer for D3DCompile
typedef WINAPI int32_t (*pfnD3DCompile)(
    const void *pSrcData, uint64_t SrcDataSize,
    const char *pSourceName, const D3D_SHADER_MACRO *pDefines, void *pInclude,
    const char *pEntrypoint, const char *pTarget,
    uint32_t Flags1, uint32_t Flags2,
    void **ppCode, void **ppErrorMsgs);

// Windows x64 ABI function pointer for D3DPreprocess
typedef WINAPI int32_t (*pfnD3DPreprocess)(
    const void *pSrcData, uint64_t SrcDataSize,
    const char *pSourceName, const D3D_SHADER_MACRO *pDefines, void *pInclude,
    void **ppCodeText, void **ppErrorMsgs);

// Windows x64 ABI function pointer for D3DStripShader
typedef WINAPI int32_t (*pfnD3DStripShader)(
    const void *pShaderBytecode, uint64_t BytecodeLength,
    uint32_t uStripFlags, void **ppStrippedBlob);

// ID3DBlob method types (Windows x64 ABI - first arg is 'this')
typedef WINAPI void* (*pfnBlobGetBufferPointer)(void *pThis);
typedef WINAPI uint64_t (*pfnBlobGetBufferSize)(void *pThis);
typedef WINAPI uint32_t (*pfnBlobRelease)(void *pThis);

static pfnD3DCompile s_D3DCompile = nullptr;
static pfnD3DPreprocess s_D3DPreprocess = nullptr;
static pfnD3DStripShader s_D3DStripShader = nullptr;
// The PE-loaded compiler corrupts its state when these entry points overlap.
static std::mutex s_compilerMutex;

// Helper: read vtable slot from a COM object
static inline void* vtable_slot(void *obj, int index)
{
    void **vtable = *(void***)obj;
    return vtable[index];
}

// ---------------------------------------------------------------------------
// ID3DInclude handler with Space Engineers' MyIncludeProcessor semantics:
// local includes resolve against the including file's directory (falling back
// to the root shader file's directory), system includes search the configured
// include directories from last to first. Lookups are case-insensitive to
// match Windows filesystem behavior.
// ---------------------------------------------------------------------------

static const int32_t SE_S_OK = 0;
static const int32_t SE_E_FAIL = (int32_t)0x80004005;

// ID3DInclude vtable entries use the Windows x64 calling convention because
// they are invoked directly by the PE-loaded d3dcompiler_47.dll.
typedef WINAPI int32_t (*pfnIncludeOpen)(
    void *self, uint32_t includeType, const char *fileName,
    const void *parentData, const void **ppData, uint32_t *pBytes);
typedef WINAPI int32_t (*pfnIncludeClose)(void *self, const void *pData);

struct SEIncludeVtbl {
    pfnIncludeOpen Open;
    pfnIncludeClose Close;
};

struct SEIncludeHandler {
    const SEIncludeVtbl *vtbl; // must stay first: this object is the ID3DInclude*
    std::string basePath;
    std::vector<std::string> includeDirs;
    std::mutex mutex;
    // Maps a buffer returned from Open to the directory of the file it holds,
    // so nested local includes resolve against their including file.
    std::unordered_map<const void *, std::string> openDirs;
};

static bool is_regular_file(const std::string &path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool path_exists(const std::string &path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

static std::string parent_dir(const std::string &path)
{
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return std::string();
    if (slash == 0)
        return "/";
    return path.substr(0, slash);
}

// Resolves relPath below baseDir, matching each segment case-insensitively
// when the exact-case path does not exist. Returns true only for a regular
// file.
static bool resolve_path_ci(const std::string &baseDir, const std::string &relPath,
                            std::string &out)
{
    if (baseDir.empty())
        return false;

    std::string direct = baseDir;
    if (direct.back() != '/')
        direct += '/';
    direct += relPath;
    if (is_regular_file(direct)) {
        out = direct;
        return true;
    }

    std::string current = baseDir;
    while (!current.empty() && current.back() == '/' && current.size() > 1)
        current.pop_back();

    size_t start = 0;
    while (start <= relPath.size()) {
        size_t end = relPath.find('/', start);
        size_t len = (end == std::string::npos ? relPath.size() : end) - start;
        std::string seg = relPath.substr(start, len);

        if (seg.empty() || seg == ".") {
            // skip
        } else if (seg == "..") {
            current = parent_dir(current);
            if (current.empty())
                return false;
        } else {
            std::string candidate = current + "/" + seg;
            if (path_exists(candidate)) {
                current = candidate;
            } else {
                DIR *dir = opendir(current.c_str());
                if (!dir)
                    return false;
                bool found = false;
                while (struct dirent *entry = readdir(dir)) {
                    if (strcasecmp(entry->d_name, seg.c_str()) == 0) {
                        current += "/";
                        current += entry->d_name;
                        found = true;
                        break;
                    }
                }
                closedir(dir);
                if (!found)
                    return false;
            }
        }

        if (end == std::string::npos)
            break;
        start = end + 1;
    }

    if (!is_regular_file(current))
        return false;
    out = current;
    return true;
}

// Reads the whole file into a malloc'd buffer. Zero-length files get a
// one-byte allocation so the buffer pointer stays a unique map key.
static bool read_file_contents(const std::string &path, void **data, uint32_t *size)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (!file)
        return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long length = ftell(file);
    if (length < 0) {
        fclose(file);
        return false;
    }
    rewind(file);
    void *buffer = malloc(length > 0 ? (size_t)length : 1);
    if (!buffer) {
        fclose(file);
        return false;
    }
    if (length > 0 && fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return false;
    }
    fclose(file);
    *data = buffer;
    *size = (uint32_t)length;
    return true;
}

static WINAPI int32_t IncludeOpen(
    void *self, uint32_t includeType, const char *fileName,
    const void *parentData, const void **ppData, uint32_t *pBytes)
{
    auto *handler = (SEIncludeHandler *)self;
    if (!fileName || !ppData || !pBytes)
        return SE_E_FAIL;

    std::string name = fileName;
    for (char &c : name) {
        if (c == '\\')
            c = '/';
    }

    std::string resolved;
    bool found = false;

    // D3D_INCLUDE_LOCAL == 0 ("file"), D3D_INCLUDE_SYSTEM == 1 (<file>)
    if (includeType == 0) {
        std::string parentDir;
        {
            std::lock_guard<std::mutex> lock(handler->mutex);
            auto it = handler->openDirs.find(parentData);
            parentDir = it != handler->openDirs.end() ? it->second : handler->basePath;
        }
        if (!parentDir.empty())
            found = resolve_path_ci(parentDir, name, resolved);
        if (!found && !handler->basePath.empty()) {
            // Vanilla MyIncludeProcessor fails local includes here instead of
            // falling back to the include directories.
            fprintf(stderr, "D3DCompiler: Include not found: %s (local to %s)\n",
                    fileName, parentDir.c_str());
            return SE_E_FAIL;
        }
    }

    if (!found) {
        for (size_t i = handler->includeDirs.size(); i-- > 0;) {
            if (resolve_path_ci(handler->includeDirs[i], name, resolved)) {
                found = true;
                break;
            }
        }
    }

    if (!found) {
        fprintf(stderr, "D3DCompiler: Include not found: %s\n", fileName);
        return SE_E_FAIL;
    }

    void *data = nullptr;
    uint32_t size = 0;
    if (!read_file_contents(resolved, &data, &size)) {
        fprintf(stderr, "D3DCompiler: Cannot read include file: %s\n", resolved.c_str());
        return SE_E_FAIL;
    }

    {
        std::lock_guard<std::mutex> lock(handler->mutex);
        handler->openDirs[data] = parent_dir(resolved);
    }

    *ppData = data;
    *pBytes = size;
    return SE_S_OK;
}

static WINAPI int32_t IncludeClose(void *self, const void *pData)
{
    auto *handler = (SEIncludeHandler *)self;
    if (pData) {
        {
            std::lock_guard<std::mutex> lock(handler->mutex);
            handler->openDirs.erase(pData);
        }
        free((void *)pData);
    }
    return SE_S_OK;
}

static const SEIncludeVtbl g_includeVtbl = { IncludeOpen, IncludeClose };

extern "C" {

void Init(const char* dllPath, const char* sidecarPath)
{
    if (g_d3dcompiler_image.image) {
        fprintf(stderr,
                "[LinuxCompat] D3DCompiler::Init: already initialized (image=%p, dllPath='%s'); "
                "ignoring duplicate call.\n",
                g_d3dcompiler_image.image, dllPath ? dllPath : "<null>");
        return;
    }

    if (!load_dll(&g_d3dcompiler_image, dllPath, sidecarPath)) {
        fprintf(stderr, "D3DCompiler: Failed to load %s\n", dllPath);
        throw std::runtime_error("Failed to load d3dcompiler_47.dll");
    }

    s_D3DCompile = (pfnD3DCompile)get_export("D3DCompile");

    if (!s_D3DCompile) {
        fprintf(stderr, "D3DCompiler: D3DCompile export not found\n");
        throw std::runtime_error("D3DCompile export not found");
    }

    s_D3DPreprocess = (pfnD3DPreprocess)get_export("D3DPreprocess");

    if (!s_D3DPreprocess) {
        fprintf(stderr, "D3DCompiler: D3DPreprocess export not found\n");
        throw std::runtime_error("D3DPreprocess export not found");
    }

    s_D3DStripShader = (pfnD3DStripShader)get_export("D3DStripShader");

    if (!s_D3DStripShader) {
        // Not fatal: only optimized tool compiles strip their bytecode.
        fprintf(stderr, "D3DCompiler: D3DStripShader export not found\n");
    }
}

void* SE_CreateIncludeHandler(const char *basePath, const char *includeDir)
{
    auto *handler = new SEIncludeHandler();
    handler->vtbl = &g_includeVtbl;
    if (basePath)
        handler->basePath = basePath;
    if (includeDir)
        handler->includeDirs.push_back(includeDir);
    return handler;
}

void SE_DestroyIncludeHandler(void *pInclude)
{
    auto *handler = (SEIncludeHandler *)pInclude;
    if (!handler)
        return;
    for (auto &entry : handler->openDirs)
        free((void *)entry.first);
    delete handler;
}

int32_t SE_D3DPreprocess(
    const void *pSrcData, uint64_t SrcDataSize,
    const char *pSourceName, const D3D_SHADER_MACRO *pDefines, void *pInclude,
    void **ppCodeText, void **ppErrorMsgs)
{
    EnsureThreadInfo();
    if (!s_D3DPreprocess) {
        fprintf(stderr, "D3DCompiler: D3DPreprocess not initialized\n");
        return -1;
    }
    std::lock_guard<std::mutex> lock(s_compilerMutex);
    return s_D3DPreprocess(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude,
                           ppCodeText, ppErrorMsgs);
}

int32_t SE_D3DStripShader(
    const void *pShaderBytecode, uint64_t BytecodeLength,
    uint32_t uStripFlags, void **ppStrippedBlob)
{
    EnsureThreadInfo();
    if (!s_D3DStripShader) {
        fprintf(stderr, "D3DCompiler: D3DStripShader not initialized\n");
        return -1;
    }
    std::lock_guard<std::mutex> lock(s_compilerMutex);
    return s_D3DStripShader(pShaderBytecode, BytecodeLength, uStripFlags, ppStrippedBlob);
}

int32_t SE_D3DCompile(
    const void *pSrcData, uint64_t SrcDataSize,
    const char *pSourceName, const D3D_SHADER_MACRO *pDefines, void *pInclude,
    const char *pEntrypoint, const char *pTarget,
    uint32_t Flags1, uint32_t Flags2,
    void **ppCode, void **ppErrorMsgs)
{
    EnsureThreadInfo();
    if (!s_D3DCompile) {
        fprintf(stderr, "D3DCompiler: D3DCompile not initialized\n");
        return -1;
    }
    std::lock_guard<std::mutex> lock(s_compilerMutex);
    return s_D3DCompile(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude,
                        pEntrypoint, pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);
}

void* SE_BlobGetBufferPointer(void *blob)
{
    EnsureThreadInfo();
    auto fn = (pfnBlobGetBufferPointer)vtable_slot(blob, 3);
    return fn(blob);
}

uint64_t SE_BlobGetBufferSize(void *blob)
{
    EnsureThreadInfo();
    auto fn = (pfnBlobGetBufferSize)vtable_slot(blob, 4);
    return fn(blob);
}

uint32_t SE_BlobRelease(void *blob)
{
    EnsureThreadInfo();
    auto fn = (pfnBlobRelease)vtable_slot(blob, 2);
    return fn(blob);
}

} // extern "C"
