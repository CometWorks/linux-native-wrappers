#!/usr/bin/env python3
"""Generate the Havok wrapper sources: src/Havok.cpp, src/HavokThunk_*.cpp and
src/HavokThunkRegistry.h.

`Havok.cpp` is a thin C++ shim: one `extern "C"` function per Havok.dll export,
parsed from the game's decompiled `[DllImport]` declarations. Delegate arguments
are routed through the callback bridge (the thunk sources) so managed (SysV)
callbacks can be handed to the Microsoft-ABI Havok.dll.

Unlike the thunk sources, generating `Havok.cpp` needs the decompiled C# wrapper
(`../dotnet-game-local/HavokWrapper/Havok`); the public repo ships the generated
output so a plain build needs no such dependency.

Usage:
    python3 tools/generate_havok_wrapper.py
"""
from pathlib import Path
import re

PROJECT_DIR = Path(__file__).parent.parent
PROJECTS_DIR = PROJECT_DIR.parent

CS_ROOT = PROJECTS_DIR / 'dotnet-game-local/HavokWrapper/Havok'

THUNK_DIR = PROJECT_DIR / 'src'
OUTPUT = THUNK_DIR / 'Havok.cpp'
THUNK_HEADER = THUNK_DIR / 'HavokThunkRegistry.h'

PRIMITIVES = {
    'void': 'void',
    'IntPtr': 'void*',
    'int': 'int32_t',
    'uint': 'uint32_t',
    'long': 'int64_t',
    'ulong': 'uint64_t',
    'short': 'int16_t',
    'ushort': 'uint16_t',
    'byte': 'uint8_t',
    'sbyte': 'int8_t',
    'float': 'float',
    'bool': 'bool',
    'string': 'const char*',
}

KNOWN_VALUE_TYPES = {
    'Vector3': 'Vector3',
    'Vector4': 'Vector4',
    'Quaternion': 'Quaternion',
    'Matrix': 'Matrix',
    'Vector3I': 'Vector3I',
    'Vector3S': 'Vector3S',
    'HkMassProperties': 'HkMassProperties',
    'HkUniformGridShape.HkUniformGridShapeArgsPOD': 'HkUniformGridShapeArgsPOD',
    'HkStaticCompoundShape.DecomposeShapeKeyResult': 'HkStaticCompoundShape_DecomposeShapeKeyResult',
    'HkJobQueue.WaitPolicyT': 'int32_t',
    'HkActionType': 'int32_t',
    'HkTaskType': 'int32_t',
}

ALL_DELEGATE_TYPES = {
    'HkActivationListener.HkActivationHandlerCpp',
    'HkBaseSystem.Log',
    'HkBreakOffPartsUtil.BreakLogicHandlerDelegate',
    'HkBreakOffPartsUtil.BreakPartsHandlerDelegate',
    'HkConstraint.ReadConstraintsCallback',
    'HkConstraintListener.OnAdded',
    'HkConstraintListener.OnRemoved',
    'HkConstraintListener.OnBreaking',
    'HkContactListener.ContactPointHandler',
    'HkContactListener.CollisionHandler',
    'HkContactSoundListener.ContactSoundHandler',
    'HkEntityListener.OnAddCpp',
    'HkEntityListener.OnRemoveCpp',
    'HkEntityListener.OnDeleteCpp',
    'HkEntityListener.OnShapeChangeCpp',
    'HkEntityListener.OnMotionTypeChangeCpp',
    'HkJobThreadPool.ThreadAction',
    'HkPhantomCallbackShape.HkPhantomHandlerCpp',
    'HkDeleteHandler',
    'HkShapeLoader.ReturnByteArray',
    'HkTaskProfiler.TaskStartedFuncCpp',
    'HkTaskProfiler.TaskFinishedFunc',
    'HkTaskProfiler.BlockBeginFuncCpp',
    'HkTaskProfiler.BlockEndFunc',
    'HkUniformGridShape.NativeBatchRequestCallback',
    'HkWheelResponseModifierUtil.CalculateModifier',
    'HkWorld.BroadPhaseExitCallback',
    'HkpAabbPhantom.CollidableAddedD',
    'HkpAabbPhantom.CollidableRemovedD',
}

