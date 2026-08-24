# Windows Memory Semantics Hardening Plan

Status: implemented for the available SE1 Havok DLL and shared virtual-memory
wrappers. The exact SE2 native import audit is complete; SE2 runtime validation
and exercised-import fixes remain. Private heaps are not required by this DLL.

## Recommendation

For the copied SE1 `Havok.dll`, the CRT allocation layer was the directly reachable
memory work: remove unsafe duplicate-free suppression, correct allocation edge
cases, and restore matching new/delete behavior. The audited SE2
`VRage.Physics.Native.dll` directly imports `VirtualAlloc` and `VirtualFree`, so the
completed reservation-semantics hardening is relevant to SE2. It does not import
`VirtualProtect`, Win32 heap APIs, or local-allocation APIs; keep those deferred.

Do not quarantine freed blocks, delay reuse, or keep deletes as no-ops. Windows does
not promise those behaviors, Wine enables delayed frees only for heap diagnostics,
and all three approaches hide bugs while increasing memory use.

The production design should continue to use Linux `mmap`, `mprotect`, `munmap`,
and libc allocation. It needs only one small record per virtual reservation and no
per-page bitmap or per-allocation global lookup.

## Confirmed Limitations

These were baseline defects before this hardening pass, independent of the
reported crash:

| Location | Baseline behavior | Required behavior and risk |
| --- | --- | --- |
| `src/winlibs.cpp:1950-1953` | Every non-null `VirtualAlloc` uses `MAP_FIXED`, ignores allocation type and protection, and creates a fresh RWX mapping. | Linux documents that `MAP_FIXED` discards overlapping mappings. Windows requires an incompatible requested range to fail, and recommitting an existing page must preserve its contents. The shim can replace live PE, heap, or physics-arena pages. |
| `src/winlibs.cpp:1955-1959` | `MEM_DECOMMIT` is a successful no-op. `VirtualFree(base, 0, MEM_RELEASE)` unmaps only 4096 bytes. All invalid operations report success. | Windows keeps decommitted address space reserved and inaccessible, then returns zero-filled pages when recommitted. `MEM_RELEASE` requires the original base and size zero and releases the complete reservation. The current behavior permits stale writes and desynchronizes arena bookkeeping. |
| `src/winlibs.cpp:109-133` | A process-global set suppresses frees based only on pointer value. Every tracked allocation/free takes one mutex, and freed addresses remain in the set until reused. | Pointer values have no generation. If allocation B reuses A's address, a delayed free of A is treated as a valid first free of B. This can create the exact use-after-free the set was intended to hide. It also adds hot-path contention and unbounded metadata. Windows treats duplicate free as invalid; it does not quarantine normal frees. |
| `src/winlibs.cpp:1475` | `realloc(p, 0)` reallocates 256 bytes instead of freeing `p`. Size addition can overflow, and a moving realloc frees the old address without recording that transition in the pointer-history set. | Microsoft CRT `realloc(p, 0)` frees and returns null. A nonzero failed realloc must preserve the original allocation. The current tracking cannot safely classify a later stale free. |
| `src/winlibs.cpp:873-881`, `1361`, `1410`, `1415`, `1475`, `1477`, `2058-2061` | Allocation sizes add 256 without overflow checks; calloc multiplication is unchecked. | A wrapped request can return a small non-null block that native code then overruns. |
| `src/winlibs.cpp:2056-2067` | Every heap is handle `1`; heap ownership and destruction are ignored. | Windows private heaps are distinct, and `HeapFree` must use the allocating heap. A wrong-heap free currently reaches libc and can release live storage. |
| `src/winlibs.cpp:1354`, `1478` | Imported scalar and array deletes do nothing. | Matching global delete releases new-allocated storage. The current workaround leaks indefinitely and is not a valid lifetime fix. |
| `src/win_types.h:29` | `BOOL` and `BOOLEAN` are both eight-bit types. | Windows `BOOL` is 32-bit while `BOOLEAN` is 8-bit. Memory API return values currently have the wrong ABI. |
| `src/winlibs.cpp:1933`, `2023` | `SetLastError` discards its value and `GetLastError` always returns zero. | Virtual-memory failures require thread-local error reporting. False success can make a native allocator continue with invalid state. |

The freed-address set was a direct wrapper-created UAF mechanism reachable through
the copied Havok imports, but the simple SE1 shape lifecycle probe below did not
reproduce it. The baseline virtual-memory mismatch was not reachable from this SE1
DLL, but the audited SE2 DLL directly imports that surface.

## Observed SE1 Havok Evidence

The copied root-level `Havok.dll` is a PE32+ x86-64 DLL with SHA-256
`f780dc090b725fdb06b66672805cbed3cf401c24e32ad36c9eaeadd3f8599d72`.
It is ignored by `.gitignore` and must not be committed or packaged.

