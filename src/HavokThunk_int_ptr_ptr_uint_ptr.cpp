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
using int_ptr_ptr_uint_ptr_ms_t = int32_t (WINAPI *)(void* arg0, void* arg1, uint32_t arg2, void* arg3);
static std::mutex int_ptr_ptr_uint_ptr_mutex;

// Per-slot target read lock-free by the thunks (acquire) and published by
// bridge()/release() (release) under the mutex. Slot management (dedup +
// allocation) is O(1): an index map for dedup and a free-list stack for
// allocation, both touched only under the mutex, never by the thunks.
static std::atomic_uintptr_t int_ptr_ptr_uint_ptr_targets[256] = {};
struct int_ptr_ptr_uint_ptr_slot { uint32_t index; uint32_t refs; };
static std::unordered_map<void *, int_ptr_ptr_uint_ptr_slot> int_ptr_ptr_uint_ptr_index;
static std::vector<uint32_t> int_ptr_ptr_uint_ptr_free_slots;
static uint32_t int_ptr_ptr_uint_ptr_high_water = 0;

template <size_t Index>
static int32_t WINAPI int_ptr_ptr_uint_ptr_thunk(void* arg0, void* arg1, uint32_t arg2, void* arg3)
{
    auto fn_bits = int_ptr_ptr_uint_ptr_targets[Index].load(std::memory_order_acquire);
    auto fn = reinterpret_cast<int_ptr_ptr_uint_ptr_sysv_t>(fn_bits);
    if (!fn) {
        return int32_t{};
    }
    return fn(arg0, arg1, arg2, arg3);
}