DELEGATE_FAMILIES = {
    'HkActivationListener.HkActivationHandlerCpp': 'void_ptr',
    'HkBaseSystem.Log': 'void_charptr',
    'HkBreakOffPartsUtil.BreakLogicHandlerDelegate': 'int_ptr_ptr_uint_ptr',
    'HkBreakOffPartsUtil.BreakPartsHandlerDelegate': 'bool_ptr_ptr',
    'HkConstraint.ReadConstraintsCallback': 'void_ptr_int_ptr',
    'HkConstraintListener.OnAdded': 'void_ptr',
    'HkConstraintListener.OnRemoved': 'void_ptr',
    'HkConstraintListener.OnBreaking': 'void_ptr',
    'HkContactListener.ContactPointHandler': 'void_ptr_ptr',
    'HkContactListener.CollisionHandler': 'void_ptr_ptr',
    'HkContactSoundListener.ContactSoundHandler': 'void_ptr_ptr',
    'HkEntityListener.OnAddCpp': 'void_ptr_ptr',
    'HkEntityListener.OnRemoveCpp': 'void_ptr_ptr',
    'HkEntityListener.OnDeleteCpp': 'void_ptr_ptr',
    'HkEntityListener.OnShapeChangeCpp': 'void_ptr_ptr',
    'HkEntityListener.OnMotionTypeChangeCpp': 'void_ptr_ptr',
    'HkJobThreadPool.ThreadAction': 'void_ptr',
    'HkPhantomCallbackShape.HkPhantomHandlerCpp': 'void_ptr_ptr',
    'HkDeleteHandler': 'void_ptr',
    'HkShapeLoader.ReturnByteArray': 'void_ptr_int',
    'HkTaskProfiler.TaskStartedFuncCpp': 'void_charptr_int',
    'HkTaskProfiler.TaskFinishedFunc': 'void_void',
    'HkTaskProfiler.BlockBeginFuncCpp': 'void_charptr',
    'HkTaskProfiler.BlockEndFunc': 'void_i64',
    'HkUniformGridShape.NativeBatchRequestCallback': 'void_ptr_int',
    'HkWheelResponseModifierUtil.CalculateModifier': 'float_ptr',
    'HkWorld.BroadPhaseExitCallback': 'void_ptr_ptr',
    'HkpAabbPhantom.CollidableAddedD': 'void_ptr_ptr',
    'HkpAabbPhantom.CollidableRemovedD': 'void_ptr_ptr',
}

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

# Per-family bridge slot counts. Each slot is one compile-time-instantiated thunk,
# so the count is baked into every generated source and fixed for the lifetime of
# the build; growing the pool at run time is not practical (it would need JIT
# trampolines). Each family is sized to its worst case rather than sharing one
# large pool, because slot demand depends on how the wrapper marshals the delegate:
#
#   * static readonly delegates (entity/contact/sound/constraint/activation
#     listeners, wheel modifier, break-off, task profiler, log) marshal to ONE
#     shared pointer regardless of instance count (dispatch is via the listener
#     handle passed as arg0) -> a handful of slots (DEFAULT_CALLBACK_SLOTS).
#   * HkPhantomCallbackShape is the ONLY producer that marshals PER-INSTANCE
#     native delegates: each live phantom shape uses 2 void_ptr_ptr slots
#     (enter/leave). The game makes one per trigger/detector volume (ship
#     connectors + ejectors, collectors, gravity generators, merge blocks, safe
#     zones), so void_ptr_ptr scales with block count -> 16384. The phantom-slot
#     reclaim (see OWNER_HELPERS) frees these when a shape is destroyed, so 16384
#     bounds *concurrent* live phantoms, not the cumulative total ever created.
#     The phantom delete callback is NOT bridged (Havok is given one shared
#     dispatcher), so void_ptr has no per-instance producer and stays at default.
#   * HkShapeLoader cleanup and HkConstraint.FindConnectedConstraints marshal a
#     fresh, never-released callback per call -> a 4096 margin above the default.
DEFAULT_CALLBACK_SLOTS = 256

