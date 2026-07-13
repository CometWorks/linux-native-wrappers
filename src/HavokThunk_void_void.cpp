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


using void_void_sysv_t = void (*)(void);
using void_void_ms_t = void (WINAPI *)(void);
static std::mutex void_void_mutex;
static std::atomic_uintptr_t void_void_targets[256] = {};
static std::atomic_uint32_t void_void_refcounts[256] = {};

template <size_t Index>
static void WINAPI void_void_thunk(void)
{
    auto fn_bits = void_void_targets[Index].load(std::memory_order_acquire);
    auto fn = reinterpret_cast<void_void_sysv_t>(fn_bits);
    if (!fn) {
        return;
    }
    fn();
}

static void_void_ms_t void_void_thunks[256] = {
    &void_void_thunk<0>,
    &void_void_thunk<1>,
    &void_void_thunk<2>,
    &void_void_thunk<3>,
    &void_void_thunk<4>,
    &void_void_thunk<5>,
    &void_void_thunk<6>,
    &void_void_thunk<7>,
    &void_void_thunk<8>,
    &void_void_thunk<9>,
    &void_void_thunk<10>,
    &void_void_thunk<11>,
    &void_void_thunk<12>,
    &void_void_thunk<13>,
    &void_void_thunk<14>,
    &void_void_thunk<15>,
    &void_void_thunk<16>,
    &void_void_thunk<17>,
    &void_void_thunk<18>,
    &void_void_thunk<19>,
    &void_void_thunk<20>,
    &void_void_thunk<21>,
    &void_void_thunk<22>,
    &void_void_thunk<23>,
    &void_void_thunk<24>,
    &void_void_thunk<25>,
    &void_void_thunk<26>,
    &void_void_thunk<27>,
    &void_void_thunk<28>,
    &void_void_thunk<29>,
    &void_void_thunk<30>,
    &void_void_thunk<31>,
    &void_void_thunk<32>,
    &void_void_thunk<33>,
    &void_void_thunk<34>,
    &void_void_thunk<35>,
    &void_void_thunk<36>,
    &void_void_thunk<37>,
    &void_void_thunk<38>,
    &void_void_thunk<39>,
    &void_void_thunk<40>,
    &void_void_thunk<41>,
    &void_void_thunk<42>,
    &void_void_thunk<43>,
    &void_void_thunk<44>,
    &void_void_thunk<45>,
    &void_void_thunk<46>,
    &void_void_thunk<47>,
    &void_void_thunk<48>,
    &void_void_thunk<49>,
    &void_void_thunk<50>,
    &void_void_thunk<51>,
    &void_void_thunk<52>,
    &void_void_thunk<53>,
    &void_void_thunk<54>,
    &void_void_thunk<55>,
    &void_void_thunk<56>,
    &void_void_thunk<57>,
    &void_void_thunk<58>,
    &void_void_thunk<59>,
    &void_void_thunk<60>,
    &void_void_thunk<61>,
    &void_void_thunk<62>,
    &void_void_thunk<63>,
    &void_void_thunk<64>,
    &void_void_thunk<65>,
    &void_void_thunk<66>,
    &void_void_thunk<67>,
    &void_void_thunk<68>,
    &void_void_thunk<69>,
    &void_void_thunk<70>,
    &void_void_thunk<71>,
    &void_void_thunk<72>,
    &void_void_thunk<73>,
    &void_void_thunk<74>,
    &void_void_thunk<75>,
    &void_void_thunk<76>,
    &void_void_thunk<77>,
    &void_void_thunk<78>,
    &void_void_thunk<79>,
    &void_void_thunk<80>,
    &void_void_thunk<81>,
    &void_void_thunk<82>,
    &void_void_thunk<83>,
    &void_void_thunk<84>,
    &void_void_thunk<85>,
    &void_void_thunk<86>,
    &void_void_thunk<87>,
    &void_void_thunk<88>,
    &void_void_thunk<89>,
    &void_void_thunk<90>,
    &void_void_thunk<91>,
    &void_void_thunk<92>,
    &void_void_thunk<93>,
    &void_void_thunk<94>,
    &void_void_thunk<95>,
    &void_void_thunk<96>,
    &void_void_thunk<97>,
    &void_void_thunk<98>,
    &void_void_thunk<99>,
    &void_void_thunk<100>,
    &void_void_thunk<101>,
    &void_void_thunk<102>,
    &void_void_thunk<103>,
    &void_void_thunk<104>,
    &void_void_thunk<105>,
    &void_void_thunk<106>,
    &void_void_thunk<107>,
    &void_void_thunk<108>,
    &void_void_thunk<109>,
    &void_void_thunk<110>,
    &void_void_thunk<111>,
    &void_void_thunk<112>,
    &void_void_thunk<113>,
    &void_void_thunk<114>,
    &void_void_thunk<115>,
    &void_void_thunk<116>,
    &void_void_thunk<117>,
    &void_void_thunk<118>,
    &void_void_thunk<119>,
    &void_void_thunk<120>,
    &void_void_thunk<121>,
    &void_void_thunk<122>,
    &void_void_thunk<123>,
    &void_void_thunk<124>,
    &void_void_thunk<125>,
    &void_void_thunk<126>,
    &void_void_thunk<127>,
    &void_void_thunk<128>,
    &void_void_thunk<129>,
    &void_void_thunk<130>,
    &void_void_thunk<131>,
    &void_void_thunk<132>,
    &void_void_thunk<133>,
    &void_void_thunk<134>,
    &void_void_thunk<135>,
    &void_void_thunk<136>,
    &void_void_thunk<137>,
    &void_void_thunk<138>,
    &void_void_thunk<139>,
    &void_void_thunk<140>,
    &void_void_thunk<141>,
    &void_void_thunk<142>,
    &void_void_thunk<143>,
    &void_void_thunk<144>,
    &void_void_thunk<145>,
    &void_void_thunk<146>,
    &void_void_thunk<147>,
    &void_void_thunk<148>,
    &void_void_thunk<149>,
    &void_void_thunk<150>,
    &void_void_thunk<151>,
    &void_void_thunk<152>,
    &void_void_thunk<153>,
    &void_void_thunk<154>,
    &void_void_thunk<155>,
    &void_void_thunk<156>,
    &void_void_thunk<157>,
    &void_void_thunk<158>,
    &void_void_thunk<159>,
    &void_void_thunk<160>,
    &void_void_thunk<161>,
    &void_void_thunk<162>,
    &void_void_thunk<163>,
    &void_void_thunk<164>,
    &void_void_thunk<165>,
    &void_void_thunk<166>,
    &void_void_thunk<167>,
    &void_void_thunk<168>,
    &void_void_thunk<169>,
    &void_void_thunk<170>,
    &void_void_thunk<171>,
    &void_void_thunk<172>,
    &void_void_thunk<173>,
    &void_void_thunk<174>,
    &void_void_thunk<175>,
    &void_void_thunk<176>,
    &void_void_thunk<177>,
    &void_void_thunk<178>,
    &void_void_thunk<179>,
    &void_void_thunk<180>,
    &void_void_thunk<181>,
    &void_void_thunk<182>,
    &void_void_thunk<183>,
    &void_void_thunk<184>,
    &void_void_thunk<185>,
    &void_void_thunk<186>,
    &void_void_thunk<187>,
    &void_void_thunk<188>,
    &void_void_thunk<189>,
    &void_void_thunk<190>,
    &void_void_thunk<191>,
    &void_void_thunk<192>,
    &void_void_thunk<193>,
    &void_void_thunk<194>,
    &void_void_thunk<195>,
    &void_void_thunk<196>,
    &void_void_thunk<197>,
    &void_void_thunk<198>,
    &void_void_thunk<199>,
    &void_void_thunk<200>,
    &void_void_thunk<201>,
    &void_void_thunk<202>,
    &void_void_thunk<203>,
    &void_void_thunk<204>,
    &void_void_thunk<205>,
    &void_void_thunk<206>,
    &void_void_thunk<207>,
    &void_void_thunk<208>,
    &void_void_thunk<209>,
    &void_void_thunk<210>,
    &void_void_thunk<211>,
    &void_void_thunk<212>,
    &void_void_thunk<213>,
    &void_void_thunk<214>,
    &void_void_thunk<215>,
    &void_void_thunk<216>,
    &void_void_thunk<217>,
    &void_void_thunk<218>,
    &void_void_thunk<219>,
    &void_void_thunk<220>,
    &void_void_thunk<221>,
    &void_void_thunk<222>,
    &void_void_thunk<223>,
    &void_void_thunk<224>,
    &void_void_thunk<225>,
    &void_void_thunk<226>,
    &void_void_thunk<227>,
    &void_void_thunk<228>,
    &void_void_thunk<229>,
    &void_void_thunk<230>,
    &void_void_thunk<231>,
    &void_void_thunk<232>,
    &void_void_thunk<233>,
    &void_void_thunk<234>,
    &void_void_thunk<235>,
    &void_void_thunk<236>,
    &void_void_thunk<237>,
    &void_void_thunk<238>,
    &void_void_thunk<239>,
    &void_void_thunk<240>,
    &void_void_thunk<241>,
    &void_void_thunk<242>,
    &void_void_thunk<243>,
    &void_void_thunk<244>,
    &void_void_thunk<245>,
    &void_void_thunk<246>,
    &void_void_thunk<247>,
    &void_void_thunk<248>,
    &void_void_thunk<249>,
    &void_void_thunk<250>,
    &void_void_thunk<251>,
    &void_void_thunk<252>,
    &void_void_thunk<253>,
    &void_void_thunk<254>,
    &void_void_thunk<255>
};

