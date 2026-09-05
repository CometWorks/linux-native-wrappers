// Loads ms_abi_tls_probe at run time, like the game loads the wrappers, and
// calls its probe from fresh threads. CTest disables glibc's optional static
// TLS allocation so these calls exercise the dynamic TLS resolver.
// With the traditional TLS dialect the probe crashes (GCC keeps the out
// pointer in RDI across __tls_get_addr);
// with TLS descriptors it must keep both the pointer and XMM6 to XMM15.
#include <cstdint>
#include <cstdio>
#include <thread>

#include <dlfcn.h>

using probe_t = int (__attribute__((ms_abi)) *)(uint64_t, uint64_t *);

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fputs("usage: ms_abi_tls_test <probe library>\n", stderr);
        return 2;
    }
    void *library = dlopen(argv[1], RTLD_NOW);
    if (!library) {
        std::fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 2;
    }
    auto probe = reinterpret_cast<probe_t>(dlsym(library, "probe"));
    if (!probe) {
        std::fputs("probe symbol missing\n", stderr);
        return 2;
    }
    int failures = 0;
    for (uint64_t round = 1; round <= 200; ++round) {
        std::thread([&] {
            uint64_t value = 0;
            if (probe(round, &value) != 0 || value != round)
                ++failures;
        }).join();
    }
    std::printf("%d of 200 probe calls corrupted a register\n", failures);
    return failures == 0 ? 0 : 1;
}