CALLBACK_SLOTS = {
    'void_ptr_ptr':      16384,  # 2 per live HkPhantomCallbackShape (enter/leave), reclaimed on destroy
    'void_ptr_int':       4096,  # HkShapeLoader cleanup marshals a fresh callback per call
    'void_ptr_int_ptr':   4096,  # HkConstraint.FindConnectedConstraints reader, per call
}


def slots_for(family_name):
    return CALLBACK_SLOTS.get(family_name, DEFAULT_CALLBACK_SLOTS)


OWNER_CALLBACKS = {
    'HkActivationListener_Create': [('void_ptr', 'onActivate'), ('void_ptr', 'onDeactivate')],
    'HkContactListener_Create': [('void_ptr_ptr', 'onContact'), ('void_ptr_ptr', 'collisionAdded'), ('void_ptr_ptr', 'collisionRemoved')],
    'HkContactSoundListener_Create': [('void_ptr_ptr', 'onContact')],
    'HkEntityListener_Create': [('void_ptr_ptr', 'onAdd'), ('void_ptr_ptr', 'onRemove'), ('void_ptr_ptr', 'onDelete'), ('void_ptr_ptr', 'onShapeChange'), ('void_ptr_ptr', 'onMotionTypeChange')],
    'HkpAabbPhantom_Create': [('void_ptr_ptr', 'collidableAddedD'), ('void_ptr_ptr', 'collidableRemovedD')],
}

OWNER_RELEASE_FUNCS = {
    'HkGlobal_ReleasePtr',
    'HkEntityListener_Release',
    'HkpAabbPhantom_Release',
}

# HkPhantomCallbackShape marshals a distinct native delegate per instance, so each
# live phantom shape permanently consumes bridge slots. Havok only signals that a
# shape is gone by invoking its delete callback, so these creators get special
# emission (see emit_phantom_create) that routes the delete through a shared
# dispatcher which reclaims the enter/leave slots. Keep in sync with the
# HkPhantomHandlerCpp / HkDeleteHandler argument order.
PHANTOM_SHAPE_CREATE_FUNCS = {
    'HkPhantomCallbackShape_Create',
}

EXPORT_ALIASES = {
    'HkJobThreadPool_RemoveReference': '?HkJobThreadPool_RemoveReference@Havok@@YAXPEAVhkThreadPool@@@Z',
}

PREAMBLE = '''#include <cstdint>
#include <cstddef>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "dll_loader.h"
#include "HavokThunkRegistry.h"

#define DECLARE_FUNCTION_POINTER(func) static WINAPI func##_t p##func = nullptr;

#define SET_FUNCTION_POINTER(func) p##func = (WINAPI func##_t)get_export(#func);

#define REQUIRE_FUNCTION_POINTER(func) if (!p##func) {     SET_FUNCTION_POINTER(func)     if (!p##func) {         fprintf(stderr, "Failed to load function: " #func "\\n");         throw std::runtime_error("Failed to load function: " #func);     } }

static void LogMessage(const char *text)
{
    std::ofstream("/tmp/ds.txt", std::ios::app) << text << "\\n";
}

#define LOG_CALL(func) ;
// Uncomment/comment to enable/disable detailed logging
/*
static long long TimestampMs()
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

#define LOG_CALL(func) fprintf(stderr, "[%lld] %s\\\\n", TimestampMs(), #func)
*/

static void EnsureThreadInfo()
{
    if (!setup_nt_threadinfo(nullptr)) {
        fprintf(stderr, "Failed to initialize thread info\\n");
        std::abort();
    }
    pe_ensure_tls_for_loaded_images();
}
'''

