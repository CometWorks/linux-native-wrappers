# Code Review — `memory` Branch

Review of the four commits on the `memory` branch (Research, Pass 1, More
research, Pass 2) against `main`, followed by the fixes applied for every
confirmed finding. The review ran eight independent finder passes (line-by-line
diff scan, removed-behavior audit, cross-file tracing, reuse, simplification,
efficiency, altitude, conventions) producing 36 raw candidates, which were
deduplicated and adversarially verified. Ten findings survived; all ten were
fixed. The notable refuted candidates are recorded at the end because they
document deliberate design decisions worth not re-litigating.

Status: all fixes applied, full ctest suite passes (4/4).

---

## Findings and Fixes

### 1. Deadlock: onexit condition wait on a recursive mutex

**Where:** `src/winlibs.cpp` — `g_onexit_mutex`, `crt__execute_onexit_table`,
`msvcr__lock`/`msvcr__unlock`.

**Problem.** `msvcr__lock` ignored its lock number and locked the global
`std::recursive_mutex g_onexit_mutex` — the same mutex the onexit machinery
uses with `g_onexit_condition.wait()`. A condition variable's `wait()` releases
exactly **one** ownership level of the lock it is given. So a thread that
entered `crt__execute_onexit_table` while already holding the mutex through
`_lock(_EXIT_LOCK1)` (depth 2) would wait while still owning the mutex at depth
1. The thread currently draining the table could then never re-acquire the
mutex for its per-callback `lock_guard`, and neither thread could ever make
progress — a mutual deadlock that also hangs every other thread touching
`_lock`/`_onexit`/`__dllonexit`. MSVCR-era CRTs hold `_EXIT_LOCK1` around exit
processing, so two threads driving `_cexit` at shutdown is a realistic
interleaving.

**Fix.**
- `g_onexit_mutex` is now a plain (non-recursive) `std::mutex`, with a comment
  stating why it must stay non-recursive. None of the remaining onexit paths
  recurse on it: callbacks always run outside the lock.
- `msvcr__lock`/`msvcr__unlock` moved onto their own array of 64
  `std::recursive_mutex` indexed by `locknum % 64`. MSVCR lock numbers name
  independent CRT subsystems (heap, stdio, exit, locale, …) and behave like
  recursive critical sections; giving them separate mutexes both removes the
  deadlock and stops unrelated subsystems from serializing on one lock.

**Not changed:** the `g_executing_onexit_tables` set + condition-variable
protocol itself. Verification showed it is load-bearing, not redundant: it
guarantees a concurrent `_execute_onexit_table` caller does not return until
the table is fully drained (including the last in-flight callback), and the
thread-local `g_executing_onexit_table` check is what makes same-thread
reentrancy terminate.

### 2. Hot-path cost: global loader lock on every wrapper call

**Where:** `src/pe_loader.cpp` — `pe_ensure_tls_for_loaded_images`.

**Problem.** The branch unified per-thread attachment through
`pe_notify_loaded_images(DLL_THREAD_ATTACH)`. That made every exported wrapper
call (`EnsureThreadInfo()` prefixes all Havok/Physics/Voxels/RecastDetour/
VRageNative entry points — thousands of calls per frame across threads)
acquire the global recursive `g_loader_mutex`, then `g_loaded_images_mutex`,
and heap-allocate a `std::vector` snapshot — before discovering the thread was
already attached. `g_loader_mutex` is also held across DLL loads, `DllMain`
execution, and thread-detach notification, so one slow `DllMain` stalled every
wrapper call in the process, and all physics worker threads serialized on one
lock in steady state.

**Fix.** Added an attach-generation fast path:
- `g_image_list_generation` (`std::atomic<uint64_t>`, starts at 1) is bumped
  whenever an image becomes thread-visible (`pe_finish_process_attach`, and
  `pe_register_loaded_image_for_test`).
- A `thread_local uint64_t t_thread_attach_generation` records the generation
  the thread last attached under. `pe_ensure_tls_for_loaded_images` now
  early-outs with a single acquire load when the generations match; only the
  first call per thread (or the first call after a new image loads) takes the
  slow notify path. The counter is reset in `pe_cleanup_current_thread`.

The generation read happens before the notify and is stored after it, so an
image loaded concurrently with the slow path just causes one extra (correct)
re-notify on the next call.

### 3. Thread-shutdown ordering: FLS drained before DLL_THREAD_DETACH

**Where:** `src/pe_loader.cpp` — `pe_cleanup_current_thread`.

