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


using void_void_sysv_t = void (*)(void);
static std::mutex void_void_mutex;

// Single-slot family: one target read lock-free by the thunk (acquire) and
// published under the mutex (release). No collection -- just this pointer.
static std::atomic_uintptr_t void_void_target = 0;
static uint32_t void_void_refcount = 0;  // guarded by void_void_mutex

static void WINAPI void_void_thunk(void)
{
    auto fn = reinterpret_cast<void_void_sysv_t>(void_void_target.load(std::memory_order_acquire));
    if (!fn) {
        return;
    }
    fn();
}

void *bridge_void_void(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(void_void_mutex);
    auto current = void_void_target.load(std::memory_order_acquire);
    if (current == 0) {
        void_void_target.store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
        void_void_refcount = 1;
    } else if (current == reinterpret_cast<uintptr_t>(target_ptr)) {
        ++void_void_refcount;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge single-slot family 'void_void' received a\n"
                "  second distinct target while the first was still live. It was sized for one\n"
                "  callback pointer; give it a multi-slot pool in CALLBACK_SLOTS in\n"
                "  tools/generate_havok_wrapper.py, regenerate, and rebuild.\n");
        std::abort();
    }
    return reinterpret_cast<void *>(&void_void_thunk);
}

void release_void_void(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(void_void_mutex);
    if (void_void_target.load(std::memory_order_acquire) != reinterpret_cast<uintptr_t>(target_ptr)) {
        return;
    }
    if (void_void_refcount > 0 && --void_void_refcount == 0) {
        void_void_target.store(0, std::memory_order_release);
    }
}