OWNER_HELPERS = '''static std::mutex g_callback_owner_mutex;
static std::unordered_map<void *, std::vector<callback_owner_binding>> g_callback_owner_bindings;

void register_callback_owner(void *owner, std::initializer_list<callback_owner_binding> bindings)
{
    if (!owner) {
        return;
    }
    // fprintf(stderr, "register_callback_owner owner=%p count=%zu\\n", owner, bindings.size());
    std::lock_guard<std::mutex> lock(g_callback_owner_mutex);
    auto &entry = g_callback_owner_bindings[owner];
    entry.insert(entry.end(), bindings.begin(), bindings.end());
}

void release_callback_owner(void *owner)
{
    if (!owner) {
        return;
    }
    // fprintf(stderr, "release_callback_owner owner=%p\\n", owner);
    std::lock_guard<std::mutex> lock(g_callback_owner_mutex);
    auto it = g_callback_owner_bindings.find(owner);
    if (it == g_callback_owner_bindings.end()) {
        return;
    }
    g_callback_owner_bindings.erase(it);
}

// HkPhantomCallbackShape is the only Havok wrapper that hands Havok a *distinct*
// native delegate per instance (enter/leave/delete), so each live phantom shape
// permanently consumes callback-bridge slots. Havok only signals that a shape is
// gone by invoking its delete callback, so every phantom is handed the single
// shared delete dispatcher below instead of a per-instance bridged delete. When
// Havok calls it (with the shape handle) we forward to the managed delete handler
// and then reclaim the enter/leave bridge slots so later phantoms can reuse them.
struct phantom_callback_shape_binding {
    void *enter;  // managed (SysV) enter callback target
    void *leave;  // managed (SysV) leave callback target
    void *del;    // managed (SysV) delete callback target
};

static std::mutex g_phantom_shape_mutex;
static std::unordered_map<void *, phantom_callback_shape_binding> g_phantom_shape_bindings;

using phantom_delete_sysv_t = void (*)(void *shape);

// One shared MS-ABI callback handed to Havok as the delete handler for EVERY
// phantom callback shape; Havok invokes it with the shape handle when the shape
// is destroyed.
static void WINAPI phantom_delete_dispatch(void *shape)
{
    phantom_callback_shape_binding binding{};
    {
        std::lock_guard<std::mutex> lock(g_phantom_shape_mutex);
        auto it = g_phantom_shape_bindings.find(shape);
        if (it == g_phantom_shape_bindings.end()) {
            return;
        }
        binding = it->second;
        g_phantom_shape_bindings.erase(it);
    }
    // Forward to the managed delete handler (HkDeleteHandler: Instances.Remove).
    if (binding.del) {
        reinterpret_cast<phantom_delete_sysv_t>(binding.del)(shape);
    }
    // Reclaim the per-instance enter/leave bridge slots. The delete callback is
    // never bridged (Havok holds this shared dispatcher), so it needs no release.
    release_void_ptr_ptr(binding.enter);
    release_void_ptr_ptr(binding.leave);
}

static void register_phantom_callback_shape(void *shape, void *enter, void *leave, void *del)
{
    if (!shape) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_phantom_shape_mutex);
    g_phantom_shape_bindings[shape] = phantom_callback_shape_binding{enter, leave, del};
}

'''

STRUCTS = '''
struct Vector3 {
    float X;
    float Y;
    float Z;
};

struct Vector4 {
    float X;
    float Y;
    float Z;
    float W;
};

struct Quaternion {
    float X;
    float Y;
    float Z;
    float W;
};

struct Matrix {
    float M11;
    float M12;
    float M13;
    float M14;
    float M21;
    float M22;
    float M23;
    float M24;
    float M31;
    float M32;
    float M33;
    float M34;
    float M41;
    float M42;
    float M43;
    float M44;
};

struct Vector3I {
    int32_t X;
    int32_t Y;
    int32_t Z;
};

struct Vector3S {
    int16_t X;
    int16_t Y;
    int16_t Z;
};

struct HkMassProperties {
    float Volume;
    float Mass;
    Vector3 CenterOfMass;
    Matrix InertiaTensor;
};

struct HkUniformGridShapeArgsPOD {
    int32_t CellsCount_X;
    int32_t CellsCount_Y;
    int32_t CellsCount_Z;
    float CellSize;
    float CellOffset;
    float CellExpand;
};

struct HkStaticCompoundShape_DecomposeShapeKeyResult {
    int32_t instanceId;
    uint32_t childKey;
};
'''

