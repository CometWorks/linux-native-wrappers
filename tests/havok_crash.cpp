#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

extern "C" void Init(const char *dll_path, const char *sidecar_path);
struct Vector3 { float X, Y, Z; };
extern "C" void HkBaseSystem_Init(int solver_memory_size, void *log, bool deep_profiling);
extern "C" void *HkBoxShape_Create(Vector3 half_extents);
extern "C" unsigned HkShape_CastRayCollectSingleHit(void *shape, Vector3 from, Vector3 to);

static bool sidecar_has_compatible_tail()
{
    void *handle = dlopen("./Havok.dll", RTLD_NOW | RTLD_NOLOAD);
    auto *end = handle ? static_cast<unsigned char *>(dlsym(handle, "__lnw_pe_image_end")) : nullptr;
    long page_size = sysconf(_SC_PAGESIZE);
    bool compatible = end && page_size > 0;
    for (long i = 0; compatible && i < page_size; ++i)
        compatible = end[i] == 0;
    if (compatible) {
        end[page_size - 1] = 1;
        compatible = end[page_size - 1] == 1;
        end[page_size - 1] = 0;
    }
    if (handle)
        dlclose(handle);
    return compatible;
}

static void print_backtrace(int)
{
    void *frames[32];
    int count = backtrace(frames, 32);
    char **symbols = backtrace_symbols(frames, count);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);

    int pe_frames = 0;
    bool wrapper_frame = false;
    bool exact_pe_offsets = true;
    for (int i = 0; symbols && i < count; ++i) {
        if (std::strstr(symbols[i], "Havok.dll")) {
            ++pe_frames;
            Dl_info info{};
            exact_pe_offsets &= dladdr(frames[i], &info) && info.dli_fbase &&
                                std::memcmp(info.dli_fbase, "MZ", 2) == 0;
        } else if (pe_frames >= 2 && std::strstr(symbols[i], "libHavok.so") &&
                  std::strstr(symbols[i], "HkShape_CastRayCollectSingleHit"))
            wrapper_frame = true;
    }
    _exit(wrapper_frame && exact_pe_offsets ? 0 : 1);
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: %s /path/to/Havok.dll [--trace]\n", argv[0]);
        return 2;
    }

    if (argc == 3 && std::strcmp(argv[2], "--trace") == 0) {
        struct sigaction action{};
        action.sa_handler = print_backtrace;
        sigemptyset(&action.sa_mask);
        sigaction(SIGSEGV, &action, nullptr);
    } else if (argc == 3) {
        return 2;
    }

    Init(argv[1], "./Havok.dll");
    if (!sidecar_has_compatible_tail())
        return 5;
    HkBaseSystem_Init(16 * 1024 * 1024, nullptr, false);
    void *shape = HkBoxShape_Create({1, 1, 1});
    if (!shape)
        return 3;
    void *storage = std::malloc(80);
    if (!storage)
        return 4;
    auto *misaligned_shape = static_cast<char *>(storage) + 8;
    std::memcpy(misaligned_shape, shape, 64);
    return HkShape_CastRayCollectSingleHit(misaligned_shape, {}, {1, 1, 1});
}
