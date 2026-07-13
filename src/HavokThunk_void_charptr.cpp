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


using void_charptr_sysv_t = void (*)(char* arg0);
using void_charptr_ms_t = void (WINAPI *)(char* arg0);
static std::mutex void_charptr_mutex;
static std::atomic_uintptr_t void_charptr_targets[256] = {};
static std::atomic_uint32_t void_charptr_refcounts[256] = {};

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

static void_charptr_ms_t void_charptr_thunks[256] = {
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
    &void_charptr_thunk<31>,
    &void_charptr_thunk<32>,
    &void_charptr_thunk<33>,
    &void_charptr_thunk<34>,
    &void_charptr_thunk<35>,
    &void_charptr_thunk<36>,
    &void_charptr_thunk<37>,
    &void_charptr_thunk<38>,
    &void_charptr_thunk<39>,
    &void_charptr_thunk<40>,
    &void_charptr_thunk<41>,
    &void_charptr_thunk<42>,
    &void_charptr_thunk<43>,
    &void_charptr_thunk<44>,
    &void_charptr_thunk<45>,
    &void_charptr_thunk<46>,
    &void_charptr_thunk<47>,
    &void_charptr_thunk<48>,
    &void_charptr_thunk<49>,
    &void_charptr_thunk<50>,
    &void_charptr_thunk<51>,
    &void_charptr_thunk<52>,
    &void_charptr_thunk<53>,
    &void_charptr_thunk<54>,
    &void_charptr_thunk<55>,
    &void_charptr_thunk<56>,
    &void_charptr_thunk<57>,
    &void_charptr_thunk<58>,
    &void_charptr_thunk<59>,
    &void_charptr_thunk<60>,
    &void_charptr_thunk<61>,
    &void_charptr_thunk<62>,
    &void_charptr_thunk<63>,
    &void_charptr_thunk<64>,
    &void_charptr_thunk<65>,
    &void_charptr_thunk<66>,
    &void_charptr_thunk<67>,
    &void_charptr_thunk<68>,
    &void_charptr_thunk<69>,
    &void_charptr_thunk<70>,
    &void_charptr_thunk<71>,
    &void_charptr_thunk<72>,
    &void_charptr_thunk<73>,
    &void_charptr_thunk<74>,
    &void_charptr_thunk<75>,
    &void_charptr_thunk<76>,
    &void_charptr_thunk<77>,
    &void_charptr_thunk<78>,
    &void_charptr_thunk<79>,
    &void_charptr_thunk<80>,
    &void_charptr_thunk<81>,
    &void_charptr_thunk<82>,
    &void_charptr_thunk<83>,
    &void_charptr_thunk<84>,
    &void_charptr_thunk<85>,
    &void_charptr_thunk<86>,
    &void_charptr_thunk<87>,
    &void_charptr_thunk<88>,
    &void_charptr_thunk<89>,
    &void_charptr_thunk<90>,
    &void_charptr_thunk<91>,
    &void_charptr_thunk<92>,
    &void_charptr_thunk<93>,
    &void_charptr_thunk<94>,
    &void_charptr_thunk<95>,
    &void_charptr_thunk<96>,
    &void_charptr_thunk<97>,
    &void_charptr_thunk<98>,
    &void_charptr_thunk<99>,
    &void_charptr_thunk<100>,
    &void_charptr_thunk<101>,
    &void_charptr_thunk<102>,
    &void_charptr_thunk<103>,
    &void_charptr_thunk<104>,
    &void_charptr_thunk<105>,
    &void_charptr_thunk<106>,
    &void_charptr_thunk<107>,
    &void_charptr_thunk<108>,
    &void_charptr_thunk<109>,
    &void_charptr_thunk<110>,
    &void_charptr_thunk<111>,
    &void_charptr_thunk<112>,
    &void_charptr_thunk<113>,
    &void_charptr_thunk<114>,
    &void_charptr_thunk<115>,
    &void_charptr_thunk<116>,
    &void_charptr_thunk<117>,
    &void_charptr_thunk<118>,
    &void_charptr_thunk<119>,
    &void_charptr_thunk<120>,
    &void_charptr_thunk<121>,
    &void_charptr_thunk<122>,
    &void_charptr_thunk<123>,
    &void_charptr_thunk<124>,
    &void_charptr_thunk<125>,
    &void_charptr_thunk<126>,
    &void_charptr_thunk<127>,
    &void_charptr_thunk<128>,
    &void_charptr_thunk<129>,
    &void_charptr_thunk<130>,
    &void_charptr_thunk<131>,
    &void_charptr_thunk<132>,
    &void_charptr_thunk<133>,
    &void_charptr_thunk<134>,
    &void_charptr_thunk<135>,
    &void_charptr_thunk<136>,
    &void_charptr_thunk<137>,
    &void_charptr_thunk<138>,
    &void_charptr_thunk<139>,
    &void_charptr_thunk<140>,
    &void_charptr_thunk<141>,
    &void_charptr_thunk<142>,
    &void_charptr_thunk<143>,
    &void_charptr_thunk<144>,
    &void_charptr_thunk<145>,
    &void_charptr_thunk<146>,
    &void_charptr_thunk<147>,
    &void_charptr_thunk<148>,
    &void_charptr_thunk<149>,
    &void_charptr_thunk<150>,
    &void_charptr_thunk<151>,
    &void_charptr_thunk<152>,
    &void_charptr_thunk<153>,
    &void_charptr_thunk<154>,
    &void_charptr_thunk<155>,
    &void_charptr_thunk<156>,
    &void_charptr_thunk<157>,
    &void_charptr_thunk<158>,
    &void_charptr_thunk<159>,
    &void_charptr_thunk<160>,
    &void_charptr_thunk<161>,
    &void_charptr_thunk<162>,
    &void_charptr_thunk<163>,
    &void_charptr_thunk<164>,
    &void_charptr_thunk<165>,
    &void_charptr_thunk<166>,
    &void_charptr_thunk<167>,
    &void_charptr_thunk<168>,
    &void_charptr_thunk<169>,
    &void_charptr_thunk<170>,
    &void_charptr_thunk<171>,
    &void_charptr_thunk<172>,
    &void_charptr_thunk<173>,
    &void_charptr_thunk<174>,
    &void_charptr_thunk<175>,
    &void_charptr_thunk<176>,
    &void_charptr_thunk<177>,
    &void_charptr_thunk<178>,
    &void_charptr_thunk<179>,
    &void_charptr_thunk<180>,
    &void_charptr_thunk<181>,
    &void_charptr_thunk<182>,
    &void_charptr_thunk<183>,
    &void_charptr_thunk<184>,
    &void_charptr_thunk<185>,
    &void_charptr_thunk<186>,
    &void_charptr_thunk<187>,
    &void_charptr_thunk<188>,
    &void_charptr_thunk<189>,
    &void_charptr_thunk<190>,
    &void_charptr_thunk<191>,
    &void_charptr_thunk<192>,
    &void_charptr_thunk<193>,
    &void_charptr_thunk<194>,
    &void_charptr_thunk<195>,
    &void_charptr_thunk<196>,
    &void_charptr_thunk<197>,
    &void_charptr_thunk<198>,
    &void_charptr_thunk<199>,
    &void_charptr_thunk<200>,
    &void_charptr_thunk<201>,
    &void_charptr_thunk<202>,
    &void_charptr_thunk<203>,
    &void_charptr_thunk<204>,
    &void_charptr_thunk<205>,
    &void_charptr_thunk<206>,
    &void_charptr_thunk<207>,
    &void_charptr_thunk<208>,
    &void_charptr_thunk<209>,
    &void_charptr_thunk<210>,
    &void_charptr_thunk<211>,
    &void_charptr_thunk<212>,
    &void_charptr_thunk<213>,
    &void_charptr_thunk<214>,
    &void_charptr_thunk<215>,
    &void_charptr_thunk<216>,
    &void_charptr_thunk<217>,
    &void_charptr_thunk<218>,
    &void_charptr_thunk<219>,
    &void_charptr_thunk<220>,
    &void_charptr_thunk<221>,
    &void_charptr_thunk<222>,
    &void_charptr_thunk<223>,
    &void_charptr_thunk<224>,
    &void_charptr_thunk<225>,
    &void_charptr_thunk<226>,
    &void_charptr_thunk<227>,
    &void_charptr_thunk<228>,
    &void_charptr_thunk<229>,
    &void_charptr_thunk<230>,
    &void_charptr_thunk<231>,
    &void_charptr_thunk<232>,
    &void_charptr_thunk<233>,
    &void_charptr_thunk<234>,
    &void_charptr_thunk<235>,
    &void_charptr_thunk<236>,
    &void_charptr_thunk<237>,
    &void_charptr_thunk<238>,
    &void_charptr_thunk<239>,
    &void_charptr_thunk<240>,
    &void_charptr_thunk<241>,
    &void_charptr_thunk<242>,
    &void_charptr_thunk<243>,
    &void_charptr_thunk<244>,
    &void_charptr_thunk<245>,
    &void_charptr_thunk<246>,
    &void_charptr_thunk<247>,
    &void_charptr_thunk<248>,
    &void_charptr_thunk<249>,
    &void_charptr_thunk<250>,
    &void_charptr_thunk<251>,
    &void_charptr_thunk<252>,
    &void_charptr_thunk<253>,
    &void_charptr_thunk<254>,
    &void_charptr_thunk<255>
};

