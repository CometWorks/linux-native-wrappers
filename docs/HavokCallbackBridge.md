# Havok callback bridge — family mappings, sizing, and margins

This document explains how the Havok callback bridge maps the game's callbacks onto
signature *families*, how each family's slot pool (`CALLBACK_SLOTS`) is sized, and
why those sizes are safe. It is the reference behind the numbers in
[`tools/generate_havok_wrapper.py`](../tools/generate_havok_wrapper.py) and the
generated `src/HavokThunk_*.cpp`.

## Background

`Havok.dll` takes raw C function pointers for its callbacks (contact/entity
listeners, phantom handlers, …) and passes **no user-data / context pointer** when
it later invokes them. Two consequences drive the whole design:

1. **We need a distinct code address per live callback.** Because the game (Mono,
   SysV ABI) and `Havok.dll` (Microsoft ABI) disagree on calling convention, each
   managed callback is handed to Havok as a small ABI-translating *thunk*. Since
   there is no context argument, the thunk must know *which* managed target to call
   purely from its own address — so we pre-instantiate a pool of thunks
   `<family>_thunk<Index>`, each of which reads its target from `<family>_targets[Index]`.
   These addresses are compile-time template instantiations, so **the pool size is
   fixed per build and cannot grow at run time** (that would need JIT trampolines).

2. **Callbacks with the same C signature share a pool ("family").** All callbacks
   that lower to, say, `void(void*, void*)` draw thunks from the `void_ptr_ptr`
   pool. There are 11 families, one `src/HavokThunk_<family>.cpp` each.

Registration is `bridge_<family>(target)` → returns a thunk address; teardown is
`release_<family>(target)`. Both are **O(1)** (an `unordered_map` target→slot for
dedup + refcount, plus a free-list stack for allocation, under a per-family mutex).
The callback-invocation hot path — what runs every physics tick — is a single
lock-free atomic load of `targets[Index]`, independent of pool size.

If a pool is exhausted, `bridge_` prints a diagnostic naming the family and the
knob to raise, then `std::abort()`s. It never silently overflows.

## What determines a family's size

The number of **distinct** function pointers a family can have live at once depends
entirely on how the game's Havok wrapper (`../dotnet-game-local/HavokWrapper/Havok`)
marshals the delegate. There are three classes:

| Class | How the wrapper marshals it | Distinct pointers | Sizing |
| --- | --- | --- | --- |
| **Shared static** | delegate held in a `static readonly` field; per-instance dispatch happens via the listener *handle* passed as the callback's first argument | one per distinct static delegate, **independent of world size** | default (256) |
| **Per-instance** | a fresh delegate is marshalled for every wrapper instance | one per live instance | large, sized to the concurrent instance count |
| **Per-call** | a fresh (often capturing) delegate is marshalled on each call and never released | grows with call count (a slow leak) | a runway margin above the default |

Because the bridge de-duplicates by target pointer, a shared static delegate that is
bridged from thousands of listeners still occupies **one** slot (refcounted). Only
per-instance and per-call producers actually accumulate slots.