`objdump -p Havok.dll` shows these imported allocation/lifetime functions from
`MSVCR120.dll`:

- `malloc`, `_malloc_crt`, `_calloc_crt`, `realloc`, and `free`;
- `_aligned_malloc` and `_aligned_free`;
- scalar `operator new` (`??2@YAPEAX_K@Z`);
- scalar `operator delete` (`??3@YAXPEAX@Z`);
- array `operator delete[]` (`??_V@YAXPEAX@Z`).

It does not import `VirtualAlloc`, `VirtualFree`, `VirtualProtect`, `HeapCreate`,
`HeapAlloc`, `HeapReAlloc`, `HeapFree`, `GetProcessHeap`, `LocalAlloc`, or
`LocalFree`. Therefore:

- sections 4 and 5 are the first SE1 implementation work;
- sections 2 and 3 are shared hardening, not an explanation for an SE1 Havok UAF;
- section 6 is not needed for this Havok build;
- the audited SE2 table makes virtual memory directly relevant there and confirms
  that private heaps are not.

The SE1 managed code confirms high-frequency native lifetime transitions:

- `Havok/HkReferenceObject.cs:24-52` forwards disposal and explicit reference
  release to `HkReferenceObject_RemoveReference`;
- `Havok/HkShape.cs:200-211` removes shape ownership before the native reference;
- `Havok/HkBaseSystem.cs:87-88,196-199` exposes native memory consumption;
- `Havok/Utils/HkManagedIntermediateBuffer.cs:68-78` explicitly releases buffers
  that native code moved to unmanaged storage.

Before hardening, the DLL-backed build and all five configured tests passed. This
proved that the DLL loaded and the sidecar/unwind path worked, but
`tests/havok_crash.cpp` created one shape and intentionally faulted without
releasing it. The focused tests added in section 7 now cover allocator contracts
and repeated shape release while keeping the intentional unwind crash separate.

A temporary probe outside the repository then ran 100,000
`HkBoxShape_Create`/`HkReferenceObject_ReferenceCount`/
`HkReferenceObject_RemoveReference` cycles. It emitted no duplicate-free diagnostic
and reported an unchanged `HkBaseSystem_GetCurrentMemoryConsumption` value of
34,431,984 bytes before and after the loop. This is a useful regression baseline,
not a reproduction of the reported failure. Broader object graphs, unmanaged
intermediate buffers, multithreaded destruction, or SE2 may use different paths.

### SE1 Imported API Compatibility Findings

The copied Havok DLL has 145 imports: 47 from `KERNEL32.dll`, seven from
`MSVCP120.dll`, 69 from `MSVCR120.dll`, and 22 from `WS2_32.dll`. Every import
currently resolves; none is linked to the unknown-symbol or unsupported-ordinal
trap. Resolution alone is not proof of compatibility because several imports bind
to no-op or partial implementations.

The lifecycle trace covered DLL attach, `HkBaseSystem_Init`, 100,000 box-shape
create/release cycles, memory accounting, and `HkBaseSystem_Quit`. It confirms the
normal allocator path but does not exercise every imported feature.

| Status | Finding | Risk and evidence |
| --- | --- | --- |
| Implemented | `GetLogicalProcessorInformation` now emits ABI-accurate 32-byte records from the host affinity/topology snapshot and enforces the two-call buffer contract. | The focused native test verifies size, stride, masks, short-buffer rejection, and `ERROR_INSUFFICIENT_BUFFER`; the Havok lifecycle test still passes. |
| Implemented | Loaded-image notifications are serialized by a recursive loader lock. `DisableThreadLibraryCalls`, thread detach, PE TLS callbacks, TLS block reclamation, and NT-context destruction are implemented. | Thread churn tests cover nested creation, enabled and disabled notifications, static-TLS rejection, TLS attach/detach, and FLS cleanup before the thread handle is signaled. |
| Explicit boundary | MSVC language handlers, throw helpers, and error helpers now terminate with a named diagnostic instead of returning fake success or throwing host C++ objects through PE frames. `__RTDynamicCast` accepts checked non-virtual, unambiguous metadata and rejects unsupported forms. | This prevents silent continuation with corrupted unwind or object state. It is not MSVC exception interoperability. |
| Implemented | `_onexit`, `__dllonexit`, UCRT on-exit tables, `_crt_atexit`, and `_cexit` now use synchronized LIFO callback storage with exact-once execution and reentrant registration ordering. | Reusable PE unload remains rejected until executing PE calls can be quiesced safely. |
| High when file features are used | `CreateFileW` returns handle `1`; `FlushFileBuffers`, `SetFilePointer`, and `WriteFile` report success without performing I/O at `src/winlibs.cpp:786-797`. Directory enumeration always fails at `src/winlibs.cpp:680-690`. | Havok imports these APIs and contains file-output and enumeration code. Profiling, diagnostics, cache, or resource features can silently lose data. Closing fake handle `1` can be interpreted as closing Linux file descriptor zero. These paths were not exercised by the lifecycle probe. |
| Medium-high when networking is used | Winsock wrappers mostly pass Windows structures and constants directly to Linux; `ioctlsocket` is a successful no-op, `select` uses Linux semantics, and `WSAGetLastError` returns raw `errno` at `src/winlibs.cpp:2328-2350`. | Havok imports the socket surface. Visual Debugger or remote networking can fail, block, or consume incompatible structures. No socket import ran in the lifecycle trace. |
| Implemented | `QueryPerformanceCounter` uses `CLOCK_MONOTONIC` nanoseconds and reports a 1 GHz frequency. | The focused native test checks monotonicity and elapsed-time conversion. |

