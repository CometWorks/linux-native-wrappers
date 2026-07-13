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


using bool_ptr_ptr_sysv_t = bool (*)(void* arg0, void* arg1);
static std::mutex bool_ptr_ptr_mutex;

// Single-slot family: one target read lock-free by the thunk (acquire) and
// published under the mutex (release). No collection -- just this pointer.
static std::atomic_uintptr_t bool_ptr_ptr_target = 0;
static uint32_t bool_ptr_ptr_refcount = 0;  // guarded by bool_ptr_ptr_mutex

static bool WINAPI bool_ptr_ptr_thunk(void* arg0, void* arg1)
{
    auto fn = reinterpret_cast<bool_ptr_ptr_sysv_t>(bool_ptr_ptr_target.load(std::memory_order_acquire));
    if (!fn) {
        return bool{};
    }
    return fn(arg0, arg1);
}

void *bridge_bool_ptr_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(bool_ptr_ptr_mutex);
    auto current = bool_ptr_ptr_target.load(std::memory_order_acquire);
    if (current == 0) {
        bool_ptr_ptr_target.store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
        bool_ptr_ptr_refcount = 1;
    } else if (current == reinterpret_cast<uintptr_t>(target_ptr)) {
        ++bool_ptr_ptr_refcount;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge single-slot family 'bool_ptr_ptr' received a\n"
                "  second distinct target while the first was still live. It was sized for one\n"
                "  callback pointer; give it a multi-slot pool in CALLBACK_SLOTS in\n"
                "  tools/generate_havok_wrapper.py, regenerate, and rebuild.\n");
        std::abort();
    }
    return reinterpret_cast<void *>(&bool_ptr_ptr_thunk);
}

void release_bool_ptr_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(bool_ptr_ptr_mutex);
    if (bool_ptr_ptr_target.load(std::memory_order_acquire) != reinterpret_cast<uintptr_t>(target_ptr)) {
        return;
    }
    if (bool_ptr_ptr_refcount > 0 && --bool_ptr_ptr_refcount == 0) {
        bool_ptr_ptr_target.store(0, std::memory_order_release);
    }
}
