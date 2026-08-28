# Linux Native Wrappers

Linux shared libraries that let Space Engineers 1 and Space Engineers 2 load
their Windows native DLLs. Most wrappers are C++17 shims around a custom PE
(Portable Executable) loader. They load the original DLL, translate calls
between the System V and Microsoft x64 ABIs, and implement the Win32 and CRT
functions those DLLs need. Events and semaphores use ntsync when `/dev/ntsync`
is available, with Linux synchronization primitives as the fallback.

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

CMake also builds the `generate_pe_sidecar` and `d3dcompiler_tool` command-line
tools. Release archives contain only the shared libraries in the table.

## Runtime setup and limits

Loader-backed wrappers expose `Init(dllPath, sidecarPath)`. The first argument
is the path to the matching Windows DLL. The second is an optional cache path
for its ELF sidecar; the parent directory must be writable when the sidecar
needs to be created or replaced. Call `Init` before using the wrapper's other
exports. Physics, Slug, and Voxels enforce this explicitly. Wrappers do not
search for game installations or DLLs.

This loader implements the Windows APIs used by these DLLs. It is not a general
PE or Win32 runtime. It supports x86-64 PE32+ images and selected imports.
Unsupported imports use trap stubs, MSVC exception boundaries abort the
process, and loaded PE images cannot be unloaded safely. These failures are
intentional: continuing would leave the process in an unknown state.

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
smoke tests after regeneration:

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
and a C++17-capable `g++`. The supplied Makefile uses a CMake preset and
therefore requires CMake 3.21 or newer. The packaging script also uses Bash,
GNU Make, GNU tar, and GNU coreutils.

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

CTest is enabled by default. Pass `-DBUILD_TESTING=OFF` when only the libraries
and command-line tools are needed.

## Tests

A normal build has four self-contained tests:

| Test | Coverage |
| --- | --- |
| `se1_generators` | SE1 generator callback patterns and committed source markers |
| `se2_generators` | SE2 generators with synthetic C# declarations |
| `thread_id_dll_attach` | PE thread attach, detach, TLS, and FLS lifetime |
| `windows_memory` | Windows memory and runtime behavior |

Run them with:

```bash
ctest --test-dir build --output-on-failure
```

The generator tests are smoke tests. They do not compare a full regeneration
from the game assemblies with every committed wrapper.

## Packaging locally

`build_and_package.sh` builds the project, runs CTest, and packages the wrappers
in both configurations. It produces the four archives used by the release
workflow:

```bash
./build_and_package.sh
ls dist/*.tar.gz
```

It builds and packages Release first, then Debug, starting each configuration
from `make clean`. The archives land in `dist/`, which is excluded from Git,
and `build/` is left holding the **Debug** libraries, so local development gets
unoptimized binaries with full symbols and usable backtraces without another
build. The build workflow runs the same script, so local archives have the same
names and layout as published archives. Binary contents still depend on the
local compiler and system libraries.

## PE sidecars

Loader-backed wrappers can write an ELF sidecar to the cache path supplied by
their caller. The sidecar keeps exported names, synthetic names for runtime
functions, and supported unwind metadata available to Linux crash tools after
the process exits. It does not contain PDB or private symbols.

Create a persistent sidecar directly when a saved core or another tool needs
to reopen it later:

```bash
build/generate_pe_sidecar /path/to/Havok.dll /path/to/cache/Havok.dll
```

The loader checks the sidecar version, source size, and full-file FNV-1a
fingerprint before using its mapped image. It atomically replaces a stale
sidecar. If the cache path cannot provide a usable sidecar, the loader falls
back to the raw PE image. Sidecar generation supports x86-64 PE32+ images and a
subset of Windows x64 unwind records.

## DLL-backed tests

The optional tests below use proprietary game DLLs. CI does not provide those
DLLs, so it skips these tests. Set one or both directory options while
configuring the build:

```bash
cmake -S . -B build \
  -DBIN64=/path/to/SpaceEngineers/Bin64 \
  -DGAME2=/path/to/SpaceEngineers2/Game2
cmake --build build
ctest --test-dir build --output-on-failure
```

| Option and required file | Added tests |
| --- | --- |
| `BIN64/d3dcompiler_47.dll` | `d3dcompiler_preprocess` |
| `BIN64/RecastDetour.dll` | `recast_detour` |
| `BIN64/Havok.dll` | `pe_sidecar_generation`, `havok_unwind`, `havok_memory` |
| `GAME2/VRage.Physics.Native.dll` | `physics_init` |

The Havok crash harness can also be run directly:

```bash
build/havok_crash_test /path/to/SpaceEngineers/Bin64/Havok.dll --trace
gdb --args build/havok_crash_test /path/to/SpaceEngineers/Bin64/Havok.dll
```

The `--trace` form checks the unwind trace and exits normally. Without
`--trace`, the harness leaves the crash unhandled for GDB or core testing.

## Releases

The [build workflow](.github/workflows/build.yml) runs for pushes to `main`. It
also runs when a non-draft pull request is opened, updated, reopened, or marked
ready for review. The workflow invokes
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
release with the tag `pr-<number>`. Draft publication requires repository write
permission, which fork pull requests do not receive from the standard token.

Pulsar for Linux and Magnetar consume `se1-native-wrappers.tar.gz` by asset
name. The Debug archives retain symbols for debugging. These per-game archives
replaced the single combined `linux-native-wrappers.tar.gz`, which releases no
longer carry.

## License

The project is MIT licensed; see [LICENSE](LICENSE). The bundled Linux ntsync
UAPI header in `src/linux/ntsync.h` carries its own
`GPL-2.0 WITH Linux-syscall-note` SPDX identifier.
