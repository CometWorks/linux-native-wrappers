#!/usr/bin/env python3
"""Generate VRage.Physics.Native forwarders from decompiled P/Invokes."""

import re
from collections import Counter
from pathlib import Path

from csharp_pinvoke import (
    CPP_TYPES, cpp_identifier, load_generator_declarations, normalize_type, parameter_names,
)


ROOT = Path(__file__).parent.parent
OUTPUT = ROOT / "src/Physics.cpp"
MANIFEST = ROOT / "src/Physics.exports"

STRUCT_TYPES = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t", 8: "uint64_t"}

SET_EXPAND = "?SetExpandStub@HkBuffer@@SAXP6APEAXP6APEAXPEAU1@PEAXH@Z01H@Z@Z"
HK_INIT = "?HkInit@HkMainSystem@@SAXAEBUHkMainCinfo@1@@Z"
SET_DELETION = "?SetColliderDeletionCallback@@YAXP6AXPEBVhknpShape@@@Z@Z"
CREATE_DEBUG_DRAW = "?CreateDebugDrawSystem@HkSession@@QEAAPEAVHkDebugProcessManager@@AEBUDebugDrawImplementationTable@@PEAX@Z"
CREATE_CHARACTER = "?Create@HkCharacterController@@SAPEAV1@AEBUHkCharacterControllerCinfo@@@Z"
SESSION_CTOR = "??0HkSession@@QEAA@AEBUHkSessionCinfo@0@@Z"
SESSION_DTOR = "??1HkSession@@QEAA@XZ"
ALLOCATE_WORLD = "?AllocateWorld@HkSession@@QEAAAEAVhknpWorld@@_NAEBVhkVector4d@@1H@Z"
RELEASE_WORLD = "?ReleaseWorld@HkSession@@QEAAXAEAVhknpWorld@@@Z"
SUBSCRIBE_MUTATION = "?SubscribeToMutation@@YAXPEBVhknpShape@@PEAX@Z"
UNSUBSCRIBE_MUTATION = "?UnsubscribeFromMutation@@YAXPEBVhknpShape@@PEAX@Z"
SET_CHARACTER_BODY_INFO = "?SetBodyInfo@HkCharacterController@@QEAAXAEBUBodyInfoPackf@@M_N@Z"

PRIMITIVES = {
    "void": "void", "bool": "bool4", "byte": "u1", "sbyte": "i1",
    "short": "i2", "ushort": "u2", "int": "i4", "uint": "u4",
    "long": "i8", "ulong": "u8", "float": "r4", "double": "r8",
    "nint": "ptr", "IntPtr": "ptr", "nuint": "u8", "UIntPtr": "u8",
    "string": "str",
}
BYTE_ENUMS = {
    "HkLayerInteractionInteractType", "HkLog.Level", "Level",
    "HknpConnectivityGraph.CompressedIntArray.IntSize", "IntSize", "HknpLevelOfDetail",
    "VrBodyPartProvider", "VrDestructionIgnoredReason", "VrDestructionManifoldFlags",
}
SBYTE_ENUMS = {"HkRagdoll.State", "State"}
INT_ENUMS = {
    "AxisIdentifier", "AxisMode", "HknpActivationControl.Enum",
    "HknpActivationState.Enum", "HknpBodyQualityId.Preset", "Preset",
    "HknpBreakableCompoundShape.Status", "Status", "HknpCollisionDispatchType.Enum", "Enum",
    "HknpConstraint.FlagsEnum", "FlagsEnum", "ConstraintType", "Mutability",
    "UpdateAtomsResult.Enum", "HknpMotionType.Enum", "MassConfig.Quality", "Quality",
    "ScaleMode", "HknpShapeType.Enum", "ActivationMode", "AdditionMode",
    "PivotLocation", "UpdateCachesMode", "UpdateMotionMode",
}
INTEGER_STRUCTS = {
    "HknpBodyId.__Internal": 8, "HknpConstraintId.__Internal": 8,
    "HknpCollisionFlags.__Internal": 8, "HknpMotionId.__Internal": 4,
    "HknpMaterialId.__Internal": 2, "HknpBodyQualityId.__Internal": 1,
    "__Internal": 1, "MotionSharing.BodyPartId": 4, "BodyPartId": 4,
    "HkSimplificationParameters.__Internal": 8,
}