**Problem.** On thread exit the code ran FLS callbacks first, then delivered
`DLL_THREAD_DETACH`, then ran a second FLS pass. Windows
(`LdrShutdownThread`) does the opposite: `DllMain`/TLS `DLL_THREAD_DETACH`
notifications run first, and FLS data is processed afterwards
(`RtlProcessFlsData`). A CRT that keeps its per-thread data in an FLS slot
whose callback frees it (VRage.Physics.Native imports
`FlsAlloc`/`FlsSetValue`/`FlsFree`) would find that data already freed — NULL
or dangling — inside its detach handler, where on Windows it is still alive.

**Fix.** Reordered to match Windows: `pe_notify_loaded_images(DLL_THREAD_DETACH)`
first, then a single `winlibs_cleanup_fls_for_current_thread()`. The drain's
existing multi-pass loop still catches values installed *during* detach, so the
previous "pre-drain" pass is unnecessary.

**Test impact.** `tests/thread_id_dll_attach.cpp` had codified the old
ordering: each thread's FLS value was drained twice (once before detach, once
after the detach handler re-set it), so it expected 68/69 callbacks. Under the
corrected order the detach handler's `FlsSetValue` merely overwrites the
existing value (no callback, as on Windows) and the drain fires once per
thread. Expectations updated to 34/35 — the Windows-accurate counts.

### 4. `_crt_atexit` handlers never ran at normal host exit

**Where:** `src/winlibs.cpp` — `crt__crt_atexit`, `crt__register_onexit_function`.

**Problem.** On `main`, `crt__crt_atexit` forwarded to host `std::atexit`, so
PE-registered handlers ran when the host process exited. The branch rerouted
registration into `g_process_onexit`, which is drained only by `crt__cexit` —
and nothing in the process calls `_cexit` on normal host shutdown (the managed
SE host terminates through normal runtime shutdown without touching the PE's
`_cexit` import). Handlers registered for flush-at-exit work were silently
dropped: exact-once semantics became exactly-zero.

**Fix.** `crt__register_onexit_function` now installs a one-time host
`std::atexit` hook (guarded by `std::once_flag`) the first time anything
registers into `g_process_onexit`. The hook drains the process table via the
same `crt__execute_onexit_table` path. Draining twice is safe — execution
empties the table — so a PE that *does* call `_cexit` first is unaffected.
This preserves the branch's LIFO/exact-once table semantics while restoring
the old guarantee that handlers run at host exit.

### 5. `VirtualFree(MEM_DECOMMIT)` aborted the process on a recoverable failure

**Where:** `src/winlibs.cpp` — `VirtualFree`.

**Problem.** Decommit was implemented as an `mmap(MAP_FIXED, PROT_NONE)`
replacement, and any unexpected result called `std::abort()`. But interior
decommits split one VMA into up to three; a long-running server repeatedly
decommitting arena pages can exhaust `vm.max_map_count` (default 65530), at
which point the mmap fails with `ENOMEM`. `MAP_FIXED` replacement is atomic:
on failure the old mapping is left intact, so nothing has been lost — exactly
the case where Windows returns `FALSE` and the caller degrades gracefully.
Aborting turned a recoverable allocator failure into process death (while
holding `g_virtual_memory_mutex`).

**Fix.** On `MAP_FAILED`, set `ERROR_NOT_ENOUGH_MEMORY` and return `FALSE`,
with a comment documenting the atomicity reasoning. Success with `MAP_FIXED`
always returns the requested address, so no other outcome exists.

### 6. `VirtualAlloc` rejected the valid `MEM_TOP_DOWN` modifier

**Where:** `src/winlibs.cpp` — `VirtualAlloc`.

**Problem.** The flag validation required `flAllocationType` to equal exactly
`MEM_RESERVE`, `MEM_COMMIT`, or their OR. Windows accepts modifier bits
combined with reserve/commit; `MEM_TOP_DOWN` in particular is a pure placement
hint that native arena allocators commonly pass. Such a call got `nullptr` +
`ERROR_INVALID_PARAMETER` — an error Windows never returns for it — sending
the caller down its OOM path during init or world load.

**Fix.** `MEM_TOP_DOWN` (0x100000) is masked off at the top of `VirtualAlloc`
before validation, with a comment that any address satisfies the hint.
Genuinely unsupported/unknown bits (e.g. `MEM_WRITE_WATCH`, which has real
semantics the shim cannot provide) are still rejected — that rejection is loud
and correct for this shim's scope.

**Test impact.** `tests/windows_memory.cpp` case 40 asserted the old
rejection. It now asserts that `MEM_RESERVE | MEM_TOP_DOWN` succeeds (and the
region releases cleanly) while an invalid combination
(`MEM_RESERVE | MEM_DECOMMIT`) and an invalid protection still fail.

### 7. `ALLOC_PADDING` removed ahead of its validation gate

**Where:** `src/winlibs.cpp` — CRT allocation paths.

