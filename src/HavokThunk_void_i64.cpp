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


using void_i64_sysv_t = void (*)(int64_t arg0);
using void_i64_ms_t = void (WINAPI *)(int64_t arg0);
static std::mutex void_i64_mutex;

// Per-slot target read lock-free by the thunks (acquire) and published by
// bridge()/release() (release) under the mutex. Slot management (dedup +
// allocation) is O(1): an index map for dedup and a free-list stack for
// allocation, both touched only under the mutex, never by the thunks.
static std::atomic_uintptr_t void_i64_targets[256] = {};
struct void_i64_slot { uint32_t index; uint32_t refs; };
static std::unordered_map<void *, void_i64_slot> void_i64_index;
static std::vector<uint32_t> void_i64_free_slots;
static uint32_t void_i64_high_water = 0;

template <size_t Index>
static void WINAPI void_i64_thunk(int64_t arg0)
{
    auto fn_bits = void_i64_targets[Index].load(std::memory_order_acquire);
    auto fn = reinterpret_cast<void_i64_sysv_t>(fn_bits);
    if (!fn) {
        return;
    }
    fn(arg0);
}

static void_i64_ms_t void_i64_thunks[256] = {
    &void_i64_thunk<0>,
    &void_i64_thunk<1>,
    &void_i64_thunk<2>,
    &void_i64_thunk<3>,
    &void_i64_thunk<4>,
    &void_i64_thunk<5>,
    &void_i64_thunk<6>,
    &void_i64_thunk<7>,
    &void_i64_thunk<8>,
    &void_i64_thunk<9>,
    &void_i64_thunk<10>,
    &void_i64_thunk<11>,
    &void_i64_thunk<12>,
    &void_i64_thunk<13>,
    &void_i64_thunk<14>,
    &void_i64_thunk<15>,
    &void_i64_thunk<16>,
    &void_i64_thunk<17>,
    &void_i64_thunk<18>,
    &void_i64_thunk<19>,
    &void_i64_thunk<20>,
    &void_i64_thunk<21>,
    &void_i64_thunk<22>,
    &void_i64_thunk<23>,
    &void_i64_thunk<24>,
    &void_i64_thunk<25>,
    &void_i64_thunk<26>,
    &void_i64_thunk<27>,
    &void_i64_thunk<28>,
    &void_i64_thunk<29>,
    &void_i64_thunk<30>,
    &void_i64_thunk<31>,
    &void_i64_thunk<32>,
    &void_i64_thunk<33>,
    &void_i64_thunk<34>,
    &void_i64_thunk<35>,
    &void_i64_thunk<36>,
    &void_i64_thunk<37>,
    &void_i64_thunk<38>,
    &void_i64_thunk<39>,
    &void_i64_thunk<40>,
    &void_i64_thunk<41>,
    &void_i64_thunk<42>,
    &void_i64_thunk<43>,
    &void_i64_thunk<44>,
    &void_i64_thunk<45>,
    &void_i64_thunk<46>,
    &void_i64_thunk<47>,
    &void_i64_thunk<48>,
    &void_i64_thunk<49>,
    &void_i64_thunk<50>,
    &void_i64_thunk<51>,
    &void_i64_thunk<52>,
    &void_i64_thunk<53>,
    &void_i64_thunk<54>,
    &void_i64_thunk<55>,
    &void_i64_thunk<56>,
    &void_i64_thunk<57>,
    &void_i64_thunk<58>,
    &void_i64_thunk<59>,
    &void_i64_thunk<60>,
    &void_i64_thunk<61>,
    &void_i64_thunk<62>,
    &void_i64_thunk<63>,
    &void_i64_thunk<64>,
    &void_i64_thunk<65>,
    &void_i64_thunk<66>,
    &void_i64_thunk<67>,
    &void_i64_thunk<68>,
    &void_i64_thunk<69>,
    &void_i64_thunk<70>,
    &void_i64_thunk<71>,
    &void_i64_thunk<72>,
    &void_i64_thunk<73>,
    &void_i64_thunk<74>,
    &void_i64_thunk<75>,
    &void_i64_thunk<76>,
    &void_i64_thunk<77>,
    &void_i64_thunk<78>,
    &void_i64_thunk<79>,
    &void_i64_thunk<80>,
    &void_i64_thunk<81>,
    &void_i64_thunk<82>,
    &void_i64_thunk<83>,
    &void_i64_thunk<84>,
    &void_i64_thunk<85>,
    &void_i64_thunk<86>,
    &void_i64_thunk<87>,
    &void_i64_thunk<88>,
    &void_i64_thunk<89>,
    &void_i64_thunk<90>,
    &void_i64_thunk<91>,
    &void_i64_thunk<92>,
    &void_i64_thunk<93>,
    &void_i64_thunk<94>,
    &void_i64_thunk<95>,
    &void_i64_thunk<96>,
    &void_i64_thunk<97>,
    &void_i64_thunk<98>,
    &void_i64_thunk<99>,
    &void_i64_thunk<100>,
    &void_i64_thunk<101>,
    &void_i64_thunk<102>,
    &void_i64_thunk<103>,
    &void_i64_thunk<104>,
    &void_i64_thunk<105>,
    &void_i64_thunk<106>,
    &void_i64_thunk<107>,
    &void_i64_thunk<108>,
    &void_i64_thunk<109>,
    &void_i64_thunk<110>,
    &void_i64_thunk<111>,
    &void_i64_thunk<112>,
    &void_i64_thunk<113>,
    &void_i64_thunk<114>,
    &void_i64_thunk<115>,
    &void_i64_thunk<116>,
    &void_i64_thunk<117>,
    &void_i64_thunk<118>,
    &void_i64_thunk<119>,
    &void_i64_thunk<120>,
    &void_i64_thunk<121>,
    &void_i64_thunk<122>,
    &void_i64_thunk<123>,
    &void_i64_thunk<124>,
    &void_i64_thunk<125>,
    &void_i64_thunk<126>,
    &void_i64_thunk<127>,
    &void_i64_thunk<128>,
    &void_i64_thunk<129>,
    &void_i64_thunk<130>,
    &void_i64_thunk<131>,
    &void_i64_thunk<132>,
    &void_i64_thunk<133>,
    &void_i64_thunk<134>,
    &void_i64_thunk<135>,
    &void_i64_thunk<136>,
    &void_i64_thunk<137>,
    &void_i64_thunk<138>,
    &void_i64_thunk<139>,
    &void_i64_thunk<140>,
    &void_i64_thunk<141>,
    &void_i64_thunk<142>,
    &void_i64_thunk<143>,
    &void_i64_thunk<144>,
    &void_i64_thunk<145>,
    &void_i64_thunk<146>,
    &void_i64_thunk<147>,
    &void_i64_thunk<148>,
    &void_i64_thunk<149>,
    &void_i64_thunk<150>,
    &void_i64_thunk<151>,
    &void_i64_thunk<152>,
    &void_i64_thunk<153>,
    &void_i64_thunk<154>,
    &void_i64_thunk<155>,
    &void_i64_thunk<156>,
    &void_i64_thunk<157>,
    &void_i64_thunk<158>,
    &void_i64_thunk<159>,
    &void_i64_thunk<160>,
    &void_i64_thunk<161>,
    &void_i64_thunk<162>,
    &void_i64_thunk<163>,
    &void_i64_thunk<164>,
    &void_i64_thunk<165>,
    &void_i64_thunk<166>,
    &void_i64_thunk<167>,
    &void_i64_thunk<168>,
    &void_i64_thunk<169>,
    &void_i64_thunk<170>,
    &void_i64_thunk<171>,
    &void_i64_thunk<172>,
    &void_i64_thunk<173>,
    &void_i64_thunk<174>,
    &void_i64_thunk<175>,
    &void_i64_thunk<176>,
    &void_i64_thunk<177>,
    &void_i64_thunk<178>,
    &void_i64_thunk<179>,
    &void_i64_thunk<180>,
    &void_i64_thunk<181>,
    &void_i64_thunk<182>,
    &void_i64_thunk<183>,
    &void_i64_thunk<184>,
    &void_i64_thunk<185>,
    &void_i64_thunk<186>,
    &void_i64_thunk<187>,
    &void_i64_thunk<188>,
    &void_i64_thunk<189>,
    &void_i64_thunk<190>,
    &void_i64_thunk<191>,
    &void_i64_thunk<192>,
    &void_i64_thunk<193>,
    &void_i64_thunk<194>,
    &void_i64_thunk<195>,
    &void_i64_thunk<196>,
    &void_i64_thunk<197>,
    &void_i64_thunk<198>,
    &void_i64_thunk<199>,
    &void_i64_thunk<200>,
    &void_i64_thunk<201>,
    &void_i64_thunk<202>,
    &void_i64_thunk<203>,
    &void_i64_thunk<204>,
    &void_i64_thunk<205>,
    &void_i64_thunk<206>,
    &void_i64_thunk<207>,
    &void_i64_thunk<208>,
    &void_i64_thunk<209>,
    &void_i64_thunk<210>,
    &void_i64_thunk<211>,
    &void_i64_thunk<212>,
    &void_i64_thunk<213>,
    &void_i64_thunk<214>,
    &void_i64_thunk<215>,
    &void_i64_thunk<216>,
    &void_i64_thunk<217>,
    &void_i64_thunk<218>,
    &void_i64_thunk<219>,
    &void_i64_thunk<220>,
    &void_i64_thunk<221>,
    &void_i64_thunk<222>,
    &void_i64_thunk<223>,
    &void_i64_thunk<224>,
    &void_i64_thunk<225>,
    &void_i64_thunk<226>,
    &void_i64_thunk<227>,
    &void_i64_thunk<228>,
    &void_i64_thunk<229>,
    &void_i64_thunk<230>,
    &void_i64_thunk<231>,
    &void_i64_thunk<232>,
    &void_i64_thunk<233>,
    &void_i64_thunk<234>,
    &void_i64_thunk<235>,
    &void_i64_thunk<236>,
    &void_i64_thunk<237>,
    &void_i64_thunk<238>,
    &void_i64_thunk<239>,
    &void_i64_thunk<240>,
    &void_i64_thunk<241>,
    &void_i64_thunk<242>,
    &void_i64_thunk<243>,
    &void_i64_thunk<244>,
    &void_i64_thunk<245>,
    &void_i64_thunk<246>,
    &void_i64_thunk<247>,
    &void_i64_thunk<248>,
    &void_i64_thunk<249>,
    &void_i64_thunk<250>,
    &void_i64_thunk<251>,
    &void_i64_thunk<252>,
    &void_i64_thunk<253>,
    &void_i64_thunk<254>,
    &void_i64_thunk<255>
};