void *bridge_void_void(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    auto target = reinterpret_cast<void_void_sysv_t>(target_ptr);
    std::lock_guard<std::mutex> lock(void_void_mutex);
    for (size_t i = 0; i < 256; ++i) {
        if (reinterpret_cast<void_void_sysv_t>(void_void_targets[i].load(std::memory_order_acquire)) == target) {
            void_void_refcounts[i].fetch_add(1, std::memory_order_relaxed);
            return reinterpret_cast<void *>(void_void_thunks[i]);
        }
    }
    for (size_t i = 0; i < 256; ++i) {
        if (void_void_targets[i].load(std::memory_order_acquire) == 0) {
            void_void_targets[i].store(reinterpret_cast<uintptr_t>(target), std::memory_order_release);
            void_void_refcounts[i].store(1, std::memory_order_relaxed);
            return reinterpret_cast<void *>(void_void_thunks[i]);
        }
    }
    fprintf(stderr,
            "FATAL: Havok callback bridge pool exhausted for the 'void_void' signature family:\n"
            "  All 256 bridge slots are in use. Every distinct live native callback of this\n"
            "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\n"
            "  in worlds with very many grids/blocks can register more than the pool can hold.\n"
            "  Fix: raise this family in CALLBACK_SLOTS (currently 256) in tools/generate_havok_thunks.py,\n"
            "  regenerate the thunks (python3 tools/generate_havok_thunks.py), and rebuild.\n");
    std::abort();
}

void release_void_void(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    auto target = reinterpret_cast<void_void_sysv_t>(target_ptr);
    std::lock_guard<std::mutex> lock(void_void_mutex);
    for (size_t i = 0; i < 256; ++i) {
        if (reinterpret_cast<void_void_sysv_t>(void_void_targets[i].load(std::memory_order_acquire)) == target) {
            auto refs = void_void_refcounts[i].load(std::memory_order_relaxed);
            if (refs > 0) {
                refs = void_void_refcounts[i].fetch_sub(1, std::memory_order_acq_rel) - 1;
            }
            if (refs == 0) {
                void_void_targets[i].store(0, std::memory_order_release);
            }
            return;
        }
    }
}