**Problem.** The 256-byte trailing pad existed because MSVC-compiled PE code
may write slightly past the requested size (within MSVC's own allocator
rounding/metadata), which corrupts glibc heap metadata. The branch's own plan
(MEMORY.md section 5) directed: land the contract fixes *while retaining the
padding*, and remove it only in a later commit once DLL lifecycle and managed
integration validation passes. Pass 1 removed all nine uses in the same commit
as the contract fixes, while MEMORY.md's status still records SE2 runtime
validation as outstanding. Besides the corruption risk (the only coverage is
the single-pattern havok_memory box-shape probe), removing both in one commit
makes any new heap-corruption crash unbisectable between the two causes.

**Fix.** Restored the padding per the plan's own sequencing:
`ALLOC_PADDING = 256` plus an overflow-checked `padded_alloc_size()` helper
(built on the branch's `checked_add_size`), applied at `crt_malloc`,
`msvcr__calloc_crt`, `msvcr_realloc`, `msvcr__aligned_malloc`, and `HeapAlloc`
— which transitively covers `operator new`, `operator new[]`, and
`_malloc_crt`, i.e. all nine original paths. The comment ties removal to the
MEMORY.md section 5 gate, in a separate commit. The branch's other allocator
corrections (checked arithmetic, real frees, standard `realloc` behavior) are
untouched.

### 8. `DllMain(DLL_THREAD_ATTACH)` now runs on foreign threads — made deliberate

**Where:** `src/pe_loader.cpp` / `MEMORY.md`.

**Problem.** The old `pe_ensure_tls_for_loaded_images` only initialized TLS
storage and ran TLS callbacks. The unified path also invokes the PE's module
entry point, so `DllMain(DLL_THREAD_ATTACH)` now executes for the first time
on .NET GC, finalizer, and thread-pool threads at their first wrapper call.
This mirrors Windows semantics for threads created after load, but it was an
implicit side effect of the unification, not a recorded decision.

**Fix.** Kept the behavior — the test suite (`thread_id_dll_attach`)
explicitly asserts entry-point attach/detach delivery, and it is the
Windows-consistent choice — and documented it as a deliberate decision in
MEMORY.md section 9, together with the corrected detach ordering from finding
3. Any future issue on runtime-internal threads now traces to a recorded
decision instead of an accident.

### 9. FLS get/set serialized every thread on one global mutex

**Where:** `src/winlibs.cpp` — FLS implementation.

**Problem.** `FlsGetValue`/`FlsSetValue` took `g_fls_mutex` on every call even
though values live in per-thread state. On Windows `FlsGetValue` is a
lock-free TEB array read, and the UCRT reaches per-thread CRT state through
FLS — so every CRT call from every physics thread contended on one
process-wide mutex.

The slot `generation` counter and the per-thread `generations[1024]` array
(8 KB per thread) were also provably redundant: `FlsFree` nulls every
registered thread's value under the lock in the same critical section that
clears `allocated`, and threads register before writing values, so a
generation mismatch could only ever be observed alongside a null value —
where the outcome is identical.

**Fix.**
- `fls_slot::allocated` became `std::atomic<bool>`; per-thread `values` became
  `std::array<std::atomic<void *>, MAX_FLS_SLOTS>` (atomic because `FlsFree`
  nulls other threads' entries).
- `FlsGetValue`/`FlsSetValue` are now lock-free: an acquire load of
  `allocated` plus a relaxed load/store of the value. Racing a set against
  `FlsFree` of the same slot is a caller bug, as on Windows.
- `g_fls_mutex` still covers slot allocation, thread registration, and the
  drains (`FlsFree`, thread cleanup), where cross-thread mutation genuinely
  needs it. `FlsFree` uses `exchange` on `allocated` and on each value, which
  also simplified its drain.
- The generation machinery was deleted outright.

### 10. Flat export namespace let PE exports shadow shims

**Where:** `src/pe_loader.cpp`, `src/pe_loader.h` — `get_export`,
`process_import_descriptor`.

**Problem.** `get_export` scanned one flat table in reverse (last-registered
wins) by bare symbol name, and import binding passed only the symbol name —
the import descriptor's DLL name was used solely for logging and WS2_32
ordinal mapping. With two PE images loaded (the SE2 goal: Havok.dll plus
VRage.Physics.Native.dll), any name collision between a PE's exports and a
shim (common CRT names) or another image's exports silently rebound every
later importer to the last registrant — and the reverse scan made PE exports
shadow shim implementations by construction. The per-export `dll`/`owner`
fields added by this branch already carried the information needed to prevent
this.

**Fix.** `get_export` gained an optional `dll` parameter
(`get_export(const char *name, const char *dll = nullptr)`). When a module
name is supplied, the scan first looks for an export registered under that
module (case-insensitive name match, since PE import names vary in case), and
only falls back to the flat bare-name scan when nothing registered under that
module name — so module names nothing registers under (e.g. api-set DLLs
aliasing MSVCR registrations) resolve exactly as before. Import binding now
passes the descriptor's DLL name. `GetProcAddress`-style bare lookups are
unchanged.

---

## Verification and test results

- Build: clean (`cmake --build build`).
- Tests: 4/4 pass (`se1_generators`, `se2_generators`, `thread_id_dll_attach`,
  `windows_memory`).
- Two tests were updated because they asserted the pre-fix divergent behavior,
  not because the fixes broke them:
  - `tests/thread_id_dll_attach.cpp`: FLS callback counts 68/69 → 34/35
    (finding 3 — one drain per thread under the Windows detach order).
  - `tests/windows_memory.cpp` case 40: `MEM_TOP_DOWN` now expected to succeed
    (finding 6); an invalid flag combination still fails.

## Refuted candidates worth recording

These were surfaced by finders and killed in verification. Recorded so the
same concerns are not re-raised against deliberate decisions.

- **Export table overflow from repeated `load_dll`** (flagged independently by
  three finders): refuted — `register_windows_library_functions()` guards
  itself with a `static bool registered` flag, so shim exports register
  exactly once per process; failed PE loads roll back their own entries via
  `pe_discard_image_exports`.
- **Explicit-address `VirtualAlloc` committing from the 64 KB-rounded base**:
  matches documented Windows semantics for a single reserve+commit call at a
  non-NULL address (Windows rounds down to allocation granularity and commits
  the whole region); the page-rounded behavior applies only to committing into
  an existing reservation, which the code handles separately.
- **Recommit changing protection of already-committed pages**: also matches
  Windows — re-committing a committed page applies the new protection.
- **Removal of the duplicate-free suppression set with no quarantine**:
  deliberate and documented (MEMORY.md sections 1 and 4) — the set itself was
  a use-after-free mechanism (pointer reuse turns a delayed free of A into a
  false first free of B), and the plan explicitly forbids production
  quarantines. The `FAIL_REGULAR_EXPRESSION "duplicate free suppressed"` in
  CMakeLists.txt is likewise a deliberate tripwire against reintroducing the
  suppression, per the test spec in MEMORY.md, not a stale leftover.
- **`RaiseException` aborting on non-thread-name codes**: deliberate explicit
  boundary per MEMORY.md, with a loud diagnostic (`unsupported_msvc_exception`
  prints symbol, code, and caller) and no SEH dispatch machinery available to
  do better; a fork-based test asserts the abort.
- **`pe_unload_library` returning false unconditionally**: deliberate,
  documented ("PE unload is unsupported until executing calls can be
  quiesced"), and currently has zero callers.
- **`GetSystemInfo` hardcoding granularity/page size inline**: the inline
  values are provably identical to `WINDOWS_ALLOCATION_GRANULARITY` /
  `host_page_size()`, and those lines were not touched by this branch —
  a maintainability nit only.

## Known remaining observations (not fixed, lower severity)

Surfaced and verified during review but intentionally left out of this fix
pass; candidates for follow-up cleanup:

- `tests/windows_memory.cpp` re-declares the `ERROR_*`/`MEM_*`/`PAGE_*`
  constants that `src/winlibs.cpp` defines privately; `win_types.h` (which
  already hosts `STATUS_*`) is the natural shared home.
- The "free TLS block and zero slot" sequence appears three times in
  `src/pe_loader.cpp` with hand-written `1024` bounds duplicating the locally
  computed `MAX_TLS_SLOTS`; the failed-attach rollback re-implements the
  detach branch of `pe_notify_loaded_images`.
- ~27 sites assign `g_last_error = ERROR_...` directly instead of calling
  `SetLastError`, the canonical writer.
- `checked_add_size`/`checked_add_address` and `round_up_size`/
  `round_up_address` are `size_t`/`uintptr_t` copy-paste twins; a template
  would collapse them. `checked_mul_size` sits ~1000 lines away with a
  divergent errno contract.
- `pe_image::registered` is write-only in production code; its only reader is
  the test-only registration helper, which could scan `g_loaded_images`
  instead.
- `GetLogicalProcessorInformationEx` rejects `RelationAll` (and other
  relationship classes) with a silent `FALSE`; the scoping is documented, but
  a one-line stderr diagnostic on the rejection path would surface the next
  caller loudly. The processor snapshot is also bounded at 64 logical CPUs
  (one full processor group) — coherent, but worth a log line on >64-thread
  hosts.
- `HeapCreate` still returns fake handle `1` / `HeapDestroy` no-ops `TRUE`
  (untouched by this branch), contradicting MEMORY.md's "reject unsupported
  `HeapCreate` use" policy; no audited DLL imports it today.