def lower_type(value, return_i1=False, modifier=None):
    value = normalize_type(value)
    if modifier in {"ref", "out", "in"} or value.endswith("*"):
        return {"kind": "ptr"}
    if value == "bool" and return_i1:
        return {"kind": "bool1"}
    if value in PRIMITIVES:
        return {"kind": PRIMITIVES[value]}
    if any(value == name or value.endswith(f".{name}") for name in BYTE_ENUMS):
        return {"kind": "u1"}
    if any(value == name or value.endswith(f".{name}") for name in SBYTE_ENUMS):
        return {"kind": "i1"}
    if any(value == name or value.endswith(f".{name}") for name in INT_ENUMS):
        return {"kind": "i4"}
    if value.startswith("HkFlags<"):
        storage = value.rsplit(",", 1)[-1].rstrip(">")
        if storage in PRIMITIVES:
            return {"kind": PRIMITIVES[storage]}
    for name, size in INTEGER_STRUCTS.items():
        if value == name or value.endswith(f".{name}"):
            return {"kind": "struct", "cls": ["int"], "size": size}
    if value == "HkSimdFloat" or value.endswith(".HkSimdFloat"):
        return {"kind": "struct", "type": "HkSimdFloat", "size": 16}
    raise SystemExit(f"unsupported Physics C# ABI type: {value}")


def load_signatures(declarations):
    signatures = {}
    for declaration in declarations:
        signature = {
            "name": declaration["name"],
            "ret": lower_type(declaration["ret"], declaration["return_i1"]),
            "params": [dict(lower_type(param["type"], modifier=param["modifier"]), n=param["name"])
                       for param in declaration["params"]],
        }
        identity = (signature["ret"], [{k: v for k, v in param.items() if k != "n"}
                                        for param in signature["params"]])
        previous = signatures.get(declaration["entry_point"])
        if previous and previous[0] != identity:
            raise SystemExit(f"conflicting signature for {declaration['entry_point']}")
        signatures.setdefault(declaration["entry_point"], (identity, signature))
    signatures = sorted((entry_point, value[1]) for entry_point, value in signatures.items())
    counts = Counter(signature["name"] for _, signature in signatures)
    generated = []
    for entry_point, signature in signatures:
        name = cpp_identifier(signature["name"])
        if counts[signature["name"]] > 1:
            name = f"{cpp_identifier(owner(entry_point))}_{name}"
        generated.append(name)
    generated_counts = Counter(generated)
    used = set()
    for (entry_point, signature), name in zip(signatures, generated):
        if generated_counts[name] > 1:
            params = [param for param in parameter_names(signature["params"]) if param != "instance"]
            name = f"{name}_{'_'.join(params) or 'void'}"
        base = name
        suffix = 2
        while name in used:
            name = f"{base}_{suffix}"
            suffix += 1
        signature["cpp_name"] = name
        used.add(name)
    return signatures


def owner(entry_point):
    match = re.match(r"\?\?[01]([^@]+)@@", entry_point)
    if match is None:
        match = re.match(r"\?[^@]+@([^@]+)@@", entry_point)
    return match.group(1) if match else "Global"


def cpp_type(value):
    if value["kind"] == "struct":
        if value.get("type") == "HkSimdFloat":
            return "HkSimdFloat"
        if value.get("cls") != ["int"] or value.get("size") not in STRUCT_TYPES:
            raise SystemExit(f"unsupported struct ABI: {value}")
        return STRUCT_TYPES[value["size"]]
    try:
        return CPP_TYPES[value["kind"]]
    except KeyError:
        raise SystemExit(f"unsupported ABI kind: {value['kind']}") from None


def pe_cpp_type(value):
    if value["kind"] == "struct" and value.get("size", 0) > 8:
        return f"{cpp_type(value)} *"
    return cpp_type(value)


