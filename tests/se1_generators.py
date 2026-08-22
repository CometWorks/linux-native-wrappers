#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import sys


root = Path(__file__).parent.parent
sys.path.insert(0, str(root / "tools"))
spec = importlib.util.spec_from_file_location(
    "generate_havok_wrapper", root / "tools/generate_havok_wrapper.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)

phantom = {
    "ret": "IntPtr",
    "name": "HkPhantomCallbackShape_Create",
    "args": [
        ("HkPhantomCallbackShape.HkPhantomHandlerCpp", "enterCallback"),
        ("HkPhantomCallbackShape.HkPhantomHandlerCpp", "leaveCallback"),
        ("HkDeleteHandler", "deleteCallback"),
    ],
}
wrapper = "\n".join(generator.emit_wrapper(phantom))
specs = generator.callback_specs([phantom])
helpers = generator.emit_callback_helpers(specs)

assert specs == {
}
assert "&phantom_enter_bridge" in wrapper
assert "&phantom_leave_bridge" in wrapper
assert "&phantom_delete_bridge" in wrapper
assert "register_phantom_callbacks(result" in wrapper
assert "phantom_enter_bridge" not in helpers
assert "g_phantom_shape_bindings" in generator.PHANTOM_CALLBACK_HELPERS

constraint_reader = "\n".join(generator.emit_wrapper({
    "ret": "void",
    "name": "HkConstraint_FindConnectedConstraints",
    "args": [
        ("IntPtr", "rigidBody"),
        ("HkConstraint.ReadConstraintsCallback", "reader"),
        ("IntPtr", "userData"),
    ],
}))
assert "std::lock_guard<std::mutex> lock(g_constraint_reader_mutex)" in constraint_reader
assert "g_constraint_reader_target.store(0" in constraint_reader

cleanup = "\n".join(generator.emit_wrapper({
    "ret": "bool",
    "name": "HkShapeLoader_CleanupShapesBuffer",
    "args": [
        ("int", "cBuffer"),
        ("byte[]", "buffer"),
        ("HkShapeLoader.ReturnByteArray", "returnByteArray"),
    ],
}))
assert "std::lock_guard<std::mutex> lock(g_shape_loader_return_mutex)" in cleanup
assert "g_shape_loader_return_target.store(0" in cleanup

signatures = generator.load_signatures([{
    "entry_point": "Example",
    "name": "Example",
    "ret": "void",
    "return_i1": False,
    "params": [{"modifier": "out", "type": "int", "name": "value"}],
    "source": "Example.cs",
}])
assert signatures == [{"ret": "void", "name": "Example", "args": [("out int", "value")]}]

assert generator.map_value_type("WaitPolicyT") == "int32_t"
assert generator.map_value_type("HkUniformGridShapeArgsPOD") == "HkUniformGridShapeArgsPOD"
assert generator.map_value_type("DecomposeShapeKeyResult") == "HkStaticCompoundShape_DecomposeShapeKeyResult"

source = (root / "src/Havok.cpp").read_text()
for function, callbacks in generator.STATIC_CALLBACKS.items():
    assert function in source
    for argument, semantic in callbacks.items():
        assert f'register_static_callback("{semantic}", {argument},' in source
        assert f'{semantic}_bridge' in source
for callbacks in generator.TRANSIENT_CALLBACKS.values():
    for semantic in callbacks.values():
        assert f'g_{semantic}_mutex' in source
        assert f'g_{semantic}_target.store(0' in source
