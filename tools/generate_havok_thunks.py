#!/usr/bin/env python3
"""Generate the Havok callback bridge thunk sources.

Havok's C callback ABIs (contact listeners, entity listeners, phantom handlers,
etc.) pass no user-data / context pointer. To route a native callback back to
the correct managed target we therefore need a *distinct code address per live
callback*: each address is a template instantiation `<family>_thunk<Index>` that
reads its target from `<family>_targets[Index]`. Those addresses are baked into
the binary at compile time, so the pool size (CALLBACK_SLOTS) is fixed per build
and cannot be grown at run time without emitting machine code (JIT trampolines).

This script regenerates the per-family thunk sources (`src/HavokThunk_*.cpp`) and
their shared header (`src/HavokThunkRegistry.h`). It is self-contained: unlike the
full Havok wrapper generator it needs no decompiled C# sources, only the callback
signature families listed below.

Usage:
    python3 tools/generate_havok_thunks.py
"""
from pathlib import Path

PROJECT_DIR = Path(__file__).parent.parent
THUNK_DIR = PROJECT_DIR / 'src'
THUNK_HEADER = THUNK_DIR / 'HavokThunkRegistry.h'

# Number of bridge slots reserved per callback signature family.
#
# Each slot is one compile-time-instantiated thunk, so this is baked into every
# generated source and fixed for the lifetime of the build. Raising it grows the
# binary and the compile time roughly linearly (there is one templated function
# per slot per family), so keep it as small as the worst-case world requires.
#
# 16384 was chosen because dynamic (run-time) extension of the pool is not
# practical for this design -- see the module docstring. Worlds with very many
# grids/blocks register a large number of simultaneous callbacks: e.g. loading
# "Many Lifters Slowness" (699 grids, ~13k thrusters) exhausted the previous
# limit of 4096 for the `void_ptr_ptr` family and aborted the process. If this
# limit is ever hit again the generated bridge prints a diagnostic naming the
# family and pointing back here; raise the value, regenerate, and rebuild.
CALLBACK_SLOTS = 16384

# ret, params, args for each callback signature family.
FAMILY_SIGNATURES = {
    'void_ptr': ('void', 'void* arg0', 'arg0'),
    'void_charptr': ('void', 'char* arg0', 'arg0'),
    'int_ptr_ptr_uint_ptr': ('int32_t', 'void* arg0, void* arg1, uint32_t arg2, void* arg3', 'arg0, arg1, arg2, arg3'),
    'bool_ptr_ptr': ('bool', 'void* arg0, void* arg1', 'arg0, arg1'),
    'void_ptr_int_ptr': ('void', 'void* arg0, int32_t arg1, void* arg2', 'arg0, arg1, arg2'),
    'void_ptr_ptr': ('void', 'void* arg0, void* arg1', 'arg0, arg1'),
    'void_void': ('void', 'void', ''),
    'void_i64': ('void', 'int64_t arg0', 'arg0'),
    'void_charptr_int': ('void', 'char* arg0, int32_t arg1', 'arg0, arg1'),
    'void_ptr_int': ('void', 'void* arg0, int32_t arg1', 'arg0, arg1'),
    'float_ptr': ('float', 'void* arg0', 'arg0'),
}

GENERATED_BANNER = '''// ============================================================================
// GENERATED FILE -- do not edit by hand.
// Regenerate with: python3 tools/generate_havok_thunks.py
//
// Havok callback bridge pool. Each slot is a distinct compile-time thunk address
// handed to Havok as a C callback; the pool size (CALLBACK_SLOTS) is fixed per
// build and cannot be grown at run time. If the pool is exhausted the bridge
// prints a diagnostic and aborts -- raise CALLBACK_SLOTS in the generator,
// regenerate, and rebuild.
// ============================================================================
'''

THUNK_CPP_PREAMBLE = '''#include <cstdint>
#include <cstddef>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "win_types.h"
#include "HavokThunkRegistry.h"

'''


def thunk_file_name(name: str) -> str:
    return f'HavokThunk_{name}.cpp'


def emit_thunk_header(families):
    lines = [
        '#pragma once',
        '',
        '#include <initializer_list>',
        '',
        'struct callback_owner_binding {',
        '    void (*release_fn)(void *target_ptr);',
        '    void *target_ptr;',
        '};',
        '',
        'void register_callback_owner(void *owner, std::initializer_list<callback_owner_binding> bindings);',
        'void release_callback_owner(void *owner);',
        '',
    ]
    for family_name in families:
        lines.append(f'void *bridge_{family_name}(void *target_ptr);')
        lines.append(f'void release_{family_name}(void *target_ptr);')
    lines.append('')
    return '\n'.join(lines)