def emit(signatures):
    lines = [
        "// Generated by tools/generate_physics_wrapper.py. Do not edit.",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <cstdlib>",
        "#include <cstring>",
        "#include <mutex>",
        "#include <stdexcept>",
        "#include <unordered_map>",
        "",
        '#include "dll_loader.h"',
        "",
        "struct HkSimdFloat { float values[4]; };",
        "static_assert(sizeof(HkSimdFloat) == 16);",
        "",
        "namespace {",
        "pe_image physics_image;",
        "std::mutex physics_mutex;",
        "",
        "void *sysv_expand_callback;",
        "void *sysv_log_callback;",
        "void *sysv_debug_callback;",
        "void *sysv_deletion_callback;",
        "void *sysv_mutation_callback;",
        "",
        "void *WINAPI expand_bridge(void *resize, void *buffer, void *current_data, int requested_size)",
        "{",
        "    using Function = void *(*)(void *, void *, void *, int);",
        "    return reinterpret_cast<Function>(sysv_expand_callback)(resize, buffer, current_data, requested_size);",
        "}",
        "",
        "void WINAPI log_bridge(uint32_t message_id, uint8_t level, void *message, void *context)",
        "{",
        "    using Function = void (*)(uint32_t, uint8_t, void *, void *);",
        "    reinterpret_cast<Function>(sysv_log_callback)(message_id, level, message, context);",
        "}",
        "",
        "void WINAPI debug_bridge(void *message, void *context)",
        "{",
        "    using Function = void (*)(void *, void *);",
        "    reinterpret_cast<Function>(sysv_debug_callback)(message, context);",
        "}",
        "",
        "void WINAPI deletion_bridge(void *shape)",
        "{",
        "    using Function = void (*)(void *);",
        "    reinterpret_cast<Function>(sysv_deletion_callback)(shape);",
        "}",
        "",
        "void WINAPI debug_draw_bridge() {}",
        "",
        "struct ContactVector4 { float x, y, z, w; };",
        "static_assert(sizeof(ContactVector4) == 16);",
        "using ContactImpulseCallback = void (*)(uint64_t, uint64_t, void *, float *, void *, uint16_t, uint16_t, ContactVector4, uint32_t, uint32_t);",
        "struct ContactRoute { void *session; ContactImpulseCallback callback; };",
        "std::mutex contact_mutex;",
        "std::unordered_map<void *, ContactImpulseCallback> contact_callbacks;",
        "std::unordered_map<void *, ContactRoute> contact_routes;",
        "",
        "void WINAPI contact_impulse_bridge(uint64_t body_a, uint64_t body_b, void *manifold, float *impulses, void *world, uint16_t material_a, uint16_t material_b, ContactVector4 projected_velocities, uint32_t shape_key_a, uint32_t shape_key_b)",
        "{",
        "    ContactImpulseCallback callback = nullptr;",
        "    {",
        "        std::lock_guard<std::mutex> lock(contact_mutex);",
        "        auto route = contact_routes.find(world);",
        "        if (route != contact_routes.end())",
        "            callback = route->second.callback;",
        "    }",
        "    if (callback)",
        "        callback(body_a, body_b, manifold, impulses, world, material_a, material_b, projected_velocities, shape_key_a, shape_key_b);",
        "}",
        "",
        "void *character_original_vtable[20];",
        "void *character_vtable[20];",
        "std::once_flag character_vtable_once;",
        "",
        "template<std::size_t Index, typename Return, typename... Args>",
        "Return character_call(void *instance, Args... args)",
        "{",
        "    using Function = Return(WINAPI *)(void *, Args...);",
        "    return reinterpret_cast<Function>(character_original_vtable[Index])(instance, args...);",
        "}",
        "",
        "void character_dtor(void *i) { character_call<0, void>(i, 0); }",
        "void character_set_support_distance(void *i, float v) { character_call<1, void>(i, v); }",
        "float character_get_support_distance(void *i) { return character_call<2, float>(i); }",
        "void character_set_sticky_factor(void *i, float v) { character_call<3, void>(i, v); }",
        "float character_get_sticky_factor(void *i) { return character_call<4, float>(i); }",
        "void character_set_transform(void *i, void *v) { character_call<5, void>(i, v); }",
        "void *character_get_transform(void *i) { return character_call<6, void *>(i); }",
        "void character_set_velocities(void *i, void *v) { character_call<7, void>(i, v); }",
        "void character_get_velocities(void *i, void *v) { character_call<8, void>(i, v); }",
        "void character_add_to_world(void *i, int32_t a, int32_t b) { character_call<9, void>(i, a, b); }",
        "void character_remove_from_world(void *i, int32_t v) { character_call<10, void>(i, v); }",
        "void character_set_floating(void *i, uint8_t v) { character_call<11, void>(i, v); }",
        "void character_set_shape(void *i, void *v) { character_call<12, void>(i, v); }",
        "void character_get_support_contacts(void *i, void *v) { character_call<13, void>(i, v); }",
        "void character_get_body(void *i, void *v) { character_call<14, void>(i, v); }",
        "void character_detach_from_body(void *i) { character_call<15, void>(i); }",
        "void character_migrate(void *i, void *w, uint64_t id) { character_call<16, void>(i, w, id); }",
        "void character_pre_migrate(void *i) { character_call<17, void>(i); }",
        "void character_post_migrate(void *i, void *v) { character_call<18, void>(i, v); }",
        "",
        "void bridge_character_vtable(void *instance)",
        "{",
        "    std::call_once(character_vtable_once, [instance] {",
        "        void **original = *reinterpret_cast<void ***>(instance);",
        "        std::memcpy(character_original_vtable, original, sizeof(character_original_vtable));",
        "        std::memcpy(character_vtable, original, sizeof(character_vtable));",
        "        void *bridges[] = { reinterpret_cast<void *>(&character_dtor), reinterpret_cast<void *>(&character_set_support_distance), reinterpret_cast<void *>(&character_get_support_distance), reinterpret_cast<void *>(&character_set_sticky_factor), reinterpret_cast<void *>(&character_get_sticky_factor), reinterpret_cast<void *>(&character_set_transform), reinterpret_cast<void *>(&character_get_transform), reinterpret_cast<void *>(&character_set_velocities), reinterpret_cast<void *>(&character_get_velocities), reinterpret_cast<void *>(&character_add_to_world), reinterpret_cast<void *>(&character_remove_from_world), reinterpret_cast<void *>(&character_set_floating), reinterpret_cast<void *>(&character_set_shape), reinterpret_cast<void *>(&character_get_support_contacts), reinterpret_cast<void *>(&character_get_body), reinterpret_cast<void *>(&character_detach_from_body), reinterpret_cast<void *>(&character_migrate), reinterpret_cast<void *>(&character_pre_migrate), reinterpret_cast<void *>(&character_post_migrate) };",
        "        std::memcpy(character_vtable, bridges, sizeof(bridges));",
        "    });",
        "    *reinterpret_cast<void ***>(instance) = character_vtable;",
        "}",
        "",
        "void WINAPI mutation_bridge(void *shape, uint8_t flags)",
        "{",
        "    using Function = void (*)(void *, uint8_t);",
        "    reinterpret_cast<Function>(sysv_mutation_callback)(shape, flags);",
        "}",
        "",
        "void initialize(const char *dll_path, const char *sidecar_path);",
        "",
        "void ensure_thread_info()",
        "{",
        "    if (!physics_image.image)",
        '        throw std::runtime_error("Physics is not initialized; call Init first");',
        "    if (!setup_nt_threadinfo(nullptr))",
        "        std::abort();",
        "    pe_ensure_tls_for_loaded_images();",
        "}",
        "",
    ]

    for _, signature in signatures:
        ret = cpp_type(signature["ret"])
        params = ", ".join(pe_cpp_type(param) for param in signature["params"]) or "void"
        name = signature["cpp_name"]
        lines.append(f"using {name}_t = {ret}(WINAPI *)({params});")
        lines.append(f"{name}_t p{name};")

    lines += [
        "",
        "void initialize(const char *dll_path, const char *sidecar_path)",
        "{",
        "    std::lock_guard<std::mutex> lock(physics_mutex);",
        "    if (physics_image.image)",
        "        return;",
        "",
        "    if (!dll_path)",
        '        throw std::invalid_argument("Physics Init requires a DLL path");',
        "    if (!load_dll(&physics_image, dll_path, sidecar_path))",
        '        throw std::runtime_error("Failed to load VRage.Physics.Native.dll");',
    ]
    for entry_point, signature in signatures:
        name = signature["cpp_name"]
        lines += [
            f'    p{name} = reinterpret_cast<{name}_t>(get_export("{entry_point}"));',
        ]
    lines += ["}", "}", "", 'extern "C" {', "", "void Init(const char *dll_path, const char *sidecar_path)", "{", "    initialize(dll_path, sidecar_path);", "}", ""]

    manifest = []
    for entry_point, signature in signatures:
        ret = cpp_type(signature["ret"])
        name = signature["cpp_name"]
        names = parameter_names(signature["params"])
        declarations = ", ".join(f"{cpp_type(param)} {param_name}" for param, param_name in zip(signature["params"], names)) or "void"
        arguments = ", ".join(f"&{name}_pe" if param["kind"] == "struct" and param.get("size", 0) > 8 else name
                              for param, name in zip(signature["params"], names))
        placeholder = entry_point.replace("@", "$")
        if placeholder != entry_point:
            manifest.append(f"{placeholder}\t{entry_point}")
        lines += [
            f'{ret} {name}({declarations}) __asm__("\\\"{placeholder}\\\"");',
            f"{ret} {name}({declarations})",
            "{",
            "    ensure_thread_info();",
            f"    if (!p{name})",
            f'        throw std::runtime_error("Missing Physics export: {entry_point}");',
        ]
        for param, param_name in zip(signature["params"], names):
            if param["kind"] == "struct" and param.get("size", 0) > 8:
                lines.append(f"    alignas(16) {cpp_type(param)} {param_name}_pe = {param_name};")
        first = names[0] if names else None
        if entry_point == SESSION_CTOR:
            lines += [
                "    alignas(void *) uint8_t cinfo[96];",
                f"    std::memcpy(cinfo, {names[1]}, sizeof(cinfo));",
                "    void *callback = *reinterpret_cast<void **>(cinfo);",
                "    if (callback)",
                "        *reinterpret_cast<void **>(cinfo) = reinterpret_cast<void *>(&contact_impulse_bridge);",
                f"    {names[1]} = cinfo;",
            ]
        elif entry_point == CREATE_DEBUG_DRAW:
            lines += [
                "    void *callbacks[7];",
                "    std::memcpy(callbacks, table, sizeof(callbacks));",
                "    for (void *&callback : callbacks)",
                "        callback = reinterpret_cast<void *>(&debug_draw_bridge);",
                "    table = callbacks;",
            ]
        elif entry_point in {SUBSCRIBE_MUTATION, UNSUBSCRIBE_MUTATION}:
            lines += [
                f"    sysv_mutation_callback = {names[1]};",
                f"    {names[1]} = {names[1]} ? reinterpret_cast<void *>(&mutation_bridge) : nullptr;",
            ]
        elif entry_point == SET_CHARACTER_BODY_INFO:
            lines.append(f"    *reinterpret_cast<void ***>({first}) = character_original_vtable;")
        elif entry_point == SET_EXPAND:
            lines += [f"    sysv_expand_callback = {first};", f"    {first} = reinterpret_cast<void *>(&expand_bridge);"]
        elif entry_point == SET_DELETION:
            lines += [f"    sysv_deletion_callback = {first};", f"    {first} = reinterpret_cast<void *>(&deletion_bridge);"]
        elif entry_point == HK_INIT:
            lines += [
                "    uint8_t cinfo[88];",
                f"    std::memcpy(cinfo, {first}, sizeof(cinfo));",
                "    sysv_log_callback = *reinterpret_cast<void **>(cinfo);",
                "    sysv_debug_callback = *reinterpret_cast<void **>(cinfo + 8);",
                "    *reinterpret_cast<void **>(cinfo) = reinterpret_cast<void *>(&log_bridge);",
                "    *reinterpret_cast<void **>(cinfo + 8) = reinterpret_cast<void *>(&debug_bridge);",
                f"    {first} = cinfo;",
            ]
        call = f"p{name}({arguments})"
        if entry_point == SESSION_CTOR:
            lines += [
                f"    void *result = {call};",
                "    if (result && callback) {",
                "        std::lock_guard<std::mutex> lock(contact_mutex);",
                "        contact_callbacks[result] = reinterpret_cast<ContactImpulseCallback>(callback);",
                "    }",
                "    return result;",
            ]
        elif entry_point == ALLOCATE_WORLD:
            lines += [
                f"    void *result = {call};",
                "    if (result) {",
                "        std::lock_guard<std::mutex> lock(contact_mutex);",
                f"        auto callback = contact_callbacks.find({first});",
                "        if (callback != contact_callbacks.end())",
                f"            contact_routes[result] = {{ {first}, callback->second }};",
                "    }",
                "    return result;",
            ]
        elif entry_point == RELEASE_WORLD:
            lines += [
                "    {",
                "        std::lock_guard<std::mutex> lock(contact_mutex);",
                f"        contact_routes.erase({names[1]});",
                "    }",
                f"    {call};",
            ]
        elif entry_point == SESSION_DTOR:
            lines += [
                "    {",
                "        std::lock_guard<std::mutex> lock(contact_mutex);",
                "        for (auto route = contact_routes.begin(); route != contact_routes.end();) {",
                f"            if (route->second.session == {first})",
                "                route = contact_routes.erase(route);",
                "            else",
                "                ++route;",
                "        }",
                f"        contact_callbacks.erase({first});",
                "    }",
                f"    {call};",
            ]
        elif entry_point == CREATE_CHARACTER:
            lines += [
                f"    void *result = {call};",
                "    if (result)",
                "        bridge_character_vtable(result);",
                "    return result;",
            ]
        else:
            lines.append(f"    {call};" if ret == "void" else f"    return {call};")
        lines += ["}", ""]
    lines += ["}", ""]
    return "\n".join(lines), "\n".join(manifest) + "\n"


if __name__ == "__main__":
    declarations = load_generator_declarations("VRage.Physics", "VRage.Physics.Native.dll")
    source, manifest = emit(load_signatures(declarations))
    OUTPUT.write_text(source, encoding="utf-8")
    MANIFEST.write_text(manifest, encoding="utf-8")
