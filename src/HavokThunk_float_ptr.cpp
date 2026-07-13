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


using float_ptr_sysv_t = float (*)(void* arg0);
using float_ptr_ms_t = float (WINAPI *)(void* arg0);
static std::mutex float_ptr_mutex;

// Per-slot target read lock-free by the thunks (acquire) and published by
// bridge()/release() (release) under the mutex. Slot management (dedup +
// allocation) is O(1): an index map for dedup and a free-list stack for
// allocation, both touched only under the mutex, never by the thunks.
static std::atomic_uintptr_t float_ptr_targets[32] = {};
struct float_ptr_slot { uint32_t index; uint32_t refs; };
static std::unordered_map<void *, float_ptr_slot> float_ptr_index;
static std::vector<uint32_t> float_ptr_free_slots;
static uint32_t float_ptr_high_water = 0;

template <size_t Index>
static float WINAPI float_ptr_thunk(void* arg0)
{
    auto fn_bits = float_ptr_targets[Index].load(std::memory_order_acquire);
    auto fn = reinterpret_cast<float_ptr_sysv_t>(fn_bits);
    if (!fn) {
        return float{};
    }
    return fn(arg0);
}

// Thunk pointer table -- see emit_thunk_table() in the generator. This is the
// power-of-two doubling-macro form of an explicit 32-entry initializer
// list (&float_ptr_thunk<0> .. &float_ptr_thunk<31>), one instantiation per slot.
#define HKTHUNK_1(n) &float_ptr_thunk<(n)>
#define HKTHUNK_2(n) HKTHUNK_1(n), HKTHUNK_1((n) + 1)
#define HKTHUNK_4(n) HKTHUNK_2(n), HKTHUNK_2((n) + 2)
#define HKTHUNK_8(n) HKTHUNK_4(n), HKTHUNK_4((n) + 4)
#define HKTHUNK_16(n) HKTHUNK_8(n), HKTHUNK_8((n) + 8)
#define HKTHUNK_32(n) HKTHUNK_16(n), HKTHUNK_16((n) + 16)
static float_ptr_ms_t float_ptr_thunks[32] = { HKTHUNK_32(0) };
#undef HKTHUNK_1
#undef HKTHUNK_2
#undef HKTHUNK_4
#undef HKTHUNK_8
#undef HKTHUNK_16
#undef HKTHUNK_32

void *bridge_float_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(float_ptr_mutex);
    auto existing = float_ptr_index.find(target_ptr);
    if (existing != float_ptr_index.end()) {
        ++existing->second.refs;
        return reinterpret_cast<void *>(float_ptr_thunks[existing->second.index]);
    }
    uint32_t index;
    if (!float_ptr_free_slots.empty()) {
        index = float_ptr_free_slots.back();
        float_ptr_free_slots.pop_back();
    } else if (float_ptr_high_water < 32) {
        index = float_ptr_high_water++;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge pool exhausted for the 'float_ptr' signature family:\n"
                "  All 32 bridge slots are in use. Every distinct live native callback of this\n"
                "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\n"
                "  in worlds with very many grids/blocks can register more than the pool can hold.\n"
                "  Fix: raise this family in CALLBACK_SLOTS (currently 32) in tools/generate_havok_wrapper.py,\n"
                "  regenerate the thunks (python3 tools/generate_havok_wrapper.py), and rebuild.\n");
        std::abort();
    }
    float_ptr_targets[index].store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
    float_ptr_index.emplace(target_ptr, float_ptr_slot{index, 1});
    return reinterpret_cast<void *>(float_ptr_thunks[index]);
}

void release_float_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(float_ptr_mutex);
    auto it = float_ptr_index.find(target_ptr);
    if (it == float_ptr_index.end()) {
        return;
    }
    if (--it->second.refs == 0) {
        uint32_t index = it->second.index;
        float_ptr_targets[index].store(0, std::memory_order_release);
        float_ptr_free_slots.push_back(index);
        float_ptr_index.erase(it);
    }
}
