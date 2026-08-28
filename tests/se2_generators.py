#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import sys
import tempfile


root = Path(__file__).parent.parent
sys.path.insert(0, str(root / "tools"))


def load(name):
    spec = importlib.util.spec_from_file_location(name, root / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


pinvoke = load("csharp_pinvoke")
physics = load("generate_physics_wrapper")
voxels = load("generate_voxels_wrapper")

source = '''
using System.Runtime.InteropServices;

[global::System.Runtime.InteropServices.DllImportAttribute(
    "VRage.Physics.Native.dll",
    CallingConvention = CallingConvention.Cdecl,
    EntryPoint = "?Example@@YA_NPEAX@Z")]
[return: MarshalAs(UnmanagedType.I1)]
internal unsafe static extern bool Example(
    nint instance,
    [MarshalAs(UnmanagedType.CustomMarshaler, MarshalType = "A, B")] string value,
    HkFlags<Foo, ushort> flags);
'''

with tempfile.TemporaryDirectory() as temporary:
    assembly = Path(temporary) / "VRage.Physics"
    assembly.mkdir()
    path = assembly / "Example.cs"
    path.write_text(source, encoding="utf-8")
    declarations = pinvoke.load_decompiled_sources(
        temporary, "VRage.Physics", "VRage.Physics.Native.dll")

assert len(declarations) == 1
assert declarations[0]["entry_point"] == "?Example@@YA_NPEAX@Z"
assert declarations[0]["return_i1"]
assert [parameter["type"] for parameter in declarations[0]["params"]] == [
    "nint", "string", "HkFlags<Foo, ushort>",
]
assert physics.lower_type("bool", return_i1=True) == {"kind": "bool1"}
assert physics.lower_type("HkFlags<Foo, ushort>") == {"kind": "u2"}
assert physics.lower_type("HkSimdFloat")["size"] == 16
assert voxels.lower_type("Vector2")["size"] == 8
assert voxels.lower_type("Vector3", modifier="out") == {"kind": "ptr"}

physics_source, _ = physics.emit(physics.load_signatures([{
    "entry_point": "Example",
    "name": "Example",
    "ret": "void",
    "return_i1": False,
    "params": [{"modifier": None, "type": "HkSimdFloat", "name": "value"}],
}]))
assert "alignas(16) HkSimdFloat value_pe = value;" in physics_source
assert "pExample(&value_pe);" in physics_source
assert "Physics Init requires a DLL path" in physics_source
assert "Documents/" not in physics_source

voxels_source = voxels.emit([
    ("Example", {
        "ret": {"kind": "struct", "type": "Vector3I", "size": 12},
        "params": [{"kind": "struct", "type": "Vector3", "size": 12, "n": "value"}],
    }),
    ("UnregisterAssertionCallback", {"ret": {"kind": "void"}, "params": []}),
])
assert "alignas(16) Vector3 value_pe = value;" in voxels_source
assert "alignas(16) Vector3I result;" in voxels_source
assert "Voxels Init requires a DLL path" in voxels_source
assert "sysv_assertion_callback = nullptr;" in voxels_source
assert "Documents/" not in voxels_source

slug_source = (root / "src" / "Slug.cpp").read_text(encoding="utf-8")
assert "Slug Init requires a DLL path" in slug_source
assert 'getenv("HOME")' not in slug_source