The Havok-imported allocation APIs are not stubs after this hardening pass. The
copied DLL does not import virtual-memory, heap, local-allocation, or mapped-file
APIs. Therefore the findings above do not change the completed CRT allocator work
or make the deferred private-heap work relevant to this SE1 build.

## Observed SE2 Physics Evidence

The root-level `VRage.Physics.Native.dll` is a PE32+ x86-64 DLL with SHA-256
`54ada165179ff29a3029d8e3cbc79bc06fa6c4f22529e09ed92d6d6f549ad322`. The
accompanying `VRage.Physics.Client.dll` and `VRage.Physics.dll` are managed .NET
assemblies with SHA-256 values
`65b84d63fbe73ad6966fbd6fafad04ecb4760a110fe215900b45d4521dbec425` and
`d5bb284d1fcf98547e6e21df14d970f391f1e134193bddff4ca3bf1cc34cbb08`.
Only the native DLL supplies Win32 import evidence. These proprietary binaries are
local audit inputs and must not be committed or packaged.

`objdump -p VRage.Physics.Native.dll` shows 169 imports from 13 DLLs:

| DLL | Imports |
| --- | ---: |
| `KERNEL32.dll` | 77 |
| `WS2_32.dll` | 25 |
| `VCRUNTIME140.dll` | 18 |
| `api-ms-win-crt-runtime-l1-1-0.dll` | 13 |
| `api-ms-win-crt-stdio-l1-1-0.dll` | 10 |
| `api-ms-win-crt-math-l1-1-0.dll` | 7 |
| `api-ms-win-crt-string-l1-1-0.dll` | 6 |
| `api-ms-win-crt-heap-l1-1-0.dll` | 4 |
| `api-ms-win-crt-convert-l1-1-0.dll` | 4 |
| `api-ms-win-crt-time-l1-1-0.dll` | 2 |
| `MSVCP140.dll`, `VCRUNTIME140_1.dll`, and `api-ms-win-crt-locale-l1-1-0.dll` | 1 each |

The directly imported allocation and lifetime surface is:

- `VirtualAlloc` and `VirtualFree`;
- `malloc`, `free`, `_aligned_free`, `_callnewh`, and `_strdup`;
- FLS allocation, assignment, and release through `FlsAlloc`, `FlsSetValue`, and
  `FlsFree`.

It does not import `VirtualProtect`, `VirtualQuery`, `realloc`, `calloc`,
`_aligned_malloc`, decorated new/delete operators, `HeapCreate`, `HeapAlloc`,
`HeapReAlloc`, `HeapFree`, `GetProcessHeap`, `LocalAlloc`, or `LocalFree`.
Therefore virtual-memory correctness is directly relevant, while private-heap and
local-allocation work remains unnecessary for this exact binary. Import inspection
cannot reveal the allocation flags used at runtime or the origin of pointers passed
to `_aligned_free`; both require a runtime trace.

The current wrapper and generated sidecar loaded this exact DLL and completed
`Init`, but import linking now reports 23 unresolved entries. The loader installs
deferred `SIGTRAP` stubs for them at `src/pe_loader.cpp:296-308,351-375`, so a
successful attach does not mean these APIs are supported:

- `KERNEL32.dll`: `GetErrorMode`, `SetErrorMode`, `LoadLibraryW`,
  `SetThreadPriority`, `ResumeThread`,
  `SetThreadAffinityMask`, `FindFirstFileExW`, `RemoveDirectoryW`, `CancelIo`,
  `SleepEx`, `QueueUserAPC`, `CopyFileW`, `MoveFileExW`,
  `ReadDirectoryChangesW`, and `GetComputerNameW`;
- `WS2_32.dll`: `WSASend` and ordinals 17 (`recvfrom`), 20 (`sendto`), and 23
  (`socket`);