void *bridge_void_charptr(void *target_ptr)
{
    if (!target_ptr) {
        return nullptr;
    }
    auto target = reinterpret_cast<void_charptr_sysv_t>(target_ptr);
    std::lock_guard<std::mutex> lock(void_charptr_mutex);
    for (size_t i = 0; i < 256; ++i) {
        if (reinterpret_cast<void_charptr_sysv_t>(void_charptr_targets[i].load(std::memory_order_acquire)) == target) {
            void_charptr_refcounts[i].fetch_add(1, std::memory_order_relaxed);
            return reinterpret_cast<void *>(void_charptr_thunks[i]);
        }
    }
    for (size_t i = 0; i < 256; ++i) {
        if (void_charptr_targets[i].load(std::memory_order_acquire) == 0) {
            void_charptr_targets[i].store(reinterpret_cast<uintptr_t>(target), std::memory_order_release);
            void_charptr_refcounts[i].store(1, std::memory_order_relaxed);
            return reinterpret_cast<void *>(void_charptr_thunks[i]);
        }
    }
    fprintf(stderr,
            "FATAL: Havok callback bridge pool exhausted for the 'void_charptr' signature family:\n"
            "  All 256 bridge slots are in use. Every distinct live native callback of this\n"
            "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\n"
            "  in worlds with very many grids/blocks can register more than the pool can hold.\n"
            "  Fix: raise this family in CALLBACK_SLOTS (currently 256) in tools/generate_havok_thunks.py,\n"
            "  regenerate the thunks (python3 tools/generate_havok_thunks.py), and rebuild.\n");
    std::abort();
}

void release_void_charptr(void *target_ptr)
{
    if (!target_ptr) {
        return;
    }
    auto target = reinterpret_cast<void_charptr_sysv_t>(target_ptr);
    std::lock_guard<std::mutex> lock(void_charptr_mutex);
    for (size_t i = 0; i < 256; ++i) {
        if (reinterpret_cast<void_charptr_sysv_t>(void_charptr_targets[i].load(std::memory_order_acquire)) == target) {
            auto refs = void_charptr_refcounts[i].load(std::memory_order_relaxed);
            if (refs > 0) {
                refs = void_charptr_refcounts[i].fetch_sub(1, std::memory_order_acq_rel) - 1;
            }
            if (refs == 0) {
                void_charptr_targets[i].store(0, std::memory_order_release);
            }
            return;
        }
    }
}
