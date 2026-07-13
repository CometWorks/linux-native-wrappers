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


using void_ptr_int_ptr_sysv_t = void (*)(void* arg0, int32_t arg1, void* arg2);
using void_ptr_int_ptr_ms_t = void (WINAPI *)(void* arg0, int32_t arg1, void* arg2);
static std::mutex void_ptr_int_ptr_mutex;

// Per-slot target read lock-free by the thunks (acquire) and published by
// bridge()/release() (release) under the mutex. Slot management (dedup +
// allocation) is O(1): an index map for dedup and a free-list stack for
// allocation, both touched only under the mutex, never by the thunks.
static std::atomic_uintptr_t void_ptr_int_ptr_targets[256] = {};
struct void_ptr_int_ptr_slot { uint32_t index; uint32_t refs; };
static std::unordered_map<void *, void_ptr_int_ptr_slot> void_ptr_int_ptr_index;
static std::vector<uint32_t> void_ptr_int_ptr_free_slots;
static uint32_t void_ptr_int_ptr_high_water = 0;

template <size_t Index>
static void WINAPI void_ptr_int_ptr_thunk(void* arg0, int32_t arg1, void* arg2)
{
    auto fn_bits = void_ptr_int_ptr_targets[Index].load(std::memory_order_acquire);
    auto fn = reinterpret_cast<void_ptr_int_ptr_sysv_t>(fn_bits);
    if (!fn) {
        return;
    }
    fn(arg0, arg1, arg2);
}

