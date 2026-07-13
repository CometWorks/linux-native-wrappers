// ============================================================================
// GENERATED FILE -- do not edit by hand.
// Regenerate with: python3 tools/generate_havok_thunks.py
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

#include "win_types.h"
#include "HavokThunkRegistry.h"


using float_ptr_sysv_t = float (*)(void* arg0);
using float_ptr_ms_t = float (WINAPI *)(void* arg0);
static std::mutex float_ptr_mutex;
static std::atomic_uintptr_t float_ptr_targets[256] = {};
static std::atomic_uint32_t float_ptr_refcounts[256] = {};

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

static float_ptr_ms_t float_ptr_thunks[256] = {
    &float_ptr_thunk<0>,
    &float_ptr_thunk<1>,
    &float_ptr_thunk<2>,
    &float_ptr_thunk<3>,
    &float_ptr_thunk<4>,
    &float_ptr_thunk<5>,
    &float_ptr_thunk<6>,
    &float_ptr_thunk<7>,
    &float_ptr_thunk<8>,
    &float_ptr_thunk<9>,
    &float_ptr_thunk<10>,
    &float_ptr_thunk<11>,
    &float_ptr_thunk<12>,
    &float_ptr_thunk<13>,
    &float_ptr_thunk<14>,
    &float_ptr_thunk<15>,
    &float_ptr_thunk<16>,
    &float_ptr_thunk<17>,
    &float_ptr_thunk<18>,
    &float_ptr_thunk<19>,
    &float_ptr_thunk<20>,
    &float_ptr_thunk<21>,
    &float_ptr_thunk<22>,
    &float_ptr_thunk<23>,
    &float_ptr_thunk<24>,
    &float_ptr_thunk<25>,
    &float_ptr_thunk<26>,
    &float_ptr_thunk<27>,
    &float_ptr_thunk<28>,
    &float_ptr_thunk<29>,
    &float_ptr_thunk<30>,
    &float_ptr_thunk<31>,
    &float_ptr_thunk<32>,
    &float_ptr_thunk<33>,
    &float_ptr_thunk<34>,
    &float_ptr_thunk<35>,
    &float_ptr_thunk<36>,
    &float_ptr_thunk<37>,
    &float_ptr_thunk<38>,
    &float_ptr_thunk<39>,
    &float_ptr_thunk<40>,
    &float_ptr_thunk<41>,
    &float_ptr_thunk<42>,
    &float_ptr_thunk<43>,
    &float_ptr_thunk<44>,
    &float_ptr_thunk<45>,
    &float_ptr_thunk<46>,
    &float_ptr_thunk<47>,
    &float_ptr_thunk<48>,
    &float_ptr_thunk<49>,
    &float_ptr_thunk<50>,
    &float_ptr_thunk<51>,
    &float_ptr_thunk<52>,
    &float_ptr_thunk<53>,
    &float_ptr_thunk<54>,
    &float_ptr_thunk<55>,
    &float_ptr_thunk<56>,
    &float_ptr_thunk<57>,
    &float_ptr_thunk<58>,
    &float_ptr_thunk<59>,
    &float_ptr_thunk<60>,
    &float_ptr_thunk<61>,
    &float_ptr_thunk<62>,
    &float_ptr_thunk<63>,
    &float_ptr_thunk<64>,
    &float_ptr_thunk<65>,
    &float_ptr_thunk<66>,
    &float_ptr_thunk<67>,
    &float_ptr_thunk<68>,
    &float_ptr_thunk<69>,
    &float_ptr_thunk<70>,
    &float_ptr_thunk<71>,
    &float_ptr_thunk<72>,
    &float_ptr_thunk<73>,
    &float_ptr_thunk<74>,
    &float_ptr_thunk<75>,
    &float_ptr_thunk<76>,
    &float_ptr_thunk<77>,
    &float_ptr_thunk<78>,
    &float_ptr_thunk<79>,
    &float_ptr_thunk<80>,
    &float_ptr_thunk<81>,
    &float_ptr_thunk<82>,
    &float_ptr_thunk<83>,
    &float_ptr_thunk<84>,
    &float_ptr_thunk<85>,
    &float_ptr_thunk<86>,
    &float_ptr_thunk<87>,
    &float_ptr_thunk<88>,
    &float_ptr_thunk<89>,
    &float_ptr_thunk<90>,
    &float_ptr_thunk<91>,
    &float_ptr_thunk<92>,
    &float_ptr_thunk<93>,
    &float_ptr_thunk<94>,
    &float_ptr_thunk<95>,
    &float_ptr_thunk<96>,
    &float_ptr_thunk<97>,
    &float_ptr_thunk<98>,
    &float_ptr_thunk<99>,
    &float_ptr_thunk<100>,
    &float_ptr_thunk<101>,
    &float_ptr_thunk<102>,
    &float_ptr_thunk<103>,
    &float_ptr_thunk<104>,
    &float_ptr_thunk<105>,
    &float_ptr_thunk<106>,
    &float_ptr_thunk<107>,
    &float_ptr_thunk<108>,
    &float_ptr_thunk<109>,
    &float_ptr_thunk<110>,
    &float_ptr_thunk<111>,
    &float_ptr_thunk<112>,
    &float_ptr_thunk<113>,
    &float_ptr_thunk<114>,
    &float_ptr_thunk<115>,
    &float_ptr_thunk<116>,
    &float_ptr_thunk<117>,
    &float_ptr_thunk<118>,
    &float_ptr_thunk<119>,
    &float_ptr_thunk<120>,
    &float_ptr_thunk<121>,
    &float_ptr_thunk<122>,
    &float_ptr_thunk<123>,
    &float_ptr_thunk<124>,
    &float_ptr_thunk<125>,
    &float_ptr_thunk<126>,
    &float_ptr_thunk<127>,
    &float_ptr_thunk<128>,
    &float_ptr_thunk<129>,
    &float_ptr_thunk<130>,
    &float_ptr_thunk<131>,
    &float_ptr_thunk<132>,
    &float_ptr_thunk<133>,
    &float_ptr_thunk<134>,
    &float_ptr_thunk<135>,
    &float_ptr_thunk<136>,
    &float_ptr_thunk<137>,
    &float_ptr_thunk<138>,
    &float_ptr_thunk<139>,
    &float_ptr_thunk<140>,
    &float_ptr_thunk<141>,
    &float_ptr_thunk<142>,
    &float_ptr_thunk<143>,
    &float_ptr_thunk<144>,
    &float_ptr_thunk<145>,
    &float_ptr_thunk<146>,
    &float_ptr_thunk<147>,
    &float_ptr_thunk<148>,
    &float_ptr_thunk<149>,
    &float_ptr_thunk<150>,
    &float_ptr_thunk<151>,
    &float_ptr_thunk<152>,
    &float_ptr_thunk<153>,
    &float_ptr_thunk<154>,
    &float_ptr_thunk<155>,
    &float_ptr_thunk<156>,
    &float_ptr_thunk<157>,
    &float_ptr_thunk<158>,
    &float_ptr_thunk<159>,
    &float_ptr_thunk<160>,
    &float_ptr_thunk<161>,
    &float_ptr_thunk<162>,
    &float_ptr_thunk<163>,
    &float_ptr_thunk<164>,
    &float_ptr_thunk<165>,
    &float_ptr_thunk<166>,
    &float_ptr_thunk<167>,
    &float_ptr_thunk<168>,
    &float_ptr_thunk<169>,
    &float_ptr_thunk<170>,
    &float_ptr_thunk<171>,
    &float_ptr_thunk<172>,
    &float_ptr_thunk<173>,
    &float_ptr_thunk<174>,
    &float_ptr_thunk<175>,
    &float_ptr_thunk<176>,
    &float_ptr_thunk<177>,
    &float_ptr_thunk<178>,
    &float_ptr_thunk<179>,
    &float_ptr_thunk<180>,
    &float_ptr_thunk<181>,
    &float_ptr_thunk<182>,
    &float_ptr_thunk<183>,
    &float_ptr_thunk<184>,
    &float_ptr_thunk<185>,
    &float_ptr_thunk<186>,
    &float_ptr_thunk<187>,
    &float_ptr_thunk<188>,
    &float_ptr_thunk<189>,
    &float_ptr_thunk<190>,
    &float_ptr_thunk<191>,
    &float_ptr_thunk<192>,
    &float_ptr_thunk<193>,
    &float_ptr_thunk<194>,
    &float_ptr_thunk<195>,
    &float_ptr_thunk<196>,
    &float_ptr_thunk<197>,
    &float_ptr_thunk<198>,
    &float_ptr_thunk<199>,
    &float_ptr_thunk<200>,
    &float_ptr_thunk<201>,
    &float_ptr_thunk<202>,
    &float_ptr_thunk<203>,
    &float_ptr_thunk<204>,
    &float_ptr_thunk<205>,
    &float_ptr_thunk<206>,
    &float_ptr_thunk<207>,
    &float_ptr_thunk<208>,
    &float_ptr_thunk<209>,
    &float_ptr_thunk<210>,
    &float_ptr_thunk<211>,
    &float_ptr_thunk<212>,
    &float_ptr_thunk<213>,
    &float_ptr_thunk<214>,
    &float_ptr_thunk<215>,
    &float_ptr_thunk<216>,
    &float_ptr_thunk<217>,
    &float_ptr_thunk<218>,
    &float_ptr_thunk<219>,
    &float_ptr_thunk<220>,
    &float_ptr_thunk<221>,
    &float_ptr_thunk<222>,
    &float_ptr_thunk<223>,
    &float_ptr_thunk<224>,
    &float_ptr_thunk<225>,
    &float_ptr_thunk<226>,
    &float_ptr_thunk<227>,
    &float_ptr_thunk<228>,
    &float_ptr_thunk<229>,
    &float_ptr_thunk<230>,
    &float_ptr_thunk<231>,
    &float_ptr_thunk<232>,
    &float_ptr_thunk<233>,
    &float_ptr_thunk<234>,
    &float_ptr_thunk<235>,
    &float_ptr_thunk<236>,
    &float_ptr_thunk<237>,
    &float_ptr_thunk<238>,
    &float_ptr_thunk<239>,
    &float_ptr_thunk<240>,
    &float_ptr_thunk<241>,
    &float_ptr_thunk<242>,
    &float_ptr_thunk<243>,
    &float_ptr_thunk<244>,
    &float_ptr_thunk<245>,
    &float_ptr_thunk<246>,
    &float_ptr_thunk<247>,
    &float_ptr_thunk<248>,
    &float_ptr_thunk<249>,
    &float_ptr_thunk<250>,
    &float_ptr_thunk<251>,
    &float_ptr_thunk<252>,
    &float_ptr_thunk<253>,
    &float_ptr_thunk<254>,
    &float_ptr_thunk<255>
};