- runtime support: `fflush`, `__acrt_iob_func`, `__stdio_common_vfwprintf`, and
  `_fdclass`.

The other 146 imports resolve, but several bind partial or fake implementations:

| Priority | Finding | Risk and evidence |
| --- | --- | --- |
| Implemented | `GetLogicalProcessorInformationEx(RelationProcessorCore)` emits 48-byte variable records with group masks from the same bounded host snapshot as the legacy API. | Other relationship classes are rejected with `ERROR_INVALID_PARAMETER` rather than fabricated. The audited Physics caller requests processor-core records. |
| Implemented | FLS callbacks run on slot release and thread exit; PE thread detach, TLS callbacks, and TLS block/context reclamation are serialized and tested. | FLS remains pthread-local rather than a complete Windows fiber scheduler. |
| Explicit boundary | `__CxxFrameHandler4`, `RaiseException`, current-exception helpers, `terminate`, and `_CxxThrowException` resolve to deterministic diagnostics. The debugger-only thread-name exception is ignored safely. | A real MSVC exception still terminates; no host exception crosses PE frames. |
| High when exercised | Fifteen Kernel32, four Winsock, and four runtime imports use deferred traps. File APIs that do resolve still include fake handles and successful discarded writes; resolved Winsock wrappers use incompatible direct Linux structures and constants. | Optional filesystem, watcher, asynchronous, or network features may either trap or silently behave incorrectly. Implement only paths demonstrated by a representative runtime trace. |
| Medium | Named imports are resolved from one process-global bare-symbol table, without respecting the requested runtime DLL, at `src/pe_loader.cpp:47-80,366-375`. | UCRT, VCRUNTIME140, and MSVCP140 names can bind older MSVCR120/MSVCP120 aliases. Resolution is not evidence of version-compatible ABI or behavior. |

The static audit changes the memory conclusion: the shared `VirtualAlloc` and
`VirtualFree` work is required for SE2, while the SE1-specific new/delete and
`realloc` paths are not imported by this DLL. It does not establish the cause of a
runtime failure; the first invalid access and the first exercised trap still need a
representative SE2 trace.

## Implementation Plan

### 1. Record the Actual Physics Imports and Baseline

- The copied SE1 Havok and SE2 Physics import audits are complete and recorded
  above. Repeat `objdump -p` if either recorded hash changes (`llvm-readobj` is not
  installed here).
- Preserve the SE2 result: it imports `VirtualAlloc` and `VirtualFree`, but not
  `VirtualProtect`, private/process heap APIs, local-allocation APIs, `realloc`,
  `calloc`, or decorated new/delete operators.
- Treat the 23 unresolved SE2 imports as runtime traps, not supported imports. The
  attach-only `Init` probe proves that they are not called during DLL attach; it
  does not prove that normal physics execution avoids them.
- Capture the current failure with native symbols, its first invalid access, the
  freeing stack, and the allocation stack where available. Record any existing
  allocator diagnostics and the first deferred import trap.
- For SE1, record release-build runtime, peak RSS, and
  `HkBaseSystem_GetCurrentMemoryConsumption` over the repeated shape lifecycle test
  in section 7 and a physics-heavy game load/unload loop. For SE2, use
  `GetTotalAllocatedMemory`. These are comparison baselines, not reasons to retain
  invalid behavior.

No tracing feature needs to be added to production for this step. Use the PE import
table, GDB/core data, glibc diagnostics, Valgrind, or sanitizers around a minimal
native reproducer.

### 2. Correct the Shared ABI and Error State

Files: `src/win_types.h`, `src/winlibs.cpp`.

- Split `BOOL` into a signed 32-bit type while keeping `BOOLEAN` eight-bit. Add
  compile-time size assertions.
- Back `SetLastError`/`GetLastError` with `thread_local DWORD`.
- Set `ERROR_INVALID_PARAMETER`, `ERROR_INVALID_ADDRESS`, or
  `ERROR_NOT_ENOUGH_MEMORY` on failed virtual-memory operations. Do not clear last
  error on success, matching Win32 conventions.
- Keep this as a separate commit so any broad ABI regression is immediately
  bisectable.

### 3. Replace `VirtualAlloc`/`VirtualFree` With Reservation Semantics

File: `src/winlibs.cpp`.

Use a mutex-protected ordered map from reservation base address to rounded size.
This is one node per reservation. Do not allocate a byte per page.

`VirtualAlloc` behavior:

- Validate zero sizes, arithmetic overflow, supported allocation flags, and page
  protection before changing mappings.
- Use Windows' 64 KiB allocation granularity for new reservations and the host page
  size for commit/decommit ranges. `GetSystemInfo` already reports 65536-byte
  allocation granularity.