static int_ptr_ptr_uint_ptr_ms_t int_ptr_ptr_uint_ptr_thunks[256] = {
    &int_ptr_ptr_uint_ptr_thunk<0>,
    &int_ptr_ptr_uint_ptr_thunk<1>,
    &int_ptr_ptr_uint_ptr_thunk<2>,
    &int_ptr_ptr_uint_ptr_thunk<3>,
    &int_ptr_ptr_uint_ptr_thunk<4>,
    &int_ptr_ptr_uint_ptr_thunk<5>,
    &int_ptr_ptr_uint_ptr_thunk<6>,
    &int_ptr_ptr_uint_ptr_thunk<7>,
    &int_ptr_ptr_uint_ptr_thunk<8>,
    &int_ptr_ptr_uint_ptr_thunk<9>,
    &int_ptr_ptr_uint_ptr_thunk<10>,
    &int_ptr_ptr_uint_ptr_thunk<11>,
    &int_ptr_ptr_uint_ptr_thunk<12>,
    &int_ptr_ptr_uint_ptr_thunk<13>,
    &int_ptr_ptr_uint_ptr_thunk<14>,
    &int_ptr_ptr_uint_ptr_thunk<15>,
    &int_ptr_ptr_uint_ptr_thunk<16>,
    &int_ptr_ptr_uint_ptr_thunk<17>,
    &int_ptr_ptr_uint_ptr_thunk<18>,
    &int_ptr_ptr_uint_ptr_thunk<19>,
    &int_ptr_ptr_uint_ptr_thunk<20>,
    &int_ptr_ptr_uint_ptr_thunk<21>,
    &int_ptr_ptr_uint_ptr_thunk<22>,
    &int_ptr_ptr_uint_ptr_thunk<23>,
    &int_ptr_ptr_uint_ptr_thunk<24>,
    &int_ptr_ptr_uint_ptr_thunk<25>,
    &int_ptr_ptr_uint_ptr_thunk<26>,
    &int_ptr_ptr_uint_ptr_thunk<27>,
    &int_ptr_ptr_uint_ptr_thunk<28>,
    &int_ptr_ptr_uint_ptr_thunk<29>,
    &int_ptr_ptr_uint_ptr_thunk<30>,
    &int_ptr_ptr_uint_ptr_thunk<31>,
    &int_ptr_ptr_uint_ptr_thunk<32>,
    &int_ptr_ptr_uint_ptr_thunk<33>,
    &int_ptr_ptr_uint_ptr_thunk<34>,
    &int_ptr_ptr_uint_ptr_thunk<35>,
    &int_ptr_ptr_uint_ptr_thunk<36>,
    &int_ptr_ptr_uint_ptr_thunk<37>,
    &int_ptr_ptr_uint_ptr_thunk<38>,
    &int_ptr_ptr_uint_ptr_thunk<39>,
    &int_ptr_ptr_uint_ptr_thunk<40>,
    &int_ptr_ptr_uint_ptr_thunk<41>,
    &int_ptr_ptr_uint_ptr_thunk<42>,
    &int_ptr_ptr_uint_ptr_thunk<43>,
    &int_ptr_ptr_uint_ptr_thunk<44>,
    &int_ptr_ptr_uint_ptr_thunk<45>,
    &int_ptr_ptr_uint_ptr_thunk<46>,
    &int_ptr_ptr_uint_ptr_thunk<47>,
    &int_ptr_ptr_uint_ptr_thunk<48>,
    &int_ptr_ptr_uint_ptr_thunk<49>,
    &int_ptr_ptr_uint_ptr_thunk<50>,
    &int_ptr_ptr_uint_ptr_thunk<51>,
    &int_ptr_ptr_uint_ptr_thunk<52>,
    &int_ptr_ptr_uint_ptr_thunk<53>,
    &int_ptr_ptr_uint_ptr_thunk<54>,
    &int_ptr_ptr_uint_ptr_thunk<55>,
    &int_ptr_ptr_uint_ptr_thunk<56>,
    &int_ptr_ptr_uint_ptr_thunk<57>,
    &int_ptr_ptr_uint_ptr_thunk<58>,
    &int_ptr_ptr_uint_ptr_thunk<59>,
    &int_ptr_ptr_uint_ptr_thunk<60>,
    &int_ptr_ptr_uint_ptr_thunk<61>,
    &int_ptr_ptr_uint_ptr_thunk<62>,
    &int_ptr_ptr_uint_ptr_thunk<63>,
    &int_ptr_ptr_uint_ptr_thunk<64>,
    &int_ptr_ptr_uint_ptr_thunk<65>,
    &int_ptr_ptr_uint_ptr_thunk<66>,
    &int_ptr_ptr_uint_ptr_thunk<67>,
    &int_ptr_ptr_uint_ptr_thunk<68>,
    &int_ptr_ptr_uint_ptr_thunk<69>,
    &int_ptr_ptr_uint_ptr_thunk<70>,
    &int_ptr_ptr_uint_ptr_thunk<71>,
    &int_ptr_ptr_uint_ptr_thunk<72>,
    &int_ptr_ptr_uint_ptr_thunk<73>,
    &int_ptr_ptr_uint_ptr_thunk<74>,
    &int_ptr_ptr_uint_ptr_thunk<75>,
    &int_ptr_ptr_uint_ptr_thunk<76>,
    &int_ptr_ptr_uint_ptr_thunk<77>,
    &int_ptr_ptr_uint_ptr_thunk<78>,
    &int_ptr_ptr_uint_ptr_thunk<79>,
    &int_ptr_ptr_uint_ptr_thunk<80>,
    &int_ptr_ptr_uint_ptr_thunk<81>,
    &int_ptr_ptr_uint_ptr_thunk<82>,
    &int_ptr_ptr_uint_ptr_thunk<83>,
    &int_ptr_ptr_uint_ptr_thunk<84>,
    &int_ptr_ptr_uint_ptr_thunk<85>,
    &int_ptr_ptr_uint_ptr_thunk<86>,
    &int_ptr_ptr_uint_ptr_thunk<87>,
    &int_ptr_ptr_uint_ptr_thunk<88>,
    &int_ptr_ptr_uint_ptr_thunk<89>,
    &int_ptr_ptr_uint_ptr_thunk<90>,
    &int_ptr_ptr_uint_ptr_thunk<91>,
    &int_ptr_ptr_uint_ptr_thunk<92>,
    &int_ptr_ptr_uint_ptr_thunk<93>,
    &int_ptr_ptr_uint_ptr_thunk<94>,
    &int_ptr_ptr_uint_ptr_thunk<95>,
    &int_ptr_ptr_uint_ptr_thunk<96>,
    &int_ptr_ptr_uint_ptr_thunk<97>,
    &int_ptr_ptr_uint_ptr_thunk<98>,
    &int_ptr_ptr_uint_ptr_thunk<99>,
    &int_ptr_ptr_uint_ptr_thunk<100>,
    &int_ptr_ptr_uint_ptr_thunk<101>,
    &int_ptr_ptr_uint_ptr_thunk<102>,
    &int_ptr_ptr_uint_ptr_thunk<103>,
    &int_ptr_ptr_uint_ptr_thunk<104>,
    &int_ptr_ptr_uint_ptr_thunk<105>,
    &int_ptr_ptr_uint_ptr_thunk<106>,
    &int_ptr_ptr_uint_ptr_thunk<107>,
    &int_ptr_ptr_uint_ptr_thunk<108>,
    &int_ptr_ptr_uint_ptr_thunk<109>,
    &int_ptr_ptr_uint_ptr_thunk<110>,
    &int_ptr_ptr_uint_ptr_thunk<111>,
    &int_ptr_ptr_uint_ptr_thunk<112>,
    &int_ptr_ptr_uint_ptr_thunk<113>,
    &int_ptr_ptr_uint_ptr_thunk<114>,
    &int_ptr_ptr_uint_ptr_thunk<115>,
    &int_ptr_ptr_uint_ptr_thunk<116>,
    &int_ptr_ptr_uint_ptr_thunk<117>,
    &int_ptr_ptr_uint_ptr_thunk<118>,
    &int_ptr_ptr_uint_ptr_thunk<119>,
    &int_ptr_ptr_uint_ptr_thunk<120>,
    &int_ptr_ptr_uint_ptr_thunk<121>,
    &int_ptr_ptr_uint_ptr_thunk<122>,
    &int_ptr_ptr_uint_ptr_thunk<123>,
    &int_ptr_ptr_uint_ptr_thunk<124>,
    &int_ptr_ptr_uint_ptr_thunk<125>,
    &int_ptr_ptr_uint_ptr_thunk<126>,
    &int_ptr_ptr_uint_ptr_thunk<127>,
    &int_ptr_ptr_uint_ptr_thunk<128>,
    &int_ptr_ptr_uint_ptr_thunk<129>,
    &int_ptr_ptr_uint_ptr_thunk<130>,
    &int_ptr_ptr_uint_ptr_thunk<131>,
    &int_ptr_ptr_uint_ptr_thunk<132>,
    &int_ptr_ptr_uint_ptr_thunk<133>,
    &int_ptr_ptr_uint_ptr_thunk<134>,
    &int_ptr_ptr_uint_ptr_thunk<135>,
    &int_ptr_ptr_uint_ptr_thunk<136>,
    &int_ptr_ptr_uint_ptr_thunk<137>,
    &int_ptr_ptr_uint_ptr_thunk<138>,
    &int_ptr_ptr_uint_ptr_thunk<139>,
    &int_ptr_ptr_uint_ptr_thunk<140>,
    &int_ptr_ptr_uint_ptr_thunk<141>,
    &int_ptr_ptr_uint_ptr_thunk<142>,
    &int_ptr_ptr_uint_ptr_thunk<143>,
    &int_ptr_ptr_uint_ptr_thunk<144>,
    &int_ptr_ptr_uint_ptr_thunk<145>,
    &int_ptr_ptr_uint_ptr_thunk<146>,
    &int_ptr_ptr_uint_ptr_thunk<147>,
    &int_ptr_ptr_uint_ptr_thunk<148>,
    &int_ptr_ptr_uint_ptr_thunk<149>,
    &int_ptr_ptr_uint_ptr_thunk<150>,
    &int_ptr_ptr_uint_ptr_thunk<151>,
    &int_ptr_ptr_uint_ptr_thunk<152>,
    &int_ptr_ptr_uint_ptr_thunk<153>,
    &int_ptr_ptr_uint_ptr_thunk<154>,
    &int_ptr_ptr_uint_ptr_thunk<155>,
    &int_ptr_ptr_uint_ptr_thunk<156>,
    &int_ptr_ptr_uint_ptr_thunk<157>,
    &int_ptr_ptr_uint_ptr_thunk<158>,
    &int_ptr_ptr_uint_ptr_thunk<159>,
    &int_ptr_ptr_uint_ptr_thunk<160>,
    &int_ptr_ptr_uint_ptr_thunk<161>,
    &int_ptr_ptr_uint_ptr_thunk<162>,
    &int_ptr_ptr_uint_ptr_thunk<163>,
    &int_ptr_ptr_uint_ptr_thunk<164>,
    &int_ptr_ptr_uint_ptr_thunk<165>,
    &int_ptr_ptr_uint_ptr_thunk<166>,
    &int_ptr_ptr_uint_ptr_thunk<167>,
    &int_ptr_ptr_uint_ptr_thunk<168>,
    &int_ptr_ptr_uint_ptr_thunk<169>,
    &int_ptr_ptr_uint_ptr_thunk<170>,
    &int_ptr_ptr_uint_ptr_thunk<171>,
    &int_ptr_ptr_uint_ptr_thunk<172>,
    &int_ptr_ptr_uint_ptr_thunk<173>,
    &int_ptr_ptr_uint_ptr_thunk<174>,
    &int_ptr_ptr_uint_ptr_thunk<175>,
    &int_ptr_ptr_uint_ptr_thunk<176>,
    &int_ptr_ptr_uint_ptr_thunk<177>,
    &int_ptr_ptr_uint_ptr_thunk<178>,
    &int_ptr_ptr_uint_ptr_thunk<179>,
    &int_ptr_ptr_uint_ptr_thunk<180>,
    &int_ptr_ptr_uint_ptr_thunk<181>,
    &int_ptr_ptr_uint_ptr_thunk<182>,
    &int_ptr_ptr_uint_ptr_thunk<183>,
    &int_ptr_ptr_uint_ptr_thunk<184>,
    &int_ptr_ptr_uint_ptr_thunk<185>,
    &int_ptr_ptr_uint_ptr_thunk<186>,
    &int_ptr_ptr_uint_ptr_thunk<187>,
    &int_ptr_ptr_uint_ptr_thunk<188>,
    &int_ptr_ptr_uint_ptr_thunk<189>,
    &int_ptr_ptr_uint_ptr_thunk<190>,
    &int_ptr_ptr_uint_ptr_thunk<191>,
    &int_ptr_ptr_uint_ptr_thunk<192>,
    &int_ptr_ptr_uint_ptr_thunk<193>,
    &int_ptr_ptr_uint_ptr_thunk<194>,
    &int_ptr_ptr_uint_ptr_thunk<195>,
    &int_ptr_ptr_uint_ptr_thunk<196>,
    &int_ptr_ptr_uint_ptr_thunk<197>,
    &int_ptr_ptr_uint_ptr_thunk<198>,
    &int_ptr_ptr_uint_ptr_thunk<199>,
    &int_ptr_ptr_uint_ptr_thunk<200>,
    &int_ptr_ptr_uint_ptr_thunk<201>,
    &int_ptr_ptr_uint_ptr_thunk<202>,
    &int_ptr_ptr_uint_ptr_thunk<203>,
    &int_ptr_ptr_uint_ptr_thunk<204>,
    &int_ptr_ptr_uint_ptr_thunk<205>,
    &int_ptr_ptr_uint_ptr_thunk<206>,
    &int_ptr_ptr_uint_ptr_thunk<207>,
    &int_ptr_ptr_uint_ptr_thunk<208>,
    &int_ptr_ptr_uint_ptr_thunk<209>,
    &int_ptr_ptr_uint_ptr_thunk<210>,
    &int_ptr_ptr_uint_ptr_thunk<211>,
    &int_ptr_ptr_uint_ptr_thunk<212>,
    &int_ptr_ptr_uint_ptr_thunk<213>,
    &int_ptr_ptr_uint_ptr_thunk<214>,
    &int_ptr_ptr_uint_ptr_thunk<215>,
    &int_ptr_ptr_uint_ptr_thunk<216>,
    &int_ptr_ptr_uint_ptr_thunk<217>,
    &int_ptr_ptr_uint_ptr_thunk<218>,
    &int_ptr_ptr_uint_ptr_thunk<219>,
    &int_ptr_ptr_uint_ptr_thunk<220>,
    &int_ptr_ptr_uint_ptr_thunk<221>,
    &int_ptr_ptr_uint_ptr_thunk<222>,
    &int_ptr_ptr_uint_ptr_thunk<223>,
    &int_ptr_ptr_uint_ptr_thunk<224>,
    &int_ptr_ptr_uint_ptr_thunk<225>,
    &int_ptr_ptr_uint_ptr_thunk<226>,
    &int_ptr_ptr_uint_ptr_thunk<227>,
    &int_ptr_ptr_uint_ptr_thunk<228>,
    &int_ptr_ptr_uint_ptr_thunk<229>,
    &int_ptr_ptr_uint_ptr_thunk<230>,
    &int_ptr_ptr_uint_ptr_thunk<231>,
    &int_ptr_ptr_uint_ptr_thunk<232>,
    &int_ptr_ptr_uint_ptr_thunk<233>,
    &int_ptr_ptr_uint_ptr_thunk<234>,
    &int_ptr_ptr_uint_ptr_thunk<235>,
    &int_ptr_ptr_uint_ptr_thunk<236>,
    &int_ptr_ptr_uint_ptr_thunk<237>,
    &int_ptr_ptr_uint_ptr_thunk<238>,
    &int_ptr_ptr_uint_ptr_thunk<239>,
    &int_ptr_ptr_uint_ptr_thunk<240>,
    &int_ptr_ptr_uint_ptr_thunk<241>,
    &int_ptr_ptr_uint_ptr_thunk<242>,
    &int_ptr_ptr_uint_ptr_thunk<243>,
    &int_ptr_ptr_uint_ptr_thunk<244>,
    &int_ptr_ptr_uint_ptr_thunk<245>,
    &int_ptr_ptr_uint_ptr_thunk<246>,
    &int_ptr_ptr_uint_ptr_thunk<247>,
    &int_ptr_ptr_uint_ptr_thunk<248>,
    &int_ptr_ptr_uint_ptr_thunk<249>,
    &int_ptr_ptr_uint_ptr_thunk<250>,
    &int_ptr_ptr_uint_ptr_thunk<251>,
    &int_ptr_ptr_uint_ptr_thunk<252>,
    &int_ptr_ptr_uint_ptr_thunk<253>,
    &int_ptr_ptr_uint_ptr_thunk<254>,
    &int_ptr_ptr_uint_ptr_thunk<255>
};