void *bridge_float_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    auto target = reinterpret_cast<float_ptr_sysv_t>(target_ptr);
    std::lock_guard<std::mutex> lock(float_ptr_mutex);
    for (size_t i = 0; i < 256; ++i) {
        if (reinterpret_cast<float_ptr_sysv_t>(float_ptr_targets[i].load(std::memory_order_acquire)) == target) {
            float_ptr_refcounts[i].fetch_add(1, std::memory_order_relaxed);
            return reinterpret_cast<void *>(float_ptr_thunks[i]);
        }
    }
    for (size_t i = 0; i < 256; ++i) {
        if (float_ptr_targets[i].load(std::memory_order_acquire) == 0) {
            float_ptr_targets[i].store(reinterpret_cast<uintptr_t>(target), std::memory_order_release);
            float_ptr_refcounts[i].store(1, std::memory_order_relaxed);
            return reinterpret_cast<void *>(float_ptr_thunks[i]);
        }
    }
    fprintf(stderr,
            "FATAL: Havok callback bridge pool exhausted for the 'float_ptr' signature family:\n"
            "  All 256 bridge slots are in use. Every distinct live native callback of this\n"
            "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\n"
            "  in worlds with very many grids/blocks can register more than the pool can hold.\n"
            "  Fix: raise this family in CALLBACK_SLOTS (currently 256) in tools/generate_havok_thunks.py,\n"
            "  regenerate the thunks (python3 tools/generate_havok_thunks.py), and rebuild.\n");
    std::abort();
}

void release_float_ptr(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    auto target = reinterpret_cast<float_ptr_sysv_t>(target_ptr);
    std::lock_guard<std::mutex> lock(float_ptr_mutex);
    for (size_t i = 0; i < 256; ++i) {
        if (reinterpret_cast<float_ptr_sysv_t>(float_ptr_targets[i].load(std::memory_order_acquire)) == target) {
            auto refs = float_ptr_refcounts[i].load(std::memory_order_relaxed);
            if (refs > 0) {
                refs = float_ptr_refcounts[i].fetch_sub(1, std::memory_order_acq_rel) - 1;
            }
            if (refs == 0) {
                float_ptr_targets[i].store(0, std::memory_order_release);
            }
            return;
        }
    }
}