- For an explicit reservation, round the base down to 64 KiB and round the end from
  the original `lpAddress + dwSize`, with checked `uintptr_t` arithmetic. For
  commit/decommit, round the base down and end up to host page boundaries.
- Reserve anonymous address space as `PROT_NONE`. For a caller-supplied reservation
  address, use `MAP_FIXED_NOREPLACE`, verify the returned address, and fail on any
  collision. Never use unrestricted `MAP_FIXED` to claim new address space.
- For an unspecified reservation address, transiently over-map enough space to
  select a 64 KiB-aligned base, then unmap the prefix and suffix. This consumes no
  extra steady-state address space or physical memory.
- Implement `MEM_RESERVE`, `MEM_COMMIT`, and their combination. A null-address
  commit-only call creates a new reserved-and-committed region; a non-null
  commit-only range must fit wholly inside one tracked reservation.
- Commit with `mprotect` using a small `PAGE_*` to `PROT_*` translation. This keeps
  already committed contents intact and faults physical pages lazily.
- Support `PAGE_NOACCESS`, `PAGE_READONLY`, `PAGE_READWRITE`, `PAGE_EXECUTE`,
  `PAGE_EXECUTE_READ`, and `PAGE_EXECUTE_READWRITE`. Reject copy-on-write, guard,
  cache, and invalid modifier combinations. Document that execute-only mappings may
  also be readable on x86-64 Linux.
- Reject unsupported modes such as large pages, write-watch, guard, and physical
  memory rather than returning false success. Add only modes observed in the PE
  import/call trace.
- Treat mapping, trimming, protection, and metadata insertion as one transaction
  under the reservation mutex. Insert the record only after mapping and protection
  succeed; if any syscall or map allocation fails, unmap all temporary ranges and
  return failure. If `MAP_FIXED_NOREPLACE` returns a different address on an older
  kernel, unmap that returned mapping and fail.

`VirtualFree` behavior:

- `MEM_DECOMMIT`: validate that the rounded range belongs to one reservation, then
  replace that owned subrange with anonymous `PROT_NONE` pages using `MAP_FIXED`.
  Here `MAP_FIXED` is safe because the reservation table proves ownership. The
  replacement discards contents and physical backing; recommit through `mprotect`
  exposes zero-filled pages. A zero size decommits the whole reservation only when
  the supplied address is its exact base. Require the replacement to return the
  exact requested address; otherwise fail without changing the reservation record
  and treat any lost owned mapping as a fatal invariant violation.
- `MEM_RELEASE`: require size zero and the exact original reservation base. Unmap
  the complete recorded size and erase the record only after `munmap` succeeds.
- Reject all other combinations without touching memory.
- Hold the reservation mutex through validation and the corresponding mapping
  operation so commit, decommit, and release cannot race each other.

Do not add `VirtualProtect` to this reservation-only design. If the actual PE imports
it, revise the design to track committed state and prior protection per interval so
`VirtualProtect` can reject uncommitted pages and return the previous protection.

### 4. Remove the Wrapper-Created Free Hazard

File: `src/winlibs.cpp`.

- Delete `g_freed_blocks`, `g_heap_mutex`, `heap_track_alloc`, and
  `heap_track_free`.
- Return blocks to libc immediately from valid CRT, aligned, heap, and delete
  paths. A stale pointer after a valid free is a caller bug on Windows too; normal
  operation must not retain memory to make it less likely.
- Give scalar and array delete separate correctly named wrappers and route both to
  the same deallocator used by the imported operator new. Both delete symbols are
  present in the copied Havok DLL and both current implementations are no-ops.
- Land delete restoration separately from removing the pointer-history set. Run
  the lifecycle test after each change so a newly exposed ownership error has one
  cause.
- Keep diagnostics external or behind a debug-only build option if another capture
  is needed. Do not add a production quarantine or allocation log.

This change removes one mutex acquisition from each tracked allocation/free and
removes the unbounded freed-address set, so it should improve rather than reduce
steady-state performance.

### 5. Correct CRT Allocation Edge Cases

File: `src/winlibs.cpp`.

- Add one checked-size helper for addition and one for multiplication. Use them in
  all exported malloc, calloc, realloc, aligned allocation, operator new, and heap
  paths.
- Implement `realloc(nullptr, n)` as malloc behavior and `realloc(p, 0)` as free
  plus null. On nonzero failure, leave `p` untouched.
- Validate aligned-allocation size and power-of-two alignment, preserve at least
  16-byte Win64 alignment, and set `errno` consistently.
- Audit throwing operator new only if the exact PE imports and exercises it. Do not
  throw a host `std::bad_alloc` across PE/MSVC frames; use a compatible imported CRT
  failure path or fail fast until PE exception propagation is supported.