void *bridge_int_ptr_ptr_uint_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(int_ptr_ptr_uint_ptr_mutex);
    auto existing = int_ptr_ptr_uint_ptr_index.find(target_ptr);
    if (existing != int_ptr_ptr_uint_ptr_index.end()) {
        ++existing->second.refs;
        return reinterpret_cast<void *>(int_ptr_ptr_uint_ptr_thunks[existing->second.index]);
    }
    uint32_t index;
    if (!int_ptr_ptr_uint_ptr_free_slots.empty()) {
        index = int_ptr_ptr_uint_ptr_free_slots.back();
        int_ptr_ptr_uint_ptr_free_slots.pop_back();
    } else if (int_ptr_ptr_uint_ptr_high_water < 256) {
        index = int_ptr_ptr_uint_ptr_high_water++;
    } else {
        fprintf(stderr,
                "FATAL: Havok callback bridge pool exhausted for the 'int_ptr_ptr_uint_ptr' signature family:\n"
                "  All 256 bridge slots are in use. Every distinct live native callback of this\n"
                "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\n"
                "  in worlds with very many grids/blocks can register more than the pool can hold.\n"
                "  Fix: raise this family in CALLBACK_SLOTS (currently 256) in tools/generate_havok_wrapper.py,\n"
                "  regenerate the thunks (python3 tools/generate_havok_wrapper.py), and rebuild.\n");
        std::abort();
    }
    int_ptr_ptr_uint_ptr_targets[index].store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);
    int_ptr_ptr_uint_ptr_index.emplace(target_ptr, int_ptr_ptr_uint_ptr_slot{index, 1});
    return reinterpret_cast<void *>(int_ptr_ptr_uint_ptr_thunks[index]);
}

void release_int_ptr_ptr_uint_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(int_ptr_ptr_uint_ptr_mutex);
    auto it = int_ptr_ptr_uint_ptr_index.find(target_ptr);
    if (it == int_ptr_ptr_uint_ptr_index.end()) {
        return;
    }
    if (--it->second.refs == 0) {
        uint32_t index = it->second.index;
        int_ptr_ptr_uint_ptr_targets[index].store(0, std::memory_order_release);
        int_ptr_ptr_uint_ptr_free_slots.push_back(index);
        int_ptr_ptr_uint_ptr_index.erase(it);
    }
}
