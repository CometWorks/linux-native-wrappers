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
using bool_ptr_ptr_ms_t = bool (WINAPI *)(void* arg0, void* arg1);
static std::mutex bool_ptr_ptr_mutex;

// Per-slot target read lock-free by the thunks (acquire) and published by
// bridge()/release() (release) under the mutex. Slot management (dedup +
// allocation) is O(1): an index map for dedup and a free-list stack for
// allocation, both touched only under the mutex, never by the thunks.
static std::atomic_uintptr_t bool_ptr_ptr_targets[256] = {};
struct bool_ptr_ptr_slot { uint32_t index; uint32_t refs; };
static std::unordered_map<void *, bool_ptr_ptr_slot> bool_ptr_ptr_index;
static std::vector<uint32_t> bool_ptr_ptr_free_slots;
static uint32_t bool_ptr_ptr_high_water = 0;

template <size_t Index>
static bool WINAPI bool_ptr_ptr_thunk(void* arg0, void* arg1)
{
    auto fn_bits = bool_ptr_ptr_targets[Index].load(std::memory_order_acquire);
    auto fn = reinterpret_cast<bool_ptr_ptr_sysv_t>(fn_bits);
    if (!fn) {
        return bool{};
    }
    return fn(arg0, arg1);
}

static bool_ptr_ptr_ms_t bool_ptr_ptr_thunks[256] = {
    &bool_ptr_ptr_thunk<0>,
    &bool_ptr_ptr_thunk<1>,
    &bool_ptr_ptr_thunk<2>,
    &bool_ptr_ptr_thunk<3>,
    &bool_ptr_ptr_thunk<4>,
    &bool_ptr_ptr_thunk<5>,
    &bool_ptr_ptr_thunk<6>,
    &bool_ptr_ptr_thunk<7>,
    &bool_ptr_ptr_thunk<8>,
    &bool_ptr_ptr_thunk<9>,
    &bool_ptr_ptr_thunk<10>,
    &bool_ptr_ptr_thunk<11>,
    &bool_ptr_ptr_thunk<12>,
    &bool_ptr_ptr_thunk<13>,
    &bool_ptr_ptr_thunk<14>,
    &bool_ptr_ptr_thunk<15>,
    &bool_ptr_ptr_thunk<16>,
    &bool_ptr_ptr_thunk<17>,
    &bool_ptr_ptr_thunk<18>,
    &bool_ptr_ptr_thunk<19>,
    &bool_ptr_ptr_thunk<20>,
    &bool_ptr_ptr_thunk<21>,
    &bool_ptr_ptr_thunk<22>,
    &bool_ptr_ptr_thunk<23>,
    &bool_ptr_ptr_thunk<24>,
    &bool_ptr_ptr_thunk<25>,
    &bool_ptr_ptr_thunk<26>,
    &bool_ptr_ptr_thunk<27>,
    &bool_ptr_ptr_thunk<28>,
    &bool_ptr_ptr_thunk<29>,
    &bool_ptr_ptr_thunk<30>,
    &bool_ptr_ptr_thunk<31>,
    &bool_ptr_ptr_thunk<32>,
    &bool_ptr_ptr_thunk<33>,
    &bool_ptr_ptr_thunk<34>,
    &bool_ptr_ptr_thunk<35>,
    &bool_ptr_ptr_thunk<36>,
    &bool_ptr_ptr_thunk<37>,
    &bool_ptr_ptr_thunk<38>,
    &bool_ptr_ptr_thunk<39>,
    &bool_ptr_ptr_thunk<40>,
    &bool_ptr_ptr_thunk<41>,
    &bool_ptr_ptr_thunk<42>,
    &bool_ptr_ptr_thunk<43>,
    &bool_ptr_ptr_thunk<44>,
    &bool_ptr_ptr_thunk<45>,
    &bool_ptr_ptr_thunk<46>,
    &bool_ptr_ptr_thunk<47>,
    &bool_ptr_ptr_thunk<48>,
    &bool_ptr_ptr_thunk<49>,
    &bool_ptr_ptr_thunk<50>,
    &bool_ptr_ptr_thunk<51>,
    &bool_ptr_ptr_thunk<52>,
    &bool_ptr_ptr_thunk<53>,
    &bool_ptr_ptr_thunk<54>,
    &bool_ptr_ptr_thunk<55>,
    &bool_ptr_ptr_thunk<56>,
    &bool_ptr_ptr_thunk<57>,
    &bool_ptr_ptr_thunk<58>,
    &bool_ptr_ptr_thunk<59>,
    &bool_ptr_ptr_thunk<60>,
    &bool_ptr_ptr_thunk<61>,
    &bool_ptr_ptr_thunk<62>,
    &bool_ptr_ptr_thunk<63>,
    &bool_ptr_ptr_thunk<64>,
    &bool_ptr_ptr_thunk<65>,
    &bool_ptr_ptr_thunk<66>,
    &bool_ptr_ptr_thunk<67>,
    &bool_ptr_ptr_thunk<68>,
    &bool_ptr_ptr_thunk<69>,
    &bool_ptr_ptr_thunk<70>,
    &bool_ptr_ptr_thunk<71>,
    &bool_ptr_ptr_thunk<72>,
    &bool_ptr_ptr_thunk<73>,
    &bool_ptr_ptr_thunk<74>,
    &bool_ptr_ptr_thunk<75>,
    &bool_ptr_ptr_thunk<76>,
    &bool_ptr_ptr_thunk<77>,
    &bool_ptr_ptr_thunk<78>,
    &bool_ptr_ptr_thunk<79>,
    &bool_ptr_ptr_thunk<80>,
    &bool_ptr_ptr_thunk<81>,
    &bool_ptr_ptr_thunk<82>,
    &bool_ptr_ptr_thunk<83>,
    &bool_ptr_ptr_thunk<84>,
    &bool_ptr_ptr_thunk<85>,
    &bool_ptr_ptr_thunk<86>,
    &bool_ptr_ptr_thunk<87>,
    &bool_ptr_ptr_thunk<88>,
    &bool_ptr_ptr_thunk<89>,
    &bool_ptr_ptr_thunk<90>,
    &bool_ptr_ptr_thunk<91>,
    &bool_ptr_ptr_thunk<92>,
    &bool_ptr_ptr_thunk<93>,
    &bool_ptr_ptr_thunk<94>,
    &bool_ptr_ptr_thunk<95>,
    &bool_ptr_ptr_thunk<96>,
    &bool_ptr_ptr_thunk<97>,
    &bool_ptr_ptr_thunk<98>,
    &bool_ptr_ptr_thunk<99>,
    &bool_ptr_ptr_thunk<100>,
    &bool_ptr_ptr_thunk<101>,
    &bool_ptr_ptr_thunk<102>,
    &bool_ptr_ptr_thunk<103>,
    &bool_ptr_ptr_thunk<104>,
    &bool_ptr_ptr_thunk<105>,
    &bool_ptr_ptr_thunk<106>,
    &bool_ptr_ptr_thunk<107>,
    &bool_ptr_ptr_thunk<108>,
    &bool_ptr_ptr_thunk<109>,
    &bool_ptr_ptr_thunk<110>,
    &bool_ptr_ptr_thunk<111>,
    &bool_ptr_ptr_thunk<112>,
    &bool_ptr_ptr_thunk<113>,
    &bool_ptr_ptr_thunk<114>,
    &bool_ptr_ptr_thunk<115>,
    &bool_ptr_ptr_thunk<116>,
    &bool_ptr_ptr_thunk<117>,
    &bool_ptr_ptr_thunk<118>,
    &bool_ptr_ptr_thunk<119>,
    &bool_ptr_ptr_thunk<120>,
    &bool_ptr_ptr_thunk<121>,
    &bool_ptr_ptr_thunk<122>,
    &bool_ptr_ptr_thunk<123>,
    &bool_ptr_ptr_thunk<124>,
    &bool_ptr_ptr_thunk<125>,
    &bool_ptr_ptr_thunk<126>,
    &bool_ptr_ptr_thunk<127>,
    &bool_ptr_ptr_thunk<128>,
    &bool_ptr_ptr_thunk<129>,
    &bool_ptr_ptr_thunk<130>,
    &bool_ptr_ptr_thunk<131>,
    &bool_ptr_ptr_thunk<132>,
    &bool_ptr_ptr_thunk<133>,
    &bool_ptr_ptr_thunk<134>,
    &bool_ptr_ptr_thunk<135>,
    &bool_ptr_ptr_thunk<136>,
    &bool_ptr_ptr_thunk<137>,
    &bool_ptr_ptr_thunk<138>,
    &bool_ptr_ptr_thunk<139>,
    &bool_ptr_ptr_thunk<140>,
    &bool_ptr_ptr_thunk<141>,
    &bool_ptr_ptr_thunk<142>,
    &bool_ptr_ptr_thunk<143>,
    &bool_ptr_ptr_thunk<144>,
    &bool_ptr_ptr_thunk<145>,
    &bool_ptr_ptr_thunk<146>,
    &bool_ptr_ptr_thunk<147>,
    &bool_ptr_ptr_thunk<148>,
    &bool_ptr_ptr_thunk<149>,
    &bool_ptr_ptr_thunk<150>,
    &bool_ptr_ptr_thunk<151>,
    &bool_ptr_ptr_thunk<152>,
    &bool_ptr_ptr_thunk<153>,
    &bool_ptr_ptr_thunk<154>,
    &bool_ptr_ptr_thunk<155>,
    &bool_ptr_ptr_thunk<156>,
    &bool_ptr_ptr_thunk<157>,
    &bool_ptr_ptr_thunk<158>,
    &bool_ptr_ptr_thunk<159>,
    &bool_ptr_ptr_thunk<160>,
    &bool_ptr_ptr_thunk<161>,
    &bool_ptr_ptr_thunk<162>,
    &bool_ptr_ptr_thunk<163>,
    &bool_ptr_ptr_thunk<164>,
    &bool_ptr_ptr_thunk<165>,
    &bool_ptr_ptr_thunk<166>,
    &bool_ptr_ptr_thunk<167>,
    &bool_ptr_ptr_thunk<168>,
    &bool_ptr_ptr_thunk<169>,
    &bool_ptr_ptr_thunk<170>,
    &bool_ptr_ptr_thunk<171>,
    &bool_ptr_ptr_thunk<172>,
    &bool_ptr_ptr_thunk<173>,
    &bool_ptr_ptr_thunk<174>,
    &bool_ptr_ptr_thunk<175>,
    &bool_ptr_ptr_thunk<176>,
    &bool_ptr_ptr_thunk<177>,
    &bool_ptr_ptr_thunk<178>,
    &bool_ptr_ptr_thunk<179>,
    &bool_ptr_ptr_thunk<180>,
    &bool_ptr_ptr_thunk<181>,
    &bool_ptr_ptr_thunk<182>,
    &bool_ptr_ptr_thunk<183>,
    &bool_ptr_ptr_thunk<184>,
    &bool_ptr_ptr_thunk<185>,
    &bool_ptr_ptr_thunk<186>,
    &bool_ptr_ptr_thunk<187>,
    &bool_ptr_ptr_thunk<188>,
    &bool_ptr_ptr_thunk<189>,
    &bool_ptr_ptr_thunk<190>,
    &bool_ptr_ptr_thunk<191>,
    &bool_ptr_ptr_thunk<192>,
    &bool_ptr_ptr_thunk<193>,
    &bool_ptr_ptr_thunk<194>,
    &bool_ptr_ptr_thunk<195>,
    &bool_ptr_ptr_thunk<196>,
    &bool_ptr_ptr_thunk<197>,
    &bool_ptr_ptr_thunk<198>,
    &bool_ptr_ptr_thunk<199>,
    &bool_ptr_ptr_thunk<200>,
    &bool_ptr_ptr_thunk<201>,
    &bool_ptr_ptr_thunk<202>,
    &bool_ptr_ptr_thunk<203>,
    &bool_ptr_ptr_thunk<204>,
    &bool_ptr_ptr_thunk<205>,
    &bool_ptr_ptr_thunk<206>,
    &bool_ptr_ptr_thunk<207>,
    &bool_ptr_ptr_thunk<208>,
    &bool_ptr_ptr_thunk<209>,
    &bool_ptr_ptr_thunk<210>,
    &bool_ptr_ptr_thunk<211>,
    &bool_ptr_ptr_thunk<212>,
    &bool_ptr_ptr_thunk<213>,
    &bool_ptr_ptr_thunk<214>,
    &bool_ptr_ptr_thunk<215>,
    &bool_ptr_ptr_thunk<216>,
    &bool_ptr_ptr_thunk<217>,
    &bool_ptr_ptr_thunk<218>,
    &bool_ptr_ptr_thunk<219>,
    &bool_ptr_ptr_thunk<220>,
    &bool_ptr_ptr_thunk<221>,
    &bool_ptr_ptr_thunk<222>,
    &bool_ptr_ptr_thunk<223>,
    &bool_ptr_ptr_thunk<224>,
    &bool_ptr_ptr_thunk<225>,
    &bool_ptr_ptr_thunk<226>,
    &bool_ptr_ptr_thunk<227>,
    &bool_ptr_ptr_thunk<228>,
    &bool_ptr_ptr_thunk<229>,
    &bool_ptr_ptr_thunk<230>,
    &bool_ptr_ptr_thunk<231>,
    &bool_ptr_ptr_thunk<232>,
    &bool_ptr_ptr_thunk<233>,
    &bool_ptr_ptr_thunk<234>,
    &bool_ptr_ptr_thunk<235>,
    &bool_ptr_ptr_thunk<236>,
    &bool_ptr_ptr_thunk<237>,
    &bool_ptr_ptr_thunk<238>,
    &bool_ptr_ptr_thunk<239>,
    &bool_ptr_ptr_thunk<240>,
    &bool_ptr_ptr_thunk<241>,
    &bool_ptr_ptr_thunk<242>,
    &bool_ptr_ptr_thunk<243>,
    &bool_ptr_ptr_thunk<244>,
    &bool_ptr_ptr_thunk<245>,
    &bool_ptr_ptr_thunk<246>,
    &bool_ptr_ptr_thunk<247>,
    &bool_ptr_ptr_thunk<248>,
    &bool_ptr_ptr_thunk<249>,
    &bool_ptr_ptr_thunk<250>,
    &bool_ptr_ptr_thunk<251>,
    &bool_ptr_ptr_thunk<252>,
    &bool_ptr_ptr_thunk<253>,
    &bool_ptr_ptr_thunk<254>,
    &bool_ptr_ptr_thunk<255>
};