- Replace the unconditional 256-byte addition with the minimum alignment/metadata
  required by the API. Windows guarantees alignment, not 256 writable bytes past
  the request. Keep the current padding temporarily only if the baseline produces
  evidence of a specific ABI requirement; document and test that requirement
  before retaining it.
- Exercise every allocation function actually imported by the copied Havok DLL,
  including `_malloc_crt`, `_calloc_crt`, aligned allocation, and both delete
  operators. Do not limit the test to the plain `malloc`/`free` aliases.
- First land checked arithmetic and standard `realloc` behavior while retaining the
  existing padding. Remove or reduce the padding in a later commit after the DLL
  lifecycle and managed integration checks pass, because its original purpose is
  undocumented.

Keep this in a separate commit from virtual memory so the physics reproducer can
identify which contract correction changes the failure.

### 6. Keep Private-Heap Work Deferred

The audited SE1 and SE2 DLLs do not import `HeapCreate`, `HeapAlloc`, `HeapReAlloc`,
`HeapFree`, `GetProcessHeap`, `LocalAlloc`, or `LocalFree`. Do not build private-heap
or movable-local-allocation support for these binaries.

Files, only if a future audited DLL imports and exercises this surface:
`src/winlibs.cpp` and its memory test.

If Physics uses `HeapCreate` or passes multiple heap handles:

- Represent the process heap and each private heap with distinct validated handles.
- Store a compact owner pointer, requested size, and, for private heaps, intrusive
  list links immediately before each returned block while maintaining 16-byte user
  alignment. This replaces the existing 256-byte padding rather than adding to it
  or allocating separate tracking nodes.
- Validate heap ownership in `HeapFree`, implement `HeapReAlloc` with failure
  atomicity, and invalidate/release private heaps in `HeapDestroy`.
- Use the intrusive list to release outstanding private-heap blocks in
  `HeapDestroy`. Protect each private heap with its own lock; do not add a global
  heap lock. The process heap is not destroyable and does not need an allocation
  list.
- Use libc as the allocator. Do not build a custom free-list allocator or copy
  Wine's complete heap implementation.
- Serialize only heap bookkeeping that actually needs it. Libc allocation is
  already thread-safe; there should be no process-global allocation lock.

If a future Physics DLL uses only the process heap, defer private heaps and reject
unsupported `HeapCreate` use rather than maintaining a fake handle that claims
success. A pointer header cannot detect a stale same-address pointer after reuse;
that is not a solvable promise on Windows either, so the plan does not claim it can.

Similarly, implement movable `LocalAlloc` handles only if imports and flags show
they are used. Fixed local allocations can use the process-heap path. Unsupported
movable allocations should fail rather than return a misleading pointer.

### 7. Add Focused Native and Havok Memory Tests

Files: `tests/windows_memory.cpp`, `tests/havok_memory.cpp`, `CMakeLists.txt`.

The DLL-independent `windows_memory` test should link the existing shared loader
sources and call the exported shim functions as the current thread test does.
Cover:

- a requested reservation colliding with an existing sentinel mapping fails and
  leaves the sentinel unchanged;
- explicit reservations round correctly at 64 KiB boundaries, and commit/decommit
  ranges spanning page boundaries round correctly;
- `VirtualAlloc(nullptr, size, MEM_COMMIT, ...)` reserves and commits one region;
- reserve-only memory is inaccessible in a child process;
- commit is writable and initially zero;
- recommitting an already committed page preserves data;
- commit outside a tracked reservation fails without changing mappings;
- decommit makes pages inaccessible, keeps the address reserved, and recommit
  returns zero-filled data;
- zero-size decommit works only at the exact reservation base and covers the whole
  reservation;
- `MEM_RELEASE` rejects a nonzero size or interior address and releases the entire
  original reservation for exact base plus zero size;
- unsupported flags and protections fail without changing mappings;
- two threads repeatedly commit/decommit disjoint pages while another attempts
  invalid release operations;
- overflow requests fail without changing existing allocations;
- `realloc(p, 0)` frees and returns null, while failed nonzero realloc preserves
  the original bytes;
- new/delete and aligned allocate/free pairs balance under ASan/LSan;
- distinct heap ownership cases, only if step 6 is required.
- failure paths set the documented last error, while successful calls preserve a
  sentinel last-error value;
- supported page protections permit and fault reads, writes, and execution as
  expected in child processes.

Use subprocesses only for expected access faults. Do not install a test-only signal
handler in production code.

The optional `havok_memory` test should be enabled by the existing
`NATIVE_DLL_DIR/Havok.dll` check and use only stable exports already generated in
`src/Havok.cpp`:

