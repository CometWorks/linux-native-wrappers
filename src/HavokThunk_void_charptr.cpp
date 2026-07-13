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


using void_charptr_sysv_t = void (*)(char* arg0);
using void_charptr_ms_t = void (WINAPI *)(char* arg0);
static std::mutex void_charptr_mutex;

// Per-slot target read lock-free by the thunks (acquire) and published by
// bridge()/release() (release) under the mutex. Slot management (dedup +
// allocation) is O(1): an index map for dedup and a free-list stack for
// allocation, both touched only under the mutex, never by the thunks.
static std::atomic_uintptr_t void_charptr_targets[32] = {};
struct void_charptr_slot { uint32_t index; uint32_t refs; };
static std::unordered_map<void *, void_charptr_slot> void_charptr_index;
static std::vector<uint32_t> void_charptr_free_slots;
static uint32_t void_charptr_high_water = 0;

template <size_t Index>
static void WINAPI void_charptr_thunk(char* arg0)
{
    auto fn_bits = void_charptr_targets[Index].load(std::memory_order_acquire);
    auto fn = reinterpret_cast<void_charptr_sysv_t>(fn_bits);
    if (!fn) {
        return;
    }
    fn(arg0);
}

static void_charptr_ms_t void_charptr_thunks[32] = {
    &void_charptr_thunk<0>,
    &void_charptr_thunk<1>,
    &void_charptr_thunk<2>,
    &void_charptr_thunk<3>,
    &void_charptr_thunk<4>,
    &void_charptr_thunk<5>,
    &void_charptr_thunk<6>,
    &void_charptr_thunk<7>,
    &void_charptr_thunk<8>,
    &void_charptr_thunk<9>,
    &void_charptr_thunk<10>,
    &void_charptr_thunk<11>,
    &void_charptr_thunk<12>,
    &void_charptr_thunk<13>,
    &void_charptr_thunk<14>,
    &void_charptr_thunk<15>,
    &void_charptr_thunk<16>,
    &void_charptr_thunk<17>,
    &void_charptr_thunk<18>,
    &void_charptr_thunk<19>,
    &void_charptr_thunk<20>,
    &void_charptr_thunk<21>,
    &void_charptr_thunk<22>,
    &void_charptr_thunk<23>,
    &void_charptr_thunk<24>,
    &void_charptr_thunk<25>,
    &void_charptr_thunk<26>,
    &void_charptr_thunk<27>,
    &void_charptr_thunk<28>,
    &void_charptr_thunk<29>,
    &void_charptr_thunk<30>,
    &void_charptr_thunk<31>
};

void *bridge_void_charptr(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(void_charptr_mutex);
    auto existing = void_charptr_index.find(target_ptr);
    if (existing != void_charptr_index.end()) {
        ++existing->second.refs;
        return reinterpret_cast<void *>(void_charptr_thunks[existing->second.index]);
    }
    uint32_t index;
    if (!void_charptr_free_slots.empty()) {
        index = void_charptr_free_slots.back();
        void_charptr_free_slots.pop_back();
    } else if (void_charptr_high_water < 32) {
        index = void_charptr_high_water++;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge pool exhausted for the 'void_charptr' signature family:\n"
                "  All 32 bridge slots are in use. Every distinct live native callback of this\n"
                "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\n"
                "  in worlds with very many grids/blocks can register more than the pool can hold.\n"
                "  Fix: raise this family in CALLBACK_SLOTS (currently 32) in tools/generate_havok_wrapper.py,\n"
                "  regenerate the thunks (python3 tools/generate_havok_wrapper.py), and rebuild.\n");
        std::abort();
    }
    void_charptr_targets[index].store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
    void_charptr_index.emplace(target_ptr, void_charptr_slot{index, 1});
    return reinterpret_cast<void *>(void_charptr_thunks[index]);
}

void release_void_charptr(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(void_charptr_mutex);
    auto it = void_charptr_index.find(target_ptr);
    if (it == void_charptr_index.end()) {
        return;
    }
    if (--it->second.refs == 0) {
        uint32_t index = it->second.index;
        void_charptr_targets[index].store(0, std::memory_order_release);
        void_charptr_free_slots.push_back(index);
        void_charptr_index.erase(it);
    }
}