void *bridge_void_i64(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(void_i64_mutex);
    auto existing = void_i64_index.find(target_ptr);
    if (existing != void_i64_index.end()) {
        ++existing->second.refs;
        return reinterpret_cast<void *>(void_i64_thunks[existing->second.index]);
    }
    uint32_t index;
    if (!void_i64_free_slots.empty()) {
        index = void_i64_free_slots.back();
        void_i64_free_slots.pop_back();
    } else if (void_i64_high_water < 256) {
        index = void_i64_high_water++;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge pool exhausted for the 'void_i64' signature family:\n"
                "  All 256 bridge slots are in use. Every distinct live native callback of this\n"
                "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\n"
                "  in worlds with very many grids/blocks can register more than the pool can hold.\n"
                "  Fix: raise this family in CALLBACK_SLOTS (currently 256) in tools/generate_havok_wrapper.py,\n"
                "  regenerate the thunks (python3 tools/generate_havok_wrapper.py), and rebuild.\n");
        std::abort();
    }
    void_i64_targets[index].store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
    void_i64_index.emplace(target_ptr, void_i64_slot{index, 1});
    return reinterpret_cast<void *>(void_i64_thunks[index]);
}

void release_void_i64(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(void_i64_mutex);
    auto it = void_i64_index.find(target_ptr);
    if (it == void_i64_index.end()) {
        return;
    }
    if (--it->second.refs == 0) {
        uint32_t index = it->second.index;
        void_i64_targets[index].store(0, std::memory_order_release);
        void_i64_free_slots.push_back(index);
        void_i64_index.erase(it);
    }
}
