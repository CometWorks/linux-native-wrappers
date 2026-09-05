// Shared library for the ms_abi_tls test. It has the shape of every Win32
// shim in winlibs.cpp: an ms_abi function that touches a thread_local while
// holding live values in registers that ms_abi treats as callee-saved (RDI,
// RSI, XMM6 to XMM15). Built with the same TLS dialect as the wrappers.
#include <cstdint>
#include <cstring>

static thread_local uint64_t t_value;

extern "C" __attribute__((ms_abi)) int probe(uint64_t seed, uint64_t *out)
{
    alignas(16) uint64_t before[20];
    alignas(16) uint64_t after[20];
    for (int i = 0; i < 20; ++i)
        before[i] = seed * 0x9E3779B97F4A7C15ull + i;
    asm volatile(
        "movdqa 0(%0), %%xmm6\n\t"   "movdqa 16(%0), %%xmm7\n\t"
        "movdqa 32(%0), %%xmm8\n\t"  "movdqa 48(%0), %%xmm9\n\t"
        "movdqa 64(%0), %%xmm10\n\t" "movdqa 80(%0), %%xmm11\n\t"
        "movdqa 96(%0), %%xmm12\n\t" "movdqa 112(%0), %%xmm13\n\t"
        "movdqa 128(%0), %%xmm14\n\t" "movdqa 144(%0), %%xmm15"
        : : "r"(before)
        : "memory", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12",
          "xmm13", "xmm14", "xmm15");
    t_value = seed;  // the TLS access under test
    asm volatile(
        "movdqa %%xmm6, 0(%0)\n\t"   "movdqa %%xmm7, 16(%0)\n\t"
        "movdqa %%xmm8, 32(%0)\n\t"  "movdqa %%xmm9, 48(%0)\n\t"
        "movdqa %%xmm10, 64(%0)\n\t" "movdqa %%xmm11, 80(%0)\n\t"
        "movdqa %%xmm12, 96(%0)\n\t" "movdqa %%xmm13, 112(%0)\n\t"
        "movdqa %%xmm14, 128(%0)\n\t" "movdqa %%xmm15, 144(%0)"
        : : "r"(after) : "memory");
    *out = t_value;
    return std::memcmp(before, after, sizeof before) == 0 ? 0 : 1;
}