def emit_bridge_family(name: str, ret: str, params: str, args: str):
    sysv_type = f'{name}_sysv_t'
    ms_type = f'{name}_ms_t'
    thunk_params = params
    thunk_args = args
    lines = [
        GENERATED_BANNER + THUNK_CPP_PREAMBLE,
        f'using {sysv_type} = {ret} (*)({params});',
        f'using {ms_type} = {ret} (WINAPI *)({params});',
        f'static std::mutex {name}_mutex;',
        f'static std::atomic_uintptr_t {name}_targets[{CALLBACK_SLOTS}] = {{}};',
        f'static std::atomic_uint32_t {name}_refcounts[{CALLBACK_SLOTS}] = {{}};',
        '',
        'template <size_t Index>',
        f'static {ret} WINAPI {name}_thunk({thunk_params})',
        '{',
        f'    auto fn_bits = {name}_targets[Index].load(std::memory_order_acquire);',
        f'    auto fn = reinterpret_cast<{sysv_type}>(fn_bits);',
    ]
    if ret == 'void':
        lines.extend([
            '    if (!fn) {',
            '        return;',
            '    }',
            f'    fn({thunk_args});' if thunk_args else '    fn();',
        ])
    else:
        lines.extend([
            '    if (!fn) {',
            f'        return {ret}{{}};',
            '    }',
            f'    return fn({thunk_args});' if thunk_args else '    return fn();',
        ])
    lines.extend([
        '}',
        '',
        f'static {ms_type} {name}_thunks[{CALLBACK_SLOTS}] = {{',
    ])
    for index in range(CALLBACK_SLOTS):
        suffix = ',' if index + 1 < CALLBACK_SLOTS else ''
        lines.append(f'    &{name}_thunk<{index}>{suffix}')
    lines.extend([
        '};',
        '',
        f'void *bridge_{name}(void *target_ptr)',
        '{',
        '    if (!target_ptr) {',
        '        return nullptr;',
        '    }',
        f'    auto target = reinterpret_cast<{sysv_type}>(target_ptr);',
        f'    std::lock_guard<std::mutex> lock({name}_mutex);',
        f'    for (size_t i = 0; i < {CALLBACK_SLOTS}; ++i) {{',
        f'        if (reinterpret_cast<{sysv_type}>({name}_targets[i].load(std::memory_order_acquire)) == target) {{',
        f'            {name}_refcounts[i].fetch_add(1, std::memory_order_relaxed);',
        f'            return reinterpret_cast<void *>({name}_thunks[i]);',
        '        }',
        '    }',
        f'    for (size_t i = 0; i < {CALLBACK_SLOTS}; ++i) {{',
        f'        if ({name}_targets[i].load(std::memory_order_acquire) == 0) {{',
        f'            {name}_targets[i].store(reinterpret_cast<uintptr_t>(target), std::memory_order_release);',
        f'            {name}_refcounts[i].store(1, std::memory_order_relaxed);',
        f'            return reinterpret_cast<void *>({name}_thunks[i]);',
        '        }',
        '    }',
        '    fprintf(stderr,',
        f'            "FATAL: Havok callback bridge pool exhausted for the \'{name}\' signature family:\\n"',
        f'            "  All {CALLBACK_SLOTS} bridge slots are in use. Every live Havok callback with this\\n"',
        '            "  signature consumes one slot, so worlds with very many grids/blocks (e.g. thousands\\n"',
        '            "  of thrusters or listeners) can register more callbacks than the pool can hold.\\n"',
        f'            "  Fix: raise CALLBACK_SLOTS (currently {CALLBACK_SLOTS}) in tools/generate_havok_thunks.py,\\n"',
        '            "  regenerate the thunks (python3 tools/generate_havok_thunks.py), and rebuild.\\n");',
        '    std::abort();',
        '}',
        '',
        f'void release_{name}(void *target_ptr)',
        '{',
        '    if (!target_ptr) {',
        '        return;',
        '    }',
        f'    auto target = reinterpret_cast<{sysv_type}>(target_ptr);',
        f'    std::lock_guard<std::mutex> lock({name}_mutex);',
        f'    for (size_t i = 0; i < {CALLBACK_SLOTS}; ++i) {{',
        f'        if (reinterpret_cast<{sysv_type}>({name}_targets[i].load(std::memory_order_acquire)) == target) {{',
        f'            auto refs = {name}_refcounts[i].load(std::memory_order_relaxed);',
        f'            if (refs > 0) {{',
        f'                refs = {name}_refcounts[i].fetch_sub(1, std::memory_order_acq_rel) - 1;',
        '            }',
        f'            if (refs == 0) {{',
        f'                {name}_targets[i].store(0, std::memory_order_release);',
        '            }',
        '            return;',
        '        }',
        '    }',
        '}',
        '',
    ])
    return lines


def main():
    family_names = sorted(FAMILY_SIGNATURES)
    for path in THUNK_DIR.glob('HavokThunk_*.cpp'):
        path.unlink()
    THUNK_HEADER.write_text(emit_thunk_header(family_names))
    for family_name in family_names:
        ret, params, args = FAMILY_SIGNATURES[family_name]
        (THUNK_DIR / thunk_file_name(family_name)).write_text('\n'.join(emit_bridge_family(family_name, ret, params, args)))
    print(f'Generated {len(family_names)} thunk families with CALLBACK_SLOTS={CALLBACK_SLOTS}')


if __name__ == '__main__':
    main()