`HkPhantomCallbackShape` is the **only** per-instance producer in the entire wrapper
(see [Phantom-shape reclaim](#phantom-shape-reclaim) below).

## Per-family table

Sizes as generated (`DEFAULT_CALLBACK_SLOTS = 256`; total 43008 slots →
`libHavok.so` ~22 MB):

| Family | C signature | Slots | Dominant class | Peak distinct pointers | Margin |
| --- | --- | ---: | --- | --- | --- |
| `void_ptr_ptr` | `void(void*, void*)` | **32768** | per-instance | 2 × concurrent phantom shapes (+12 static) | bounds ~16 384 concurrent phantoms |
| `void_ptr_int` | `void(void*, int32)` | 4096 | per-call | 1 static + N leaked per shape-buffer cleanup | runway |
| `void_ptr_int_ptr` | `void(void*, int32, void*)` | 4096 | per-call | N leaked per `FindConnectedConstraints` | runway |
| `void_ptr` | `void(void*)` | 256 | shared static | **7** | ~36× |
| `void_charptr` | `void(char*)` | 256 | shared static | ~2 | ~128× |
| `void_charptr_int` | `void(char*, int32)` | 256 | shared static | 1 | ~256× |
| `void_i64` | `void(int64)` | 256 | shared static | 1 | ~256× |
| `void_void` | `void(void)` | 256 | shared static | 1 | ~256× |
| `bool_ptr_ptr` | `bool(void*, void*)` | 256 | shared static | 1 | ~256× |
| `int_ptr_ptr_uint_ptr` | `int32(void*, void*, uint32, void*)` | 256 | shared static | 1 | ~256× |
| `float_ptr` | `float(void*)` | 256 | shared static | 2 | ~128× |

"Peak distinct pointers" is the maximum a family can hold **simultaneously**, which
is what the pool must cover — not the number of `bridge_` calls.

## Per-family detail

Each Havok delegate below is listed as `HkClass.DelegateType → its C# holder`. Unless
noted "per-instance"/"per-call", the holder is a `static readonly` field, so every
instance shares one pointer.

### `void_ptr` — 256 slots, peak 7 (verified exhaustively)

Every `void_ptr`-family delegate reaches the bridge through a `bridge_void_ptr(...)`
call in `src/Havok.cpp`; there are exactly **four** such call sites, all passing
static delegates:

- `HkActivationListener_Create(onActivate, onDeactivate)` → `OnActivatedCpp`,
  `OnDeactivatedCpp` — **2**
- `HkConstraintListener_SetCallbacks(onAdded, onRemoved, onBreaking)` →
  `OnAddedHolder`, `OnRemovedHolder`, `OnBreakingHolder` — **3**
- `HkJobThreadPool_RunOnEachWorker(action)` → `m_action`. (The public overload takes
  a per-call lambda, but stores it in a dictionary keyed by an int token and passes
  only the static `m_action` to native.) — **1**
- `HkUniformGridShape_SetDeleteHandler(handler)` → `DeleteHandlerCallback` — **1**

Total: **7 distinct pointers for the entire process lifetime, independent of world
size.** The phantom-shape delete handler (`HkDeleteHandler`) is *not* here — it is no
longer bridged (see reclaim). 256 gives ~36× headroom.

### `void_ptr_ptr` — 32768 slots, per-instance

- Static (12 total): `HkContactListener` onContact / collisionAdded / collisionRemoved
  (3); `HkContactSoundListener` onContact (1); `HkEntityListener` onAdd / onRemove /
  onDelete / onShapeChange / onMotionTypeChange (5); `HkWorld.BroadPhaseExitCallback`
  → `MaxPositionExceededDelegate` (1); `HkpAabbPhantom` collidableAdded / collidableRemoved (2).
- **Per-instance:** `HkPhantomCallbackShape.HkPhantomHandlerCpp` — a distinct enter
  and leave delegate **per phantom shape**. The game creates one phantom shape per
  trigger/detector volume: ship connectors + ejectors (2 each), collectors, gravity
  generators, merge blocks, safe zones, and opt-in detector entities.

So peak ≈ `12 + 2 × (concurrent live phantom shapes)`. 32768 covers ~16 384
concurrent phantoms. Because the slots are **reclaimed on shape destruction**, this
is a *concurrency* ceiling, not a cumulative-total one. Loading *"Many Lifters
Slowness"* (699 grids) exhausted the earlier flat pool of 4096 and aborted; it peaks
around a few thousand.

### `void_ptr_int` — 4096 slots, per-call

- `HkUniformGridShape.NativeBatchRequestCallback` → `BlockingRequestHandlerCallback`
  (static, 1).
- **Per-call:** `HkShapeLoader.ReturnByteArray` — `HkShapeLoader.CleanupShapesBuffer`
  passes a fresh **capturing** lambda (it writes to a local `result`) on each call.
  It is used synchronously and never released, so each call permanently consumes a
  slot. `CleanupShapesBuffer` runs during shape/model loading, so this is a slow
  drip bounded by distinct model-load operations, not by frame count. 4096 is a
  runway (the value the family ran at historically without incident), not a margin
  over a fixed number.

### `void_ptr_int_ptr` — 4096 slots, per-call

- `HkConstraint.ReadConstraintsCallback` — the `reader` passed to
  `HkConstraint_FindConnectedConstraints` is caller-supplied and not released. Occasional
  (constraint-graph queries), so 4096 is a comfortable runway.

### Remaining shared-static families — 256 slots each

- `bool_ptr_ptr`: `HkBreakOffPartsUtil.BreakPartsHandlerDelegate` → `BreakPartsHandlerCallback` (1).
- `int_ptr_ptr_uint_ptr`: `HkBreakOffPartsUtil.BreakLogicHandlerDelegate` → `BreakLogicHandlerCallback` (1).
- `void_charptr`: `HkBaseSystem.Log` (set once at init) + `HkTaskProfiler.BlockBeginFuncCpp` → `m_blockBeginFuncCpp` (~2).
- `void_charptr_int`: `HkTaskProfiler.TaskStartedFuncCpp` → `m_taskStartedFuncCpp` (1).
- `void_i64`: `HkTaskProfiler.BlockEndFunc` → `m_blockEndFuncCpp` (1).
- `void_void`: `HkTaskProfiler.TaskFinishedFunc` → `m_taskFinishedFuncCpp` (1).
- `float_ptr`: `HkWheelResponseModifierUtil.CalculateModifier` → `Softness`, `Acceleration` (2), shared across every wheel.

The task-profiler families are also effectively dormant in production (deep
profiling is off). 256 is a large safety factor over a handful of fixed pointers.

## Phantom-shape reclaim

`HkPhantomCallbackShape` marshals a distinct enter/leave/delete delegate per instance,
and Havok signals a shape's destruction only by invoking its **delete callback**. So
rather than bridging a per-instance delete, every phantom is handed one **shared**
native dispatcher (`phantom_delete_dispatch` in `Havok.cpp`). When Havok destroys a
shape it calls that dispatcher with the shape handle; the dispatcher forwards to the
managed delete handler and then `release_void_ptr_ptr`s the shape's enter/leave slots
for reuse.

Two consequences:

- `void_ptr_ptr`'s 32768 slots bound the number of **concurrent** live phantoms, not
  the cumulative number ever created. Without the reclaim, a session that streams
  grids or repeatedly builds/destroys phantom blocks would leak these slots and
  eventually abort with only a few phantoms alive at once.
- The delete callback is no longer bridged, so `void_ptr` has **no** per-instance
  producer — that is why it sits at the default 256.

## Changing a limit

Edit the family's entry in `CALLBACK_SLOTS` (or `DEFAULT_CALLBACK_SLOTS`) in
`tools/generate_havok_wrapper.py`, then regenerate and rebuild:

```bash
python3 tools/generate_havok_wrapper.py
make
```

Cost is linear in the slot count of the family you change (one templated thunk +
one function pointer per slot). Measured for `void_ptr_ptr`:

| `void_ptr_ptr` slots | `libHavok.so` | clean build (`-O0`) |
| ---: | ---: | ---: |
| 16384 | ~15 MB | ~21 s |
| 32768 (current) | ~22 MB | ~34 s |
| 65536 | ~37 MB | ~47 s |

**Runtime is unaffected by pool size** — register/lookup are O(1) and the invocation
hot path is a single atomic load; a larger pool only enlarges the (mostly-zero)
`targets[]`/`thunks[]` arrays in memory. The cost is purely binary size (it ships in
the release tarball) and compile time.

## Verification and caveat

The mappings and peak counts above were derived by:

1. Enumerating every `bridge_<family>(` call site in the generated `src/Havok.cpp`
   (the generator routes *all* delegate arguments through the bridge, so this is
   exhaustive).
2. Tracing each argument to its holder in the decompiled C# wrapper and classifying
   it as shared-static, per-instance, or per-call.

This reflects the wrapper as of the committed generation. If a future game version
adds a new **per-instance** or **per-call** callback in one of the currently
static-only families, that family's real peak would change — but the exhaustion
diagnostic names the exact family and knob, so it surfaces loudly rather than
silently. Re-run the two steps above after any wrapper update that touches callbacks.