- initialize Havok through `Init` and `HkBaseSystem_Init`;
- warm up repeated `HkBoxShape_Create` and
  `HkReferenceObject_RemoveReference` pairs on one initialized thread;
- assert each new shape has a positive `HkReferenceObject_ReferenceCount` and
  release it exactly once;
- run enough batches to force allocator address reuse and sample
  `HkBaseSystem_GetCurrentMemoryConsumption` after each batch;
- preserve the observed 100,000-cycle zero-growth baseline: consumption must
  plateau after warmup rather than grow once per shape;
- call `HkBaseSystem_Quit` on the normal path;
- fail on any `duplicate free suppressed` message, allocator abort, or invalid
  reference count;
- run the same executable under Valgrind for the review build because the Windows
  PE itself is not compiler-instrumented by ASan.

Keep `havok_unwind` unchanged. Combining an intentional crash test with lifecycle
assertions would make failures ambiguous.

### 8. Integration Validation

`linux-native-wrappers`:

```bash
cmake --preset default
cmake --build --preset default -j2
ctest --test-dir build --output-on-failure
```

- Repeat in Release and Debug/package configurations.
- Run the focused test under ASan/UBSan.
- Configure with `-DNATIVE_DLL_DIR="$PWD"` and run the new `havok_memory` test
  against the ignored local DLL.
- Run the exact SE2 physics scenario after each separate memory commit. Compare
  crash behavior, `GetTotalAllocatedMemory`, RSS, and runtime with the baseline.
- Until safe PE unload is implemented, repeat the workload in fresh processes to
  expose initialization and shutdown regressions. Do not treat the current rejected
  `pe_unload_library` path as unload/reload coverage.
- The native wrapper plus generated sidecar already completes `Init` for the audited
  SE2 DLL while reporting all 23 unresolved imports. Repeat this smoke check after
  loader changes, but do not count it as physics runtime coverage.

`/home/space/Documents/se-linux-compat`:

- No managed memory shim belongs here. `Shared/Compatibility/NativeLibraries.cs`
  only aliases and initializes SE1 wrapper libraries; allocation semantics execute
  inside `linux-native-wrappers` after PE import binding.
- Build both consumers against the rebuilt wrappers:

```bash
dotnet build ClientPlugin/ClientPlugin.csproj -c Release
dotnet build ServerPlugin/ServerPlugin.csproj -c Release
```

- Smoke-test client and server world load, physics activity, save, unload, reload,
  and shutdown. This validates the shared `winlibs.cpp` changes through Havok even
  though this checkout does not load the SE2 Physics wrapper. Treat these as
  non-blocking regression checks; the exact SE2 scenario is the blocking result.
- Change LinuxCompat only if wrapper filenames, the `Init` ABI, or packaged asset
  versions change. This plan changes none of them.
- Preserve the existing uncommitted local path edits in `ClientPlugin.xml` and
  `Directory.Build.props`; they are unrelated to memory semantics.

Only one `/home/space/Documents/se-linux-compat` tree exists in the current
workspace. There is no second managed compatibility tree or local SE2 host here to
edit or test during planning.

### 9. Exercised and Lifetime-Critical API Status

Implemented in the shared loader and shim layer:

- Win64 topology structures, bounded host core masks, exact two-call sizing, and
  short-buffer error behavior for both topology APIs. The SE2-used
  `RelationProcessorCore` form is supported; unimplemented relationships fail.
- A serialized loader lifecycle. Images become thread-visible only after successful
  process attach, failed loads roll back their exports and mappings, and image-owned
  export bindings restore the previous bare-name binding when discarded.
- Per-image `DisableThreadLibraryCalls`, `DLL_THREAD_ATTACH`/`DLL_THREAD_DETACH`, PE
  TLS callbacks, per-thread TLS block reclamation, and NT-context destruction.
- FLS callback preservation and bounded callback re-entry on slot release and
  thread exit. A second drain catches values installed by detach callbacks.
- Named deterministic boundaries for unsupported MSVC handlers, throws, current
  exception state, and nontrivial RTTI forms. No shim throws a host C++ object
  through PE frames.
- Synchronized UCRT and MSVCR on-exit registration with LIFO, exact-once, and
  reentrant callback behavior.
- `CLOCK_MONOTONIC` nanosecond performance counters with a matching 1 GHz frequency.

The focused native tests cover topology layout and buffers, timing conversion,
on-exit ordering, deterministic exception failure, nested thread creation,
notification suppression, static-TLS rejection, TLS attach/detach, and FLS cleanup.
The complete seven-test suite and the exact SE2 `Init` probe pass after these
changes.

Remaining explicit boundaries:

- `pe_unload_library` returns failure without unmapping. Safe unload needs a way to
  quiesce threads executing exported PE code; thread notifications alone are not a
  sufficient lifetime pin.
