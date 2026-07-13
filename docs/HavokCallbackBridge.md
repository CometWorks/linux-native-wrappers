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
| **Shared static** | delegate held in a `static readonly` field; per-instance dispatch happens via the listener *handle* passed as the callback's first argument | one per distinct static delegate, **independent of world size** | peak 1 → a single collapsed slot; peak N > 1 → smallest power of two ≥ 16 × N |
| **Per-instance** | a fresh delegate is marshalled for every wrapper instance | one per live instance | large, sized to the concurrent instance count |
| **Per-call (synchronous)** | a fresh (often capturing) delegate is marshalled on each call, but Havok invokes it *only during the call* and never retains it | the wrapper releases the slot right after the call, so ≤ one per concurrent call | sized for concurrent in-flight calls |

A family whose peak is exactly one distinct pointer uses a **collapsed single-slot
bridge** — one thunk backed by one target, with no index map, free-list or thunk
array (a second distinct *live* target aborts). Everything else uses the O(1)
multi-slot pool.

Because the bridge de-duplicates by target pointer, a shared static delegate that is
bridged from thousands of listeners still occupies **one** slot (refcounted). A
per-instance producer accumulates slots until the instance dies; a per-call
producer would leak one slot per call *unless* its slot is released — which is safe
only when Havok does not retain the pointer past the call (see
[Per-call release](#per-call-release)).

`HkPhantomCallbackShape` is the **only** per-instance producer in the entire wrapper
(see [Phantom-shape reclaim](#phantom-shape-reclaim) below).

## Per-family table

Sizes as generated (`DEFAULT_CALLBACK_SLOTS = 64` fallback; total 33094 slots →
`libHavok.so` ~17 MB):

| Family | C signature | Slots | Dominant class | Peak distinct pointers | Margin |
| --- | --- | ---: | --- | --- | --- |
| `void_ptr_ptr` | `void(void*, void*)` | **32768** | per-instance | 2 × concurrent phantom shapes (+12 static) | bounds up to 16384 concurrent phantoms |
| `void_ptr` | `void(void*)` | 128 | shared static | **7** | ~18× |
| `void_ptr_int` | `void(void*, int32)` | 128 | static + per-call (sync) | 1 retained static + ≤1 per concurrent cleanup call | sized for loader concurrency |
| `void_charptr` | `void(char*)` | 32 | shared static | 2 | 16× |
| `float_ptr` | `float(void*)` | 32 | shared static | 2 | 16× |
| `void_charptr_int` | `void(char*, int32)` | **1** | shared static | 1 | single slot |
| `void_i64` | `void(int64)` | **1** | shared static | 1 | single slot |
| `void_void` | `void(void)` | **1** | shared static | 1 | single slot |
| `bool_ptr_ptr` | `bool(void*, void*)` | **1** | shared static | 1 | single slot |
| `int_ptr_ptr_uint_ptr` | `int32(void*, void*, uint32, void*)` | **1** | shared static | 1 | single slot |
| `void_ptr_int_ptr` | `void(void*, int32, void*)` | **1** | shared static | 1 | single slot |

"Peak distinct pointers" is the maximum a family can hold **simultaneously**, which
is what the pool must cover — not the number of `bridge_` calls. The `1`-slot
families use the collapsed single-slot bridge described above; the others (`≥ 16 ×`
peak, rounded up to a power of two) use the multi-slot pool.

## Per-family detail

Each Havok delegate below is listed as `HkClass.DelegateType → its C# holder`. Unless
noted "per-instance"/"per-call", the holder is a `static readonly` field, so every
instance shares one pointer.

### `void_ptr` — 128 slots, peak 7 (verified exhaustively)

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
longer bridged (see reclaim). 128 = 16 × 7 rounded up to a power of two (~18×).

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

### `void_ptr_int` — 128 slots, static + per-call (synchronous)

Two consumers, and they are the reason this family is *not* a simple round-robin:

- **Retained static:** `HkUniformGridShape.NativeBatchRequestCallback` →
  `BlockingRequestHandlerCallback`, installed via `HkUniformGridShape_SetShapeRequestHandler`.
  Havok **keeps** this pointer and invokes it later, on demand, for lazy voxel-batch
  loading — for the lifetime of the shape. It is a shared static delegate, so it
  occupies **one** deduplicated slot, but that slot must never be recycled while the
  shape is alive.
- **Per-call synchronous:** `HkShapeLoader.ReturnByteArray` — `HkShapeLoader.CleanupShapesBuffer`
  passes a fresh **capturing** lambda (it writes to a local `result`). Havok calls it
  once during `CleanupShapesBuffer` to hand back the cleaned buffer, then discards it.
  The wrapper therefore **releases the slot immediately after the call**, so it does
  not leak (see [Per-call release](#per-call-release)).

Because the retained handler and the per-call callback share this pool, blind
round-robin reuse would eventually overwrite the retained slot and crash. Releasing
only the synchronous callback (by target pointer) leaves the retained one untouched.
Peak is 1 retained + one in-flight cleanup per concurrent caller; 128 is sized for
loader-thread concurrency (not a fixed static count), so it keeps a multi-slot pool.

### Two-pointer families — 32 slots each

Peak of two distinct static pointers → 16 × 2 = 32 (already a power of two):

- `void_charptr` (2): `HkBaseSystem.Log` (set once at init) + `HkTaskProfiler.BlockBeginFuncCpp`
  → `m_blockBeginFuncCpp`.
- `float_ptr` (2): `HkWheelResponseModifierUtil.CalculateModifier` → `Softness`,
  `Acceleration`, shared across every wheel.

### Single-pointer families — 1 slot each (collapsed bridge)

Each of these has exactly one distinct static callback pointer, so it uses the
collapsed single-slot bridge (one thunk, one target, no collection):

- `bool_ptr_ptr`: `HkBreakOffPartsUtil.BreakPartsHandlerDelegate` → `BreakPartsHandlerCallback`.
- `int_ptr_ptr_uint_ptr`: `HkBreakOffPartsUtil.BreakLogicHandlerDelegate` → `BreakLogicHandlerCallback`.
- `void_charptr_int`: `HkTaskProfiler.TaskStartedFuncCpp` → `m_taskStartedFuncCpp`.
- `void_i64`: `HkTaskProfiler.BlockEndFunc` → `m_blockEndFuncCpp`.
- `void_void`: `HkTaskProfiler.TaskFinishedFunc` → `m_taskFinishedFuncCpp`.
- `void_ptr_int_ptr`: `HkConstraint.ReadConstraintsCallback` → the `static` method
  `ConstraintReader` (its state is threaded through `userData`/`GCHandle`, not
  captured; the C# compiler caches the method-group→delegate conversion in a hidden
  static field, so it is one stable pointer — not per-call, and it does not leak).

The task-profiler families are also effectively dormant in production (deep
profiling is off).

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
  producer — it is sized only for its 7 static pointers (128).

## Per-call release

A callback that Havok invokes *only synchronously during the call* and never retains
can have its bridge slot freed the instant the call returns — the target is dead by
then. The generator does this for functions listed in `SYNC_RELEASE_ARGS`: it emits
`bridge_<family>(arg)` for the call, then `release_<family>(arg)` immediately after.
Currently that is just `HkShapeLoader_CleanupShapesBuffer` (its `returnByteArray`
callback). This keeps `void_ptr_int` at the default instead of leaking one slot per
shape-buffer cleanup.

**Why release-after-call rather than round-robin.** A round-robin buffer (recycle
slot `i mod N`, trusting the delegate from `N` calls ago is dead) only works if
*every* delegate in the family is short-lived. `void_ptr_int` violates that: it also
carries the **retained** voxel batch handler (above), whose slot Havok may call into
at any time. Round-robin would eventually recycle that live slot and crash. Releasing
by target pointer, only for the callback we have verified is synchronous, is both
safer (it never touches the retained slot) and tighter (the slot is freed at the
exact moment it dies, not `N` calls later). Only add an argument to `SYNC_RELEASE_ARGS`
after confirming in the C# wrapper that Havok does not store it past the call.

## Changing a limit

Edit the family's entry in `CALLBACK_SLOTS` (or `DEFAULT_CALLBACK_SLOTS`) in
`tools/generate_havok_wrapper.py`, then regenerate and rebuild:

```bash
python3 tools/generate_havok_wrapper.py
make
```

Cost is linear in the slot count of the family you change (one templated thunk +
one function pointer per slot). For `void_ptr_ptr` (current row measured, others
extrapolated ~linearly):

| `void_ptr_ptr` slots | `libHavok.so` | clean build (`-O0`) |
| ---: | ---: | ---: |
| 16384 | ~10 MB | ~18 s |
| 32768 (current) | ~17 MB | ~34 s |
| 65536 | ~32 MB | ~47 s |

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