INIT_PREFIX = '''
static pe_image g_havok_image;

static void InitImpl(const char* dllPath)
{
    if (!load_dll(&g_havok_image, dllPath)) {
        LogMessage("Failed to load Havok.dll");
        throw std::runtime_error("Failed to load Havok.dll");
    }
'''

FOOTER = '''}

extern "C" {

void Init(const char* dllPath)
{
    if (g_havok_image.image) {
        fprintf(stderr,
                "[LinuxCompat] Havok::Init: already initialized (image=%p, dllPath='%s'); "
                "ignoring duplicate call.\\n",
                g_havok_image.image, dllPath ? dllPath : "<null>");
        return;
    }
    InitImpl(dllPath);
}
'''

THUNK_CPP_PREAMBLE = '''#include <cstdint>
#include <cstddef>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "win_types.h"
#include "HavokThunkRegistry.h"

'''

GENERATED_BANNER = '''// ============================================================================
// GENERATED FILE -- do not edit by hand.
// Regenerate with: python3 tools/generate_havok_wrapper.py
//
// Havok callback bridge pool. Each slot is a distinct compile-time thunk address
// handed to Havok as a C callback; the per-family pool size is fixed per build
// and cannot be grown at run time. If the pool is exhausted the bridge prints a
// diagnostic and aborts -- raise this family's entry in CALLBACK_SLOTS in the
// generator, regenerate, and rebuild.
// ============================================================================
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
    slots = slots_for(name)
    sysv_type = f'{name}_sysv_t'
    ms_type = f'{name}_ms_t'
    thunk_params = params
    thunk_args = args
    lines = [
        GENERATED_BANNER + THUNK_CPP_PREAMBLE,
        f'using {sysv_type} = {ret} (*)({params});',
        f'using {ms_type} = {ret} (WINAPI *)({params});',
        f'static std::mutex {name}_mutex;',
        '',
        '// Per-slot target read lock-free by the thunks (acquire) and published by',
        '// bridge()/release() (release) under the mutex. Slot management (dedup +',
        '// allocation) is O(1): an index map for dedup and a free-list stack for',
        '// allocation, both touched only under the mutex, never by the thunks.',
        f'static std::atomic_uintptr_t {name}_targets[{slots}] = {{}};',
        f'struct {name}_slot {{ uint32_t index; uint32_t refs; }};',
        f'static std::unordered_map<void *, {name}_slot> {name}_index;',
        f'static std::vector<uint32_t> {name}_free_slots;',
        f'static uint32_t {name}_high_water = 0;',
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
        f'static {ms_type} {name}_thunks[{slots}] = {{',
    ])
    for index in range(slots):
        suffix = ',' if index + 1 < slots else ''
        lines.append(f'    &{name}_thunk<{index}>{suffix}')
    lines.extend([
        '};',
        '',
        f'void *bridge_{name}(void *target_ptr)',
        '{',
        '    if (!target_ptr) {',
        '        return nullptr;',
        '    }',
        f'    std::lock_guard<std::mutex> lock({name}_mutex);',
        f'    auto existing = {name}_index.find(target_ptr);',
        f'    if (existing != {name}_index.end()) {{',
        '        ++existing->second.refs;',
        f'        return reinterpret_cast<void *>({name}_thunks[existing->second.index]);',
        '    }',
        '    uint32_t index;',
        f'    if (!{name}_free_slots.empty()) {{',
        f'        index = {name}_free_slots.back();',
        f'        {name}_free_slots.pop_back();',
        f'    }} else if ({name}_high_water < {slots}) {{',
        f'        index = {name}_high_water++;',
        '    } else {',
        '        fprintf(stderr,',
        f'                "FATAL: Havok callback bridge pool exhausted for the \'{name}\' signature family:\\n"',
        f'                "  All {slots} bridge slots are in use. Every distinct live native callback of this\\n"',
        '                "  signature consumes one slot; phantom/trigger volumes and other per-block callbacks\\n"',
        '                "  in worlds with very many grids/blocks can register more than the pool can hold.\\n"',
        f'                "  Fix: raise this family in CALLBACK_SLOTS (currently {slots}) in tools/generate_havok_wrapper.py,\\n"',
        '                "  regenerate the thunks (python3 tools/generate_havok_wrapper.py), and rebuild.\\n");',
        '        std::abort();',
        '    }',
        f'    {name}_targets[index].store(reinterpret_cast<uintptr_t>(target_ptr), std::memory_order_release);',
        f'    {name}_index.emplace(target_ptr, {name}_slot{{index, 1}});',
        f'    return reinterpret_cast<void *>({name}_thunks[index]);',
        '}',
        '',
        f'void release_{name}(void *target_ptr)',
        '{',
        '    if (!target_ptr) {',
        '        return;',
        '    }',
        f'    std::lock_guard<std::mutex> lock({name}_mutex);',
        f'    auto it = {name}_index.find(target_ptr);',
        f'    if (it == {name}_index.end()) {{',
        '        return;',
        '    }',
        '    if (--it->second.refs == 0) {',
        '        uint32_t index = it->second.index;',
        f'        {name}_targets[index].store(0, std::memory_order_release);',
        f'        {name}_free_slots.push_back(index);',
        f'        {name}_index.erase(it);',
        '    }',
        '}',
        '',
    ])
    return lines


def split_args(arg_string: str):
    arg_string = arg_string.strip()
    if not arg_string:
        return []
    parts = []
    current = []
    depth = 0
    for ch in arg_string:
        if ch == ',' and depth == 0:
            parts.append(''.join(current).strip())
            current = []
            continue
        current.append(ch)
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
    if current:
        parts.append(''.join(current).strip())
    return parts


def parse_arg(arg: str):
    cleaned = re.sub(r'\[[^\]]+\]\s*', '', arg).strip()
    tokens = cleaned.split()
    if len(tokens) < 2:
        raise ValueError(f'Cannot parse argument: {arg!r}')
    return ' '.join(tokens[:-1]), tokens[-1]


def map_value_type(cs_type: str) -> str:
    if cs_type in PRIMITIVES:
        return PRIMITIVES[cs_type]
    if cs_type in KNOWN_VALUE_TYPES:
        return KNOWN_VALUE_TYPES[cs_type]
    return 'void*'


def map_param_type(cs_type: str) -> str:
    if cs_type.startswith('ref ') or cs_type.startswith('out '):
        return 'void*'
    if cs_type.endswith('[]') or cs_type.endswith('*'):
        return 'void*'
    if cs_type in ALL_DELEGATE_TYPES:
        return 'void*'
    return map_value_type(cs_type)


def map_return_type(cs_type: str) -> str:
    return map_value_type(cs_type)


def map_call_arg(cs_type: str, name: str) -> str:
    family = DELEGATE_FAMILIES.get(cs_type)
    if family is None:
        return name
    return f'bridge_{family}({name})'


def load_signatures():
    signatures = []
    seen_names = set()
    for path in sorted(CS_ROOT.rglob('*.cs')):
        lines = path.read_text(errors='ignore').splitlines()
        i = 0
        while i < len(lines):
            if '[DllImport(' not in lines[i]:
                i += 1
                continue

            i += 1
            while i < len(lines) and lines[i].strip().startswith('['):
                i += 1

            signature_lines = []
            while i < len(lines):
                signature_lines.append(lines[i].strip())
                if ';' in lines[i]:
                    break
                i += 1

            signature = ' '.join(signature_lines)
            match = re.search(r'internal\s+(?:unsafe\s+)?static\s+extern\s+([^\s]+(?:\.[^\s]+)?)\s+(\w+)\((.*)\);', signature)
            if match is None:
                # Skip DllImports that are not `internal ... static extern`. The only
                # such case is HavokLinux.Init (the Linux entry point), which is
                # provided by hand in FOOTER rather than generated as a wrapper.
                i += 1
                continue

            name = match.group(2).strip()
            if name in seen_names:
                i += 1
                continue

            signatures.append({
                'ret': match.group(1).strip(),
                'name': name,
                'args': [parse_arg(arg) for arg in split_args(match.group(3))],
            })
            seen_names.add(name)
            i += 1
    return signatures


def emit_wrapper(sig):
    ret = map_return_type(sig['ret'])
    params = ', '.join(f'{map_param_type(t)} {n}' for t, n in sig['args']) or 'void'
    arg_names = ', '.join(map_call_arg(t, n) for t, n in sig['args'])
    name = sig['name']
    lines = [f'{ret} {name}({params}) {{ EnsureThreadInfo();']
    lines.append(f'    LOG_CALL({name});')
    lines.append(f'    REQUIRE_FUNCTION_POINTER({name})')
    if name in PHANTOM_SHAPE_CREATE_FUNCS:
        enter, leave, delete = (a[1] for a in sig['args'])
        lines.append(f'    void *enterThunk = bridge_void_ptr_ptr({enter});')
        lines.append(f'    void *leaveThunk = bridge_void_ptr_ptr({leave});')
        lines.append(f'    auto result = p{name}(enterThunk, leaveThunk, reinterpret_cast<void *>(&phantom_delete_dispatch));')
        lines.append(f'    register_phantom_callback_shape(result, {enter}, {leave}, {delete});')
        lines.append('    return result;')
    elif name in OWNER_CALLBACKS:
        lines.append(f'    auto result = p{name}({arg_names});')
        bindings = ', '.join(f'callback_owner_binding{{&release_{family}, {arg}}}' for family, arg in OWNER_CALLBACKS[name])
        lines.append(f'    register_callback_owner(result, {{{bindings}}});')
        lines.append('    return result;')
    elif name in OWNER_RELEASE_FUNCS:
        release_target = sig['args'][0][1] if sig['args'] else 'nullptr'
        lines.append(f'    p{name}({arg_names});')
        lines.append(f'    release_callback_owner({release_target});')
    elif ret == 'void':
        lines.append(f'    p{name}({arg_names});')
    else:
        lines.append(f'    return p{name}({arg_names});')
    lines.append('}')
    lines.append('')
    return lines


def main():
    signatures = load_signatures()
    family_names = sorted(FAMILY_SIGNATURES)
    for path in THUNK_DIR.glob('HavokThunk_*.cpp'):
        path.unlink()
    THUNK_HEADER.write_text(emit_thunk_header(family_names))
    for family_name in family_names:
        ret, params, args = FAMILY_SIGNATURES[family_name]
        (THUNK_DIR / thunk_file_name(family_name)).write_text('\n'.join(emit_bridge_family(family_name, ret, params, args)))

    lines = [PREAMBLE, OWNER_HELPERS, STRUCTS]
    for sig in signatures:
        ret = map_return_type(sig['ret'])
        params = ', '.join(f'{map_param_type(t)} {n}' for t, n in sig['args']) or 'void'
        lines.append(f'typedef WINAPI {ret} (*{sig["name"]}_t)({params});')
    lines.append('')
    for sig in signatures:
        lines.append(f'DECLARE_FUNCTION_POINTER({sig["name"]})')
    lines.append(INIT_PREFIX)
    for sig in signatures:
        lines.append(f'    SET_FUNCTION_POINTER({sig["name"]})')
    for alias, target in EXPORT_ALIASES.items():
        lines.append(f'    register_function("Havok.dll", "{alias}", get_export("{target}"));')
    lines.append(FOOTER)
    for sig in signatures:
        lines.extend(emit_wrapper(sig))
    lines.append('} // extern "C"')
    OUTPUT.write_text('\n'.join(lines))

    total = sum(slots_for(f) for f in family_names)
    print(f'Generated Havok.cpp and {len(family_names)} thunk families ({total} slots total):')
    for f in family_names:
        print(f'  {f}: {slots_for(f)}')


if __name__ == '__main__':
    main()
