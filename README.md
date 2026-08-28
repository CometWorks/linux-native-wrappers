# Linux Native Wrappers

Linux shared libraries that let Space Engineers 1 and Space Engineers 2 load
the Windows native DLLs they use. Most wrappers are thin C++17 shims around a
custom PE (Portable Executable) loader. They load the original DLL, translate
calls between the System V and Microsoft x64 ABIs, and provide the required
Win32 calls with Linux primitives. The Win32 layer can use ntsync when the
kernel supports it.

KytheraV2 and PlatformWindows are exceptions. KytheraV2 is a compatibility
stub with no navigation implementation. PlatformWindows is a native presenter
adapter and does not load a Windows DLL.

## Libraries built

| Output | Game | Purpose |
| --- | --- | --- |
| `libHavok.so` | SE1 | Loads `Havok.dll` for physics |
| `libRecastDetour.so` | SE1 | Loads `RecastDetour.dll` for navmesh |
| `libVRageNative.so` | SE1 | Loads `VRage.Native.dll` for voxels |
| `libD3DCompiler.so` | SE1 | Loads `d3dcompiler_47.dll` for shader compilation |
| `libVRage.KytheraV2.Native.so` | SE2 | Provides the KytheraV2 compatibility stub |
| `libVRage.Platform.Windows.Native.so` | SE2 | Provides the native presenter adapter |
| `libVRage.Slug.Native.so` | SE2 | Loads `VRage.Slug.Native.dll` for text rendering |
| `libVRage.Physics.Native.so` | SE2 | Loads `VRage.Physics.Native.dll` for physics |
| `libVRage.Voxels.Native.so` | SE2 | Loads `VRage.Voxels.Native.dll` for voxels |

## Generated wrappers

The Havok, Physics, and Voxels wrappers are generated from decompiled C#
`DllImport` declarations. Their generated C++ files are committed, so building
the project does not require decompiled sources.

Each generator takes one decompiled source root. The root may contain the
assembly directly, under `src/`, or be the assembly directory itself.

```bash
python3 tools/generate_havok_wrapper.py /path/to/se1/decompiled
python3 tools/generate_physics_wrapper.py /path/to/se2/decompiled
python3 tools/generate_voxels_wrapper.py /path/to/se2/decompiled
```

These commands rewrite `src/Havok.cpp`, `src/Physics.cpp`,
`src/Physics.exports`, and `src/Voxels.cpp` as applicable. Run the generator
tests after regeneration:

```bash
ctest --test-dir build --output-on-failure
```

### Havok callbacks

Havok accepts Microsoft-ABI callback pointers, while managed delegates use the
System V ABI on Linux. The generated wrapper uses one fixed bridge for each
static semantic callback, serialized bridges for the two callbacks that live
only for a native call, and three shared bridges for phantom shapes. Phantom
callbacks are routed through a map keyed by the native shape pointer.

### Decorated SE2 exports

Some MSVC-decorated Physics and Slug exports contain `@`, which GNU ELF tools
interpret as symbol-version syntax. The source uses equal-length `$`
placeholders. Post-build scripts replace those names in the ELF dynamic string
table and rebuild its System V symbol hash.

Physics records exact placeholder-to-export mappings in
`src/Physics.exports`. Slug has 13 known exports and derives each final name by
replacing `$` with `@`, so it does not need a manifest. CMake runs both renamers
automatically and fails the build if the expected symbols are not found.

Both targets also compile with `-fno-reorder-blocks-and-partition`. At `-O2`
and above GCC otherwise splits cold paths into a separate section and appends
`.cold` to the mangled symbol name, producing identifiers the GNU assembler
cannot parse.

## Building locally

The direct CMake build requires x86-64 Linux, CMake 3.13 or newer, Python 3,
Make, and a C++17-capable `g++`. The supplied Makefile uses CMake presets, which
require CMake 3.21 or newer.

```bash
make
ls build/*.so
make clean
```

