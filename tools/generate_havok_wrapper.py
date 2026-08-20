#!/usr/bin/env python3
"""Generate the Havok wrapper source: src/Havok.cpp.

`Havok.cpp` is a thin C++ shim: one `extern "C"` function per Havok.dll export,
parsed from the game's decompiled `[DllImport]` declarations. Delegate arguments
are routed through fixed callback bridges so managed (SysV) callbacks can be
handed to the Microsoft-ABI Havok.dll.

Generating `Havok.cpp` needs the decompiled C# wrapper
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

SRC_DIR = PROJECT_DIR / 'src'
OUTPUT = SRC_DIR / 'Havok.cpp'

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

for qualified_name, cpp_type in list(KNOWN_VALUE_TYPES.items()):
    short_name = qualified_name.rsplit('.', 1)[-1]
    previous = KNOWN_VALUE_TYPES.setdefault(short_name, cpp_type)
    if previous != cpp_type:
        raise ValueError(f'Ambiguous Havok value type name: {short_name}')

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

# Decompiled nested declarations use either `Owner.Delegate` or the short name,
# depending on the decompiler version.
for qualified_name, family in list(DELEGATE_FAMILIES.items()):
    short_name = qualified_name.rsplit('.', 1)[-1]
    previous = DELEGATE_FAMILIES.setdefault(short_name, family)
    if previous != family:
        raise ValueError(f'Ambiguous Havok delegate name: {short_name}')
ALL_DELEGATE_TYPES.update(DELEGATE_FAMILIES)

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

# These callbacks reuse the exact rooted delegate object under CoreCLR, so each
# semantic callback can use one process-lifetime target and one fixed MS-ABI bridge.
# Keep separate bridge names for methods which share a C signature.
STATIC_CALLBACKS = {
    'HkActivationListener_Create': {
        'onActivate': 'activation_activate',
        'onDeactivate': 'activation_deactivate',
    },
    'HkBaseSystem_Init': {'log': 'base_system_log'},
    'HkBreakOffPartsUtil_Create': {
        'breakLogicHandler': 'break_off_logic',
        'breakPartsHandler': 'break_off_parts',
    },
    'HkConstraintListener_SetCallbacks': {
        'onAdded': 'constraint_added',
        'onRemoved': 'constraint_removed',
        'onBreaking': 'constraint_breaking',
    },
    'HkContactListener_Create': {
        'onContact': 'contact_point',
        'collisionAdded': 'contact_collision_added',
        'collisionRemoved': 'contact_collision_removed',
    },
    'HkContactSoundListener_Create': {'onContact': 'contact_sound'},
    'HkEntityListener_Create': {
        'onAdd': 'entity_add',
        'onRemove': 'entity_remove',
        'onDelete': 'entity_delete',
        'onShapeChange': 'entity_shape_change',
        'onMotionTypeChange': 'entity_motion_type_change',
    },
    'HkJobThreadPool_RunOnEachWorker': {'action': 'job_thread_action'},
    'HkTaskProfiler_Init': {
        'onTaskStarted': 'task_started',
        'onTaskFinished': 'task_finished',
    },
    'HkTaskProfiler_ReplayTimers': {
        'blockBegin': 'task_block_begin',
        'blockEnd': 'task_block_end',
    },
    'HkUniformGridShape_SetDeleteHandler': {'handler': 'uniform_grid_delete'},
    'HkUniformGridShape_SetShapeRequestHandler': {'blockingCallback': 'uniform_grid_request'},
    'HkWheelResponseModifierUtil_Create': {
        'softness': 'wheel_softness',
        'acceleration': 'wheel_acceleration',
    },
    'HkWorld_Create': {'broadPhaseCallback': 'world_broad_phase_exit'},
    'HkWorld_CreateCInfo': {'broadPhaseCallback': 'world_broad_phase_exit'},
    'HkpAabbPhantom_Create': {
        'collidableAddedD': 'aabb_phantom_added',
        'collidableRemovedD': 'aabb_phantom_removed',
    },
}

# These callbacks are invoked synchronously. A fixed bridge plus a mutex serializes
# each short native call without executable thunks.
TRANSIENT_CALLBACKS = {
    'HkConstraint_FindConnectedConstraints': {'reader': 'constraint_reader'},
    'HkShapeLoader_CleanupShapesBuffer': {'returnByteArray': 'shape_loader_return'},
}

PHANTOM_CALLBACK_CREATE = 'HkPhantomCallbackShape_Create'

EXPORT_ALIASES = {
    'HkJobThreadPool_RemoveReference': '?HkJobThreadPool_RemoveReference@Havok@@YAXPEAVhkThreadPool@@@Z',
}

PINVOKE_SIGNATURE = re.compile(
    r'(?:public|protected(?:\s+internal)?|internal|private)\s+'
    r'(?:unsafe\s+)?static\s+extern\s+([^\s]+(?:\.[^\s]+)?)\s+'
    r'(\w+)\((.*)\);')

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

#include "dll_loader.h"

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

PHANTOM_CALLBACK_HELPERS = '''struct phantom_callback_shape_binding {
    void *enter;
    void *leave;
    void *del;
};

static std::mutex g_phantom_shape_mutex;
static std::unordered_map<void *, phantom_callback_shape_binding> g_phantom_shape_bindings;

static phantom_callback_shape_binding get_phantom_callbacks(void *shape, bool remove)
{
    std::lock_guard<std::mutex> lock(g_phantom_shape_mutex);
    auto it = g_phantom_shape_bindings.find(shape);
    if (it == g_phantom_shape_bindings.end()) {
        fprintf(stderr, "FATAL: Havok callback for unknown phantom shape %p\\n", shape);
        std::abort();
    }
    auto callbacks = it->second;
    if (remove) {
        g_phantom_shape_bindings.erase(it);
    }
    return callbacks;
}

using phantom_handler_sysv_t = void (*)(void *, void *);
using phantom_delete_sysv_t = void (*)(void *);

static void WINAPI phantom_enter_bridge(void *shape, void *body)
{
    auto target = get_phantom_callbacks(shape, false).enter;
    if (target) {
        reinterpret_cast<phantom_handler_sysv_t>(target)(shape, body);
    }
}

static void WINAPI phantom_leave_bridge(void *shape, void *body)
{
    auto target = get_phantom_callbacks(shape, false).leave;
    if (target) {
        reinterpret_cast<phantom_handler_sysv_t>(target)(shape, body);
    }
}

static void WINAPI phantom_delete_bridge(void *shape)
{
    auto target = get_phantom_callbacks(shape, true).del;
    if (target) {
        reinterpret_cast<phantom_delete_sysv_t>(target)(shape);
    }
}

static void register_phantom_callbacks(void *shape, void *enter, void *leave, void *del)
{
    if (!shape) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_phantom_shape_mutex);
    g_phantom_shape_bindings[shape] = {enter, leave, del};
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

static void InitImpl(const char* dllPath, const char* sidecarPath)
{
    if (!load_dll(&g_havok_image, dllPath, sidecarPath)) {
        LogMessage("Failed to load Havok.dll");
        throw std::runtime_error("Failed to load Havok.dll");
    }
'''

FOOTER = '''}

extern "C" {

void Init(const char* dllPath, const char* sidecarPath)
{
    if (g_havok_image.image) {
        fprintf(stderr,
                "[LinuxCompat] Havok::Init: already initialized (image=%p, dllPath='%s'); "
                "ignoring duplicate call.\\n",
                g_havok_image.image, dllPath ? dllPath : "<null>");
        return;
    }
    InitImpl(dllPath, sidecarPath);
}
'''

def callback_specs(signatures):
    specs = {}
    for sig in signatures:
        if sig['name'] == PHANTOM_CALLBACK_CREATE:
            continue
        mappings = {**STATIC_CALLBACKS.get(sig['name'], {}),
                    **TRANSIENT_CALLBACKS.get(sig['name'], {})}
        for cs_type, arg_name in sig['args']:
            family = DELEGATE_FAMILIES.get(cs_type)
            semantic = mappings.get(arg_name)
            if family is None:
                continue
            if semantic is None:
                raise ValueError(f'No fixed callback bridge for {sig["name"]}.{arg_name}')
            previous = specs.setdefault(semantic, family)
            if previous != family:
                raise ValueError(f'Conflicting signatures for callback {semantic}')
    return specs


def emit_callback_helpers(specs):
    lines = [
        'static void *register_static_callback(const char *name, void *target,',
        '                                      std::atomic_uintptr_t &slot, void *bridge)',
        '{',
        '    if (!target) {',
        '        return nullptr;',
        '    }',
        '    uintptr_t target_bits = reinterpret_cast<uintptr_t>(target);',
        '    uintptr_t expected = 0;',
        '    if (!slot.compare_exchange_strong(expected, target_bits,',
        '                                      std::memory_order_release,',
        '                                      std::memory_order_acquire)',
        '        && expected != target_bits) {',
        '        fprintf(stderr, "FATAL: Havok static callback \'%s\' changed target "',
        '                        "from %p to %p\\n", name,',
        '                reinterpret_cast<void *>(expected), target);',
        '        std::abort();',
        '    }',
        '    return bridge;',
        '}',
        '',
    ]
    transient_names = {name for mappings in TRANSIENT_CALLBACKS.values()
                       for name in mappings.values()}
    for name, family in sorted(specs.items()):
        ret, params, args = FAMILY_SIGNATURES[family]
        lines.extend([
            f'using {name}_sysv_t = {ret} (*)({params});',
            f'static std::atomic_uintptr_t g_{name}_target{{0}};',
        ])
        if name in transient_names:
            lines.append(f'static std::mutex g_{name}_mutex;')
        lines.extend([
            f'static {ret} WINAPI {name}_bridge({params})',
            '{',
            f'    auto fn = reinterpret_cast<{name}_sysv_t>(',
            f'        g_{name}_target.load(std::memory_order_acquire));',
            '    if (!fn) {',
            f'        fprintf(stderr, "FATAL: Havok static callback \'{name}\' has no target\\n");',
            '        std::abort();',
            '    }',
        ])
        call = f'fn({args})' if args else 'fn()'
        lines.append(f'    {call};' if ret == 'void' else f'    return {call};')
        lines.extend(['}', ''])
    return '\n'.join(lines)


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


def map_call_arg(function: str, cs_type: str, name: str) -> str:
    if cs_type not in ALL_DELEGATE_TYPES:
        return name
    semantic = STATIC_CALLBACKS.get(function, {}).get(name)
    if semantic:
        return (f'register_static_callback("{semantic}", {name}, '
                f'g_{semantic}_target, reinterpret_cast<void *>(&{semantic}_bridge))')
    semantic = TRANSIENT_CALLBACKS.get(function, {}).get(name)
    if semantic:
        return f'{name} ? reinterpret_cast<void *>(&{semantic}_bridge) : nullptr'
    raise ValueError(f'No fixed callback bridge for {function}.{name}')


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
            match = PINVOKE_SIGNATURE.search(signature)
            if match is None:
                # HavokLinux.Init is provided by hand in FOOTER rather than
                # generated as a wrapper.
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
    name = sig['name']
    arg_names = '' if name == PHANTOM_CALLBACK_CREATE else ', '.join(
        map_call_arg(name, t, n) for t, n in sig['args'])
    lines = [f'{ret} {name}({params}) {{ EnsureThreadInfo();']
    lines.append(f'    LOG_CALL({name});')
    lines.append(f'    REQUIRE_FUNCTION_POINTER({name})')
    if name == PHANTOM_CALLBACK_CREATE:
        enter, leave, delete = (arg_name for _, arg_name in sig['args'])
        lines.append(
            f'    auto result = p{name}('
            f'{enter} ? reinterpret_cast<void *>(&phantom_enter_bridge) : nullptr, '
            f'{leave} ? reinterpret_cast<void *>(&phantom_leave_bridge) : nullptr, '
            f'{delete} ? reinterpret_cast<void *>(&phantom_delete_bridge) : nullptr);')
        lines.append(f'    register_phantom_callbacks(result, {enter}, {leave}, {delete});')
        lines.append('    return result;')
    elif name in TRANSIENT_CALLBACKS:
        callback_arg, semantic = next(iter(TRANSIENT_CALLBACKS[name].items()))
        lines.append(f'    std::lock_guard<std::mutex> lock(g_{semantic}_mutex);')
        lines.append(f'    g_{semantic}_target.store(reinterpret_cast<uintptr_t>({callback_arg}), std::memory_order_release);')
        if ret == 'void':
            lines.append(f'    p{name}({arg_names});')
        else:
            lines.append(f'    auto result = p{name}({arg_names});')
        lines.append(f'    g_{semantic}_target.store(0, std::memory_order_release);')
        if ret != 'void':
            lines.append('    return result;')
    elif ret == 'void':
        lines.append(f'    p{name}({arg_names});')
    else:
        lines.append(f'    return p{name}({arg_names});')
    lines.append('}')
    lines.append('')
    return lines


def main():
    signatures = load_signatures()
    specs = callback_specs(signatures)
    for path in SRC_DIR.glob('HavokThunk_*.cpp'):
        path.unlink()
    (SRC_DIR / 'HavokThunkRegistry.h').unlink(missing_ok=True)

    lines = [PREAMBLE, emit_callback_helpers(specs), PHANTOM_CALLBACK_HELPERS, STRUCTS]
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

    print(f'Generated Havok.cpp with {len(specs) + 3} fixed callback bridges')


if __name__ == '__main__':
    main()
