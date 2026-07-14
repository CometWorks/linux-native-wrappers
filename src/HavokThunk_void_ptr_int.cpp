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


using void_ptr_int_sysv_t = void (*)(void* arg0, int32_t arg1);
using void_ptr_int_ms_t = void (WINAPI *)(void* arg0, int32_t arg1);
static std::mutex void_ptr_int_mutex;

// Per-slot target read lock-free by the thunks (acquire) and published by
// bridge()/release() (release) under the mutex. Slot management (dedup +
// allocation) is O(1): an index map for dedup and a free-list stack for
// allocation, both touched only under the mutex, never by the thunks.
static std::atomic_uintptr_t void_ptr_int_targets[128] = {};
struct void_ptr_int_slot { uint32_t index; uint32_t refs; };
static std::unordered_map<void *, void_ptr_int_slot> void_ptr_int_index;
static std::vector<uint32_t> void_ptr_int_free_slots;
static uint32_t void_ptr_int_high_water = 0;

template <size_t Index>
static void WINAPI void_ptr_int_thunk(void* arg0, int32_t arg1)
{
    auto fn_bits = void_ptr_int_targets[Index].load(std::memory_order_acquire);
    auto fn = reinterpret_cast<void_ptr_int_sysv_t>(fn_bits);
    if (!fn) {
        return;
    }
    fn(arg0, arg1);
}

// Thunk pointer table -- see emit_thunk_table() in the generator. This is the
// power-of-two doubling-macro form of an explicit 128-entry initializer
// list (&void_ptr_int_thunk<0> .. &void_ptr_int_thunk<127>), one instantiation per slot.
#define HKTHUNK_1(n) &void_ptr_int_thunk<(n)>
#define HKTHUNK_2(n) HKTHUNK_1(n), HKTHUNK_1((n) + 1)
#define HKTHUNK_4(n) HKTHUNK_2(n), HKTHUNK_2((n) + 2)
#define HKTHUNK_8(n) HKTHUNK_4(n), HKTHUNK_4((n) + 4)
#define HKTHUNK_16(n) HKTHUNK_8(n), HKTHUNK_8((n) + 8)
#define HKTHUNK_32(n) HKTHUNK_16(n), HKTHUNK_16((n) + 16)
#define HKTHUNK_64(n) HKTHUNK_32(n), HKTHUNK_32((n) + 32)
#define HKTHUNK_128(n) HKTHUNK_64(n), HKTHUNK_64((n) + 64)
static void_ptr_int_ms_t void_ptr_int_thunks[128] = { HKTHUNK_128(0) };
#undef HKTHUNK_1
#undef HKTHUNK_2
#undef HKTHUNK_4
#undef HKTHUNK_8
#undef HKTHUNK_16
#undef HKTHUNK_32
#undef HKTHUNK_64
#undef HKTHUNK_128

void *bridge_void_ptr_int(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(void_ptr_int_mutex);
    auto existing = void_ptr_int_index.find(target_ptr);
    if (existing != void_ptr_int_index.end()) {
        ++existing->second.refs;
        return reinterpret_cast<void *>(void_ptr_int_thunks[existing->second.index]);
    }
    uint32_t index;
    if (!void_ptr_int_free_slots.empty()) {
        index = void_ptr_int_free_slots.back();
        void_ptr_int_free_slots.pop_back();
    } else if (void_ptr_int_high_water < 128) {
        index = void_ptr_int_high_water++;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge pool exhausted for the 'void_ptr_int' signature family:\n"
                "  All 128 bridge slots are in use. Every distinct live native callback of this\n"
                "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\n"
                "  in worlds with very many grids/blocks can register more than the pool can hold.\n"
                "  Fix: raise this family in CALLBACK_SLOTS (currently 128) in tools/generate_havok_wrapper.py,\n"
                "  regenerate the thunks (python3 tools/generate_havok_wrapper.py), and rebuild.\n");
        std::abort();
    }
    void_ptr_int_targets[index].store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
    void_ptr_int_index.emplace(target_ptr, void_ptr_int_slot{index, 1});
    return reinterpret_cast<void *>(void_ptr_int_thunks[index]);
}

void release_void_ptr_int(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(void_ptr_int_mutex);
    auto it = void_ptr_int_index.find(target_ptr);
    if (it == void_ptr_int_index.end()) {
        return;
    }
    if (--it->second.refs == 0) {
        uint32_t index = it->second.index;
        void_ptr_int_targets[index].store(0, std::memory_order_release);
        void_ptr_int_free_slots.push_back(index);
        void_ptr_int_index.erase(it);
    }
}
