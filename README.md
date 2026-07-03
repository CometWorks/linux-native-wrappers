# linux-native-wrappers

Linux native wrapper shared libraries for **Space Engineers (version 1)**.

These are thin C++17 shims built around a custom PE (Portable Executable)
loader. They load the original, unmodified Windows native DLLs shipped with the
game and thunk calls across the ABI boundary so the game runs on Linux — the
wrappers do **not** reimplement any of the underlying libraries.

Four libraries are built:

| Output                 | Wraps the Windows DLL |
| ---------------------- | --------------------- |
| `libHavok.so`          | `Havok.dll` (physics) |
| `libRecastDetour.so`   | `RecastDetour.dll` (navmesh) |
| `libVRageNative.so`    | `VRage.Native.dll` |
| `libD3DCompiler.so`    | `d3dcompiler_47.dll` |

The source originates from the
[CometWorks/linux-compat](https://github.com/CometWorks/linux-compat)
`NativeWrappers/` folder and is factored out here so the build can be shared by
both **Pulsar for Linux** and **Magnetar** (Magnetar bundles only the first
three; Pulsar for Linux uses all four).

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
- **Non-draft pull request** → a **draft** release (not public, not *latest*,
  no git tag until published). Draft PRs are not built.

Consumers should download the `linux-native-wrappers.tar.gz` asset from the
latest release; it contains the four `.so` files at the archive root.

## License

MIT — see [LICENSE](LICENSE).