static void_ptr_int_ptr_ms_t void_ptr_int_ptr_thunks[256] = {
    &void_ptr_int_ptr_thunk<0>,
    &void_ptr_int_ptr_thunk<1>,
    &void_ptr_int_ptr_thunk<2>,
    &void_ptr_int_ptr_thunk<3>,
    &void_ptr_int_ptr_thunk<4>,
    &void_ptr_int_ptr_thunk<5>,
    &void_ptr_int_ptr_thunk<6>,
    &void_ptr_int_ptr_thunk<7>,
    &void_ptr_int_ptr_thunk<8>,
    &void_ptr_int_ptr_thunk<9>,
    &void_ptr_int_ptr_thunk<10>,
    &void_ptr_int_ptr_thunk<11>,
    &void_ptr_int_ptr_thunk<12>,
    &void_ptr_int_ptr_thunk<13>,
    &void_ptr_int_ptr_thunk<14>,
    &void_ptr_int_ptr_thunk<15>,
    &void_ptr_int_ptr_thunk<16>,
    &void_ptr_int_ptr_thunk<17>,
    &void_ptr_int_ptr_thunk<18>,
    &void_ptr_int_ptr_thunk<19>,
    &void_ptr_int_ptr_thunk<20>,
    &void_ptr_int_ptr_thunk<21>,
    &void_ptr_int_ptr_thunk<22>,
    &void_ptr_int_ptr_thunk<23>,
    &void_ptr_int_ptr_thunk<24>,
    &void_ptr_int_ptr_thunk<25>,
    &void_ptr_int_ptr_thunk<26>,
    &void_ptr_int_ptr_thunk<27>,
    &void_ptr_int_ptr_thunk<28>,
    &void_ptr_int_ptr_thunk<29>,
    &void_ptr_int_ptr_thunk<30>,
    &void_ptr_int_ptr_thunk<31>,
    &void_ptr_int_ptr_thunk<32>,
    &void_ptr_int_ptr_thunk<33>,
    &void_ptr_int_ptr_thunk<34>,
    &void_ptr_int_ptr_thunk<35>,
    &void_ptr_int_ptr_thunk<36>,
    &void_ptr_int_ptr_thunk<37>,
    &void_ptr_int_ptr_thunk<38>,
    &void_ptr_int_ptr_thunk<39>,
    &void_ptr_int_ptr_thunk<40>,
    &void_ptr_int_ptr_thunk<41>,
    &void_ptr_int_ptr_thunk<42>,
    &void_ptr_int_ptr_thunk<43>,
    &void_ptr_int_ptr_thunk<44>,
    &void_ptr_int_ptr_thunk<45>,
    &void_ptr_int_ptr_thunk<46>,
    &void_ptr_int_ptr_thunk<47>,
    &void_ptr_int_ptr_thunk<48>,
    &void_ptr_int_ptr_thunk<49>,
    &void_ptr_int_ptr_thunk<50>,
    &void_ptr_int_ptr_thunk<51>,
    &void_ptr_int_ptr_thunk<52>,
    &void_ptr_int_ptr_thunk<53>,
    &void_ptr_int_ptr_thunk<54>,
    &void_ptr_int_ptr_thunk<55>,
    &void_ptr_int_ptr_thunk<56>,
    &void_ptr_int_ptr_thunk<57>,
    &void_ptr_int_ptr_thunk<58>,
    &void_ptr_int_ptr_thunk<59>,
    &void_ptr_int_ptr_thunk<60>,
    &void_ptr_int_ptr_thunk<61>,
    &void_ptr_int_ptr_thunk<62>,
    &void_ptr_int_ptr_thunk<63>,
    &void_ptr_int_ptr_thunk<64>,
    &void_ptr_int_ptr_thunk<65>,
    &void_ptr_int_ptr_thunk<66>,
    &void_ptr_int_ptr_thunk<67>,
    &void_ptr_int_ptr_thunk<68>,
    &void_ptr_int_ptr_thunk<69>,
    &void_ptr_int_ptr_thunk<70>,
    &void_ptr_int_ptr_thunk<71>,
    &void_ptr_int_ptr_thunk<72>,
    &void_ptr_int_ptr_thunk<73>,
    &void_ptr_int_ptr_thunk<74>,
    &void_ptr_int_ptr_thunk<75>,
    &void_ptr_int_ptr_thunk<76>,
    &void_ptr_int_ptr_thunk<77>,
    &void_ptr_int_ptr_thunk<78>,
    &void_ptr_int_ptr_thunk<79>,
    &void_ptr_int_ptr_thunk<80>,
    &void_ptr_int_ptr_thunk<81>,
    &void_ptr_int_ptr_thunk<82>,
    &void_ptr_int_ptr_thunk<83>,
    &void_ptr_int_ptr_thunk<84>,
    &void_ptr_int_ptr_thunk<85>,
    &void_ptr_int_ptr_thunk<86>,
    &void_ptr_int_ptr_thunk<87>,
    &void_ptr_int_ptr_thunk<88>,
    &void_ptr_int_ptr_thunk<89>,
    &void_ptr_int_ptr_thunk<90>,
    &void_ptr_int_ptr_thunk<91>,
    &void_ptr_int_ptr_thunk<92>,
    &void_ptr_int_ptr_thunk<93>,
    &void_ptr_int_ptr_thunk<94>,
    &void_ptr_int_ptr_thunk<95>,
    &void_ptr_int_ptr_thunk<96>,
    &void_ptr_int_ptr_thunk<97>,
    &void_ptr_int_ptr_thunk<98>,
    &void_ptr_int_ptr_thunk<99>,
    &void_ptr_int_ptr_thunk<100>,
    &void_ptr_int_ptr_thunk<101>,
    &void_ptr_int_ptr_thunk<102>,
    &void_ptr_int_ptr_thunk<103>,
    &void_ptr_int_ptr_thunk<104>,
    &void_ptr_int_ptr_thunk<105>,
    &void_ptr_int_ptr_thunk<106>,
    &void_ptr_int_ptr_thunk<107>,
    &void_ptr_int_ptr_thunk<108>,
    &void_ptr_int_ptr_thunk<109>,
    &void_ptr_int_ptr_thunk<110>,
    &void_ptr_int_ptr_thunk<111>,
    &void_ptr_int_ptr_thunk<112>,
    &void_ptr_int_ptr_thunk<113>,
    &void_ptr_int_ptr_thunk<114>,
    &void_ptr_int_ptr_thunk<115>,
    &void_ptr_int_ptr_thunk<116>,
    &void_ptr_int_ptr_thunk<117>,
    &void_ptr_int_ptr_thunk<118>,
    &void_ptr_int_ptr_thunk<119>,
    &void_ptr_int_ptr_thunk<120>,
    &void_ptr_int_ptr_thunk<121>,
    &void_ptr_int_ptr_thunk<122>,
    &void_ptr_int_ptr_thunk<123>,
    &void_ptr_int_ptr_thunk<124>,
    &void_ptr_int_ptr_thunk<125>,
    &void_ptr_int_ptr_thunk<126>,
    &void_ptr_int_ptr_thunk<127>,
    &void_ptr_int_ptr_thunk<128>,
    &void_ptr_int_ptr_thunk<129>,
    &void_ptr_int_ptr_thunk<130>,
    &void_ptr_int_ptr_thunk<131>,
    &void_ptr_int_ptr_thunk<132>,
    &void_ptr_int_ptr_thunk<133>,
    &void_ptr_int_ptr_thunk<134>,
    &void_ptr_int_ptr_thunk<135>,
    &void_ptr_int_ptr_thunk<136>,
    &void_ptr_int_ptr_thunk<137>,
    &void_ptr_int_ptr_thunk<138>,
    &void_ptr_int_ptr_thunk<139>,
    &void_ptr_int_ptr_thunk<140>,
    &void_ptr_int_ptr_thunk<141>,
    &void_ptr_int_ptr_thunk<142>,
    &void_ptr_int_ptr_thunk<143>,
    &void_ptr_int_ptr_thunk<144>,
    &void_ptr_int_ptr_thunk<145>,
    &void_ptr_int_ptr_thunk<146>,
    &void_ptr_int_ptr_thunk<147>,
    &void_ptr_int_ptr_thunk<148>,
    &void_ptr_int_ptr_thunk<149>,
    &void_ptr_int_ptr_thunk<150>,
    &void_ptr_int_ptr_thunk<151>,
    &void_ptr_int_ptr_thunk<152>,
    &void_ptr_int_ptr_thunk<153>,
    &void_ptr_int_ptr_thunk<154>,
    &void_ptr_int_ptr_thunk<155>,
    &void_ptr_int_ptr_thunk<156>,
    &void_ptr_int_ptr_thunk<157>,
    &void_ptr_int_ptr_thunk<158>,
    &void_ptr_int_ptr_thunk<159>,
    &void_ptr_int_ptr_thunk<160>,
    &void_ptr_int_ptr_thunk<161>,
    &void_ptr_int_ptr_thunk<162>,
    &void_ptr_int_ptr_thunk<163>,
    &void_ptr_int_ptr_thunk<164>,
    &void_ptr_int_ptr_thunk<165>,
    &void_ptr_int_ptr_thunk<166>,
    &void_ptr_int_ptr_thunk<167>,
    &void_ptr_int_ptr_thunk<168>,
    &void_ptr_int_ptr_thunk<169>,
    &void_ptr_int_ptr_thunk<170>,
    &void_ptr_int_ptr_thunk<171>,
    &void_ptr_int_ptr_thunk<172>,
    &void_ptr_int_ptr_thunk<173>,
    &void_ptr_int_ptr_thunk<174>,
    &void_ptr_int_ptr_thunk<175>,
    &void_ptr_int_ptr_thunk<176>,
    &void_ptr_int_ptr_thunk<177>,
    &void_ptr_int_ptr_thunk<178>,
    &void_ptr_int_ptr_thunk<179>,
    &void_ptr_int_ptr_thunk<180>,
    &void_ptr_int_ptr_thunk<181>,
    &void_ptr_int_ptr_thunk<182>,
    &void_ptr_int_ptr_thunk<183>,
    &void_ptr_int_ptr_thunk<184>,
    &void_ptr_int_ptr_thunk<185>,
    &void_ptr_int_ptr_thunk<186>,
    &void_ptr_int_ptr_thunk<187>,
    &void_ptr_int_ptr_thunk<188>,
    &void_ptr_int_ptr_thunk<189>,
    &void_ptr_int_ptr_thunk<190>,
    &void_ptr_int_ptr_thunk<191>,
    &void_ptr_int_ptr_thunk<192>,
    &void_ptr_int_ptr_thunk<193>,
    &void_ptr_int_ptr_thunk<194>,
    &void_ptr_int_ptr_thunk<195>,
    &void_ptr_int_ptr_thunk<196>,
    &void_ptr_int_ptr_thunk<197>,
    &void_ptr_int_ptr_thunk<198>,
    &void_ptr_int_ptr_thunk<199>,
    &void_ptr_int_ptr_thunk<200>,
    &void_ptr_int_ptr_thunk<201>,
    &void_ptr_int_ptr_thunk<202>,
    &void_ptr_int_ptr_thunk<203>,
    &void_ptr_int_ptr_thunk<204>,
    &void_ptr_int_ptr_thunk<205>,
    &void_ptr_int_ptr_thunk<206>,
    &void_ptr_int_ptr_thunk<207>,
    &void_ptr_int_ptr_thunk<208>,
    &void_ptr_int_ptr_thunk<209>,
    &void_ptr_int_ptr_thunk<210>,
    &void_ptr_int_ptr_thunk<211>,
    &void_ptr_int_ptr_thunk<212>,
    &void_ptr_int_ptr_thunk<213>,
    &void_ptr_int_ptr_thunk<214>,
    &void_ptr_int_ptr_thunk<215>,
    &void_ptr_int_ptr_thunk<216>,
    &void_ptr_int_ptr_thunk<217>,
    &void_ptr_int_ptr_thunk<218>,
    &void_ptr_int_ptr_thunk<219>,
    &void_ptr_int_ptr_thunk<220>,
    &void_ptr_int_ptr_thunk<221>,
    &void_ptr_int_ptr_thunk<222>,
    &void_ptr_int_ptr_thunk<223>,
    &void_ptr_int_ptr_thunk<224>,
    &void_ptr_int_ptr_thunk<225>,
    &void_ptr_int_ptr_thunk<226>,
    &void_ptr_int_ptr_thunk<227>,
    &void_ptr_int_ptr_thunk<228>,
    &void_ptr_int_ptr_thunk<229>,
    &void_ptr_int_ptr_thunk<230>,
    &void_ptr_int_ptr_thunk<231>,
    &void_ptr_int_ptr_thunk<232>,
    &void_ptr_int_ptr_thunk<233>,
    &void_ptr_int_ptr_thunk<234>,
    &void_ptr_int_ptr_thunk<235>,
    &void_ptr_int_ptr_thunk<236>,
    &void_ptr_int_ptr_thunk<237>,
    &void_ptr_int_ptr_thunk<238>,
    &void_ptr_int_ptr_thunk<239>,
    &void_ptr_int_ptr_thunk<240>,
    &void_ptr_int_ptr_thunk<241>,
    &void_ptr_int_ptr_thunk<242>,
    &void_ptr_int_ptr_thunk<243>,
    &void_ptr_int_ptr_thunk<244>,
    &void_ptr_int_ptr_thunk<245>,
    &void_ptr_int_ptr_thunk<246>,
    &void_ptr_int_ptr_thunk<247>,
    &void_ptr_int_ptr_thunk<248>,
    &void_ptr_int_ptr_thunk<249>,
    &void_ptr_int_ptr_thunk<250>,
    &void_ptr_int_ptr_thunk<251>,
    &void_ptr_int_ptr_thunk<252>,
    &void_ptr_int_ptr_thunk<253>,
    &void_ptr_int_ptr_thunk<254>,
    &void_ptr_int_ptr_thunk<255>
};

