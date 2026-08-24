#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pe_loader.h"
#include "winlibs.h"

extern void *WINAPI VirtualAlloc(void *, SIZE_T, DWORD, DWORD);
extern BOOL WINAPI VirtualFree(void *, SIZE_T, DWORD);
extern void WINAPI SetLastError(DWORD);
extern DWORD WINAPI GetLastError();
extern BOOL WINAPI GetLogicalProcessorInformation(void *, DWORD *);
extern BOOL WINAPI GetLogicalProcessorInformationEx(INT, void *, DWORD *);
extern BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *);
extern BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *);
extern void WINAPI RaiseException(DWORD, DWORD, DWORD, const ULONG_PTR *);
extern int WINAPI crt__initialize_onexit_table(void *);
extern int WINAPI crt__register_onexit_function(void *, void *);
extern int WINAPI crt__execute_onexit_table(void *);
extern int WINAPI crt__crt_atexit(_PVFV);
extern void WINAPI crt__cexit();

static constexpr DWORD ERROR_NOT_ENOUGH_MEMORY = 8;
static constexpr DWORD ERROR_INVALID_PARAMETER = 87;
static constexpr DWORD ERROR_INVALID_ADDRESS = 487;
static constexpr DWORD ERROR_INSUFFICIENT_BUFFER = 122;
static constexpr DWORD MEM_COMMIT = 0x1000;
static constexpr DWORD MEM_RESERVE = 0x2000;
static constexpr DWORD MEM_DECOMMIT = 0x4000;
static constexpr DWORD MEM_RELEASE = 0x8000;
static constexpr DWORD PAGE_NOACCESS = 0x01;
static constexpr DWORD PAGE_READONLY = 0x02;
static constexpr DWORD PAGE_READWRITE = 0x04;
static constexpr DWORD PAGE_EXECUTE = 0x10;
static constexpr DWORD PAGE_EXECUTE_READ = 0x20;
static constexpr DWORD PAGE_EXECUTE_READWRITE = 0x40;

template <typename T>
static T import(const char *name)
{
    return reinterpret_cast<T>(get_export(name));
}

static bool access_faults(void *address, bool write)
{
    pid_t child = fork();
    if (child == 0) {
        auto *byte = static_cast<volatile unsigned char *>(address);
        if (write)
            *byte = 1;
        else
            (void)*byte;
        _exit(0);
    }
    int status = 0;
    return child > 0 && waitpid(child, &status, 0) == child &&
           WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
}

static int test_allocators()
{
    register_windows_library_functions();
    using alloc_t = void *(WINAPI *)(size_t);
    using calloc_t = void *(WINAPI *)(size_t, size_t);
    using free_t = void (WINAPI *)(void *);
    using realloc_t = void *(WINAPI *)(void *, size_t);
    using aligned_alloc_t = void *(WINAPI *)(size_t, size_t);

    auto crt_malloc = import<alloc_t>("malloc");
    auto malloc_crt = import<alloc_t>("_malloc_crt");
    auto calloc_crt = import<calloc_t>("_calloc_crt");
    auto crt_free = import<free_t>("free");
    auto crt_realloc = import<realloc_t>("realloc");
    auto aligned_malloc = import<aligned_alloc_t>("_aligned_malloc");
    auto aligned_free = import<free_t>("_aligned_free");
    auto operator_new = import<alloc_t>("??2@YAPEAX_K@Z");
    auto operator_delete = import<free_t>("??3@YAXPEAX@Z");
    auto operator_delete_array = import<free_t>("??_V@YAXPEAX@Z");
    if (!crt_malloc || !malloc_crt || !calloc_crt || !crt_free || !crt_realloc ||
        !aligned_malloc || !aligned_free || !operator_new || !operator_delete ||
        !operator_delete_array)
        return 1;

    auto *plain = static_cast<unsigned char *>(crt_malloc(32));
    auto *internal = static_cast<unsigned char *>(malloc_crt(32));
    auto *zeroed = static_cast<unsigned char *>(calloc_crt(32, 2));
    if (!plain || !internal || !zeroed)
        return 2;
    for (size_t i = 0; i < 64; ++i)
        if (zeroed[i])
            return 3;
    crt_free(plain);
    crt_free(internal);
    crt_free(zeroed);

    errno = 0;
    if (calloc_crt(std::numeric_limits<size_t>::max(), 2) || errno != ENOMEM)
        return 4;

    auto *grown = static_cast<unsigned char *>(crt_malloc(16));
    if (!grown)
        return 5;
    std::memset(grown, 0x5a, 16);
    grown = static_cast<unsigned char *>(crt_realloc(grown, 64));
    if (!grown)
        return 6;
    for (int i = 0; i < 16; ++i)
        if (grown[i] != 0x5a)
            return 7;
    auto *preserved = static_cast<unsigned char *>(crt_realloc(grown,
        std::numeric_limits<size_t>::max()));
    if (preserved || grown[0] != 0x5a)
        return 8;
    if (crt_realloc(grown, 0))
        return 9;
    void *from_null = crt_realloc(nullptr, 32);
    if (!from_null)
        return 10;
    crt_free(from_null);

    errno = 0;
    if (aligned_malloc(32, 3) || errno != EINVAL)
        return 11;
    void *aligned16 = aligned_malloc(1, 1);
    void *aligned64 = aligned_malloc(33, 64);
    if (!aligned16 || !aligned64 || reinterpret_cast<uintptr_t>(aligned16) % 16 ||
        reinterpret_cast<uintptr_t>(aligned64) % 64)
        return 12;
    aligned_free(aligned16);
    aligned_free(aligned64);

    operator_delete(operator_new(48));
    operator_delete_array(operator_new(48));
    return 0;
}