void *bridge_bool_ptr_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(bool_ptr_ptr_mutex);
    auto existing = bool_ptr_ptr_index.find(target_ptr);
    if (existing != bool_ptr_ptr_index.end()) {
        ++existing->second.refs;
        return reinterpret_cast<void *>(bool_ptr_ptr_thunks[existing->second.index]);
    }
    uint32_t index;
    if (!bool_ptr_ptr_free_slots.empty()) {
        index = bool_ptr_ptr_free_slots.back();
        bool_ptr_ptr_free_slots.pop_back();
    } else if (bool_ptr_ptr_high_water < 256) {
        index = bool_ptr_ptr_high_water++;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge pool exhausted for the 'bool_ptr_ptr' signature family:\n"
                "  All 256 bridge slots are in use. Every distinct live native callback of this\n"
                "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\n"
                "  in worlds with very many grids/blocks can register more than the pool can hold.\n"
                "  Fix: raise this family in CALLBACK_SLOTS (currently 256) in tools/generate_havok_wrapper.py,\n"
                "  regenerate the thunks (python3 tools/generate_havok_wrapper.py), and rebuild.\n");
        std::abort();
    }
    bool_ptr_ptr_targets[index].store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
    bool_ptr_ptr_index.emplace(target_ptr, bool_ptr_ptr_slot{index, 1});
    return reinterpret_cast<void *>(bool_ptr_ptr_thunks[index]);
}

void release_bool_ptr_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(bool_ptr_ptr_mutex);
    auto it = bool_ptr_ptr_index.find(target_ptr);
    if (it == bool_ptr_ptr_index.end()) {
        return;
    }
    if (--it->second.refs == 0) {
        uint32_t index = it->second.index;
        bool_ptr_ptr_targets[index].store(0, std::memory_order_release);
        bool_ptr_ptr_free_slots.push_back(index);
        bool_ptr_ptr_index.erase(it);
    }
}
