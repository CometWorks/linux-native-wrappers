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


using int_ptr_ptr_uint_ptr_sysv_t = int32_t (*)(void* arg0, void* arg1, uint32_t arg2, void* arg3);
static std::mutex int_ptr_ptr_uint_ptr_mutex;

// Single-slot family: one target read lock-free by the thunk (acquire) and
// published under the mutex (release). No collection -- just this pointer.
static std::atomic_uintptr_t int_ptr_ptr_uint_ptr_target = 0;
static uint32_t int_ptr_ptr_uint_ptr_refcount = 0;  // guarded by int_ptr_ptr_uint_ptr_mutex

static int32_t WINAPI int_ptr_ptr_uint_ptr_thunk(void* arg0, void* arg1, uint32_t arg2, void* arg3)
{
    auto fn = reinterpret_cast<int_ptr_ptr_uint_ptr_sysv_t>(int_ptr_ptr_uint_ptr_target.load(std::memory_order_acquire));
    if (!fn) {
        return int32_t{};
    }
    return fn(arg0, arg1, arg2, arg3);
}

void *bridge_int_ptr_ptr_uint_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(int_ptr_ptr_uint_ptr_mutex);
    auto current = int_ptr_ptr_uint_ptr_target.load(std::memory_order_acquire);
    if (current == 0) {
        int_ptr_ptr_uint_ptr_target.store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
        int_ptr_ptr_uint_ptr_refcount = 1;
    } else if (current == reinterpret_cast<uintptr_t>(target_ptr)) {
        ++int_ptr_ptr_uint_ptr_refcount;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge single-slot family 'int_ptr_ptr_uint_ptr' received a\n"
                "  second distinct target while the first was still live. It was sized for one\n"
                "  callback pointer; give it a multi-slot pool in CALLBACK_SLOTS in\n"
                "  tools/generate_havok_wrapper.py, regenerate, and rebuild.\n");
        std::abort();
    }
    return reinterpret_cast<void *>(&int_ptr_ptr_uint_ptr_thunk);
}

void release_int_ptr_ptr_uint_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(int_ptr_ptr_uint_ptr_mutex);
    if (int_ptr_ptr_uint_ptr_target.load(std::memory_order_acquire) != reinterpret_cast<uintptr_t>(target_ptr)) {
        return;
    }
    if (int_ptr_ptr_uint_ptr_refcount > 0 && --int_ptr_ptr_uint_ptr_refcount == 0) {
        int_ptr_ptr_uint_ptr_target.store(0, std::memory_order_release);
    }
}
