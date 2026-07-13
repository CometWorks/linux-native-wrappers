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

## Havok callback bridge thunks

`Havok.dll` hands the engine raw C function pointers for its callbacks (contact
listeners, entity listeners, phantom handlers, …) and passes **no user-data /
context pointer** when it later invokes them. To route each callback back to the
correct managed target we therefore need a *distinct code address per live
callback*. `src/HavokThunk_*.cpp` provide these: for every callback signature
family there is a pool of `CALLBACK_SLOTS` template-instantiated thunks
(`<family>_thunk<Index>`), each of which reads its target from a per-slot table.

Those thunk addresses are baked into the binary at compile time, so **the pool
size is fixed per build and cannot be grown at run time** (doing so would require
emitting machine code / JIT trampolines, which is architecture-specific and out
of scope for a generated wrapper).

`src/Havok.cpp` (one `extern "C"` shim per Havok.dll export) and the thunk sources
are regenerated together by
[`tools/generate_havok_wrapper.py`](tools/generate_havok_wrapper.py):

```bash
python3 tools/generate_havok_wrapper.py   # rewrites src/Havok.cpp, src/HavokThunk_*.cpp, HavokThunkRegistry.h
```

Generating `Havok.cpp` parses the game's decompiled `[DllImport]` declarations from
`../dotnet-game-local/HavokWrapper/Havok`; that dependency is only needed to
*regenerate*, not to build, since the generated sources are committed.

### `CALLBACK_SLOTS` limitation and per-family sizing

Each pool holds a fixed number of slots (one templated thunk each), baked in at
compile time, so binary size and compile time grow roughly linearly with the
total slot count. Rather than one flat limit for every family, `CALLBACK_SLOTS`
in the generator sizes each family to its worst case. The size a family needs
depends entirely on how the game's Havok wrapper marshals that callback:

- **Shared `static` delegates** — entity/contact/sound listeners, constraint and
  activation listeners, wheel modifiers, break-off handlers, the task profiler
  and the log sink all hold their native delegate in a `static readonly` field
  and dispatch per-instance via the listener handle passed as the first argument.
  These marshal to a *single* pointer no matter how many grids, blocks or
  constraints exist, so they need only a handful of slots
  (`DEFAULT_CALLBACK_SLOTS = 256`).
- **Per-instance delegates** — `HkPhantomCallbackShape` is the only wrapper that
  marshals a *fresh* native delegate per instance: every live phantom shape burns
  2 `void_ptr_ptr` slots (enter/leave). The game creates one phantom shape per
  trigger/detector volume — ship connectors and ejectors, collectors, gravity
  generators, merge blocks, safe zones, and opt-in detector entities — so
  **`void_ptr_ptr` scales with block count** and gets **16384** slots. Loading
  *"Many Lifters Slowness"* (699 grids) exhausted the previous flat limit of 4096
  and aborted. These slots are **reclaimed when the shape is destroyed** (see
  below), so 16384 bounds the *concurrent* number of live phantoms, not the
  cumulative total ever created.
- **Fresh-per-call delegates** — `HkShapeLoader` buffer cleanup and
  `HkConstraint.FindConnectedConstraints` marshal a new, never-released callback
  on each call, so `void_ptr_int` and `void_ptr_int_ptr` get a **4096** safety
  margin above the default.

This keeps `libHavok.so` at ~14 MB (versus ~87 MB if every family used 16384).
If a pool is ever exhausted anyway, the bridge prints a diagnostic naming the
offending family and pointing back at the generator, then aborts; raise that
family's entry in `CALLBACK_SLOTS`, regenerate, and rebuild. Run-time growth is
not possible — the thunk addresses are compile-time template instantiations.

### Phantom-shape slot reclaim

Havok's callback bridge frees a slot only when its owner tells it to. For the
`static`-delegate listeners that happens through the owner-release path
(`register_callback_owner` / `release_callback_owner`), but those pointers are
shared and pinned for the process lifetime anyway. The pointers that actually
accumulate are `HkPhantomCallbackShape`'s per-instance enter/leave delegates —
and Havok only signals that a shape is gone by invoking its **delete callback**.

So instead of bridging a per-instance delete callback, every phantom shape is
handed one shared native dispatcher (`phantom_delete_dispatch`). When Havok
destroys a shape it calls that dispatcher with the shape handle, which forwards to
the managed delete handler and then releases the shape's enter/leave bridge slots
for reuse. Without this, a long session that repeatedly builds and destroys
phantom blocks (or streams grids in and out) would leak `void_ptr_ptr` slots and
eventually exhaust the pool even with few phantoms alive at once.

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
