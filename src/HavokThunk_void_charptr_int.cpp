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
using void_charptr_int_ms_t = void (WINAPI *)(char* arg0, int32_t arg1);
static std::mutex void_charptr_int_mutex;

// Per-slot target read lock-free by the thunks (acquire) and published by
// bridge()/release() (release) under the mutex. Slot management (dedup +
// allocation) is O(1): an index map for dedup and a free-list stack for
// allocation, both touched only under the mutex, never by the thunks.
static std::atomic_uintptr_t void_charptr_int_targets[256] = {};
struct void_charptr_int_slot { uint32_t index; uint32_t refs; };
static std::unordered_map<void *, void_charptr_int_slot> void_charptr_int_index;
static std::vector<uint32_t> void_charptr_int_free_slots;
static uint32_t void_charptr_int_high_water = 0;

template <size_t Index>
static void WINAPI void_charptr_int_thunk(char* arg0, int32_t arg1)
{
    auto fn_bits = void_charptr_int_targets[Index].load(std::memory_order_acquire);
    auto fn = reinterpret_cast<void_charptr_int_sysv_t>(fn_bits);
    if (!fn) {
        return;
    }
    fn(arg0, arg1);
}

static void_charptr_int_ms_t void_charptr_int_thunks[256] = {
    &void_charptr_int_thunk<0>,
    &void_charptr_int_thunk<1>,
    &void_charptr_int_thunk<2>,
    &void_charptr_int_thunk<3>,
    &void_charptr_int_thunk<4>,
    &void_charptr_int_thunk<5>,
    &void_charptr_int_thunk<6>,
    &void_charptr_int_thunk<7>,
    &void_charptr_int_thunk<8>,
    &void_charptr_int_thunk<9>,
    &void_charptr_int_thunk<10>,
    &void_charptr_int_thunk<11>,
    &void_charptr_int_thunk<12>,
    &void_charptr_int_thunk<13>,
    &void_charptr_int_thunk<14>,
    &void_charptr_int_thunk<15>,
    &void_charptr_int_thunk<16>,
    &void_charptr_int_thunk<17>,
    &void_charptr_int_thunk<18>,
    &void_charptr_int_thunk<19>,
    &void_charptr_int_thunk<20>,
    &void_charptr_int_thunk<21>,
    &void_charptr_int_thunk<22>,
    &void_charptr_int_thunk<23>,
    &void_charptr_int_thunk<24>,
    &void_charptr_int_thunk<25>,
    &void_charptr_int_thunk<26>,
    &void_charptr_int_thunk<27>,
    &void_charptr_int_thunk<28>,
    &void_charptr_int_thunk<29>,
    &void_charptr_int_thunk<30>,
    &void_charptr_int_thunk<31>,
    &void_charptr_int_thunk<32>,
    &void_charptr_int_thunk<33>,
    &void_charptr_int_thunk<34>,
    &void_charptr_int_thunk<35>,
    &void_charptr_int_thunk<36>,
    &void_charptr_int_thunk<37>,
    &void_charptr_int_thunk<38>,
    &void_charptr_int_thunk<39>,
    &void_charptr_int_thunk<40>,
    &void_charptr_int_thunk<41>,
    &void_charptr_int_thunk<42>,
    &void_charptr_int_thunk<43>,
    &void_charptr_int_thunk<44>,
    &void_charptr_int_thunk<45>,
    &void_charptr_int_thunk<46>,
    &void_charptr_int_thunk<47>,
    &void_charptr_int_thunk<48>,
    &void_charptr_int_thunk<49>,
    &void_charptr_int_thunk<50>,
    &void_charptr_int_thunk<51>,
    &void_charptr_int_thunk<52>,
    &void_charptr_int_thunk<53>,
    &void_charptr_int_thunk<54>,
    &void_charptr_int_thunk<55>,
    &void_charptr_int_thunk<56>,
    &void_charptr_int_thunk<57>,
    &void_charptr_int_thunk<58>,
    &void_charptr_int_thunk<59>,
    &void_charptr_int_thunk<60>,
    &void_charptr_int_thunk<61>,
    &void_charptr_int_thunk<62>,
    &void_charptr_int_thunk<63>,
    &void_charptr_int_thunk<64>,
    &void_charptr_int_thunk<65>,
    &void_charptr_int_thunk<66>,
    &void_charptr_int_thunk<67>,
    &void_charptr_int_thunk<68>,
    &void_charptr_int_thunk<69>,
    &void_charptr_int_thunk<70>,
    &void_charptr_int_thunk<71>,
    &void_charptr_int_thunk<72>,
    &void_charptr_int_thunk<73>,
    &void_charptr_int_thunk<74>,
    &void_charptr_int_thunk<75>,
    &void_charptr_int_thunk<76>,
    &void_charptr_int_thunk<77>,
    &void_charptr_int_thunk<78>,
    &void_charptr_int_thunk<79>,
    &void_charptr_int_thunk<80>,
    &void_charptr_int_thunk<81>,
    &void_charptr_int_thunk<82>,
    &void_charptr_int_thunk<83>,
    &void_charptr_int_thunk<84>,
    &void_charptr_int_thunk<85>,
    &void_charptr_int_thunk<86>,
    &void_charptr_int_thunk<87>,
    &void_charptr_int_thunk<88>,
    &void_charptr_int_thunk<89>,
    &void_charptr_int_thunk<90>,
    &void_charptr_int_thunk<91>,
    &void_charptr_int_thunk<92>,
    &void_charptr_int_thunk<93>,
    &void_charptr_int_thunk<94>,
    &void_charptr_int_thunk<95>,
    &void_charptr_int_thunk<96>,
    &void_charptr_int_thunk<97>,
    &void_charptr_int_thunk<98>,
    &void_charptr_int_thunk<99>,
    &void_charptr_int_thunk<100>,
    &void_charptr_int_thunk<101>,
    &void_charptr_int_thunk<102>,
    &void_charptr_int_thunk<103>,
    &void_charptr_int_thunk<104>,
    &void_charptr_int_thunk<105>,
    &void_charptr_int_thunk<106>,
    &void_charptr_int_thunk<107>,
    &void_charptr_int_thunk<108>,
    &void_charptr_int_thunk<109>,
    &void_charptr_int_thunk<110>,
    &void_charptr_int_thunk<111>,
    &void_charptr_int_thunk<112>,
    &void_charptr_int_thunk<113>,
    &void_charptr_int_thunk<114>,
    &void_charptr_int_thunk<115>,
    &void_charptr_int_thunk<116>,
    &void_charptr_int_thunk<117>,
    &void_charptr_int_thunk<118>,
    &void_charptr_int_thunk<119>,
    &void_charptr_int_thunk<120>,
    &void_charptr_int_thunk<121>,
    &void_charptr_int_thunk<122>,
    &void_charptr_int_thunk<123>,
    &void_charptr_int_thunk<124>,
    &void_charptr_int_thunk<125>,
    &void_charptr_int_thunk<126>,
    &void_charptr_int_thunk<127>,
    &void_charptr_int_thunk<128>,
    &void_charptr_int_thunk<129>,
    &void_charptr_int_thunk<130>,
    &void_charptr_int_thunk<131>,
    &void_charptr_int_thunk<132>,
    &void_charptr_int_thunk<133>,
    &void_charptr_int_thunk<134>,
    &void_charptr_int_thunk<135>,
    &void_charptr_int_thunk<136>,
    &void_charptr_int_thunk<137>,
    &void_charptr_int_thunk<138>,
    &void_charptr_int_thunk<139>,
    &void_charptr_int_thunk<140>,
    &void_charptr_int_thunk<141>,
    &void_charptr_int_thunk<142>,
    &void_charptr_int_thunk<143>,
    &void_charptr_int_thunk<144>,
    &void_charptr_int_thunk<145>,
    &void_charptr_int_thunk<146>,
    &void_charptr_int_thunk<147>,
    &void_charptr_int_thunk<148>,
    &void_charptr_int_thunk<149>,
    &void_charptr_int_thunk<150>,
    &void_charptr_int_thunk<151>,
    &void_charptr_int_thunk<152>,
    &void_charptr_int_thunk<153>,
    &void_charptr_int_thunk<154>,
    &void_charptr_int_thunk<155>,
    &void_charptr_int_thunk<156>,
    &void_charptr_int_thunk<157>,
    &void_charptr_int_thunk<158>,
    &void_charptr_int_thunk<159>,
    &void_charptr_int_thunk<160>,
    &void_charptr_int_thunk<161>,
    &void_charptr_int_thunk<162>,
    &void_charptr_int_thunk<163>,
    &void_charptr_int_thunk<164>,
    &void_charptr_int_thunk<165>,
    &void_charptr_int_thunk<166>,
    &void_charptr_int_thunk<167>,
    &void_charptr_int_thunk<168>,
    &void_charptr_int_thunk<169>,
    &void_charptr_int_thunk<170>,
    &void_charptr_int_thunk<171>,
    &void_charptr_int_thunk<172>,
    &void_charptr_int_thunk<173>,
    &void_charptr_int_thunk<174>,
    &void_charptr_int_thunk<175>,
    &void_charptr_int_thunk<176>,
    &void_charptr_int_thunk<177>,
    &void_charptr_int_thunk<178>,
    &void_charptr_int_thunk<179>,
    &void_charptr_int_thunk<180>,
    &void_charptr_int_thunk<181>,
    &void_charptr_int_thunk<182>,
    &void_charptr_int_thunk<183>,
    &void_charptr_int_thunk<184>,
    &void_charptr_int_thunk<185>,
    &void_charptr_int_thunk<186>,
    &void_charptr_int_thunk<187>,
    &void_charptr_int_thunk<188>,
    &void_charptr_int_thunk<189>,
    &void_charptr_int_thunk<190>,
    &void_charptr_int_thunk<191>,
    &void_charptr_int_thunk<192>,
    &void_charptr_int_thunk<193>,
    &void_charptr_int_thunk<194>,
    &void_charptr_int_thunk<195>,
    &void_charptr_int_thunk<196>,
    &void_charptr_int_thunk<197>,
    &void_charptr_int_thunk<198>,
    &void_charptr_int_thunk<199>,
    &void_charptr_int_thunk<200>,
    &void_charptr_int_thunk<201>,
    &void_charptr_int_thunk<202>,
    &void_charptr_int_thunk<203>,
    &void_charptr_int_thunk<204>,
    &void_charptr_int_thunk<205>,
    &void_charptr_int_thunk<206>,
    &void_charptr_int_thunk<207>,
    &void_charptr_int_thunk<208>,
    &void_charptr_int_thunk<209>,
    &void_charptr_int_thunk<210>,
    &void_charptr_int_thunk<211>,
    &void_charptr_int_thunk<212>,
    &void_charptr_int_thunk<213>,
    &void_charptr_int_thunk<214>,
    &void_charptr_int_thunk<215>,
    &void_charptr_int_thunk<216>,
    &void_charptr_int_thunk<217>,
    &void_charptr_int_thunk<218>,
    &void_charptr_int_thunk<219>,
    &void_charptr_int_thunk<220>,
    &void_charptr_int_thunk<221>,
    &void_charptr_int_thunk<222>,
    &void_charptr_int_thunk<223>,
    &void_charptr_int_thunk<224>,
    &void_charptr_int_thunk<225>,
    &void_charptr_int_thunk<226>,
    &void_charptr_int_thunk<227>,
    &void_charptr_int_thunk<228>,
    &void_charptr_int_thunk<229>,
    &void_charptr_int_thunk<230>,
    &void_charptr_int_thunk<231>,
    &void_charptr_int_thunk<232>,
    &void_charptr_int_thunk<233>,
    &void_charptr_int_thunk<234>,
    &void_charptr_int_thunk<235>,
    &void_charptr_int_thunk<236>,
    &void_charptr_int_thunk<237>,
    &void_charptr_int_thunk<238>,
    &void_charptr_int_thunk<239>,
    &void_charptr_int_thunk<240>,
    &void_charptr_int_thunk<241>,
    &void_charptr_int_thunk<242>,
    &void_charptr_int_thunk<243>,
    &void_charptr_int_thunk<244>,
    &void_charptr_int_thunk<245>,
    &void_charptr_int_thunk<246>,
    &void_charptr_int_thunk<247>,
    &void_charptr_int_thunk<248>,
    &void_charptr_int_thunk<249>,
    &void_charptr_int_thunk<250>,
    &void_charptr_int_thunk<251>,
    &void_charptr_int_thunk<252>,
    &void_charptr_int_thunk<253>,
    &void_charptr_int_thunk<254>,
    &void_charptr_int_thunk<255>
};

