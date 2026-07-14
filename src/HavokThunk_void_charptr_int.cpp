// ============================================================================
// GENERATED FILE -- do not edit by hand.
// Regenerate with: python3 tools/generate_havok_wrapper.py
//
// Havok callback bridge pool. Each slot is a distinct compile-time thunk address
// handed to Havok as a C callback; the per-family pool size is fixed per build
// and cannot be grown at run time. If the pool is exhausted the bridge prints a
// diagnostic and aborts -- raise this family's entry in CALLBACK_SLOTS in the
// generator, regenerate, and rebuild.
// ============================================================================
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "win_types.h"
#include "HavokThunkRegistry.h"


using void_charptr_int_sysv_t = void (*)(char* arg0, int32_t arg1);
static std::mutex void_charptr_int_mutex;

// Single-slot family: one target read lock-free by the thunk (acquire) and
// published under the mutex (release). No collection -- just this pointer.
static std::atomic_uintptr_t void_charptr_int_target = 0;
static uint32_t void_charptr_int_refcount = 0;  // guarded by void_charptr_int_mutex

static void WINAPI void_charptr_int_thunk(char* arg0, int32_t arg1)
{
    auto fn = reinterpret_cast<void_charptr_int_sysv_t>(void_charptr_int_target.load(std::memory_order_acquire));
    if (!fn) {
        return;
    }
    fn(arg0, arg1);
}

void *bridge_void_charptr_int(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(void_charptr_int_mutex);
    auto current = void_charptr_int_target.load(std::memory_order_acquire);
    if (current == 0) {
        void_charptr_int_target.store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
        void_charptr_int_refcount = 1;
    } else if (current == reinterpret_cast<uintptr_t>(target_ptr)) {
        ++void_charptr_int_refcount;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge single-slot family 'void_charptr_int' received a\n"
                "  second distinct target while the first was still live. It was sized for one\n"
                "  callback pointer; give it a multi-slot pool in CALLBACK_SLOTS in\n"
                "  tools/generate_havok_wrapper.py, regenerate, and rebuild.\n");
        std::abort();
    }
    return reinterpret_cast<void *>(&void_charptr_int_thunk);
}

void release_void_charptr_int(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(void_charptr_int_mutex);
    if (void_charptr_int_target.load(std::memory_order_acquire) != reinterpret_cast<uintptr_t>(target_ptr)) {
        return;
    }
    if (void_charptr_int_refcount > 0 && --void_charptr_int_refcount == 0) {
        void_charptr_int_target.store(0, std::memory_order_release);
    }
}