void *bridge_void_ptr_int_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(void_ptr_int_ptr_mutex);
    auto existing = void_ptr_int_ptr_index.find(target_ptr);
    if (existing != void_ptr_int_ptr_index.end()) {
        ++existing->second.refs;
        return reinterpret_cast<void *>(void_ptr_int_ptr_thunks[existing->second.index]);
    }
    uint32_t index;
    if (!void_ptr_int_ptr_free_slots.empty()) {
        index = void_ptr_int_ptr_free_slots.back();
        void_ptr_int_ptr_free_slots.pop_back();
    } else if (void_ptr_int_ptr_high_water < 256) {
        index = void_ptr_int_ptr_high_water++;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge pool exhausted for the 'void_ptr_int_ptr' signature family:\n"
                "  All 256 bridge slots are in use. Every distinct live native callback of this\n"
                "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\n"
                "  in worlds with very many grids/blocks can register more than the pool can hold.\n"
                "  Fix: raise this family in CALLBACK_SLOTS (currently 256) in tools/generate_havok_wrapper.py,\n"
                "  regenerate the thunks (python3 tools/generate_havok_wrapper.py), and rebuild.\n");
        std::abort();
    }
    void_ptr_int_ptr_targets[index].store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
    void_ptr_int_ptr_index.emplace(target_ptr, void_ptr_int_ptr_slot{index, 1});
    return reinterpret_cast<void *>(void_ptr_int_ptr_thunks[index]);
}

void release_void_ptr_int_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(void_ptr_int_ptr_mutex);
    auto it = void_ptr_int_ptr_index.find(target_ptr);
    if (it == void_ptr_int_ptr_index.end()) {
        return;
    }
    if (--it->second.refs == 0) {
        uint32_t index = it->second.index;
        void_ptr_int_ptr_targets[index].store(0, std::memory_order_release);
        void_ptr_int_ptr_free_slots.push_back(index);
        void_ptr_int_ptr_index.erase(it);
    }
}