static int test_virtual_memory()
{
    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));

    SetLastError(0x1234);
    auto *memory = static_cast<unsigned char *>(
        VirtualAlloc(nullptr, page * 3, MEM_RESERVE, PAGE_READWRITE));
    if (!memory || reinterpret_cast<uintptr_t>(memory) % 65536 || GetLastError() != 0x1234)
        return 20;
    if (!access_faults(memory, false))
        return 21;

    if (VirtualAlloc(memory + 1, page, MEM_COMMIT, PAGE_READWRITE) != memory)
        return 22;
    memory[0] = 0x31;
    memory[page] = 0x32;
    if (VirtualAlloc(memory, page * 2, MEM_COMMIT, PAGE_READWRITE) != memory ||
        memory[0] != 0x31 || memory[page] != 0x32)
        return 23;

    SetLastError(0);
    if (VirtualAlloc(memory + page * 3, page, MEM_COMMIT, PAGE_READWRITE) ||
        GetLastError() != ERROR_INVALID_ADDRESS)
        return 24;

    if (!VirtualFree(memory + page, page, MEM_DECOMMIT) ||
        !access_faults(memory + page, false))
        return 25;
    if (VirtualAlloc(memory + page, page, MEM_COMMIT, PAGE_READWRITE) != memory + page ||
        memory[page] != 0)
        return 26;

    memory[0] = memory[page] = 0x7f;
    if (!VirtualFree(memory, 0, MEM_DECOMMIT) || !access_faults(memory, false) ||
        !access_faults(memory + page, false))
        return 27;
    if (VirtualAlloc(memory, page * 2, MEM_COMMIT, PAGE_READWRITE) != memory ||
        memory[0] || memory[page])
        return 28;

    SetLastError(0);
    if (VirtualFree(memory + page, 0, MEM_DECOMMIT) ||
        GetLastError() != ERROR_INVALID_ADDRESS)
        return 29;

    SetLastError(0);
    if (VirtualFree(memory + page, 0, MEM_RELEASE) || GetLastError() != ERROR_INVALID_ADDRESS)
        return 30;
    SetLastError(0);
    if (VirtualFree(memory, page, MEM_RELEASE) || GetLastError() != ERROR_INVALID_PARAMETER)
        return 31;
    if (!VirtualFree(memory, 0, MEM_RELEASE))
        return 32;

    void *committed = VirtualAlloc(nullptr, page, MEM_COMMIT, PAGE_READWRITE);
    if (!committed || *static_cast<unsigned char *>(committed) != 0 ||
        !VirtualFree(committed, 0, MEM_RELEASE))
        return 33;

    const size_t candidate_size = 3 * 65536;
    void *candidate_mapping = mmap(nullptr, candidate_size, PROT_NONE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (candidate_mapping == MAP_FAILED)
        return 34;
    uintptr_t candidate = (reinterpret_cast<uintptr_t>(candidate_mapping) + 65535) & ~uintptr_t{65535};
    munmap(candidate_mapping, candidate_size);
    auto *fixed = static_cast<unsigned char *>(VirtualAlloc(
        reinterpret_cast<void *>(candidate + page), page,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (fixed != reinterpret_cast<void *>(candidate))
        return 35;
    fixed[0] = 0x44;
    fixed[page] = 0x55;
    if (!VirtualFree(fixed, 0, MEM_RELEASE))
        return 36;

    void *sentinel = mmap(nullptr, page, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (sentinel == MAP_FAILED)
        return 37;
    *static_cast<unsigned char *>(sentinel) = 0x6d;
    SetLastError(0);
    void *collision = VirtualAlloc(sentinel, page, MEM_RESERVE, PAGE_READWRITE);
    bool collision_ok = !collision && *static_cast<unsigned char *>(sentinel) == 0x6d &&
                        (GetLastError() == ERROR_INVALID_ADDRESS ||
                         GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
    munmap(sentinel, page);
    if (!collision_ok)
        return 38;

    SetLastError(0);
    if (VirtualAlloc(nullptr, 0, MEM_RESERVE, PAGE_READWRITE) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        return 39;
    if (VirtualAlloc(nullptr, page, MEM_RESERVE | 0x100000, PAGE_READWRITE) ||
        VirtualAlloc(nullptr, page, MEM_RESERVE, 0x104))
        return 40;
    if (VirtualAlloc(reinterpret_cast<void *>(std::numeric_limits<uintptr_t>::max() - page + 1),
                     page * 2, MEM_RESERVE, PAGE_READWRITE) ||
        GetLastError() != ERROR_NOT_ENOUGH_MEMORY)
        return 41;

    void *readonly = VirtualAlloc(nullptr, page, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY);
    void *noaccess = VirtualAlloc(nullptr, page, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    if (!readonly || !noaccess || access_faults(readonly, false) ||
        !access_faults(readonly, true) || !access_faults(noaccess, false))
        return 42;
    VirtualFree(readonly, 0, MEM_RELEASE);
    VirtualFree(noaccess, 0, MEM_RELEASE);

    auto *executable = static_cast<unsigned char *>(
        VirtualAlloc(nullptr, page, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!executable)
        return 43;
    const unsigned char return_42[] = {0xb8, 42, 0, 0, 0, 0xc3};
    std::memcpy(executable, return_42, sizeof(return_42));
    if (reinterpret_cast<int (*)()>(executable)() != 42 ||
        VirtualAlloc(executable, page, MEM_COMMIT, PAGE_EXECUTE_READ) != executable ||
        reinterpret_cast<int (*)()>(executable)() != 42 || !access_faults(executable, true) ||
        VirtualAlloc(executable, page, MEM_COMMIT, PAGE_EXECUTE) != executable ||
        reinterpret_cast<int (*)()>(executable)() != 42)
        return 44;
    VirtualFree(executable, 0, MEM_RELEASE);

    SetLastError(0x5678);
    std::atomic<DWORD> other_error{0};
    std::thread error_thread([&] { SetLastError(0x9abc); other_error = GetLastError(); });
    error_thread.join();
    if (GetLastError() != 0x5678 || other_error != 0x9abc)
        return 45;

    auto *concurrent = static_cast<unsigned char *>(
        VirtualAlloc(nullptr, page * 2, MEM_RESERVE, PAGE_READWRITE));
    if (!concurrent)
        return 46;
    std::atomic<bool> okay{true};
    auto cycle = [&](size_t offset) {
        for (int i = 0; i < 100; ++i) {
            if (VirtualAlloc(concurrent + offset, page, MEM_COMMIT, PAGE_READWRITE) !=
                    concurrent + offset ||
                !VirtualFree(concurrent + offset, page, MEM_DECOMMIT))
                okay = false;
        }
    };
    std::thread first(cycle, 0);
    std::thread second(cycle, page);
    for (int i = 0; i < 100; ++i)
        if (VirtualFree(concurrent + page, 0, MEM_RELEASE))
            okay = false;
    first.join();
    second.join();
    if (!VirtualFree(concurrent, 0, MEM_RELEASE) || !okay)
        return 47;

    return 0;
}

static std::vector<int> g_exit_order;
static _onexit_table_t *g_active_exit_table;

static void WINAPI exit_one() { g_exit_order.push_back(1); }
static void WINAPI exit_three() { g_exit_order.push_back(3); }
static void WINAPI exit_two() {
    g_exit_order.push_back(2);
    crt__register_onexit_function(g_active_exit_table, reinterpret_cast<void *>(exit_three));
}

static int test_platform_apis()
{
    DWORD size = 0;
    SetLastError(0);
    if (GetLogicalProcessorInformation(nullptr, &size) || !size || size % 32 ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return 50;
    std::vector<unsigned char> legacy(size, 0xa5);
    DWORD short_size = size - 1;
    if (GetLogicalProcessorInformation(legacy.data(), &short_size) || short_size != size ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || legacy != std::vector<unsigned char>(size, 0xa5))
        return 51;
    DWORD exact_size = size;
    if (!GetLogicalProcessorInformation(legacy.data(), &exact_size) || exact_size != size)
        return 52;
    auto *legacy_records = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION *>(legacy.data());
    ULONG_PTR legacy_mask = 0;
    for (size_t i = 0; i < size / sizeof(*legacy_records); ++i) {
        if (legacy_records[i].Relationship != RelationProcessorCore ||
            !legacy_records[i].ProcessorMask)
            return 53;
        legacy_mask |= legacy_records[i].ProcessorMask;
    }

    size = 0;
    SetLastError(0);
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size) ||
        !size || size % sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return 54;
    std::vector<unsigned char> extended(size);
    exact_size = size;
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, extended.data(), &exact_size))
        return 55;
    auto *records = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(extended.data());
    ULONG_PTR extended_mask = 0;
    for (size_t i = 0; i < size / sizeof(*records); ++i) {
        if (records[i].Relationship != RelationProcessorCore ||
            records[i].Size != sizeof(*records) ||
            records[i].DUMMYUNIONNAME.Processor.GroupCount != 1 ||
            !records[i].DUMMYUNIONNAME.Processor.GroupMask[0].Mask)
            return 56;
        extended_mask |= records[i].DUMMYUNIONNAME.Processor.GroupMask[0].Mask;
    }
    if (legacy_mask != extended_mask)
        return 57;
    SetLastError(0);
    if (GetLogicalProcessorInformationEx(RelationCache, nullptr, &size) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        return 58;

    LARGE_INTEGER frequency = 0, first = 0, second = 0;
    if (!QueryPerformanceFrequency(&frequency) || frequency != 1000000000LL ||
        !QueryPerformanceCounter(&first))
        return 59;
    usleep(1000);
    if (!QueryPerformanceCounter(&second) || second <= first || second - first < 500000)
        return 60;
    RaiseException(0x406d1388, 0, 0, nullptr);
    pid_t child = fork();
    if (child == 0) {
        RaiseException(0x12345678, 0, 0, nullptr);
        _exit(0);
    }
    int child_status = 0;
    if (child <= 0 || waitpid(child, &child_status, 0) != child ||
        !WIFSIGNALED(child_status) || WTERMSIG(child_status) != SIGABRT)
        return 61;

    _onexit_table_t table{};
    g_active_exit_table = &table;
    if (crt__initialize_onexit_table(&table) ||
        crt__register_onexit_function(&table, reinterpret_cast<void *>(exit_one)) ||
        crt__register_onexit_function(&table, reinterpret_cast<void *>(exit_two)) ||
        crt__initialize_onexit_table(&table) ||
        crt__execute_onexit_table(&table) || crt__execute_onexit_table(&table) ||
        g_exit_order != std::vector<int>({2, 3, 1}))
        return 62;
    if (crt__crt_atexit(exit_one))
        return 63;
    crt__cexit();
    crt__cexit();
    if (g_exit_order != std::vector<int>({2, 3, 1, 1}))
        return 64;
    return 0;
}

int main()
{
    static_assert(sizeof(BOOL) == 4);
    static_assert(sizeof(BOOLEAN) == 1);
    int result = test_allocators();
    if (result)
        return result;
    result = test_virtual_memory();
    return result ? result : test_platform_apis();
}