`make` configures `build/` with the `default` preset, builds every target, and
does not select a CMake build type. Use a direct CMake configuration when you
need a specific build type:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
ctest --test-dir build --output-on-failure
```

The default test configuration runs the SE1 generator checks, the SE2
generator checks, and the thread attach test.

## Packaging locally

`build_and_package.sh` clean-builds, tests and packages every wrapper in both
configurations, producing the four archives the release workflow publishes:

```bash
./build_and_package.sh
ls dist/*.tar.gz
```

It builds and packages Release first, then Debug, starting each configuration
from `make clean`. The archives land in `dist/`, which is excluded from Git,
and `build/` is left holding the **Debug** libraries, so local development gets
unoptimized binaries with full symbols and usable backtraces without another
build. The build workflow runs this same script, so a locally produced archive
matches the published one.

## PE sidecars

Loader-backed wrappers can write an ELF sidecar to the cache path supplied by
their caller. The sidecar keeps PE frames, symbols, and unwind metadata
available to Linux crash tools after the process exits.

Create a persistent sidecar directly when a saved core or another tool needs
to reopen it later:

```bash
build/generate_pe_sidecar /path/to/Havok.dll /cache/Havok.dll
```

The loader checks the sidecar version, source size, and full-file FNV-1a
fingerprint before use. It atomically replaces a stale sidecar. If the supplied
cache path cannot provide a usable sidecar, the loader falls back to loading
the raw PE image.

## Havok integration tests

Point CMake at a directory containing `Havok.dll` to add the sidecar generation
and Havok unwind tests:

```bash
cmake -S . -B build -DNATIVE_DLL_DIR=/path/to/SpaceEngineers/Bin64
cmake --build build
ctest --test-dir build --output-on-failure
build/havok_crash_test /path/to/SpaceEngineers/Bin64/Havok.dll --trace
gdb --args build/havok_crash_test /path/to/SpaceEngineers/Bin64/Havok.dll
```

The `--trace` form checks the unwind trace and exits normally. Running the same
program without `--trace` leaves the crash unhandled for GDB or core testing.
Set `SE2_NATIVE_DLL_DIR=/path/to/SpaceEngineers2/Game2` to also add the Physics
initialization test.

CI does not set `NATIVE_DLL_DIR`, so these DLL-backed tests are not part of the
release workflow.

## Releases

The [build workflow](.github/workflows/build.yml) runs for pushes to `main` and
for non-draft pull requests. It invokes
[`build_and_package.sh`](build_and_package.sh), which builds and tests the
Release and Debug configurations and packages the shared libraries at the root
of one archive per game and configuration. Each archive holds only the wrappers
its game version loads, as listed in [Libraries built](#libraries-built):

| Asset | Game | Configuration |
| --- | --- | --- |
| `se1-native-wrappers.tar.gz` | SE1 | Release (`-O3 -DNDEBUG`) |
| `se2-native-wrappers.tar.gz` | SE2 | Release (`-O3 -DNDEBUG`) |
| `se1-native-wrappers.debug.tar.gz` | SE1 | Debug (`-O0 -g`) |
| `se2-native-wrappers.debug.tar.gz` | SE2 | Debug (`-O0 -g`) |

A push to `main` publishes a public `v1.0.<run>` release and marks it as the
latest release. For a non-draft pull request, the workflow maintains a draft
release named `pr-<number>`. Draft publication requires repository write
permission, which fork pull requests do not receive from the standard token.

Pulsar for Linux and Magnetar consume `se1-native-wrappers.tar.gz` by asset
name. The Debug archives retain symbols for debugging. These per-game archives
replaced the single combined `linux-native-wrappers.tar.gz`, which releases no
longer carry.

## License

The project is MIT licensed; see [LICENSE](LICENSE). The bundled Linux ntsync
UAPI header in `src/linux/ntsync.h` carries its own
`GPL-2.0 WITH Linux-syscall-note` SPDX identifier.
