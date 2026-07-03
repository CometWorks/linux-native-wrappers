# Linux Native Wrappers

Linux shared libraries for **Space Engineers** (version 1) to load the native
Windows DLLs the game client and its Dedicated Server require.

These are thin C++17 shims built around a custom PE (Portable Executable)
loader. They load the original, unmodified Windows native DLLs shipped with the
game and thunk calls across the ABI boundary so the game runs on Linux.

The wrappers do **not** reimplement any of the underlying libraries.
Implementations of the Win32 API calls used by the native libraries
are provided based on Linux primitives, including optional ntsync support.

## Libraries built

| Output               | Wraps the Windows DLL |
| -------------------- |-----------------------|
| `libHavok.so`        | `Havok.dll` (physics) |
| `libRecastDetour.so` | `RecastDetour.dll` (navmesh) |
| `libVRageNative.so`  | `VRage.Native.dll` (voxels) |
| `libD3DCompiler.so`  | `d3dcompiler_47.dll` (shader compiler) |

## Building locally

Requirements: `cmake` (>= 3.10), `make`, `g++` (C++17).

```bash
make          # cmake --preset default + cmake --build --preset default
ls build/*.so # libHavok.so libRecastDetour.so libVRageNative.so libD3DCompiler.so
make clean    # wipe the build/ directory
```

## Releases

CI ([`.github/workflows/build.yml`](.github/workflows/build.yml)) builds on
every push and publishes the four libraries as a `linux-native-wrappers.tar.gz`
asset:

- **Push to `main`** → a public release tagged `v1.0.<run>` and marked *latest*.
- **PR** → a **draft** release (not public, not *latest*,
  no git tag until published).
- **Draft PRs** are not built.

The build process of Pulsar for Linux and Magnetar download the 
`linux-native-wrappers.tar.gz` asset from the latest release;
it contains the four `.so` files at the archive root.

## License

MIT — see [LICENSE](LICENSE).