- FLS storage is thread-local, not a complete Windows fiber implementation.
- Full MSVC x64 exception handling and virtual/ambiguous RTTI require dedicated PE
  fixtures for catches, destructor unwinding, rethrows, and cross-casts.
- Non-core topology relationships remain unsupported because the audited Physics
  binary requests only processor-core records.
- File, directory, asynchronous, and Winsock expansion remains gated on a
  representative runtime trace. Unknown imports still receive reported deferred
  traps and are not treated as supported.

## Performance and Memory Acceptance Criteria

- No production free quarantine, never-free path, pointer-history set, or complete
  allocation log.
- Wrapper-owned virtual-memory metadata is O(number of reservations), not
  O(reserved pages). Linux kernel VMA count is O(contiguous protection runs), so
  also measure mapping count and mapping-specific `smaps` RSS.
- Reserved/decommitted ranges use `PROT_NONE` anonymous mappings and consume
  no data-page RSS after successful decommit, but still consume virtual address
  space, page tables, and kernel VMA metadata.
- Run five warmed repetitions of the fixed physics workload. Patched median runtime
  must remain within 2% of baseline and peak RSS within the larger of 1% or 8 MiB.
- If safe unload is implemented later, ten repeated load/unload cycles must not show
  monotonic RSS or `GetTotalAllocatedMemory` growth greater than the larger of 1%
  or 8 MiB.
- Record `/proc/self/maps` count and `VmPTE`; the workload must remain comfortably
  below `vm.max_map_count` and show no unbounded VMA growth across cycles.
- Release builds no longer create RWX virtual allocations unless the caller
  explicitly requests executable and writable protection.
- Invalid fixed-address allocation cannot alter any pre-existing mapping.

Linux overcommit policy is an unavoidable compatibility boundary: unlike Windows
commit accounting, `mprotect` cannot guarantee that a later page fault will survive
Linux OOM handling. Do not use `MAP_NORESERVE` for committed ranges, and record
`vm.overcommit_memory` during validation. The target here is matching Windows
address, protection, zero-fill, decommit, and release behavior subject to the host
kernel's overcommit policy.

## Explicit Non-Goals

- Do not mask a genuine game-engine ownership race by retaining freed memory.
- Do not port Wine's complete virtual-memory manager or heap allocator.
- Do not add per-page state unless an observed API such as `VirtualQuery`, guard
  pages, write-watch, or mixed protection requires it.
- Do not harden unrelated PE image section permissions in this change. Loader image
  W^X is useful but separate from the allocation lifetime defect and would make the
  failure harder to bisect.
- Do not patch managed physics ownership without a managed allocation/free trace
  proving that the first invalid lifetime transition occurs there.

## Sources

- Microsoft `VirtualAlloc` contract:
  <https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc>
- Microsoft `VirtualFree` contract:
  <https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree>
- Microsoft `HeapAlloc`, `HeapFree`, and `HeapReAlloc`:
  <https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc>,
  <https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapfree>,
  <https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heaprealloc>
- Microsoft CRT `malloc`, `free`, and `realloc`:
  <https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/malloc>,
  <https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/free>,
  <https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/realloc>
- Microsoft `GetLogicalProcessorInformation` contract:
  <https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformation>
- Microsoft DLL entry-point and thread-notification contract:
  <https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain>,
  <https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-disablethreadlibrarycalls>
- Microsoft FLS callback contract:
  <https://learn.microsoft.com/en-us/windows/win32/api/winnt/nc-winnt-pfls_callback_function>
- Microsoft `QueryPerformanceCounter` contract:
  <https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter>
- Linux `mmap`; see `MAP_FIXED`, `MAP_FIXED_NOREPLACE`, and "Using MAP_FIXED
  safely": <https://man7.org/linux/man-pages/man2/mmap.2.html>
- Wine virtual-memory implementation, including reservation tracking, commit,
  decommit, and release:
  <https://github.com/wine-mirror/wine/blob/8da89f8493b21ebfbe344a54dbef0cde23c7ea59/dlls/ntdll/unix/virtual.c>
- Wine heap implementation. Its pending-free ring is tied to checking/Valgrind,
  not ordinary allocation:
  <https://github.com/wine-mirror/wine/blob/8da89f8493b21ebfbe344a54dbef0cde23c7ea59/dlls/ntdll/heap.c>
- Wine conformance tests:
  <https://github.com/wine-mirror/wine/blob/8da89f8493b21ebfbe344a54dbef0cde23c7ea59/dlls/kernel32/tests/virtual.c>,
  <https://github.com/wine-mirror/wine/blob/8da89f8493b21ebfbe344a54dbef0cde23c7ea59/dlls/kernel32/tests/heap.c>