void *bridge_void_charptr_int(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(void_charptr_int_mutex);
    auto existing = void_charptr_int_index.find(target_ptr);
    if (existing != void_charptr_int_index.end()) {
        ++existing->second.refs;
        return reinterpret_cast<void *>(void_charptr_int_thunks[existing->second.index]);
    }
    uint32_t index;
    if (!void_charptr_int_free_slots.empty()) {
        index = void_charptr_int_free_slots.back();
        void_charptr_int_free_slots.pop_back();
    } else if (void_charptr_int_high_water < 256) {
        index = void_charptr_int_high_water++;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge pool exhausted for the 'void_charptr_int' signature family:\n"
                "  All 256 bridge slots are in use. Every distinct live native callback of this\n"
                "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\n"
                "  in worlds with very many grids/blocks can register more than the pool can hold.\n"
                "  Fix: raise this family in CALLBACK_SLOTS (currently 256) in tools/generate_havok_wrapper.py,\n"
                "  regenerate the thunks (python3 tools/generate_havok_wrapper.py), and rebuild.\n");
        std::abort();
    }
    void_charptr_int_targets[index].store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
    void_charptr_int_index.emplace(target_ptr, void_charptr_int_slot{index, 1});
    return reinterpret_cast<void *>(void_charptr_int_thunks[index]);
}

void release_void_charptr_int(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(void_charptr_int_mutex);
    auto it = void_charptr_int_index.find(target_ptr);
    if (it == void_charptr_int_index.end()) {
        return;
    }
    if (--it->second.refs == 0) {
        uint32_t index = it->second.index;
        void_charptr_int_targets[index].store(0, std::memory_order_release);
        void_charptr_int_free_slots.push_back(index);
        void_charptr_int_index.erase(it);
    }
}
