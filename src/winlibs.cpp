#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <clocale>
#include <cstdarg>
#include <cctype>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <unistd.h>
#include <csignal>
#include <csetjmp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <cstdio>
#include <cmath>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/ntsync.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <vector>

#include "pe_loader.h"
#include "cpu_features.h"

// KERNEL32

enum class handle_kind {
    event_handle,
    semaphore_handle,
    thread_handle,
};

struct win_handle {
    handle_kind kind;
    void *payload;
};

struct event_handle_data {
    bool manual_reset;
    bool signaled;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int ntsync_fd;
};

struct semaphore_handle_data {
    sem_t semaphore;
    int ntsync_fd;
};

struct thread_handle_data {
    pthread_t thread;
    DWORD thread_id;
    DWORD exit_code;
    bool finished;
    bool closed;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
};

struct thread_start_info {
    DWORD (WINAPI *start_routine)(LPVOID);
    LPVOID parameter;
    thread_handle_data *thread_data;
};

static thread_local thread_start_info *g_current_thread_info;

struct system_info_stub {
    DWORD processor_architecture;
    DWORD page_size;
    void *minimum_application_address;
    void *maximum_application_address;
    ULONG_PTR active_processor_mask;
    DWORD number_of_processors;
    DWORD processor_type;
    DWORD allocation_granularity;
    WORD processor_level;
    WORD processor_revision;
};

struct processor_topology {
    DWORD logical_count;
    ULONG_PTR active_mask;
    std::vector<ULONG_PTR> core_masks;
};

static std::mutex g_tls_mutex;
static DWORD g_next_tls_index = 1;
static std::unordered_map<DWORD, pthread_key_t> g_tls_keys;
static thread_local DWORD g_last_error;
static constexpr DWORD ERROR_INSUFFICIENT_BUFFER = 122;
static constexpr size_t MAX_FLS_SLOTS = 1024;
struct fls_slot {
    PFLS_CALLBACK_FUNCTION callback;
    uint64_t generation;
    bool allocated;
};
struct fls_thread_state {
    std::array<void *, MAX_FLS_SLOTS> values{};
    std::array<uint64_t, MAX_FLS_SLOTS> generations{};
};
static std::mutex g_fls_mutex;
static std::array<fls_slot, MAX_FLS_SLOTS> g_fls_slots{};
static std::vector<fls_thread_state *> g_fls_threads;
static pthread_key_t g_fls_key;
static pthread_once_t g_fls_key_once = PTHREAD_ONCE_INIT;
static std::recursive_mutex g_onexit_mutex;
static std::condition_variable_any g_onexit_condition;
static std::unordered_set<_onexit_table_t *> g_executing_onexit_tables;
static thread_local _onexit_table_t *g_executing_onexit_table;
static _onexit_table_t g_process_onexit{};
static std::mutex g_virtual_memory_mutex;
static std::map<uintptr_t, size_t> g_virtual_reservations;
static std::once_flag g_ntsync_init_once;
static int g_ntsync_fd = -1;

static constexpr DWORD ERROR_NOT_ENOUGH_MEMORY = 8;
static constexpr DWORD ERROR_INVALID_PARAMETER = 87;
static constexpr DWORD ERROR_INVALID_ADDRESS = 487;
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
static constexpr size_t WINDOWS_ALLOCATION_GRANULARITY = 65536;

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static size_t host_page_size()
{
    static const size_t value = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return value;
}

static bool checked_add_size(size_t left, size_t right, size_t &result)
{
    if (right > std::numeric_limits<size_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

static bool checked_add_address(uintptr_t address, size_t size, uintptr_t &result)
{
    if (size > std::numeric_limits<uintptr_t>::max() - address)
        return false;
    result = address + size;
    return true;
}

static bool round_up_size(size_t value, size_t alignment, size_t &result)
{
    size_t adjusted;
    if (!checked_add_size(value, alignment - 1, adjusted))
        return false;
    result = adjusted & ~(alignment - 1);
    return true;
}

static bool round_up_address(uintptr_t value, size_t alignment, uintptr_t &result)
{
    uintptr_t adjusted;
    if (!checked_add_address(value, alignment - 1, adjusted))
        return false;
    result = adjusted & ~(static_cast<uintptr_t>(alignment) - 1);
    return true;
}

static bool windows_protection(DWORD protection, int &native)
{
    switch (protection) {
    case PAGE_NOACCESS: native = PROT_NONE; return true;
    case PAGE_READONLY: native = PROT_READ; return true;
    case PAGE_READWRITE: native = PROT_READ | PROT_WRITE; return true;
    case PAGE_EXECUTE: native = PROT_EXEC; return true;
    case PAGE_EXECUTE_READ: native = PROT_EXEC | PROT_READ; return true;
    case PAGE_EXECUTE_READWRITE: native = PROT_EXEC | PROT_READ | PROT_WRITE; return true;
    default: return false;
    }
}

static auto find_reservation(uintptr_t start, uintptr_t end)
{
    auto it = g_virtual_reservations.upper_bound(start);
    if (it == g_virtual_reservations.begin())
        return g_virtual_reservations.end();
    --it;
    uintptr_t reservation_end;
    return checked_add_address(it->first, it->second, reservation_end) &&
           start >= it->first && end <= reservation_end
        ? it : g_virtual_reservations.end();
}

static void init_ntsync()
{
    g_ntsync_fd = open("/dev/ntsync", O_CLOEXEC | O_RDONLY);
    if (g_ntsync_fd < 0) {
        fprintf(stderr, "ntsync unavailable: %s\n", strerror(errno));
    }
}

static int get_ntsync_fd()
{
    std::call_once(g_ntsync_init_once, init_ntsync);
    return g_ntsync_fd;
}

static bool ntsync_wait_single(int obj_fd, DWORD timeout_ms)
{
    uint32_t obj = static_cast<uint32_t>(obj_fd);
    ntsync_wait_args args{};
    args.timeout = timeout_ms == 0 ? 0 : UINT64_MAX;
    args.objs = reinterpret_cast<uint64_t>(&obj);
    args.count = 1;
    args.owner = 0;
    int ret = ioctl(get_ntsync_fd(), NTSYNC_IOC_WAIT_ANY, &args);
    if (ret == 0) {
        return true;
    }
    if (errno == ETIMEDOUT) {
        return false;
    }
    fprintf(stderr, "ntsync wait failed: %s\n", strerror(errno));
    return false;
}

static win_handle *make_handle(handle_kind kind, void *payload) {
    auto *handle = new win_handle{kind, payload};
    return handle;
}

static std::wstring wide_to_string(LPCWSTR value) {
    if (!value) {
        return {};
    }
    std::wstring out;
    while (*value) {
        out.push_back(static_cast<wchar_t>(*value));
        ++value;
    }
    return out;
}

static int cpu_topology_value(int cpu, const char *name)
{
    char path[128];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/%s", cpu, name);
    FILE *file = std::fopen(path, "r");
    int value = -1;
    if (file) {
        if (std::fscanf(file, "%d", &value) != 1)
            value = -1;
        std::fclose(file);
    }
    return value;
}

static const processor_topology &host_processor_topology()
{
    static const processor_topology topology = [] {
        std::vector<int> cpus;
        cpu_set_t allowed;
        CPU_ZERO(&allowed);
        if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0)
            for (int cpu = 0; cpu < CPU_SETSIZE && cpus.size() < sizeof(ULONG_PTR) * CHAR_BIT; ++cpu)
                if (CPU_ISSET(cpu, &allowed))
                    cpus.push_back(cpu);

        if (cpus.empty()) {
            long count = std::max(1L, sysconf(_SC_NPROCESSORS_ONLN));
            for (int cpu = 0; cpu < count && cpus.size() < sizeof(ULONG_PTR) * CHAR_BIT; ++cpu)
                cpus.push_back(cpu);
        }

        std::map<std::pair<int, int>, ULONG_PTR> cores;
        for (size_t bit = 0; bit < cpus.size(); ++bit) {
            int package = cpu_topology_value(cpus[bit], "physical_package_id");
            int core = cpu_topology_value(cpus[bit], "core_id");
            if (package < 0 || core < 0) {
                package = 0;
                core = cpus[bit];
            }
            cores[{package, core}] |= static_cast<ULONG_PTR>(1) << bit;
        }

        processor_topology result{};
        result.logical_count = static_cast<DWORD>(cpus.size());
        result.active_mask = cpus.size() == sizeof(ULONG_PTR) * CHAR_BIT
            ? ~static_cast<ULONG_PTR>(0)
            : (static_cast<ULONG_PTR>(1) << cpus.size()) - 1;
        for (const auto &core : cores)
            result.core_masks.push_back(core.second);
        return result;
    }();
    return topology;
}

static void finish_thread(thread_start_info *info, DWORD result)
{
    auto *thread = info->thread_data;
    pe_cleanup_current_thread();
    bool destroy_thread = false;
    pthread_mutex_lock(&thread->mutex);
    thread->exit_code = result;
    thread->finished = true;
    pthread_cond_broadcast(&thread->condition);
    destroy_thread = thread->closed;
    pthread_mutex_unlock(&thread->mutex);
    if (destroy_thread) {
        pthread_cond_destroy(&thread->condition);
        pthread_mutex_destroy(&thread->mutex);
        delete thread;
    }
    delete info;
}

static void *thread_entry(void *arg) {
    auto *info = static_cast<thread_start_info *>(arg);
    auto *thread = info->thread_data;
    g_current_thread_info = info;
    pthread_mutex_lock(&thread->mutex);
    thread->thread_id = static_cast<DWORD>(syscall(SYS_gettid));
    pthread_cond_broadcast(&thread->condition);
    pthread_mutex_unlock(&thread->mutex);
    if (!setup_nt_threadinfo(nullptr)) {
        fprintf(stderr, "thread_entry: setup_nt_threadinfo failed\n");
        std::abort();
    }
    pe_notify_loaded_images(DLL_THREAD_ATTACH);
    DWORD result = info->start_routine ? info->start_routine(info->parameter) : 0;
    g_current_thread_info = nullptr;
    finish_thread(info, result);
    return nullptr;
}

WINAPI void crt_endthreadex(unsigned result)
{
    auto *info = g_current_thread_info;
    g_current_thread_info = nullptr;
    if (info)
        finish_thread(info, result);
    else
        pe_cleanup_current_thread();
    pthread_exit(nullptr);
}

WINAPI BOOL QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount)
{
    if (!lpPerformanceCount)
        return FALSE;

    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return FALSE;
    *lpPerformanceCount = static_cast<LARGE_INTEGER>(now.tv_sec) * 1000000000LL + now.tv_nsec;
    return TRUE;
}

WINAPI BOOL QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
    if (!lpFrequency)
        return FALSE;

    *lpFrequency = 1000000000LL;
    return TRUE;
}

WINAPI HMODULE GetModuleHandleW(LPCWSTR lpModuleName) {
    // DUMMY
    return (HMODULE)1;
}

WINAPI HANDLE CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName) {
    auto *event = new event_handle_data{
        .manual_reset = bManualReset != FALSE,
        .signaled = bInitialState != FALSE,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .condition = PTHREAD_COND_INITIALIZER,
        .ntsync_fd = -1,
    };
    if (get_ntsync_fd() >= 0) {
        ntsync_event_args args{};
        args.manual = bManualReset != FALSE;
        args.signaled = bInitialState != FALSE;
        int ntsync_obj = ioctl(get_ntsync_fd(), NTSYNC_IOC_CREATE_EVENT, &args);
        if (ntsync_obj >= 0) {
            event->ntsync_fd = ntsync_obj;
        }
    }
    return reinterpret_cast<HANDLE>(make_handle(handle_kind::event_handle, event));
}

WINAPI DWORD WaitForSingleObjectEx(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable) {
    if (!hHandle) {
        return STATUS_INVALID_PARAMETER;
    }

    auto *handle = static_cast<win_handle *>(hHandle);
    if (handle->kind == handle_kind::event_handle) {
        auto *event = static_cast<event_handle_data *>(handle->payload);
        if (event->ntsync_fd >= 0 && (dwMilliseconds == 0 || dwMilliseconds == 0xffffffffu)) {
            return ntsync_wait_single(event->ntsync_fd, dwMilliseconds) ? STATUS_WAIT_0 : STATUS_TIMEOUT;
        }
        pthread_mutex_lock(&event->mutex);
        if (!event->signaled && dwMilliseconds == 0) {
            pthread_mutex_unlock(&event->mutex);
            return STATUS_TIMEOUT;
        }
        while (!event->signaled) {
            pthread_cond_wait(&event->condition, &event->mutex);
        }
        if (!event->manual_reset) {
            event->signaled = false;
        }
        pthread_mutex_unlock(&event->mutex);
        return STATUS_WAIT_0;
    }

    if (handle->kind == handle_kind::thread_handle) {
        auto *thread = static_cast<thread_handle_data *>(handle->payload);
        pthread_mutex_lock(&thread->mutex);
        if (!thread->finished && dwMilliseconds == 0) {
            pthread_mutex_unlock(&thread->mutex);
            return STATUS_TIMEOUT;
        }
        while (!thread->finished) {
            pthread_cond_wait(&thread->condition, &thread->mutex);
        }
        pthread_mutex_unlock(&thread->mutex);
        return STATUS_WAIT_0;
    }

    if (handle->kind == handle_kind::semaphore_handle) {
        auto *semaphore = static_cast<semaphore_handle_data *>(handle->payload);
        if (semaphore->ntsync_fd >= 0 && (dwMilliseconds == 0 || dwMilliseconds == 0xffffffffu)) {
            return ntsync_wait_single(semaphore->ntsync_fd, dwMilliseconds) ? STATUS_WAIT_0 : STATUS_TIMEOUT;
        }
        if (dwMilliseconds == 0) {
            return sem_trywait(&semaphore->semaphore) == 0 ? STATUS_WAIT_0 : STATUS_TIMEOUT;
        }
        sem_wait(&semaphore->semaphore);
        return STATUS_WAIT_0;
    }

    return 0;
}

WINAPI BOOL ResetEvent(HANDLE hEvent) {
    if (!hEvent) {
        return FALSE;
    }
    auto *handle = static_cast<win_handle *>(hEvent);
    if (handle->kind != handle_kind::event_handle) {
        return FALSE;
    }
    auto *event = static_cast<event_handle_data *>(handle->payload);
    if (event->ntsync_fd >= 0) {
        ioctl(event->ntsync_fd, NTSYNC_IOC_EVENT_RESET, nullptr);
    }
    pthread_mutex_lock(&event->mutex);
    event->signaled = false;
    pthread_mutex_unlock(&event->mutex);
    return TRUE;
}

WINAPI BOOL SetEvent(HANDLE hEvent) {
    if (!hEvent) {
        return FALSE;
    }
    auto *handle = static_cast<win_handle *>(hEvent);
    if (handle->kind != handle_kind::event_handle) {
        return FALSE;
    }
    auto *event = static_cast<event_handle_data *>(handle->payload);
    if (event->ntsync_fd >= 0) {
        ioctl(event->ntsync_fd, NTSYNC_IOC_EVENT_SET, nullptr);
    }
    pthread_mutex_lock(&event->mutex);
    event->signaled = true;
    if (event->manual_reset) {
        pthread_cond_broadcast(&event->condition);
    } else {
        pthread_cond_signal(&event->condition);
    }
    pthread_mutex_unlock(&event->mutex);
    return TRUE;
}

static bool initialize_critical_section(LPCRITICAL_SECTION lpCriticalSection, DWORD spin_count) {
    if (!lpCriticalSection)
        return false;
    lpCriticalSection->impl = (POSIX_CRITICAL_SECTION*)malloc(sizeof(POSIX_CRITICAL_SECTION));
    if (!lpCriticalSection->impl)
        return false;
    lpCriticalSection->impl->spin_count = spin_count;
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    int result = pthread_mutex_init(&lpCriticalSection->impl->mutex, &attributes);
    pthread_mutexattr_destroy(&attributes);
    return result == 0;
}

WINAPI void InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    initialize_critical_section(lpCriticalSection, 0);
}

// InitializeCriticalSectionAndSpinCount implementation
WINAPI uint32_t InitializeCriticalSectionAndSpinCount(
    LPCRITICAL_SECTION lpCriticalSection,
    uint32_t dwSpinCount
) {
    return initialize_critical_section(lpCriticalSection, dwSpinCount) ? 1 : 0;
}

// DeleteCriticalSection implementation
WINAPI void DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    if (lpCriticalSection && lpCriticalSection->impl) {
        pthread_mutex_destroy(&lpCriticalSection->impl->mutex);
        free(lpCriticalSection->impl);
        lpCriticalSection->impl = nullptr;
    }
}

// EnterCriticalSection implementation
WINAPI void EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    if (lpCriticalSection && lpCriticalSection->impl) {
        pthread_mutex_lock(&lpCriticalSection->impl->mutex);
    }
}

// LeaveCriticalSection implementation
WINAPI void LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    if (lpCriticalSection && lpCriticalSection->impl) {
        pthread_mutex_unlock(&lpCriticalSection->impl->mutex);
    }
}

WINAPI BOOL CloseHandle(HANDLE hObject) {
    if (!hObject || hObject == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    // CreateFileA returns (fd + 1) as a raw integer handle, not a win_handle pointer.
    // Detect these by checking if the value is too small to be a valid heap pointer.
    auto val = reinterpret_cast<uintptr_t>(hObject);
    if (val < 0x10000) {
        int fd = static_cast<int>(val) - 1;
        return close(fd) == 0 ? TRUE : FALSE;
    }
    auto *handle = static_cast<win_handle *>(hObject);
    switch (handle->kind) {
        case handle_kind::event_handle: {
            auto *event = static_cast<event_handle_data *>(handle->payload);
            if (event->ntsync_fd >= 0) {
                close(event->ntsync_fd);
            }
            pthread_cond_destroy(&event->condition);
            pthread_mutex_destroy(&event->mutex);
            delete event;
            break;
        }
        case handle_kind::semaphore_handle: {
            auto *semaphore = static_cast<semaphore_handle_data *>(handle->payload);
            if (semaphore->ntsync_fd >= 0) {
                close(semaphore->ntsync_fd);
            }
            sem_destroy(&semaphore->semaphore);
            delete semaphore;
            break;
        }
        case handle_kind::thread_handle: {
            auto *thread = static_cast<thread_handle_data *>(handle->payload);
            bool destroy_thread = false;
            pthread_mutex_lock(&thread->mutex);
            thread->closed = true;
            destroy_thread = thread->finished;
            pthread_mutex_unlock(&thread->mutex);
            if (destroy_thread) {
                pthread_cond_destroy(&thread->condition);
                pthread_mutex_destroy(&thread->mutex);
                delete thread;
            }
            break;
        }
    }
    delete handle;
    return TRUE;
}

WINAPI LPVOID TlsGetValue(DWORD dwTlsIndex) {
    std::lock_guard<std::mutex> lock(g_tls_mutex);
    auto it = g_tls_keys.find(dwTlsIndex);
    if (it == g_tls_keys.end()) {
        return nullptr;
    }
    return pthread_getspecific(it->second);
}

WINAPI DWORD TlsAlloc() {
    pthread_key_t key;
    if (pthread_key_create(&key, nullptr) != 0) {
        return 0xffffffffu;
    }
    std::lock_guard<std::mutex> lock(g_tls_mutex);
    DWORD index = g_next_tls_index++;
    g_tls_keys[index] = key;
    return index;
}

WINAPI BOOL TlsFree(DWORD dwTlsIndex) {
    std::lock_guard<std::mutex> lock(g_tls_mutex);
    auto it = g_tls_keys.find(dwTlsIndex);
    if (it == g_tls_keys.end()) {
        return FALSE;
    }
    pthread_key_delete(it->second);
    g_tls_keys.erase(it);
    return TRUE;
}

WINAPI BOOL TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue) {
    std::lock_guard<std::mutex> lock(g_tls_mutex);
    auto it = g_tls_keys.find(dwTlsIndex);
    if (it == g_tls_keys.end()) {
        return FALSE;
    }
    return pthread_setspecific(it->second, lpTlsValue) == 0 ? TRUE : FALSE;
}

static void destroy_fls_state(void *value);

static void make_fls_key()
{
    pthread_key_create(&g_fls_key, destroy_fls_state);
}

static fls_thread_state *get_fls_state()
{
    pthread_once(&g_fls_key_once, make_fls_key);
    auto *state = static_cast<fls_thread_state *>(pthread_getspecific(g_fls_key));
    if (state)
        return state;
    state = new (std::nothrow) fls_thread_state;
    if (!state)
        return nullptr;
    if (pthread_setspecific(g_fls_key, state) != 0) {
        delete state;
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_fls_mutex);
    g_fls_threads.push_back(state);
    return state;
}

void winlibs_cleanup_fls_for_current_thread()
{
    pthread_once(&g_fls_key_once, make_fls_key);
    auto *state = static_cast<fls_thread_state *>(pthread_getspecific(g_fls_key));
    if (!state)
        return;

    for (int pass = 0; pass < PTHREAD_DESTRUCTOR_ITERATIONS; ++pass) {
        std::vector<std::pair<PFLS_CALLBACK_FUNCTION, void *>> callbacks;
        {
            std::lock_guard<std::mutex> lock(g_fls_mutex);
            for (size_t i = 0; i < MAX_FLS_SLOTS; ++i) {
                auto &slot = g_fls_slots[i];
                if (!state->values[i] || !slot.allocated ||
                    state->generations[i] != slot.generation)
                    continue;
                if (slot.callback)
                    callbacks.emplace_back(slot.callback, state->values[i]);
                state->values[i] = nullptr;
            }
        }
        if (callbacks.empty())
            break;
        for (const auto &callback : callbacks)
            callback.first(callback.second);
    }

    {
        std::lock_guard<std::mutex> lock(g_fls_mutex);
        g_fls_threads.erase(std::remove(g_fls_threads.begin(), g_fls_threads.end(), state),
                            g_fls_threads.end());
    }
    pthread_setspecific(g_fls_key, nullptr);
    delete state;
}

static void destroy_fls_state(void *value)
{
    if (!value)
        return;
    pthread_setspecific(g_fls_key, value);
    winlibs_cleanup_fls_for_current_thread();
}

WINAPI DWORD FlsAlloc(PFLS_CALLBACK_FUNCTION callback) {
    std::lock_guard<std::mutex> lock(g_fls_mutex);
    for (DWORD i = 0; i < MAX_FLS_SLOTS; ++i) {
        auto &slot = g_fls_slots[i];
        if (slot.allocated)
            continue;
        slot.allocated = true;
        slot.callback = callback;
        ++slot.generation;
        return i;
    }
    return 0xffffffffu;
}

WINAPI BOOL FlsFree(DWORD index) {
    if (index >= MAX_FLS_SLOTS)
        return FALSE;
    std::vector<std::pair<PFLS_CALLBACK_FUNCTION, void *>> callbacks;
    {
        std::lock_guard<std::mutex> lock(g_fls_mutex);
        auto &slot = g_fls_slots[index];
        if (!slot.allocated)
            return FALSE;
        for (auto *state : g_fls_threads) {
            if (!state->values[index] || state->generations[index] != slot.generation)
                continue;
            if (slot.callback)
                callbacks.emplace_back(slot.callback, state->values[index]);
            state->values[index] = nullptr;
        }
        slot.allocated = false;
        slot.callback = nullptr;
    }
    for (const auto &callback : callbacks)
        callback.first(callback.second);
    return TRUE;
}

WINAPI BOOL FlsSetValue(DWORD index, LPVOID value) {
    if (index >= MAX_FLS_SLOTS)
        return FALSE;
    auto *state = get_fls_state();
    if (!state)
        return FALSE;
    std::lock_guard<std::mutex> lock(g_fls_mutex);
    auto &slot = g_fls_slots[index];
    if (!slot.allocated)
        return FALSE;
    state->values[index] = value;
    state->generations[index] = slot.generation;
    return TRUE;
}

WINAPI LPVOID FlsGetValue(DWORD index) {
    if (index >= MAX_FLS_SLOTS)
        return nullptr;
    auto *state = get_fls_state();
    if (!state)
        return nullptr;
    std::lock_guard<std::mutex> lock(g_fls_mutex);
    auto &slot = g_fls_slots[index];
    return slot.allocated && state->generations[index] == slot.generation
        ? state->values[index] : nullptr;
}

WINAPI void GetSystemInfo(void *lpSystemInfo) {
    if (!lpSystemInfo) {
        return;
    }
    auto *info = static_cast<system_info_stub *>(lpSystemInfo);
    std::memset(info, 0, sizeof(*info));
    info->page_size = static_cast<DWORD>(sysconf(_SC_PAGESIZE));
    const auto &topology = host_processor_topology();
    info->number_of_processors = topology.logical_count;
    info->allocation_granularity = 65536;
    info->active_processor_mask = topology.active_mask;
}

WINAPI BOOL GetLogicalProcessorInformation(void *buffer, DWORD *return_length) {
    if (!return_length) {
        g_last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    const auto &topology = host_processor_topology();
    DWORD supplied = *return_length;
    DWORD required = static_cast<DWORD>(topology.core_masks.size() *
                                         sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    *return_length = required;
    if (!buffer || supplied < required) {
        g_last_error = ERROR_INSUFFICIENT_BUFFER;
        return FALSE;
    }

    auto *infos = static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION *>(buffer);
    for (size_t i = 0; i < topology.core_masks.size(); ++i) {
        infos[i] = {};
        infos[i].ProcessorMask = topology.core_masks[i];
        infos[i].Relationship = RelationProcessorCore;
        infos[i].DUMMYUNIONNAME.ProcessorCore.Flags =
            __builtin_popcountl(topology.core_masks[i]) > 1 ? LTP_PC_SMT : 0;
    }
    return TRUE;
}

WINAPI BOOL GetLogicalProcessorInformationEx(INT relationship, void *buffer, DWORD *return_length) {
    if (!return_length) {
        g_last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    if (relationship != RelationProcessorCore) {
        g_last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    const auto &topology = host_processor_topology();
    DWORD supplied = *return_length;
    DWORD required = static_cast<DWORD>(topology.core_masks.size() *
                                         sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX));
    *return_length = required;
    if (!buffer || supplied < required) {
        g_last_error = ERROR_INSUFFICIENT_BUFFER;
        return FALSE;
    }

    auto *records = static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(buffer);
    for (size_t i = 0; i < topology.core_masks.size(); ++i) {
        records[i] = {};
        records[i].Relationship = RelationProcessorCore;
        records[i].Size = sizeof(records[i]);
        records[i].DUMMYUNIONNAME.Processor.Flags =
            __builtin_popcountl(topology.core_masks[i]) > 1 ? LTP_PC_SMT : 0;
        records[i].DUMMYUNIONNAME.Processor.GroupCount = 1;
        records[i].DUMMYUNIONNAME.Processor.GroupMask[0].Mask = topology.core_masks[i];
    }
    return TRUE;
}

WINAPI BOOL FreeLibrary(HMODULE hLibModule) {
    return TRUE;
}

WINAPI HMODULE LoadLibraryA(LPCSTR lpLibFileName) {
    return reinterpret_cast<HMODULE>(1);
}

WINAPI void OutputDebugStringA(LPCSTR lpOutputString) {
    if (lpOutputString) {
        fprintf(stderr, "%s", lpOutputString);
    }
}

WINAPI HANDLE GetCurrentProcess() {
    return reinterpret_cast<HANDLE>(-1);
}

WINAPI DWORD GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    if (!lpFilename || nSize == 0) {
        return 0;
    }
    char buffer[PATH_MAX] = {};
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        return 0;
    }
    size_t copy_len = std::min(static_cast<size_t>(len), static_cast<size_t>(nSize - 1));
    std::memcpy(lpFilename, buffer, copy_len);
    lpFilename[copy_len] = '\0';
    return static_cast<DWORD>(copy_len);
}

WINAPI HANDLE GetCurrentThread() {
    return reinterpret_cast<HANDLE>(-2);
}

WINAPI DWORD SetThreadIdealProcessor(HANDLE hThread, DWORD dwIdealProcessor) {
    return dwIdealProcessor;
}

WINAPI BOOL CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes) {
    std::error_code ec;
    return std::filesystem::create_directories(std::filesystem::path(wide_to_string(lpPathName)), ec) || !ec ? TRUE : FALSE;
}

WINAPI BOOL DeleteFileW(LPCWSTR lpFileName) {
    std::error_code ec;
    return std::filesystem::remove(std::filesystem::path(wide_to_string(lpFileName)), ec) ? TRUE : FALSE;
}

WINAPI HANDLE FindFirstFileW(LPCWSTR lpFileName, void *lpFindFileData) {
    return INVALID_HANDLE_VALUE;
}

WINAPI BOOL FindNextFileW(HANDLE hFindFile, void *lpFindFileData) {
    return FALSE;
}

WINAPI BOOL FindClose(HANDLE hFindFile) {
    return TRUE;
}

WINAPI DWORD GetFileAttributesW(LPCWSTR lpFileName) {
    std::error_code ec;
    auto status = std::filesystem::status(std::filesystem::path(wide_to_string(lpFileName)), ec);
    return ec ? 0xffffffffu : 0;
}

WINAPI DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    return WaitForSingleObjectEx(hHandle, dwMilliseconds, FALSE);
}

WINAPI HANDLE CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, DWORD (WINAPI *lpStartAddress)(LPVOID), LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId) {
    auto *thread = new thread_handle_data{.thread = {}, .thread_id = 0, .exit_code = STILL_ACTIVE, .finished = false, .closed = false, .mutex = PTHREAD_MUTEX_INITIALIZER, .condition = PTHREAD_COND_INITIALIZER};
    auto *info = new thread_start_info{lpStartAddress, lpParameter, thread};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    size_t requested_stack = dwStackSize != 0 ? static_cast<size_t>(dwStackSize) : (32u << 20);
    if (requested_stack < (32u << 20)) {
        requested_stack = (32u << 20);
    }
    pthread_attr_setstacksize(&attr, requested_stack);
    if (pthread_create(&thread->thread, &attr, thread_entry, info) != 0) {
        pthread_attr_destroy(&attr);
        delete info;
        pthread_cond_destroy(&thread->condition);
        delete thread;
        return nullptr;
    }
    pthread_attr_destroy(&attr);
    pthread_detach(thread->thread);
    if (lpThreadId) {
        pthread_mutex_lock(&thread->mutex);
        while (thread->thread_id == 0) {
            pthread_cond_wait(&thread->condition, &thread->mutex);
        }
        *lpThreadId = thread->thread_id;
        pthread_mutex_unlock(&thread->mutex);
    }
    return reinterpret_cast<HANDLE>(make_handle(handle_kind::thread_handle, thread));
}

WINAPI BOOL GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode) {
    if (!hThread || !lpExitCode) {
        return FALSE;
    }
    auto *handle = static_cast<win_handle *>(hThread);
    if (handle->kind != handle_kind::thread_handle) {
        return FALSE;
    }
    auto *thread = static_cast<thread_handle_data *>(handle->payload);
    pthread_mutex_lock(&thread->mutex);
    *lpExitCode = thread->finished ? thread->exit_code : STILL_ACTIVE;
    pthread_mutex_unlock(&thread->mutex);
    return TRUE;
}

WINAPI BOOL ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount) {
    auto *handle = static_cast<win_handle *>(hSemaphore);
    if (!handle || handle->kind != handle_kind::semaphore_handle) {
        return FALSE;
    }
    auto *semaphore = static_cast<semaphore_handle_data *>(handle->payload);
    if (semaphore->ntsync_fd >= 0) {
        uint32_t count = static_cast<uint32_t>(lReleaseCount);
        ioctl(semaphore->ntsync_fd, NTSYNC_IOC_SEM_RELEASE, &count);
    }
    for (LONG i = 0; i < lReleaseCount; ++i) {
        sem_post(&semaphore->semaphore);
    }
    if (lpPreviousCount) {
        *lpPreviousCount = 0;
    }
    return TRUE;
}

WINAPI HANDLE CreateSemaphoreW(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCWSTR lpName) {
    auto *semaphore = new semaphore_handle_data{};
    semaphore->ntsync_fd = -1;
    if (sem_init(&semaphore->semaphore, 0, lInitialCount) != 0) {
        fprintf(stderr, "CreateSemaphoreW sem_init failed errno=%d\n", errno);
        delete semaphore;
        return nullptr;
    }
    if (get_ntsync_fd() >= 0) {
        ntsync_sem_args args{};
        args.count = static_cast<uint32_t>(lInitialCount);
        args.max = static_cast<uint32_t>(lMaximumCount);
        int ntsync_obj = ioctl(get_ntsync_fd(), NTSYNC_IOC_CREATE_SEM, &args);
        if (ntsync_obj >= 0) {
            semaphore->ntsync_fd = ntsync_obj;
        }
    }
    return reinterpret_cast<HANDLE>(make_handle(handle_kind::semaphore_handle, semaphore));
}

WINAPI HANDLE CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    return reinterpret_cast<HANDLE>(1);
}

WINAPI BOOL FlushFileBuffers(HANDLE hFile) { return TRUE; }
WINAPI DWORD SetFilePointer(HANDLE hFile, LONG lDistanceToMove, LONG *lpDistanceToMoveHigh, DWORD dwMoveMethod) { return 0; }
WINAPI BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPVOID lpOverlapped) {
    if (lpNumberOfBytesWritten) {
        *lpNumberOfBytesWritten = nNumberOfBytesToWrite;
    }
    return TRUE;
}
WINAPI ULONGLONG VerSetConditionMask(ULONGLONG ConditionMask, DWORD TypeMask, BYTE Condition) { return ConditionMask; }
WINAPI BOOL SetHandleInformation(HANDLE hObject, DWORD dwMask, DWORD dwFlags) { return TRUE; }
WINAPI BOOL VerifyVersionInfoW(LPVOID lpVersionInformation, DWORD dwTypeMask, ULONGLONG dwlConditionMask) { return TRUE; }
WINAPI PVOID DecodePointer(PVOID Ptr) { return Ptr; }
WINAPI PVOID EncodePointer(PVOID Ptr) { return Ptr; }

WINAPI void RtlCaptureContext(PCONTEXT Context) {
    // DUMMY
}

WINAPI PRUNTIME_FUNCTION RtlLookupFunctionEntry(DWORD64 ControlPc, PDWORD64 ImageBase, PUNWIND_HISTORY_TABLE HistoryTable) {
    // DUMMY
    return nullptr;
}

WINAPI PRUNTIME_FUNCTION RtlVirtualUnwind(DWORD HandlerType, DWORD64 ImageBase, DWORD64 ControlPc,
                                    PRUNTIME_FUNCTION FunctionEntry, PCONTEXT Context,
                                    PVOID* HandlerData, PDWORD64 EstablisherFrame,
                                    PVOID ContextPointers) {
    // DUMMY
    return nullptr;
}

WINAPI BOOL IsDebuggerPresent() {
    // DUMMY
    return FALSE;
}

WINAPI LONG UnhandledExceptionFilter(EXCEPTION_POINTERS* ExceptionInfo) {
    // DUMMY
    return 0;
}

WINAPI LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter) {
    // DUMMY
    return nullptr;
}

WINAPI NTSTATUS IsProcessorFeaturePresent( uint32_t feature )
{
    return has_processor_feature(feature);
}

WINAPI DWORD GetCurrentProcessId() {
    return getpid();
}

WINAPI DWORD GetCurrentThreadId() {
    return syscall(SYS_gettid);
}

WINAPI PSLIST_HEADER InitializeSListHead(PSLIST_HEADER ListHead) {
    // DUMMY
    return nullptr;
}

WINAPI BOOL DisableThreadLibraryCalls(HMODULE hModule) {
    if (pe_disable_thread_library_calls(hModule))
        return TRUE;
    g_last_error = ERROR_INVALID_PARAMETER;
    return FALSE;
}

[[noreturn]] static void unsupported_msvc_exception(const char *symbol, DWORD code = 0)
{
    std::fprintf(stderr, "%s: unsupported MSVC exception boundary (code 0x%08x, caller %p)\n",
                 symbol, code, __builtin_return_address(0));
    std::abort();
}

WINAPI void RaiseException(DWORD code, DWORD flags, DWORD argument_count,
                           const ULONG_PTR *arguments)
{
    (void)flags;
    (void)argument_count;
    (void)arguments;
    // MSVC uses this debugger-only exception to assign a thread name.
    if (code == 0x406d1388)
        return;
    unsupported_msvc_exception("RaiseException", code);
}

// Windows FILETIME is 100-nanosecond intervals since January 1, 1601
// Unix epoch is January 1, 1970
#define WINDOWS_TICK 10000000ULL
#define SEC_TO_UNIX_EPOCH 11644473600ULL

WINAPI void GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime) {
    timeval tv;
    gettimeofday(&tv, NULL);

    // Convert to Windows FILETIME format
    uint64_t time_to_win_epoch = ((uint64_t)tv.tv_sec + SEC_TO_UNIX_EPOCH) * WINDOWS_TICK;
    time_to_win_epoch += (uint64_t)tv.tv_usec * 10; // Convert microseconds to 100-nanosecond intervals

    // Split 64-bit time into two 32-bit values
    lpSystemTimeAsFileTime->dwLowDateTime = (uint32_t)(time_to_win_epoch & 0xFFFFFFFF);
    lpSystemTimeAsFileTime->dwHighDateTime = (uint32_t)(time_to_win_epoch >> 32);
}

WINAPI FARPROC GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    return (FARPROC)get_export(lpProcName);
}

// VCRUNTIME140

WINAPI void* vcruntime_memmove(void* dest, const void* src, size_t n) {
    return std::memmove(dest, src, n);
}

WINAPI void* vcruntime_memcpy(void* dest, const void* src, size_t n) {
    return std::memcpy(dest, src, n);
}

WINAPI void* vcruntime_memchr(const void *ptr, int value, size_t count) {
    return const_cast<void *>(std::memchr(ptr, value, count));
}

WINAPI EXCEPTION_DISPOSITION vcruntime___CxxFrameHandler3(
    EXCEPTION_RECORD *record, void *establisher_frame, CONTEXT *context,
    DISPATCHER_CONTEXT *dispatcher) {
    (void)establisher_frame;
    (void)context;
    (void)dispatcher;
    unsupported_msvc_exception("__CxxFrameHandler3", record ? record->ExceptionCode : 0);
}

WINAPI EXCEPTION_DISPOSITION vcruntime___CxxFrameHandler4(
    EXCEPTION_RECORD *record, void *establisher_frame, CONTEXT *context,
    DISPATCHER_CONTEXT *dispatcher) {
    (void)establisher_frame;
    (void)context;
    (void)dispatcher;
    unsupported_msvc_exception("__CxxFrameHandler4", record ? record->ExceptionCode : 0);
}

WINAPI void vcruntime___std_terminate() {
    std::terminate();
}

WINAPI void vcruntime__purecall() {
    std::terminate();
}

WINAPI EXCEPTION_DISPOSITION vcruntime___C_specific_handler(
    EXCEPTION_RECORD* ExceptionRecord, void* EstablisherFrame,
    CONTEXT* ContextRecord, DISPATCHER_CONTEXT* DispatcherContext) {
    (void)EstablisherFrame;
    (void)ContextRecord;
    (void)DispatcherContext;
    unsupported_msvc_exception("__C_specific_handler",
                               ExceptionRecord ? ExceptionRecord->ExceptionCode : 0);
}

WINAPI void* vcruntime_memset(void* dest, int val, size_t n) {
    return std::memset(dest, val, n);
}

WINAPI void vcruntime___std_exception_copy(const std::exception_ptr* from, std::exception_ptr* to) {
    *to = *from;
}

WINAPI void vcruntime___std_exception_destroy(std::exception_ptr* ptr) {
    *ptr = nullptr;
}

WINAPI void vcruntime___std_type_info_destroy_list(void** list) {
    // DUMMY
}

WINAPI void vcruntime__CxxThrowException(void* pExceptionObject, void* pThrowInfo) {
    (void)pExceptionObject;
    (void)pThrowInfo;
    unsupported_msvc_exception("_CxxThrowException", 0xe06d7363);
}

WINAPI void *vcruntime___current_exception() {
    unsupported_msvc_exception("__current_exception");
}

WINAPI void *vcruntime___current_exception_context() {
    unsupported_msvc_exception("__current_exception_context");
}

// CRT

static bool checked_mul_size(size_t left, size_t right, size_t &result)
{
    if (left && right > std::numeric_limits<size_t>::max() / left) {
        errno = ENOMEM;
        return false;
    }
    result = left * right;
    return true;
}

WINAPI void *crt_malloc(size_t size) {
    return malloc(size);
}

WINAPI int crt__callnewh(size_t size) {
    return 1;
}

WINAPI void crt_free(void *ptr) {
    free(ptr);
}

using pe_qsort_compare_t = int (WINAPI *)(const void*, const void*);

static thread_local pe_qsort_compare_t current_qsort_compare = nullptr;

using ms_va_list = const unsigned char *;

template <typename T>
static T read_ms_va_arg(ms_va_list &cursor) {
    uint64_t slot = 0;
    memcpy(&slot, cursor, sizeof(slot));
    cursor += sizeof(slot);
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(static_cast<uintptr_t>(slot));
    } else {
        T value{};
        memcpy(&value, &slot, sizeof(T));
        return value;
    }
}

static void append_output_char(char *buffer, size_t buffer_count, size_t &written, char ch) {
    if (buffer_count != 0 && written + 1 < buffer_count) {
        buffer[written] = ch;
    }
    ++written;
    if (buffer_count != 0) {
        buffer[written < buffer_count ? written : (buffer_count - 1)] = '\0';
    }
}

template <typename... Args>
static void append_output_format(char *buffer, size_t buffer_count, size_t &written, const std::string &format, Args... args) {
    char dummy[1] = {'\0'};
    char *dest = dummy;
    size_t available = 0;
    if (buffer_count != 0 && written < buffer_count) {
        dest = buffer + written;
        available = buffer_count - written;
    }

    int result = std::snprintf(dest, available, format.c_str(), args...);
    if (result < 0) {
        return;
    }

    written += static_cast<size_t>(result);
    if (buffer_count != 0) {
        buffer[written < buffer_count ? written : (buffer_count - 1)] = '\0';
    }
}

static std::string narrow_utf16_string(const WCHAR *value) {
    if (!value) {
        return {};
    }

    std::string out;
    while (*value) {
        uint16_t code_unit = *value++;
        if (code_unit <= 0x7f) {
            out.push_back(static_cast<char>(code_unit));
        } else if (code_unit <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (code_unit >> 6)));
            out.push_back(static_cast<char>(0x80 | (code_unit & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xe0 | (code_unit >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code_unit >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code_unit & 0x3f)));
        }
    }
    return out;
}

static int format_from_ms_va_list(char *buffer, size_t buffer_count, const char *format, ms_va_list raw_args) {
    if (!format) {
        return -1;
    }
    if (buffer_count != 0 && buffer) {
        buffer[0] = '\0';
    }

    ms_va_list args = raw_args;
    size_t written = 0;
    for (const char *cursor = format; *cursor; ) {
        if (*cursor != '%') {
            append_output_char(buffer, buffer_count, written, *cursor++);
            continue;
        }

        const char *directive_start = cursor++;
        if (*cursor == '%') {
            append_output_char(buffer, buffer_count, written, *cursor++);
            continue;
        }

        std::string flags;
        while (*cursor && strchr("-+ #0'", *cursor)) {
            flags.push_back(*cursor++);
        }

        bool width_from_args = false;
        std::string width;
        if (*cursor == '*') {
            width_from_args = true;
            width.push_back(*cursor++);
        } else {
            while (*cursor && std::isdigit(static_cast<unsigned char>(*cursor))) {
                width.push_back(*cursor++);
            }
        }

        bool precision_from_args = false;
        std::string precision;
        if (*cursor == '.') {
            precision.push_back(*cursor++);
            if (*cursor == '*') {
                precision_from_args = true;
                precision.push_back(*cursor++);
            } else {
                while (*cursor && std::isdigit(static_cast<unsigned char>(*cursor))) {
                    precision.push_back(*cursor++);
                }
            }
        }

        std::string length;
        if (cursor[0] == 'h' && cursor[1] == 'h') {
            length = "hh";
            cursor += 2;
        } else if (cursor[0] == 'l' && cursor[1] == 'l') {
            length = "ll";
            cursor += 2;
        } else if (cursor[0] == 'I' && cursor[1] == '6' && cursor[2] == '4') {
            length = "I64";
            cursor += 3;
        } else if (cursor[0] == 'I' && cursor[1] == '3' && cursor[2] == '2') {
            length = "I32";
            cursor += 3;
        } else if (*cursor && strchr("hljztLIw", *cursor)) {
            length.push_back(*cursor++);
        }

        char spec = *cursor;
        if (!spec) {
            while (*directive_start) {
                append_output_char(buffer, buffer_count, written, *directive_start++);
            }
            break;
        }
        ++cursor;

        int dynamic_width = width_from_args ? static_cast<int>(read_ms_va_arg<uint64_t>(args)) : 0;
        int dynamic_precision = precision_from_args ? static_cast<int>(read_ms_va_arg<uint64_t>(args)) : 0;

        std::string native_format = "%" + flags + width + precision;
        auto append_integer = [&](bool is_signed) {
            native_format += is_signed ? "ll" : "ll";
            native_format.push_back(spec);
            if (is_signed) {
                long long value = static_cast<long long>(read_ms_va_arg<uint64_t>(args));
                if (width_from_args && precision_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_width, dynamic_precision, value);
                } else if (width_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_width, value);
                } else if (precision_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_precision, value);
                } else {
                    append_output_format(buffer, buffer_count, written, native_format, value);
                }
            } else {
                unsigned long long value = read_ms_va_arg<uint64_t>(args);
                if (width_from_args && precision_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_width, dynamic_precision, value);
                } else if (width_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_width, value);
                } else if (precision_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_precision, value);
                } else {
                    append_output_format(buffer, buffer_count, written, native_format, value);
                }
            }
        };

        switch (spec) {
            case 'd':
            case 'i':
                append_integer(true);
                break;
            case 'u':
            case 'o':
            case 'x':
            case 'X':
                append_integer(false);
                break;
            case 'p': {
                native_format.push_back('p');
                void *value = read_ms_va_arg<void *>(args);
                if (width_from_args && precision_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_width, dynamic_precision, value);
                } else if (width_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_width, value);
                } else if (precision_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_precision, value);
                } else {
                    append_output_format(buffer, buffer_count, written, native_format, value);
                }
                break;
            }
            case 'c': {
                int value = static_cast<int>(read_ms_va_arg<uint64_t>(args));
                if (length == "l") {
                    std::string wide_char = narrow_utf16_string(reinterpret_cast<const WCHAR *>(&value));
                    std::string string_format = "%" + flags + width + precision + "s";
                    if (width_from_args && precision_from_args) {
                        append_output_format(buffer, buffer_count, written, string_format, dynamic_width, dynamic_precision, wide_char.c_str());
                    } else if (width_from_args) {
                        append_output_format(buffer, buffer_count, written, string_format, dynamic_width, wide_char.c_str());
                    } else if (precision_from_args) {
                        append_output_format(buffer, buffer_count, written, string_format, dynamic_precision, wide_char.c_str());
                    } else {
                        append_output_format(buffer, buffer_count, written, string_format, wide_char.c_str());
                    }
                } else {
                    native_format.push_back('c');
                    if (width_from_args && precision_from_args) {
                        append_output_format(buffer, buffer_count, written, native_format, dynamic_width, dynamic_precision, value);
                    } else if (width_from_args) {
                        append_output_format(buffer, buffer_count, written, native_format, dynamic_width, value);
                    } else if (precision_from_args) {
                        append_output_format(buffer, buffer_count, written, native_format, dynamic_precision, value);
                    } else {
                        append_output_format(buffer, buffer_count, written, native_format, value);
                    }
                }
                break;
            }
            case 's': {
                const char *value = nullptr;
                std::string converted;
                if (length == "l" || length == "w") {
                    converted = narrow_utf16_string(read_ms_va_arg<const WCHAR *>(args));
                    value = converted.c_str();
                } else {
                    value = read_ms_va_arg<const char *>(args);
                }
                native_format.push_back('s');
                if (!value) {
                    value = "(null)";
                }
                if (width_from_args && precision_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_width, dynamic_precision, value);
                } else if (width_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_width, value);
                } else if (precision_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_precision, value);
                } else {
                    append_output_format(buffer, buffer_count, written, native_format, value);
                }
                break;
            }
            case 'a':
            case 'A':
            case 'e':
            case 'E':
            case 'f':
            case 'F':
            case 'g':
            case 'G': {
                native_format.push_back(spec);
                double value = read_ms_va_arg<double>(args);
                if (width_from_args && precision_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_width, dynamic_precision, value);
                } else if (width_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_width, value);
                } else if (precision_from_args) {
                    append_output_format(buffer, buffer_count, written, native_format, dynamic_precision, value);
                } else {
                    append_output_format(buffer, buffer_count, written, native_format, value);
                }
                break;
            }
            case 'n': {
                uint64_t current_count = static_cast<uint64_t>(written);
                if (length == "hh") {
                    if (auto *dest = read_ms_va_arg<signed char *>(args)) {
                        *dest = static_cast<signed char>(current_count);
                    }
                } else if (length == "h") {
                    if (auto *dest = read_ms_va_arg<short *>(args)) {
                        *dest = static_cast<short>(current_count);
                    }
                } else if (length == "ll" || length == "I64") {
                    if (auto *dest = read_ms_va_arg<long long *>(args)) {
                        *dest = static_cast<long long>(current_count);
                    }
                } else if (length == "z") {
                    if (auto *dest = read_ms_va_arg<size_t *>(args)) {
                        *dest = static_cast<size_t>(current_count);
                    }
                } else if (length == "t") {
                    if (auto *dest = read_ms_va_arg<ptrdiff_t *>(args)) {
                        *dest = static_cast<ptrdiff_t>(current_count);
                    }
                } else {
                    if (auto *dest = read_ms_va_arg<int *>(args)) {
                        *dest = static_cast<int>(current_count);
                    }
                }
                break;
            }
            default:
                for (const char *literal = directive_start; literal != cursor; ++literal) {
                    append_output_char(buffer, buffer_count, written, *literal);
                }
                break;
        }
    }

    return static_cast<int>(written);
}

static int crt_qsort_adapter(const void* lhs, const void* rhs) {
    return current_qsort_compare(lhs, rhs);
}

WINAPI void crt_qsort(void* base, size_t num, size_t size, pe_qsort_compare_t compar) {
    current_qsort_compare = compar;
    std::qsort(base, num, size, crt_qsort_adapter);
    current_qsort_compare = nullptr;
}

WINAPI int crt___stdio_common_vsprintf(uint64_t _Options, char* _Buffer, size_t _BufferCount,
                             const char* _Format, _locale_t _Locale, const void *_ArgList) {
    return format_from_ms_va_list(_Buffer, _BufferCount, _Format, static_cast<ms_va_list>(_ArgList));
}

WINAPI int crt___stdio_common_vsnprintf_s(uint64_t options, char *buffer, size_t buffer_count,
                                          size_t max_count, const char *format,
                                          _locale_t locale, const void *args) {
    (void)options;
    (void)max_count;
    (void)locale;
    return format_from_ms_va_list(buffer, buffer_count, format, static_cast<ms_va_list>(args));
}

WINAPI int crt__seh_filter_dll(unsigned long _ExceptionCode, struct _EXCEPTION_POINTERS* _ExceptionPointers) {
    // DUMMY
    return 0;
}

WINAPI int crt__execute_onexit_table(void *table);
WINAPI int crt__register_onexit_function(void *table, void *func);

WINAPI void crt__cexit() {
    crt__execute_onexit_table(&g_process_onexit);
}

WINAPI int crt__crt_atexit(_PVFV func) {
    return crt__register_onexit_function(&g_process_onexit,
                                          reinterpret_cast<void *>(func));
}

WINAPI int crt__execute_onexit_table(void* raw_table) {
    if (!raw_table)
        return -1;
    auto *table = static_cast<_onexit_table_t *>(raw_table);
    {
        std::unique_lock<std::recursive_mutex> lock(g_onexit_mutex);
        if (g_executing_onexit_table == table)
            return 0;
        g_onexit_condition.wait(lock, [table] {
            return g_executing_onexit_tables.count(table) == 0;
        });
        g_executing_onexit_tables.insert(table);
    }
    auto *previous_table = g_executing_onexit_table;
    g_executing_onexit_table = table;
    for (;;) {
        _PVFV callback = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(g_onexit_mutex);
            if (table->_first && table->_last > table->_first)
                callback = *--table->_last;
            else {
                std::free(table->_first);
                *table = {};
                break;
            }
        }
        if (callback)
            callback();
    }
    g_executing_onexit_table = previous_table;
    {
        std::lock_guard<std::recursive_mutex> lock(g_onexit_mutex);
        g_executing_onexit_tables.erase(table);
    }
    g_onexit_condition.notify_all();
    return 0;
}

WINAPI int crt__register_onexit_function(void* raw_table, void* raw_func) {
    if (!raw_table || !raw_func)
        return -1;
    auto *table = static_cast<_onexit_table_t *>(raw_table);
    auto func = reinterpret_cast<_PVFV>(raw_func);
    std::lock_guard<std::recursive_mutex> lock(g_onexit_mutex);
    size_t count = table->_first ? static_cast<size_t>(table->_last - table->_first) : 0;
    size_t capacity = table->_first ? static_cast<size_t>(table->_end - table->_first) : 0;
    if (count == capacity) {
        size_t new_capacity = capacity ? capacity * 2 : 32;
        auto *callbacks = static_cast<_PVFV *>(
            std::realloc(table->_first, new_capacity * sizeof(_PVFV)));
        if (!callbacks)
            return -1;
        table->_first = callbacks;
        table->_last = callbacks + count;
        table->_end = callbacks + new_capacity;
    }
    *table->_last++ = func;
    return 0;
}

WINAPI int crt__initialize_onexit_table(void* raw_table) {
    if (!raw_table)
        return -1;
    auto *table = static_cast<_onexit_table_t *>(raw_table);
    std::lock_guard<std::recursive_mutex> lock(g_onexit_mutex);
    if (table->_first || table->_last || table->_end)
        return 0;
    *table = {};
    return 0;
}

WINAPI int crt__initialize_narrow_environment() {
    // DUMMY
    return 0;
}

WINAPI int crt__configure_narrow_argv(int mode) {
    // DUMMY
    return 0;
}

WINAPI int crt__initterm_e(void (**table)(void), void (**end)(void)) {
    using initterm_e_fn = int (WINAPI *)(void);

    if (!table || !end) {
        return 0;
    }

    auto current = reinterpret_cast<initterm_e_fn *>(table);
    auto last = reinterpret_cast<initterm_e_fn *>(end);

    while (current < last) {
        auto fn = *current;
        if (fn) {
            int result = fn();
            if (result != 0) {
                return result;
            }
        }
        ++current;
    }

    return 0;
}

WINAPI void crt__initterm(void (**table)(void), void (**end)(void)) {
    using initterm_fn = void (WINAPI *)(void);

    if (!table || !end) {
        return;
    }

    auto current = reinterpret_cast<initterm_fn *>(table);
    auto last = reinterpret_cast<initterm_fn *>(end);

    while (current < last) {
        auto fn = *current;
        if (fn) {
            fn();
        }
        ++current;
    }
}

WINAPI float crt_floorf(float x) {
    return std::floor(x);
}

WINAPI float crt_roundf(float x) {
    return std::round(x);
}

WINAPI float crt_fmodf(float x, float divisor) {
    return std::fmod(x, divisor);
}

WINAPI float crt_cosf(float x) {
    return std::cos(x);
}

WINAPI float crt_ceilf(float x) {
    return std::ceil(x);
}

WINAPI float crt_sqrtf(float x) {
    return std::sqrt(x);
}

WINAPI void msvcp__Xbad_function_call() { std::terminate(); }
WINAPI const char *msvcp__Winerror_map(int err) { return strerror(err); }
WINAPI const char *msvcp__Syserror_map(int err) { return strerror(err); }
WINAPI void msvcp__Xout_of_range(const char *msg) { (void)msg; unsupported_msvc_exception("_Xout_of_range"); }
WINAPI void msvcp__Xlength_error(const char *msg) { (void)msg; unsupported_msvc_exception("_Xlength_error"); }
WINAPI void msvcp__Thrd_yield() { sched_yield(); }
WINAPI void msvcp__Xbad_alloc() { unsupported_msvc_exception("_Xbad_alloc"); }

WINAPI void msvcr_operator_delete_array(void *ptr) { crt_free(ptr); }
WINAPI int msvcr_sprintf_s(char *buffer, size_t sizeOfBuffer, const char *format, ULONG_PTR a1 = 0, ULONG_PTR a2 = 0, ULONG_PTR a3 = 0, ULONG_PTR a4 = 0, ULONG_PTR a5 = 0, ULONG_PTR a6 = 0, ULONG_PTR a7 = 0, ULONG_PTR a8 = 0) {
    return snprintf(buffer, sizeOfBuffer, format, a1, a2, a3, a4, a5, a6, a7, a8);
}
WINAPI int msvcr_printf(const char *format, ULONG_PTR a1 = 0, ULONG_PTR a2 = 0, ULONG_PTR a3 = 0, ULONG_PTR a4 = 0, ULONG_PTR a5 = 0, ULONG_PTR a6 = 0, ULONG_PTR a7 = 0, ULONG_PTR a8 = 0) {
    return printf(format, a1, a2, a3, a4, a5, a6, a7, a8);
}
WINAPI void *msvcr_operator_new(size_t size) {
    void *ptr = crt_malloc(size ? size : 1);
    if (!ptr)
        std::abort();
    return ptr;
}
WINAPI void msvcr_context_yield() { sched_yield(); }
WINAPI int msvcr_sprintf(char *buffer, const char *format, ULONG_PTR a1 = 0, ULONG_PTR a2 = 0, ULONG_PTR a3 = 0, ULONG_PTR a4 = 0, ULONG_PTR a5 = 0, ULONG_PTR a6 = 0, ULONG_PTR a7 = 0, ULONG_PTR a8 = 0) {
    return sprintf(buffer, format, a1, a2, a3, a4, a5, a6, a7, a8);
}
WINAPI int msvcr_memcpy_s(void *dest, size_t destsz, const void *src, size_t count) { if (count > destsz) return ERANGE; memcpy(dest, src, count); return 0; }
WINAPI int msvcr__snprintf(char *buffer, size_t count, const char *format, ULONG_PTR a1 = 0, ULONG_PTR a2 = 0, ULONG_PTR a3 = 0, ULONG_PTR a4 = 0, ULONG_PTR a5 = 0, ULONG_PTR a6 = 0, ULONG_PTR a7 = 0, ULONG_PTR a8 = 0) {
    return snprintf(buffer, count, format, a1, a2, a3, a4, a5, a6, a7, a8);
}
WINAPI char *msvcr_strchr(const char *str, int ch) { return const_cast<char *>(std::strchr(str, ch)); }
WINAPI char *msvcr_strncat(char *dest, const char *src, size_t count) { return std::strncat(dest, src, count); }
WINAPI int msvcr_strncmp(const char *lhs, const char *rhs, size_t count) { return std::strncmp(lhs, rhs, count); }
WINAPI char *msvcr_strncpy(char *dest, const char *src, size_t count) { return std::strncpy(dest, src, count); }
WINAPI char *msvcr_strrchr(const char *str, int ch) { return const_cast<char *>(std::strrchr(str, ch)); }
WINAPI char *msvcr_strstr(const char *haystack, const char *needle) { return const_cast<char *>(std::strstr(haystack, needle)); }
WINAPI int msvcr_tolower(int ch) { return std::tolower(ch); }
WINAPI int msvcr__vsprintf_l(char *buffer, size_t count, const char *format, _locale_t locale, const void *args) {
    return format_from_ms_va_list(buffer, count, format, static_cast<ms_va_list>(args));
}
WINAPI int msvcr__vsnprintf_l(char *buffer, size_t count, size_t maxCount, const char *format, _locale_t locale, const void *args) {
    return format_from_ms_va_list(buffer, count < maxCount ? count : maxCount, format, static_cast<ms_va_list>(args));
}
WINAPI long long msvcr__strtoi64_l(const char *nptr, char **endptr, int base, _locale_t locale) { return strtoll(nptr, endptr, base); }
WINAPI unsigned long long msvcr__strtoui64_l(const char *nptr, char **endptr, int base, _locale_t locale) { return strtoull(nptr, endptr, base); }
WINAPI double msvcr__strtod_l(const char *nptr, char **endptr, _locale_t locale) { return strtod(nptr, endptr); }
WINAPI unsigned long msvcr_strtoul(const char *nptr, char **endptr, int base) { return strtoul(nptr, endptr, base); }
WINAPI _locale_t msvcr__create_locale(int category, const char *locale) {
    locale_t created = newlocale(LC_ALL_MASK, locale ? locale : "C", nullptr);
    return reinterpret_cast<_locale_t>(created);
}
WINAPI int msvcr_memcmp(const void *lhs, const void *rhs, size_t count) { return memcmp(lhs, rhs, count); }
WINAPI long long msvcr__time64(long long *dest) { auto now = static_cast<long long>(time(nullptr)); if (dest) *dest = now; return now; }
WINAPI int msvcr_isxdigit(int ch) { return isxdigit(ch); }
WINAPI int msvcr_isalpha(int ch) { return isalpha(ch); }
WINAPI int msvcr_fclose(FILE *stream) { return fclose(stream); }
WINAPI size_t msvcr_fread(void *buffer, size_t size, size_t count, FILE *stream) { return fread(buffer, size, count, stream); }
WINAPI int msvcr_fseek(FILE *stream, long offset, int origin) { return fseek(stream, offset, origin); }
WINAPI long msvcr_ftell(FILE *stream) { return ftell(stream); }
WINAPI FILE *msvcr__wfopen(LPCWSTR filename, LPCWSTR mode) {
    auto file = wide_to_string(filename);
    auto mode_str = wide_to_string(mode);
    std::string file_utf8(file.begin(), file.end());
    std::string mode_utf8(mode_str.begin(), mode_str.end());
    return fopen(file_utf8.c_str(), mode_utf8.c_str());
}
WINAPI float msvcr_powf(float x, float y) { return std::pow(x, y); }
WINAPI float msvcr_atan2f(float y, float x) { return std::atan2(y, x); }
WINAPI void msvcr__lock(int locknum) { (void)locknum; g_onexit_mutex.lock(); }
WINAPI void msvcr__unlock(int locknum) { (void)locknum; g_onexit_mutex.unlock(); }
WINAPI void *msvcr__calloc_crt(size_t count, size_t size) {
    size_t total;
    return checked_mul_size(count, size, total) ? calloc(1, total) : nullptr;
}
WINAPI void *msvcr___dllonexit(void *raw_func, void **start, void **end) {
    if (!raw_func || !start || !end)
        return nullptr;
    std::lock_guard<std::recursive_mutex> lock(g_onexit_mutex);
    auto first = static_cast<_onexit_t *>(*start);
    auto last = static_cast<_onexit_t *>(*end);
    size_t count = first && last >= first ? static_cast<size_t>(last - first) : 0;
    auto *callbacks = static_cast<_onexit_t *>(
        std::realloc(first, (count + 1) * sizeof(_onexit_t)));
    if (!callbacks)
        return nullptr;
    callbacks[count] = reinterpret_cast<_onexit_t>(raw_func);
    *start = callbacks;
    *end = callbacks + count + 1;
    return raw_func;
}
WINAPI void *msvcr__onexit(void *func) {
    return crt__register_onexit_function(&g_process_onexit, func) == 0 ? func : nullptr;
}
WINAPI int msvcr___CppXcptFilter(unsigned long xcptnum, void *pxcptinfoptrs) { return 0; }
WINAPI void msvcr__amsg_exit(int errnum) { abort(); }
WINAPI void *msvcr__malloc_crt(size_t size) { return crt_malloc(size); }
WINAPI void msvcr___crt_debugger_hook(int reserved) {}
WINAPI LONG msvcr___crtUnhandledException(EXCEPTION_POINTERS *exceptionInfo) { return 0; }
WINAPI void msvcr___crtTerminateProcess(UINT exitCode) { abort(); }
WINAPI void msvcr___crtCapturePreviousContext(PCONTEXT contextRecord) {}
WINAPI void msvcr_terminate() { std::terminate(); }
WINAPI void msvcr_type_info_dtor_internal_method(void *self) {}
WINAPI void msvcr___clean_type_info_names_internal(void **list) {}
WINAPI float crt_sqrt(double x) { return std::sqrt(x); }
WINAPI void *msvcr___RTDynamicCast(void *inptr, LONG vfdelta, void *src, void *target, BOOL isReference) {
    if (!inptr)
        return nullptr;

    struct ObjectLocator {
        uint32_t signature;
        uint32_t base_class_offset;
        uint32_t constructor_displacement;
        uint32_t type_descriptor;
        uint32_t type_hierarchy;
        uint32_t self;
    };
    struct ClassHierarchy {
        uint32_t signature;
        uint32_t attributes;
        uint32_t count;
        uint32_t base_classes;
    };
    struct BaseClassDescriptor {
        uint32_t type_descriptor;
        uint32_t contained_bases;
        int32_t member_offset;
        int32_t vbtable_offset;
        int32_t vbase_offset;
        uint32_t attributes;
    };

    auto source_object = static_cast<uint8_t *>(inptr);
    auto vtable = *reinterpret_cast<void ***>(source_object + vfdelta);
    auto locator = reinterpret_cast<const ObjectLocator *>(vtable[-1]);
    if (locator->signature != 1)
        unsupported_msvc_exception("__RTDynamicCast: unsupported object locator");
    auto image_base = reinterpret_cast<const uint8_t *>(locator) - locator->self;
    auto hierarchy = reinterpret_cast<const ClassHierarchy *>(image_base + locator->type_hierarchy);
    if (hierarchy->attributes & 0x6)
        unsupported_msvc_exception("__RTDynamicCast: virtual or ambiguous hierarchy");
    auto base_classes = reinterpret_cast<const uint32_t *>(image_base + hierarchy->base_classes);
    auto source_name = reinterpret_cast<const char *>(src) + 16;
    auto target_name = reinterpret_cast<const char *>(target) + 16;
    auto complete_object = source_object + vfdelta - locator->base_class_offset;
    bool source_matches = false;
    uint8_t *result = nullptr;

    for (uint32_t i = 0; i < hierarchy->count; ++i) {
        auto base = reinterpret_cast<const BaseClassDescriptor *>(image_base + base_classes[i]);
        if (base->vbtable_offset != -1)
            unsupported_msvc_exception("__RTDynamicCast: virtual base");
        auto type_name = reinterpret_cast<const char *>(image_base + base->type_descriptor) + 16;
        auto candidate = complete_object + base->member_offset;
        if (std::strcmp(type_name, source_name) == 0 && candidate == source_object)
            source_matches = true;
        if (std::strcmp(type_name, target_name) != 0 || (base->attributes & 0x0f))
            continue;
        if (result && result != candidate)
            unsupported_msvc_exception("__RTDynamicCast: ambiguous target");
        result = candidate;
    }
    if (!source_matches)
        unsupported_msvc_exception("__RTDynamicCast: source subobject mismatch");
    if (result)
        return result;
    if (isReference)
        unsupported_msvc_exception("__RTDynamicCast: failed reference cast");
    return nullptr;
}
WINAPI void *msvcr_realloc(void *memblock, size_t size) {
    if (!memblock)
        return crt_malloc(size);
    if (!size) {
        crt_free(memblock);
        return nullptr;
    }
    return realloc(memblock, size);
}
WINAPI void msvcr__aligned_free(void *memblock) { free(memblock); }
WINAPI void *msvcr__aligned_malloc(size_t size, size_t alignment) {
    if (!alignment || (alignment & (alignment - 1))) {
        errno = EINVAL;
        return nullptr;
    }
    alignment = std::max(alignment, size_t{16});
    void *ptr = nullptr;
    int error = posix_memalign(&ptr, alignment, size);
    if (error)
        errno = error;
    return error ? nullptr : ptr;
}
WINAPI void msvcr_operator_delete(void *ptr) { crt_free(ptr); }
WINAPI clock_t msvcr_clock() { return clock(); }
WINAPI unsigned int msvcr_current_scheduler_id() { return 0; }
WINAPI double msvcr_sqrt(double x) { return std::sqrt(x); }
WINAPI long long msvcp__Xtime_get_ticks() {
    auto now = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    return ns / 100; // Windows returns 100-nanosecond intervals
}
WINAPI void crt__invalid_parameter_noinfo_noreturn() { abort(); }
WINAPI float crt_log2f(float x) { return std::log2(x); }
WINAPI short crt__fdtest(float *px) {
    if (std::isnan(*px)) return 2; // _NANCODE
    if (std::isinf(*px)) return 1; // _INFCODE
    if (*px == 0.0f) return 0;     // _DENORM or zero
    return -1;                     // _FINITE
}
WINAPI float crt_acosf(float x) { return std::acos(x); }

// msvcrt.dll stubs for d3dcompiler_47.dll

WINAPI void msvcrt_type_info_dtor(void *self) { /* ??1type_info@@UEAA@XZ */ }
WINAPI int msvcrt__XcptFilter(unsigned long xcptnum, void *pxcptinfoptrs) { return 0; }
WINAPI unsigned long long msvcrt__strtoui64(const char *nptr, char **endptr, int base) { return strtoull(nptr, endptr, base); }
WINAPI int msvcrt_sscanf(const char *buffer, const char *format, ULONG_PTR a1 = 0, ULONG_PTR a2 = 0, ULONG_PTR a3 = 0, ULONG_PTR a4 = 0, ULONG_PTR a5 = 0, ULONG_PTR a6 = 0, ULONG_PTR a7 = 0, ULONG_PTR a8 = 0) {
    return sscanf(buffer, format, a1, a2, a3, a4, a5, a6, a7, a8);
}
WINAPI int msvcrt__isnan(double x) { return std::isnan(x); }
WINAPI int msvcrt__vsnprintf(char *buffer, size_t count, const char *format, const void *args) {
    return format_from_ms_va_list(buffer, count, format, static_cast<ms_va_list>(args));
}
WINAPI double msvcrt_atof(const char *str) { return std::atof(str); }
WINAPI char *msvcrt_setlocale(int category, const char *locale) { return std::setlocale(category, locale); }
WINAPI char *msvcrt__strdup(const char *str) { return strdup(str); }
WINAPI size_t msvcrt__mbstrlen(const char *str) { return str ? std::strlen(str) : 0; }
WINAPI int msvcrt__vsnwprintf(WCHAR *buffer, size_t count, const WCHAR *format, const void *args) {
    // Minimal: just null-terminate
    if (buffer && count > 0) buffer[0] = 0;
    return 0;
}
WINAPI size_t msvcrt_strnlen(const char *str, size_t maxlen) { return strnlen(str, maxlen); }
WINAPI double msvcrt_modf(double x, double *iptr) { return std::modf(x, iptr); }
WINAPI int msvcrt_strncpy_s(char *dest, size_t destsz, const char *src, size_t count) {
    if (!dest || destsz == 0) return EINVAL;
    if (!src) { dest[0] = '\0'; return EINVAL; }
    size_t i = 0;
    size_t limit = (count < destsz - 1) ? count : (destsz - 1);
    while (i < limit && src[i]) { dest[i] = src[i]; i++; }
    dest[i] = '\0';
    return 0;
}
WINAPI int msvcrt_isalnum(int ch) { return std::isalnum(ch); }
WINAPI int msvcrt__finite(double x) { return std::isfinite(x); }
WINAPI unsigned int msvcrt__clearfp() { return 0; }
WINAPI unsigned int msvcrt__controlfp(unsigned int newval, unsigned int mask) { return 0; }
WINAPI int msvcrt_strcpy_s(char *dest, size_t destsz, const char *src) {
    if (!dest || destsz == 0) return EINVAL;
    if (!src) { dest[0] = '\0'; return EINVAL; }
    size_t i = 0;
    while (i < destsz - 1 && src[i]) { dest[i] = src[i]; i++; }
    dest[i] = '\0';
    return 0;
}
WINAPI int msvcrt__strnicmp(const char *s1, const char *s2, size_t n) { return strncasecmp(s1, s2, n); }
WINAPI int msvcrt__fpclass(double x) {
    if (std::isnan(x)) return 0x0002;        // _FPCLASS_QNAN
    if (std::isinf(x)) return x > 0 ? 0x0200 : 0x0004; // _FPCLASS_PINF / _FPCLASS_NINF
    if (x == 0.0) return 0x0040;             // _FPCLASS_PZ
    return x > 0 ? 0x0100 : 0x0008;          // _FPCLASS_PN / _FPCLASS_NN
}
WINAPI int msvcrt_isspace(int ch) { return std::isspace(ch); }
WINAPI int msvcrt__stricmp(const char *s1, const char *s2) { return strcasecmp(s1, s2); }
WINAPI int msvcrt_toupper(int ch) { return std::toupper(ch); }
WINAPI int msvcrt_atoi(const char *str) { return std::atoi(str); }
WINAPI int msvcrt_isdigit(int ch) { return std::isdigit(ch); }
WINAPI char *msvcrt_getenv(const char *name) { return std::getenv(name); }
WINAPI int msvcrt_wcsncmp(const WCHAR *s1, const WCHAR *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] < s2[i] ? -1 : 1;
        if (s1[i] == 0) return 0;
    }
    return 0;
}
WINAPI int msvcrt_wcsncpy_s(WCHAR *dest, size_t destsz, const WCHAR *src, size_t count) {
    if (!dest || destsz == 0) return EINVAL;
    if (!src) { dest[0] = 0; return EINVAL; }
    size_t i = 0;
    size_t limit = (count < destsz - 1) ? count : (destsz - 1);
    while (i < limit && src[i]) { dest[i] = src[i]; i++; }
    dest[i] = 0;
    return 0;
}
WINAPI int msvcrt__wcsicmp(const WCHAR *s1, const WCHAR *s2) {
    while (*s1 || *s2) {
        WCHAR c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        WCHAR c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;
        if (c1 != c2) return c1 < c2 ? -1 : 1;
        s1++; s2++;
    }
    return 0;
}
WINAPI int msvcrt_strcat_s(char *dest, size_t destsz, const char *src) {
    if (!dest || destsz == 0 || !src) return EINVAL;
    size_t dlen = strlen(dest);
    size_t i = 0;
    while (dlen + i < destsz - 1 && src[i]) { dest[dlen + i] = src[i]; i++; }
    dest[dlen + i] = '\0';
    return 0;
}
WINAPI void *msvcrt_bsearch(const void *key, const void *base, size_t num, size_t size, int (WINAPI *compar)(const void *, const void *)) {
    // bsearch with ms_abi comparator - use linear scan to avoid ABI mismatch with stdlib bsearch
    const char *p = static_cast<const char *>(base);
    for (size_t i = 0; i < num; i++) {
        if (compar(key, p + i * size) == 0)
            return const_cast<void *>(static_cast<const void *>(p + i * size));
    }
    return nullptr;
}
WINAPI int msvcrt__snwprintf_s(WCHAR *buffer, size_t sizeInWords, size_t count, const WCHAR *format, ...) {
    // Minimal stub - null-terminate
    if (buffer && sizeInWords > 0) buffer[0] = 0;
    return 0;
}
WINAPI WCHAR *msvcrt_wcschr(const WCHAR *str, WCHAR ch) {
    if (!str) return nullptr;
    while (*str) { if (*str == ch) return const_cast<WCHAR *>(str); str++; }
    return ch == 0 ? const_cast<WCHAR *>(str) : nullptr;
}
WINAPI int msvcrt_iswdigit(unsigned short ch) { return (ch >= '0' && ch <= '9') ? 1 : 0; }
WINAPI char *msvcrt___unDName(char *buffer, const char *mangled, int buflen, void *(*allocator)(size_t), void (*freefn)(void *), unsigned short flags) {
    // Minimal: just copy the mangled name
    if (buffer && buflen > 0 && mangled) {
        strncpy(buffer, mangled, buflen - 1);
        buffer[buflen - 1] = '\0';
        return buffer;
    }
    if (allocator && mangled) {
        size_t len = strlen(mangled) + 1;
        char *result = static_cast<char *>(allocator(len));
        if (result) { memcpy(result, mangled, len); }
        return result;
    }
    return nullptr;
}
WINAPI FILE *msvcrt__wfsopen(const WCHAR *filename, const WCHAR *mode, int shflag) {
    auto fname = narrow_utf16_string(filename);
    auto fmode = narrow_utf16_string(mode);
    std::string fname_utf8(fname.begin(), fname.end());
    std::string fmode_utf8(fmode.begin(), fmode.end());
    return fopen(fname_utf8.c_str(), fmode_utf8.c_str());
}
WINAPI int msvcrt_vsprintf_s(char *buffer, size_t numberOfElements, const char *format, const void *args) {
    return format_from_ms_va_list(buffer, numberOfElements, format, static_cast<ms_va_list>(args));
}
WINAPI long msvcrt_wcstol(const WCHAR *nptr, WCHAR **endptr, int base) {
    // Convert wide string to narrow, parse
    std::string narrow;
    const WCHAR *p = nptr;
    while (*p) { narrow.push_back(static_cast<char>(*p & 0x7f)); p++; }
    char *end = nullptr;
    long result = strtol(narrow.c_str(), &end, base);
    if (endptr) {
        *endptr = const_cast<WCHAR *>(nptr + (end - narrow.c_str()));
    }
    return result;
}
WINAPI int msvcrt__wcsnicmp(const WCHAR *s1, const WCHAR *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        WCHAR c1 = (s1[i] >= 'A' && s1[i] <= 'Z') ? s1[i] + 32 : s1[i];
        WCHAR c2 = (s2[i] >= 'A' && s2[i] <= 'Z') ? s2[i] + 32 : s2[i];
        if (c1 != c2) return c1 < c2 ? -1 : 1;
        if (c1 == 0) return 0;
    }
    return 0;
}
WINAPI int msvcrt__wsplitpath_s(const WCHAR *path, WCHAR *drive, size_t drivesz,
                                WCHAR *dir, size_t dirsz, WCHAR *fname, size_t fnamesz,
                                WCHAR *ext, size_t extsz) {
    if (drive && drivesz > 0) drive[0] = 0;
    if (dir && dirsz > 0) dir[0] = 0;
    if (fname && fnamesz > 0) fname[0] = 0;
    if (ext && extsz > 0) ext[0] = 0;
    if (!path) return EINVAL;
    // Find last separator
    const WCHAR *lastSep = nullptr;
    const WCHAR *lastDot = nullptr;
    for (const WCHAR *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') lastSep = p;
        if (*p == '.') lastDot = p;
    }
    const WCHAR *fnameStart = lastSep ? lastSep + 1 : path;
    if (lastDot && lastDot > fnameStart) {
        // Copy fname
        if (fname && fnamesz > 0) {
            size_t len = lastDot - fnameStart;
            if (len >= fnamesz) len = fnamesz - 1;
            for (size_t i = 0; i < len; i++) fname[i] = fnameStart[i];
            fname[len] = 0;
        }
        // Copy ext
        if (ext && extsz > 0) {
            size_t i = 0;
            while (lastDot[i] && i < extsz - 1) { ext[i] = lastDot[i]; i++; }
            ext[i] = 0;
        }
    } else {
        if (fname && fnamesz > 0) {
            size_t i = 0;
            while (fnameStart[i] && i < fnamesz - 1) { fname[i] = fnameStart[i]; i++; }
            fname[i] = 0;
        }
    }
    // Copy dir
    if (dir && dirsz > 0 && lastSep) {
        size_t len = lastSep - path + 1;
        if (len >= dirsz) len = dirsz - 1;
        for (size_t i = 0; i < len; i++) dir[i] = path[i];
        dir[len] = 0;
    }
    return 0;
}
WINAPI WCHAR msvcrt_towlower(WCHAR ch) {
    return (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
}
WINAPI int msvcrt_wcscpy_s(WCHAR *dest, size_t destsz, const WCHAR *src) {
    if (!dest || destsz == 0) return EINVAL;
    if (!src) { dest[0] = 0; return EINVAL; }
    size_t i = 0;
    while (i < destsz - 1 && src[i]) { dest[i] = src[i]; i++; }
    dest[i] = 0;
    return 0;
}
WINAPI void *msvcrt_operator_new_array(size_t size) { /* ??_U@YAPEAX_K@Z */
    return msvcr_operator_new(size);
}
WINAPI int msvcrt_swprintf_s(WCHAR *buffer, size_t sizeInWords, const WCHAR *format, ...) {
    if (buffer && sizeInWords > 0) buffer[0] = 0;
    return 0;
}
WINAPI int msvcrt_wcsncat_s(WCHAR *dest, size_t destsz, const WCHAR *src, size_t count) {
    if (!dest || destsz == 0) return EINVAL;
    if (!src) return EINVAL;
    size_t dlen = 0;
    while (dlen < destsz && dest[dlen]) dlen++;
    size_t i = 0;
    size_t limit = (count < destsz - dlen - 1) ? count : (destsz - dlen - 1);
    while (i < limit && src[i]) { dest[dlen + i] = src[i]; i++; }
    dest[dlen + i] = 0;
    return 0;
}
WINAPI WCHAR *msvcrt_wcsrchr(const WCHAR *str, WCHAR ch) {
    if (!str) return nullptr;
    const WCHAR *last = nullptr;
    while (*str) { if (*str == ch) last = str; str++; }
    return const_cast<WCHAR *>(last);
}
WINAPI WCHAR *msvcrt__wfullpath(WCHAR *absPath, const WCHAR *relPath, size_t maxLength) {
    // Minimal: copy relPath to absPath
    if (!relPath) return nullptr;
    if (!absPath) {
        // Allocate
        size_t len = 0;
        while (relPath[len]) len++;
        absPath = static_cast<WCHAR *>(malloc((len + 1) * sizeof(WCHAR)));
        if (!absPath) return nullptr;
        for (size_t i = 0; i <= len; i++) absPath[i] = relPath[i];
        return absPath;
    }
    size_t i = 0;
    while (i < maxLength - 1 && relPath[i]) { absPath[i] = relPath[i]; i++; }
    absPath[i] = 0;
    return absPath;
}
WINAPI int msvcrt__wmakepath_s(WCHAR *path, size_t sizeInWords, const WCHAR *drive,
                                const WCHAR *dir, const WCHAR *fname, const WCHAR *ext) {
    if (!path || sizeInWords == 0) return EINVAL;
    path[0] = 0;
    size_t pos = 0;
    auto append = [&](const WCHAR *src) {
        if (!src) return;
        while (*src && pos < sizeInWords - 1) { path[pos++] = *src++; }
        path[pos] = 0;
    };
    append(drive);
    append(dir);
    append(fname);
    append(ext);
    return 0;
}
WINAPI int msvcrt__chsize(int fd, long size) { return ftruncate(fd, size); }
WINAPI int msvcrt__close(int fd) { return close(fd); }
WINAPI int msvcrt__read(int fd, void *buffer, unsigned int count) { return read(fd, buffer, count); }
WINAPI int msvcrt__write(int fd, const void *buffer, unsigned int count) { return write(fd, buffer, count); }
WINAPI long long msvcrt__lseeki64(int fd, long long offset, int origin) { return lseek64(fd, offset, origin); }
WINAPI intptr_t msvcrt__get_osfhandle(int fd) { return fd; } // On Linux, fd == handle
WINAPI int msvcrt__open_osfhandle(intptr_t osfhandle, int flags) { return static_cast<int>(osfhandle); }
WINAPI WCHAR *msvcrt__wcsdup(const WCHAR *str) {
    if (!str) return nullptr;
    size_t len = 0;
    while (str[len]) len++;
    auto *dup = static_cast<WCHAR *>(malloc((len + 1) * sizeof(WCHAR)));
    if (dup) { for (size_t i = 0; i <= len; i++) dup[i] = str[i]; }
    return dup;
}
WINAPI int msvcrt_wcscat_s(WCHAR *dest, size_t destsz, const WCHAR *src) {
    if (!dest || destsz == 0 || !src) return EINVAL;
    size_t dlen = 0;
    while (dlen < destsz && dest[dlen]) dlen++;
    size_t i = 0;
    while (dlen + i < destsz - 1 && src[i]) { dest[dlen + i] = src[i]; i++; }
    dest[dlen + i] = 0;
    return 0;
}
WINAPI int msvcrt__mbscmp(const unsigned char *s1, const unsigned char *s2) {
    return strcmp(reinterpret_cast<const char *>(s1), reinterpret_cast<const char *>(s2));
}
WINAPI int msvcrt__memicmp(const void *buf1, const void *buf2, size_t count) {
    auto *s1 = static_cast<const unsigned char *>(buf1);
    auto *s2 = static_cast<const unsigned char *>(buf2);
    for (size_t i = 0; i < count; i++) {
        int c1 = tolower(s1[i]);
        int c2 = tolower(s2[i]);
        if (c1 != c2) return c1 - c2;
    }
    return 0;
}
WINAPI WCHAR *msvcrt__wgetenv(const WCHAR *name) {
    // Would need wide-to-narrow conversion, but d3dcompiler rarely uses this
    return nullptr;
}
WINAPI int msvcrt__wsopen(const WCHAR *filename, int oflag, int shflag, ...) {
    auto fname = narrow_utf16_string(filename);
    std::string fname_utf8(fname.begin(), fname.end());
    return open(fname_utf8.c_str(), oflag, 0666);
}
WINAPI double msvcrt_acos(double x) { return std::acos(x); }
WINAPI double msvcrt_asin(double x) { return std::asin(x); }
WINAPI double msvcrt_atan(double x) { return std::atan(x); }
WINAPI double msvcrt_atan2(double y, double x) { return std::atan2(y, x); }
WINAPI double msvcrt_ceil(double x) { return std::ceil(x); }
WINAPI double msvcrt_cos(double x) { return std::cos(x); }
WINAPI double msvcrt_cosh(double x) { return std::cosh(x); }
WINAPI double msvcrt_exp(double x) { return std::exp(x); }
WINAPI double msvcrt_floor(double x) { return std::floor(x); }
WINAPI double msvcrt_fmod(double x, double y) { return std::fmod(x, y); }
WINAPI double msvcrt_log(double x) { return std::log(x); }
WINAPI double msvcrt_pow(double x, double y) { return std::pow(x, y); }
WINAPI double msvcrt_sin(double x) { return std::sin(x); }
WINAPI double msvcrt_sinh(double x) { return std::sinh(x); }
WINAPI int msvcrt_strcmp(const char *s1, const char *s2) { return std::strcmp(s1, s2); }
WINAPI double msvcrt_tan(double x) { return std::tan(x); }
WINAPI double msvcrt_tanh(double x) { return std::tanh(x); }

// ADVAPI32.dll stubs

// Minimal Crypt API stubs - d3dcompiler uses these for hashing
using HCRYPTPROV = uintptr_t;
using HCRYPTHASH = uintptr_t;

struct crypt_hash_data {
    uint32_t algorithm;
    std::vector<uint8_t> data;
};

static std::mutex g_crypt_mutex;
static HCRYPTHASH g_next_hash = 1;
static std::unordered_map<HCRYPTHASH, crypt_hash_data *> g_hashes;

WINAPI BOOL CryptAcquireContextW(HCRYPTPROV *phProv, const WCHAR *szContainer, const WCHAR *szProvider, DWORD dwProvType, DWORD dwFlags) {
    if (phProv) *phProv = 1;
    return TRUE;
}
WINAPI BOOL CryptReleaseContext(HCRYPTPROV hProv, DWORD dwFlags) { return TRUE; }
WINAPI BOOL CryptCreateHash(HCRYPTPROV hProv, uint32_t Algid, uintptr_t hKey, DWORD dwFlags, HCRYPTHASH *phHash) {
    auto *h = new crypt_hash_data{Algid, {}};
    std::lock_guard<std::mutex> lock(g_crypt_mutex);
    HCRYPTHASH id = g_next_hash++;
    g_hashes[id] = h;
    if (phHash) *phHash = id;
    return TRUE;
}
WINAPI BOOL CryptHashData(HCRYPTHASH hHash, const uint8_t *pbData, DWORD dwDataLen, DWORD dwFlags) {
    std::lock_guard<std::mutex> lock(g_crypt_mutex);
    auto it = g_hashes.find(hHash);
    if (it == g_hashes.end()) return FALSE;
    it->second->data.insert(it->second->data.end(), pbData, pbData + dwDataLen);
    return TRUE;
}
WINAPI BOOL CryptGetHashParam(HCRYPTHASH hHash, DWORD dwParam, uint8_t *pbData, DWORD *pdwDataLen, DWORD dwFlags) {
    // HP_HASHVAL = 0x0002, HP_HASHSIZE = 0x0004
    if (dwParam == 0x0004) {
        // Return hash size: 16 for MD5 (0x8003), 20 for SHA1 (0x8004)
        if (pdwDataLen) {
            std::lock_guard<std::mutex> lock(g_crypt_mutex);
            auto it = g_hashes.find(hHash);
            uint32_t alg = it != g_hashes.end() ? it->second->algorithm : 0;
            DWORD size = (alg == 0x8003) ? 16 : 20;
            *pdwDataLen = size;
            if (pbData) memcpy(pbData, &size, sizeof(DWORD));
        }
        return TRUE;
    }
    if (dwParam == 0x0002) {
        // Return hash value - compute a simple hash
        std::lock_guard<std::mutex> lock(g_crypt_mutex);
        auto it = g_hashes.find(hHash);
        if (it == g_hashes.end()) return FALSE;
        DWORD hashSize = (it->second->algorithm == 0x8003) ? 16 : 20;
        if (pdwDataLen) *pdwDataLen = hashSize;
        if (pbData) {
            // Simple FNV-like hash spread across output bytes
            auto &data = it->second->data;
            memset(pbData, 0, hashSize);
            uint64_t h1 = 0xcbf29ce484222325ULL;
            uint64_t h2 = 0x100000001b3ULL;
            for (auto b : data) {
                h1 ^= b; h1 *= 0x01000193;
                h2 ^= b; h2 *= 0x01000193;
            }
            memcpy(pbData, &h1, 8);
            memcpy(pbData + 8, &h2, hashSize > 8 ? (hashSize - 8 < 8 ? hashSize - 8 : 8) : 0);
        }
        return TRUE;
    }
    return FALSE;
}
WINAPI BOOL CryptDestroyHash(HCRYPTHASH hHash) {
    std::lock_guard<std::mutex> lock(g_crypt_mutex);
    auto it = g_hashes.find(hHash);
    if (it != g_hashes.end()) { delete it->second; g_hashes.erase(it); }
    return TRUE;
}

// Registry stubs - return not-found
using HKEY = void *;
WINAPI LONG RegOpenKeyExW(HKEY hKey, const WCHAR *lpSubKey, DWORD ulOptions, DWORD samDesired, HKEY *phkResult) { return 2; /* ERROR_FILE_NOT_FOUND */ }
WINAPI LONG RegOpenKeyExA(HKEY hKey, const char *lpSubKey, DWORD ulOptions, DWORD samDesired, HKEY *phkResult) { return 2; }
WINAPI LONG RegQueryValueExA(HKEY hKey, const char *lpValueName, DWORD *lpReserved, DWORD *lpType, uint8_t *lpData, DWORD *lpcbData) { return 2; }
WINAPI LONG RegQueryValueExW(HKEY hKey, const WCHAR *lpValueName, DWORD *lpReserved, DWORD *lpType, uint8_t *lpData, DWORD *lpcbData) { return 2; }
WINAPI LONG RegEnumKeyExA(HKEY hKey, DWORD dwIndex, char *lpName, DWORD *lpcchName, DWORD *lpReserved, char *lpClass, DWORD *lpcchClass, void *lpftLastWriteTime) { return 259; /* ERROR_NO_MORE_ITEMS */ }
WINAPI LONG RegCloseKey(HKEY hKey) { return 0; }

// Additional KERNEL32.dll stubs for d3dcompiler

WINAPI void *LocalAlloc(UINT uFlags, SIZE_T uBytes) { return calloc(1, uBytes); }
WINAPI void *LocalFree(void *hMem) { free(hMem); return nullptr; }
WINAPI DWORD GetVersion() { return 0x0A00; /* Windows 10 */ }
WINAPI DWORD GetTickCount() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<DWORD>(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
WINAPI void TerminateProcess(HANDLE hProcess, UINT uExitCode) { _exit(uExitCode); }
WINAPI void SetLastError(DWORD dwErrCode) { g_last_error = dwErrCode; }
WINAPI HANDLE CreateFileMappingW(HANDLE hFile, void *lpAttributes, DWORD flProtect, DWORD dwMaxSizeHigh, DWORD dwMaxSizeLow, const WCHAR *lpName) {
    return reinterpret_cast<HANDLE>(1); // Stub
}
WINAPI BOOL UnmapViewOfFile(const void *lpBaseAddress) { return TRUE; }
WINAPI DWORD GetFileSize(HANDLE hFile, DWORD *lpFileSizeHigh) {
    if (lpFileSizeHigh) *lpFileSizeHigh = 0;
    return 0;
}
WINAPI HANDLE CreateFileA(const char *lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, void *lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    int flags = O_RDONLY;
    if (dwDesiredAccess & 0x40000000) flags = O_RDWR; // GENERIC_WRITE
    if (dwCreationDisposition == 2) flags |= O_CREAT; // CREATE_ALWAYS
    if (dwCreationDisposition == 4) flags |= O_CREAT; // OPEN_ALWAYS
    int fd = open(lpFileName, flags, 0666);
    return fd >= 0 ? reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd + 1)) : INVALID_HANDLE_VALUE;
}
WINAPI void *VirtualAlloc(void *lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect) {
    int protection;
    if (!dwSize || (flAllocationType != MEM_RESERVE &&
                    flAllocationType != MEM_COMMIT &&
                    flAllocationType != (MEM_RESERVE | MEM_COMMIT)) ||
        !windows_protection(flProtect, protection)) {
        g_last_error = ERROR_INVALID_PARAMETER;
        return nullptr;
    }

    const size_t page_size = host_page_size();
    uintptr_t requested = reinterpret_cast<uintptr_t>(lpAddress);
    uintptr_t requested_end;
    if (!checked_add_address(requested, dwSize, requested_end)) {
        g_last_error = ERROR_NOT_ENOUGH_MEMORY;
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_virtual_memory_mutex);
    if (flAllocationType == MEM_COMMIT && lpAddress) {
        uintptr_t start = requested & ~(static_cast<uintptr_t>(page_size) - 1);
        uintptr_t end;
        if (!round_up_address(requested_end, page_size, end) ||
            find_reservation(start, end) == g_virtual_reservations.end()) {
            g_last_error = ERROR_INVALID_ADDRESS;
            return nullptr;
        }
        if (mprotect(reinterpret_cast<void *>(start), end - start, protection) != 0) {
            g_last_error = ERROR_NOT_ENOUGH_MEMORY;
            return nullptr;
        }
        return reinterpret_cast<void *>(start);
    }

    size_t reservation_size;
    uintptr_t reservation_base;
    uintptr_t commit_start;
    uintptr_t commit_end;
    void *mapping;
    if (lpAddress) {
        reservation_base = requested & ~(static_cast<uintptr_t>(WINDOWS_ALLOCATION_GRANULARITY) - 1);
        if (!reservation_base) {
            g_last_error = ERROR_INVALID_PARAMETER;
            return nullptr;
        }
        if (!round_up_address(requested_end, page_size, commit_end)) {
            g_last_error = ERROR_NOT_ENOUGH_MEMORY;
            return nullptr;
        }
        reservation_size = commit_end - reservation_base;
        commit_start = reservation_base;
        mapping = mmap(reinterpret_cast<void *>(reservation_base), reservation_size, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (mapping == MAP_FAILED) {
            g_last_error = errno == EEXIST ? ERROR_INVALID_ADDRESS : ERROR_NOT_ENOUGH_MEMORY;
            return nullptr;
        }
        if (reinterpret_cast<uintptr_t>(mapping) != reservation_base) {
            munmap(mapping, reservation_size);
            g_last_error = ERROR_INVALID_ADDRESS;
            return nullptr;
        }
    } else {
        if (!round_up_size(dwSize, page_size, reservation_size)) {
            g_last_error = ERROR_NOT_ENOUGH_MEMORY;
            return nullptr;
        }
        size_t mapping_size;
        if (!checked_add_size(reservation_size, WINDOWS_ALLOCATION_GRANULARITY, mapping_size)) {
            g_last_error = ERROR_NOT_ENOUGH_MEMORY;
            return nullptr;
        }
        mapping = mmap(nullptr, mapping_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED) {
            g_last_error = ERROR_NOT_ENOUGH_MEMORY;
            return nullptr;
        }
        uintptr_t mapping_base = reinterpret_cast<uintptr_t>(mapping);
        if (!round_up_address(mapping_base, WINDOWS_ALLOCATION_GRANULARITY, reservation_base)) {
            munmap(mapping, mapping_size);
            g_last_error = ERROR_NOT_ENOUGH_MEMORY;
            return nullptr;
        }
        size_t prefix = reservation_base - mapping_base;
        size_t suffix = mapping_size - prefix - reservation_size;
        if ((prefix && munmap(mapping, prefix) != 0) ||
            (suffix && munmap(reinterpret_cast<void *>(reservation_base + reservation_size), suffix) != 0)) {
            munmap(mapping, mapping_size);
            g_last_error = ERROR_NOT_ENOUGH_MEMORY;
            return nullptr;
        }
        commit_start = reservation_base;
        commit_end = reservation_base + reservation_size;
        mapping = reinterpret_cast<void *>(reservation_base);
    }

    if ((flAllocationType & MEM_COMMIT) &&
        mprotect(reinterpret_cast<void *>(commit_start), commit_end - commit_start, protection) != 0) {
        munmap(mapping, reservation_size);
        g_last_error = ERROR_NOT_ENOUGH_MEMORY;
        return nullptr;
    }
    try {
        if (!g_virtual_reservations.emplace(reservation_base, reservation_size).second) {
            munmap(mapping, reservation_size);
            g_last_error = ERROR_INVALID_ADDRESS;
            return nullptr;
        }
    } catch (const std::bad_alloc &) {
        munmap(mapping, reservation_size);
        g_last_error = ERROR_NOT_ENOUGH_MEMORY;
        return nullptr;
    }
    return reinterpret_cast<void *>(reservation_base);
}
WINAPI BOOL VirtualFree(void *lpAddress, SIZE_T dwSize, DWORD dwFreeType) {
    if (!lpAddress || (dwFreeType != MEM_DECOMMIT && dwFreeType != MEM_RELEASE)) {
        g_last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    uintptr_t address = reinterpret_cast<uintptr_t>(lpAddress);
    std::lock_guard<std::mutex> lock(g_virtual_memory_mutex);
    if (dwFreeType == MEM_RELEASE) {
        auto it = g_virtual_reservations.find(address);
        if (dwSize || it == g_virtual_reservations.end()) {
            g_last_error = dwSize ? ERROR_INVALID_PARAMETER : ERROR_INVALID_ADDRESS;
            return FALSE;
        }
        if (munmap(lpAddress, it->second) != 0) {
            g_last_error = ERROR_INVALID_ADDRESS;
            return FALSE;
        }
        g_virtual_reservations.erase(it);
        return TRUE;
    }

    uintptr_t start;
    uintptr_t end;
    if (!dwSize) {
        auto it = g_virtual_reservations.find(address);
        if (it == g_virtual_reservations.end()) {
            g_last_error = ERROR_INVALID_ADDRESS;
            return FALSE;
        }
        start = it->first;
        end = start + it->second;
    } else {
        const size_t page_size = host_page_size();
        uintptr_t requested_end;
        start = address & ~(static_cast<uintptr_t>(page_size) - 1);
        if (!checked_add_address(address, dwSize, requested_end) ||
            !round_up_address(requested_end, page_size, end) ||
            find_reservation(start, end) == g_virtual_reservations.end()) {
            g_last_error = ERROR_INVALID_ADDRESS;
            return FALSE;
        }
    }
    void *result = mmap(reinterpret_cast<void *>(start), end - start, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (result != reinterpret_cast<void *>(start))
        std::abort();
    return TRUE;
}
WINAPI HMODULE LoadLibraryExW(const WCHAR *lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    return nullptr;
}
WINAPI DWORD GetEnvironmentVariableA(const char *lpName, char *lpBuffer, DWORD nSize) {
    const char *val = getenv(lpName);
    if (!val) return 0;
    size_t len = strlen(val);
    if (lpBuffer && nSize > len) { memcpy(lpBuffer, val, len + 1); return static_cast<DWORD>(len); }
    return static_cast<DWORD>(len + 1);
}
WINAPI DWORD GetFullPathNameA(const char *lpFileName, DWORD nBufferLength, char *lpBuffer, char **lpFilePart) {
    char resolved[PATH_MAX];
    if (!realpath(lpFileName, resolved)) {
        strncpy(resolved, lpFileName, PATH_MAX - 1);
        resolved[PATH_MAX - 1] = '\0';
    }
    size_t len = strlen(resolved);
    if (lpBuffer && nBufferLength > len) {
        memcpy(lpBuffer, resolved, len + 1);
        if (lpFilePart) {
            char *last = strrchr(lpBuffer, '/');
            *lpFilePart = last ? last + 1 : lpBuffer;
        }
    }
    return static_cast<DWORD>(len);
}
WINAPI DWORD GetFullPathNameW(const WCHAR *lpFileName, DWORD nBufferLength, WCHAR *lpBuffer, WCHAR **lpFilePart) {
    // Minimal: copy input
    if (!lpFileName) return 0;
    size_t len = 0;
    while (lpFileName[len]) len++;
    if (lpBuffer && nBufferLength > len) {
        for (size_t i = 0; i <= len; i++) lpBuffer[i] = lpFileName[i];
        if (lpFilePart) *lpFilePart = lpBuffer;
    }
    return static_cast<DWORD>(len);
}
WINAPI void Sleep(DWORD dwMilliseconds) { usleep(dwMilliseconds * 1000); }
WINAPI int LCMapStringW(DWORD Locale, DWORD dwMapFlags, const WCHAR *lpSrcStr, int cchSrc, WCHAR *lpDestStr, int cchDest) {
    if (!lpSrcStr) return 0;
    int len = cchSrc >= 0 ? cchSrc : 0;
    if (cchSrc < 0) { while (lpSrcStr[len]) len++; len++; }
    if (!lpDestStr || cchDest == 0) return len;
    int copy = len < cchDest ? len : cchDest;
    for (int i = 0; i < copy; i++) lpDestStr[i] = lpSrcStr[i];
    return copy;
}
WINAPI BOOL SetFileAttributesW(const WCHAR *lpFileName, DWORD dwFileAttributes) { return TRUE; }
WINAPI BOOL CopyFileExW(const WCHAR *lpExistingFileName, const WCHAR *lpNewFileName, void *lpProgressRoutine, void *lpData, BOOL *pbCancel, DWORD dwCopyFlags) { return FALSE; }
WINAPI DWORD GetFileType(HANDLE hFile) { return 1; /* FILE_TYPE_DISK */ }
WINAPI BOOL DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, void *lpInBuffer, DWORD nInBufferSize, void *lpOutBuffer, DWORD nOutBufferSize, DWORD *lpBytesReturned, void *lpOverlapped) { return FALSE; }
WINAPI DWORD ExpandEnvironmentStringsW(const WCHAR *lpSrc, WCHAR *lpDst, DWORD nSize) {
    if (!lpSrc) return 0;
    // Minimal: copy without expansion
    size_t len = 0;
    while (lpSrc[len]) len++;
    if (lpDst && nSize > len) { for (size_t i = 0; i <= len; i++) lpDst[i] = lpSrc[i]; }
    return static_cast<DWORD>(len + 1);
}
WINAPI BOOL FlushViewOfFile(const void *lpBaseAddress, SIZE_T dwNumberOfBytesToFlush) { return TRUE; }
WINAPI void *MapViewOfFileEx(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap, void *lpBaseAddress) { return nullptr; }
WINAPI void *MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap) { return nullptr; }
WINAPI DWORD GetLastError() { return g_last_error; }
WINAPI BOOL GetFileSizeEx(HANDLE hFile, LARGE_INTEGER *lpFileSize) {
    if (lpFileSize) *lpFileSize = 0;
    return TRUE;
}
WINAPI BOOL ReadFile(HANDLE hFile, void *lpBuffer, DWORD nNumberOfBytesToRead, DWORD *lpNumberOfBytesRead, void *lpOverlapped) {
    if (lpNumberOfBytesRead) *lpNumberOfBytesRead = 0;
    return TRUE;
}
WINAPI int WideCharToMultiByte(UINT CodePage, DWORD dwFlags, const WCHAR *lpWideCharStr, int cchWideChar,
                                char *lpMultiByteStr, int cbMultiByte, const char *lpDefaultChar, BOOL *lpUsedDefaultChar) {
    if (!lpWideCharStr) return 0;
    int srcLen = cchWideChar;
    if (srcLen < 0) { srcLen = 0; while (lpWideCharStr[srcLen]) srcLen++; srcLen++; }
    auto narrow = narrow_utf16_string(lpWideCharStr);
    int outLen = static_cast<int>(narrow.size()) + (cchWideChar < 0 ? 1 : 0);
    if (!lpMultiByteStr || cbMultiByte == 0) return outLen;
    int copy = outLen < cbMultiByte ? outLen : cbMultiByte;
    memcpy(lpMultiByteStr, narrow.c_str(), copy);
    if (lpUsedDefaultChar) *lpUsedDefaultChar = FALSE;
    return copy;
}
WINAPI int lstrcmpiA(const char *lpString1, const char *lpString2) { return strcasecmp(lpString1, lpString2); }
WINAPI int MultiByteToWideChar(UINT CodePage, DWORD dwFlags, const char *lpMultiByteStr, int cbMultiByte,
                                WCHAR *lpWideCharStr, int cchWideChar) {
    if (!lpMultiByteStr) return 0;
    int srcLen = cbMultiByte;
    if (srcLen < 0) srcLen = static_cast<int>(strlen(lpMultiByteStr)) + 1;
    if (!lpWideCharStr || cchWideChar == 0) return srcLen;
    int copy = srcLen < cchWideChar ? srcLen : cchWideChar;
    for (int i = 0; i < copy; i++) lpWideCharStr[i] = static_cast<WCHAR>(static_cast<unsigned char>(lpMultiByteStr[i]));
    return copy;
}
WINAPI HANDLE HeapCreate(DWORD flOptions, SIZE_T dwInitialSize, SIZE_T dwMaximumSize) { return reinterpret_cast<HANDLE>(1); }
WINAPI BOOL HeapDestroy(HANDLE hHeap) { return TRUE; }
WINAPI void *HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes) {
    return (dwFlags & 0x08) ? calloc(1, dwBytes) : malloc(dwBytes);
}
WINAPI HANDLE GetProcessHeap() { return reinterpret_cast<HANDLE>(1); }
WINAPI BOOL HeapFree(HANDLE hHeap, DWORD dwFlags, void *lpMem) {
    free(lpMem);
    return TRUE;
}

// RPCRT4.dll

#define RPC_S_OK               0
#define RPC_S_UUID_LOCAL_ONLY  1824

WINAPI LONG UuidCreate(void *Uuid) {
    if (!Uuid) return 1;
    // Generate a random UUID (v4)
    auto *bytes = static_cast<uint8_t *>(Uuid);
    int fd = open("/dev/urandom", O_RDONLY);
    bool ok = false;
    if (fd >= 0) { ok = read(fd, bytes, 16) == 16; close(fd); }
    if (!ok) { for (int i = 0; i < 16; i++) bytes[i] = static_cast<uint8_t>(rand()); }
    bytes[6] = (bytes[6] & 0x0F) | 0x40; // Version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80; // Variant 1
    return ok ? RPC_S_OK : RPC_S_UUID_LOCAL_ONLY;
}

// WS2_32

WINAPI int ws2_32_WSAStartup(uint16_t versionRequested, void *wsaData) { return 0; }
WINAPI int ws2_32_WSACleanup(void) { return 0; }
WINAPI int ws2_32_WSAGetLastError(void) { return errno; }
WINAPI uintptr_t ws2_32_WSASocketW(int af, int type, int protocol, void *lpProtocolInfo, unsigned int g, unsigned int dwFlags) {
    return static_cast<uintptr_t>(socket(af, type, protocol));
}
WINAPI int ws2_32_accept(uintptr_t s, void *addr, void *addrlen) { return accept(static_cast<int>(s), static_cast<sockaddr *>(addr), static_cast<socklen_t *>(addrlen)); }
WINAPI int ws2_32_bind(uintptr_t s, const void *name, int namelen) { return bind(static_cast<int>(s), static_cast<const sockaddr *>(name), static_cast<socklen_t>(namelen)); }
WINAPI int ws2_32_connect(uintptr_t s, const void *name, int namelen) { return connect(static_cast<int>(s), static_cast<const sockaddr *>(name), static_cast<socklen_t>(namelen)); }
WINAPI int ws2_32_getsockname(uintptr_t s, void *name, void *namelen) { return getsockname(static_cast<int>(s), static_cast<sockaddr *>(name), static_cast<socklen_t *>(namelen)); }
WINAPI uint32_t ws2_32_htons(uint32_t hostshort) { return htons(static_cast<uint16_t>(hostshort)); }
WINAPI int ws2_32_ioctlsocket(uintptr_t s, long cmd, void *argp) { return 0; }
WINAPI int ws2_32_listen(uintptr_t s, int backlog) { return listen(static_cast<int>(s), backlog); }
WINAPI uint32_t ws2_32_ntohs(uint32_t netshort) { return ntohs(static_cast<uint16_t>(netshort)); }
WINAPI int ws2_32_recv(uintptr_t s, char *buf, int len, int flags) { return recv(static_cast<int>(s), buf, len, flags); }
WINAPI int ws2_32_select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout) { return select(nfds, static_cast<fd_set *>(readfds), static_cast<fd_set *>(writefds), static_cast<fd_set *>(exceptfds), static_cast<timeval *>(timeout)); }
WINAPI int ws2_32_send(uintptr_t s, const char *buf, int len, int flags) { return send(static_cast<int>(s), buf, len, flags); }
WINAPI int ws2_32_setsockopt(uintptr_t s, int level, int optname, const char *optval, int optlen) { return setsockopt(static_cast<int>(s), level, optname, optval, static_cast<socklen_t>(optlen)); }
WINAPI int ws2_32_gethostname(char *name, int namelen) { return gethostname(name, namelen); }
WINAPI int ws2_32___WSAFDIsSet(uintptr_t fd, void *set) { return FD_ISSET(static_cast<int>(fd), static_cast<fd_set *>(set)); }
WINAPI int ws2_32_closesocket(uintptr_t s) { return close(static_cast<int>(s)); }
WINAPI int ws2_32_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) { return getaddrinfo(node, service, hints, res); }
WINAPI void ws2_32_freeaddrinfo(struct addrinfo *res) { freeaddrinfo(res); }
WINAPI int ws2_32_inet_pton(int af, const char *src, void *dst) { return inet_pton(af, src, dst); }
WINAPI const char *ws2_32_inet_ntop(int af, const void *src, char *dst, uint32_t size) { return inet_ntop(af, src, dst, size); }
// Registration

#define KERNEL32_FUNC(name) register_function("KERNEL32.dll", #name, generic_func(&name));
#define VCRUNTIME140_FUNC(name) register_function("VCRUNTIME140.dll", #name, generic_func(&vcruntime_##name));
#define CRT_FUNC(suffix, name) register_function("api-ms-win-crt-" #suffix "-l1-1-0.dll", #name, generic_func(&crt_##name));

void register_windows_library_functions() {
    // The Windows-shim table is process-global and immutable after first
    // population, so a second pass would just refill ~351 duplicate slots
    // and is a major contributor to MAX_EXPORTS overflow when load_dll is
    // invoked more than once.
    static bool registered = false;
    if (registered) return;
    registered = true;

    KERNEL32_FUNC(QueryPerformanceCounter);
    KERNEL32_FUNC(QueryPerformanceFrequency);
    KERNEL32_FUNC(GetModuleHandleW);
    KERNEL32_FUNC(CreateEventW);
    KERNEL32_FUNC(WaitForSingleObjectEx);
    KERNEL32_FUNC(ResetEvent);
    KERNEL32_FUNC(SetEvent);
    KERNEL32_FUNC(InitializeCriticalSectionAndSpinCount);
    KERNEL32_FUNC(DeleteCriticalSection);
    KERNEL32_FUNC(EnterCriticalSection);
    KERNEL32_FUNC(LeaveCriticalSection);
    KERNEL32_FUNC(CloseHandle);
    KERNEL32_FUNC(RtlCaptureContext);
    KERNEL32_FUNC(RtlLookupFunctionEntry);
    KERNEL32_FUNC(RtlVirtualUnwind);
    KERNEL32_FUNC(IsDebuggerPresent);
    KERNEL32_FUNC(UnhandledExceptionFilter);
    KERNEL32_FUNC(SetUnhandledExceptionFilter);
    KERNEL32_FUNC(IsProcessorFeaturePresent);
    KERNEL32_FUNC(GetCurrentProcessId);
    KERNEL32_FUNC(GetCurrentThreadId);
    KERNEL32_FUNC(InitializeSListHead);
    KERNEL32_FUNC(DisableThreadLibraryCalls);
    KERNEL32_FUNC(RaiseException);
    KERNEL32_FUNC(GetSystemTimeAsFileTime);
    KERNEL32_FUNC(GetProcAddress);
    KERNEL32_FUNC(GetLogicalProcessorInformation);
    KERNEL32_FUNC(GetLogicalProcessorInformationEx);
    KERNEL32_FUNC(TlsGetValue);
    KERNEL32_FUNC(TlsAlloc);
    KERNEL32_FUNC(TlsFree);
    KERNEL32_FUNC(TlsSetValue);
    KERNEL32_FUNC(FlsAlloc);
    KERNEL32_FUNC(FlsFree);
    KERNEL32_FUNC(FlsSetValue);
    KERNEL32_FUNC(FlsGetValue);
    KERNEL32_FUNC(GetSystemInfo);
    KERNEL32_FUNC(FreeLibrary);
    KERNEL32_FUNC(LoadLibraryA);
    KERNEL32_FUNC(OutputDebugStringA);
    KERNEL32_FUNC(InitializeCriticalSection);
    KERNEL32_FUNC(GetCurrentProcess);
    KERNEL32_FUNC(GetModuleFileNameA);
    KERNEL32_FUNC(GetCurrentThread);
    KERNEL32_FUNC(SetThreadIdealProcessor);
    KERNEL32_FUNC(CreateDirectoryW);
    KERNEL32_FUNC(DeleteFileW);
    KERNEL32_FUNC(FindClose);
    KERNEL32_FUNC(FindFirstFileW);
    KERNEL32_FUNC(FindNextFileW);
    KERNEL32_FUNC(GetFileAttributesW);
    KERNEL32_FUNC(WaitForSingleObject);
    KERNEL32_FUNC(CreateThread);
    KERNEL32_FUNC(GetExitCodeThread);
    KERNEL32_FUNC(ReleaseSemaphore);
    KERNEL32_FUNC(CreateSemaphoreW);
    KERNEL32_FUNC(CreateFileW);
    KERNEL32_FUNC(FlushFileBuffers);
    KERNEL32_FUNC(SetFilePointer);
    KERNEL32_FUNC(WriteFile);
    KERNEL32_FUNC(VerSetConditionMask);
    KERNEL32_FUNC(SetHandleInformation);
    KERNEL32_FUNC(VerifyVersionInfoW);
    KERNEL32_FUNC(DecodePointer);
    KERNEL32_FUNC(EncodePointer);

    VCRUNTIME140_FUNC(memmove);
    VCRUNTIME140_FUNC(memcpy);
    VCRUNTIME140_FUNC(memchr);
    VCRUNTIME140_FUNC(__CxxFrameHandler3);
    VCRUNTIME140_FUNC(__CxxFrameHandler4);
    VCRUNTIME140_FUNC(__std_terminate);
    VCRUNTIME140_FUNC(_purecall);
    VCRUNTIME140_FUNC(__C_specific_handler);
    VCRUNTIME140_FUNC(memset);
    VCRUNTIME140_FUNC(__std_exception_copy);
    VCRUNTIME140_FUNC(__std_exception_destroy);
    VCRUNTIME140_FUNC(__std_type_info_destroy_list);
    VCRUNTIME140_FUNC(_CxxThrowException);
    VCRUNTIME140_FUNC(__current_exception);
    VCRUNTIME140_FUNC(__current_exception_context);
    register_function("VCRUNTIME140.dll", "terminate", generic_func(&msvcr_terminate));

    CRT_FUNC(heap, malloc);
    CRT_FUNC(heap, _callnewh);
    CRT_FUNC(heap, free);
    CRT_FUNC(utility, qsort);
    CRT_FUNC(stdio, __stdio_common_vsprintf);
    CRT_FUNC(stdio, __stdio_common_vsnprintf_s);
    CRT_FUNC(runtime, _seh_filter_dll);
    CRT_FUNC(runtime, _cexit);
    CRT_FUNC(runtime, _crt_atexit);
    CRT_FUNC(runtime, _execute_onexit_table);
    CRT_FUNC(runtime, _register_onexit_function);
    CRT_FUNC(runtime, _initialize_onexit_table);
    CRT_FUNC(runtime, _initialize_narrow_environment);
    CRT_FUNC(runtime, _configure_narrow_argv);
    CRT_FUNC(runtime, _initterm_e);
    CRT_FUNC(runtime, _initterm);
    CRT_FUNC(math, floorf);
    CRT_FUNC(math, roundf);
    CRT_FUNC(math, fmodf);
    CRT_FUNC(math, cosf);
    CRT_FUNC(math, ceilf);
    CRT_FUNC(math, sqrtf);
    CRT_FUNC(math, log2f);
    CRT_FUNC(math, _fdtest);
    CRT_FUNC(math, acosf);
    CRT_FUNC(runtime, _invalid_parameter_noinfo_noreturn);

    register_function("MSVCP140.dll", "_Xtime_get_ticks", generic_func(&msvcp__Xtime_get_ticks));

    register_function("MSVCP120.dll", "?_Xbad_function_call@std@@YAXXZ", generic_func(&msvcp__Xbad_function_call));
    register_function("MSVCP120.dll", "?_Winerror_map@std@@YAPEBDH@Z", generic_func(&msvcp__Winerror_map));
    register_function("MSVCP120.dll", "?_Syserror_map@std@@YAPEBDH@Z", generic_func(&msvcp__Syserror_map));
    register_function("MSVCP120.dll", "?_Xout_of_range@std@@YAXPEBD@Z", generic_func(&msvcp__Xout_of_range));
    register_function("MSVCP120.dll", "?_Xlength_error@std@@YAXPEBD@Z", generic_func(&msvcp__Xlength_error));
    register_function("MSVCP120.dll", "_Thrd_yield", generic_func(&msvcp__Thrd_yield));
    register_function("MSVCP120.dll", "?_Xbad_alloc@std@@YAXXZ", generic_func(&msvcp__Xbad_alloc));

    register_function("MSVCR120.dll", "??_V@YAXPEAX@Z", generic_func(&msvcr_operator_delete_array));
    register_function("MSVCR120.dll", "sprintf_s", generic_func(&msvcr_sprintf_s));
    register_function("MSVCR120.dll", "printf", generic_func(&msvcr_printf));
    register_function("MSVCR120.dll", "??2@YAPEAX_K@Z", generic_func(&msvcr_operator_new));
    register_function("MSVCR120.dll", "?_Yield@_Context@details@Concurrency@@SAXXZ", generic_func(&msvcr_context_yield));
    register_function("MSVCR120.dll", "sprintf", generic_func(&msvcr_sprintf));
    register_function("MSVCR120.dll", "memcpy_s", generic_func(&msvcr_memcpy_s));
    register_function("MSVCR120.dll", "_snprintf", generic_func(&msvcr__snprintf));
    register_function("MSVCR120.dll", "strchr", generic_func(&msvcr_strchr));
    register_function("MSVCR120.dll", "strncat", generic_func(&msvcr_strncat));
    register_function("MSVCR120.dll", "strncmp", generic_func(&msvcr_strncmp));
    register_function("MSVCR120.dll", "strncpy", generic_func(&msvcr_strncpy));
    register_function("MSVCR120.dll", "strrchr", generic_func(&msvcr_strrchr));
    register_function("MSVCR120.dll", "strstr", generic_func(&msvcr_strstr));
    register_function("MSVCR120.dll", "tolower", generic_func(&msvcr_tolower));
    register_function("MSVCR120.dll", "_vsprintf_l", generic_func(&msvcr__vsprintf_l));
    register_function("MSVCR120.dll", "_vsnprintf_l", generic_func(&msvcr__vsnprintf_l));
    register_function("MSVCR120.dll", "_strtoi64_l", generic_func(&msvcr__strtoi64_l));
    register_function("MSVCR120.dll", "_strtoui64_l", generic_func(&msvcr__strtoui64_l));
    register_function("MSVCR120.dll", "_strtod_l", generic_func(&msvcr__strtod_l));
    register_function("MSVCR120.dll", "_purecall", generic_func(&vcruntime__purecall));
    register_function("MSVCR120.dll", "memmove", generic_func(&vcruntime_memmove));
    register_function("MSVCR120.dll", "memcpy", generic_func(&vcruntime_memcpy));
    register_function("MSVCR120.dll", "memset", generic_func(&vcruntime_memset));
    register_function("MSVCR120.dll", "strtoul", generic_func(&msvcr_strtoul));
    register_function("MSVCR120.dll", "_create_locale", generic_func(&msvcr__create_locale));
    register_function("MSVCR120.dll", "memcmp", generic_func(&msvcr_memcmp));
    register_function("MSVCR120.dll", "free", generic_func(&crt_free));
    register_function("MSVCR120.dll", "_time64", generic_func(&msvcr__time64));
    register_function("MSVCR120.dll", "isxdigit", generic_func(&msvcr_isxdigit));
    register_function("MSVCR120.dll", "isalpha", generic_func(&msvcr_isalpha));
    register_function("MSVCR120.dll", "fclose", generic_func(&msvcr_fclose));
    register_function("MSVCR120.dll", "fread", generic_func(&msvcr_fread));
    register_function("MSVCR120.dll", "fseek", generic_func(&msvcr_fseek));
    register_function("MSVCR120.dll", "ftell", generic_func(&msvcr_ftell));
    register_function("MSVCR120.dll", "_wfopen", generic_func(&msvcr__wfopen));
    register_function("MSVCR120.dll", "powf", generic_func(&msvcr_powf));
    register_function("MSVCR120.dll", "atan2f", generic_func(&msvcr_atan2f));
    register_function("MSVCR120.dll", "_lock", generic_func(&msvcr__lock));
    register_function("MSVCR120.dll", "_unlock", generic_func(&msvcr__unlock));
    register_function("MSVCR120.dll", "_calloc_crt", generic_func(&msvcr__calloc_crt));
    register_function("MSVCR120.dll", "__dllonexit", generic_func(&msvcr___dllonexit));
    register_function("MSVCR120.dll", "__C_specific_handler", generic_func(&vcruntime___C_specific_handler));
    register_function("MSVCR120.dll", "_onexit", generic_func(&msvcr__onexit));
    register_function("MSVCR120.dll", "__CppXcptFilter", generic_func(&msvcr___CppXcptFilter));
    register_function("MSVCR120.dll", "_amsg_exit", generic_func(&msvcr__amsg_exit));
    register_function("MSVCR120.dll", "_malloc_crt", generic_func(&msvcr__malloc_crt));
    register_function("MSVCR120.dll", "_initterm", generic_func(&crt__initterm));
    register_function("MSVCR120.dll", "_initterm_e", generic_func(&crt__initterm_e));
    register_function("MSVCR120.dll", "__crt_debugger_hook", generic_func(&msvcr___crt_debugger_hook));
    register_function("MSVCR120.dll", "__crtUnhandledException", generic_func(&msvcr___crtUnhandledException));
    register_function("MSVCR120.dll", "__crtTerminateProcess", generic_func(&msvcr___crtTerminateProcess));
    register_function("MSVCR120.dll", "__crtCapturePreviousContext", generic_func(&msvcr___crtCapturePreviousContext));
    register_function("MSVCR120.dll", "?terminate@@YAXXZ", generic_func(&msvcr_terminate));
    register_function("MSVCR120.dll", "?_type_info_dtor_internal_method@type_info@@QEAAXXZ", generic_func(&msvcr_type_info_dtor_internal_method));
    register_function("MSVCR120.dll", "__clean_type_info_names_internal", generic_func(&msvcr___clean_type_info_names_internal));
    register_function("MSVCR120.dll", "floorf", generic_func(&crt_floorf));
    register_function("MSVCR120.dll", "ceilf", generic_func(&crt_ceilf));
    register_function("MSVCR120.dll", "__RTDynamicCast", generic_func(&msvcr___RTDynamicCast));
    register_function("MSVCR120.dll", "__CxxFrameHandler3", generic_func(&vcruntime___CxxFrameHandler3));
    register_function("MSVCR120.dll", "_CxxThrowException", generic_func(&vcruntime__CxxThrowException));
    register_function("MSVCR120.dll", "malloc", generic_func(&crt_malloc));
    register_function("MSVCR120.dll", "realloc", generic_func(&msvcr_realloc));
    register_function("MSVCR120.dll", "_aligned_free", generic_func(&msvcr__aligned_free));
    register_function("MSVCR120.dll", "_aligned_malloc", generic_func(&msvcr__aligned_malloc));
    register_function("MSVCR120.dll", "??3@YAXPEAX@Z", generic_func(&msvcr_operator_delete));
    register_function("MSVCR120.dll", "clock", generic_func(&msvcr_clock));
    register_function("MSVCR120.dll", "?_Id@_CurrentScheduler@details@Concurrency@@SAIXZ", generic_func(&msvcr_current_scheduler_id));
    register_function("MSVCR120.dll", "sqrt", generic_func(&msvcr_sqrt));

    // msvcrt.dll
    register_function("msvcrt.dll", "??1type_info@@UEAA@XZ", generic_func(&msvcrt_type_info_dtor));
    register_function("msvcrt.dll", "_XcptFilter", generic_func(&msvcrt__XcptFilter));
    register_function("msvcrt.dll", "_strtoui64", generic_func(&msvcrt__strtoui64));
    register_function("msvcrt.dll", "sscanf", generic_func(&msvcrt_sscanf));
    register_function("msvcrt.dll", "_isnan", generic_func(&msvcrt__isnan));
    register_function("msvcrt.dll", "_vsnprintf", generic_func(&msvcrt__vsnprintf));
    register_function("msvcrt.dll", "atof", generic_func(&msvcrt_atof));
    register_function("msvcrt.dll", "setlocale", generic_func(&msvcrt_setlocale));
    register_function("msvcrt.dll", "_strdup", generic_func(&msvcrt__strdup));
    register_function("msvcrt.dll", "_mbstrlen", generic_func(&msvcrt__mbstrlen));
    register_function("msvcrt.dll", "_vsnwprintf", generic_func(&msvcrt__vsnwprintf));
    register_function("msvcrt.dll", "strnlen", generic_func(&msvcrt_strnlen));
    register_function("msvcrt.dll", "modf", generic_func(&msvcrt_modf));
    register_function("msvcrt.dll", "strncpy_s", generic_func(&msvcrt_strncpy_s));
    register_function("msvcrt.dll", "isalnum", generic_func(&msvcrt_isalnum));
    register_function("msvcrt.dll", "_finite", generic_func(&msvcrt__finite));
    register_function("msvcrt.dll", "_clearfp", generic_func(&msvcrt__clearfp));
    register_function("msvcrt.dll", "_controlfp", generic_func(&msvcrt__controlfp));
    register_function("msvcrt.dll", "strcpy_s", generic_func(&msvcrt_strcpy_s));
    register_function("msvcrt.dll", "_strnicmp", generic_func(&msvcrt__strnicmp));
    register_function("msvcrt.dll", "_fpclass", generic_func(&msvcrt__fpclass));
    register_function("msvcrt.dll", "isspace", generic_func(&msvcrt_isspace));
    register_function("msvcrt.dll", "_stricmp", generic_func(&msvcrt__stricmp));
    register_function("msvcrt.dll", "toupper", generic_func(&msvcrt_toupper));
    register_function("msvcrt.dll", "atoi", generic_func(&msvcrt_atoi));
    register_function("msvcrt.dll", "isdigit", generic_func(&msvcrt_isdigit));
    register_function("msvcrt.dll", "getenv", generic_func(&msvcrt_getenv));
    register_function("msvcrt.dll", "wcsncmp", generic_func(&msvcrt_wcsncmp));
    register_function("msvcrt.dll", "wcsncpy_s", generic_func(&msvcrt_wcsncpy_s));
    register_function("msvcrt.dll", "_wcsicmp", generic_func(&msvcrt__wcsicmp));
    register_function("msvcrt.dll", "strcat_s", generic_func(&msvcrt_strcat_s));
    register_function("msvcrt.dll", "bsearch", generic_func(&msvcrt_bsearch));
    register_function("msvcrt.dll", "_snwprintf_s", generic_func(&msvcrt__snwprintf_s));
    register_function("msvcrt.dll", "wcschr", generic_func(&msvcrt_wcschr));
    register_function("msvcrt.dll", "iswdigit", generic_func(&msvcrt_iswdigit));
    register_function("msvcrt.dll", "__unDName", generic_func(&msvcrt___unDName));
    register_function("msvcrt.dll", "_wfsopen", generic_func(&msvcrt__wfsopen));
    register_function("msvcrt.dll", "vsprintf_s", generic_func(&msvcrt_vsprintf_s));
    register_function("msvcrt.dll", "wcstol", generic_func(&msvcrt_wcstol));
    register_function("msvcrt.dll", "_wcsnicmp", generic_func(&msvcrt__wcsnicmp));
    register_function("msvcrt.dll", "_wsplitpath_s", generic_func(&msvcrt__wsplitpath_s));
    register_function("msvcrt.dll", "towlower", generic_func(&msvcrt_towlower));
    register_function("msvcrt.dll", "wcscpy_s", generic_func(&msvcrt_wcscpy_s));
    register_function("msvcrt.dll", "??_U@YAPEAX_K@Z", generic_func(&msvcrt_operator_new_array));
    register_function("msvcrt.dll", "swprintf_s", generic_func(&msvcrt_swprintf_s));
    register_function("msvcrt.dll", "wcsncat_s", generic_func(&msvcrt_wcsncat_s));
    register_function("msvcrt.dll", "wcsrchr", generic_func(&msvcrt_wcsrchr));
    register_function("msvcrt.dll", "_wfullpath", generic_func(&msvcrt__wfullpath));
    register_function("msvcrt.dll", "_wmakepath_s", generic_func(&msvcrt__wmakepath_s));
    register_function("msvcrt.dll", "_chsize", generic_func(&msvcrt__chsize));
    register_function("msvcrt.dll", "_close", generic_func(&msvcrt__close));
    register_function("msvcrt.dll", "_read", generic_func(&msvcrt__read));
    register_function("msvcrt.dll", "_write", generic_func(&msvcrt__write));
    register_function("msvcrt.dll", "_lseeki64", generic_func(&msvcrt__lseeki64));
    register_function("msvcrt.dll", "_get_osfhandle", generic_func(&msvcrt__get_osfhandle));
    register_function("msvcrt.dll", "_open_osfhandle", generic_func(&msvcrt__open_osfhandle));
    register_function("msvcrt.dll", "_wcsdup", generic_func(&msvcrt__wcsdup));
    register_function("msvcrt.dll", "wcscat_s", generic_func(&msvcrt_wcscat_s));
    register_function("msvcrt.dll", "_mbscmp", generic_func(&msvcrt__mbscmp));
    register_function("msvcrt.dll", "_memicmp", generic_func(&msvcrt__memicmp));
    register_function("msvcrt.dll", "_wgetenv", generic_func(&msvcrt__wgetenv));
    register_function("msvcrt.dll", "_wsopen", generic_func(&msvcrt__wsopen));
    register_function("msvcrt.dll", "acos", generic_func(&msvcrt_acos));
    register_function("msvcrt.dll", "asin", generic_func(&msvcrt_asin));
    register_function("msvcrt.dll", "atan", generic_func(&msvcrt_atan));
    register_function("msvcrt.dll", "atan2", generic_func(&msvcrt_atan2));
    register_function("msvcrt.dll", "ceil", generic_func(&msvcrt_ceil));
    register_function("msvcrt.dll", "cos", generic_func(&msvcrt_cos));
    register_function("msvcrt.dll", "cosh", generic_func(&msvcrt_cosh));
    register_function("msvcrt.dll", "exp", generic_func(&msvcrt_exp));
    register_function("msvcrt.dll", "floor", generic_func(&msvcrt_floor));
    register_function("msvcrt.dll", "fmod", generic_func(&msvcrt_fmod));
    register_function("msvcrt.dll", "log", generic_func(&msvcrt_log));
    register_function("msvcrt.dll", "pow", generic_func(&msvcrt_pow));
    register_function("msvcrt.dll", "sin", generic_func(&msvcrt_sin));
    register_function("msvcrt.dll", "sinh", generic_func(&msvcrt_sinh));
    register_function("msvcrt.dll", "strcmp", generic_func(&msvcrt_strcmp));
    register_function("msvcrt.dll", "tan", generic_func(&msvcrt_tan));
    register_function("msvcrt.dll", "tanh", generic_func(&msvcrt_tanh));
    // msvcrt.dll also needs malloc/free/memcpy/memset/memmove/strlen
    register_function("msvcrt.dll", "malloc", generic_func(&crt_malloc));
    register_function("msvcrt.dll", "free", generic_func(&crt_free));
    register_function("msvcrt.dll", "calloc", generic_func(&msvcr__calloc_crt));
    register_function("msvcrt.dll", "realloc", generic_func(&msvcr_realloc));
    register_function("msvcrt.dll", "memcpy", generic_func(&vcruntime_memcpy));
    register_function("msvcrt.dll", "memset", generic_func(&vcruntime_memset));
    register_function("msvcrt.dll", "memmove", generic_func(&vcruntime_memmove));
    register_function("msvcrt.dll", "memcmp", generic_func(&msvcr_memcmp));
    register_function("msvcrt.dll", "strlen", generic_func(static_cast<size_t(*)(const char*)>(&strlen)));
    register_function("msvcrt.dll", "strcpy", generic_func(static_cast<char*(*)(char*, const char*)>(&strcpy)));
    register_function("msvcrt.dll", "strncpy", generic_func(&msvcr_strncpy));
    register_function("msvcrt.dll", "strncmp", generic_func(&msvcr_strncmp));
    register_function("msvcrt.dll", "strchr", generic_func(&msvcr_strchr));
    register_function("msvcrt.dll", "strrchr", generic_func(&msvcr_strrchr));
    register_function("msvcrt.dll", "strstr", generic_func(&msvcr_strstr));
    register_function("msvcrt.dll", "sprintf", generic_func(&msvcr_sprintf));
    register_function("msvcrt.dll", "_snprintf", generic_func(&msvcr__snprintf));
    register_function("msvcrt.dll", "tolower", generic_func(&msvcr_tolower));
    register_function("msvcrt.dll", "??2@YAPEAX_K@Z", generic_func(&msvcr_operator_new));
    register_function("msvcrt.dll", "??3@YAXPEAX@Z", generic_func(&msvcr_operator_delete));
    register_function("msvcrt.dll", "wcslen", generic_func(static_cast<size_t(*)(const wchar_t*)>(&wcslen)));
    register_function("msvcrt.dll", "_initterm", generic_func(&crt__initterm));
    register_function("msvcrt.dll", "_initterm_e", generic_func(&crt__initterm_e));
    register_function("msvcrt.dll", "abort", generic_func(static_cast<void(*)()>(&abort)));
    register_function("msvcrt.dll", "_beginthreadex", generic_func(&CreateThread));
    register_function("msvcrt.dll", "_endthreadex", generic_func(&crt_endthreadex));

    // ADVAPI32.dll
    register_function("ADVAPI32.dll", "CryptDestroyHash", generic_func(&CryptDestroyHash));
    register_function("ADVAPI32.dll", "CryptHashData", generic_func(&CryptHashData));
    register_function("ADVAPI32.dll", "CryptCreateHash", generic_func(&CryptCreateHash));
    register_function("ADVAPI32.dll", "CryptGetHashParam", generic_func(&CryptGetHashParam));
    register_function("ADVAPI32.dll", "CryptReleaseContext", generic_func(&CryptReleaseContext));
    register_function("ADVAPI32.dll", "CryptAcquireContextW", generic_func(&CryptAcquireContextW));
    register_function("ADVAPI32.dll", "RegOpenKeyExW", generic_func(&RegOpenKeyExW));
    register_function("ADVAPI32.dll", "RegQueryValueExA", generic_func(&RegQueryValueExA));
    register_function("ADVAPI32.dll", "RegEnumKeyExA", generic_func(&RegEnumKeyExA));
    register_function("ADVAPI32.dll", "RegOpenKeyExA", generic_func(&RegOpenKeyExA));
    register_function("ADVAPI32.dll", "RegCloseKey", generic_func(&RegCloseKey));
    register_function("ADVAPI32.dll", "RegQueryValueExW", generic_func(&RegQueryValueExW));

    // Additional KERNEL32.dll
    KERNEL32_FUNC(LocalFree);
    KERNEL32_FUNC(LocalAlloc);
    KERNEL32_FUNC(GetVersion);
    KERNEL32_FUNC(GetTickCount);
    KERNEL32_FUNC(TerminateProcess);
    KERNEL32_FUNC(SetLastError);
    KERNEL32_FUNC(CreateFileMappingW);
    KERNEL32_FUNC(UnmapViewOfFile);
    KERNEL32_FUNC(GetFileSize);
    KERNEL32_FUNC(CreateFileA);
    KERNEL32_FUNC(VirtualAlloc);
    KERNEL32_FUNC(VirtualFree);
    KERNEL32_FUNC(LoadLibraryExW);
    KERNEL32_FUNC(GetEnvironmentVariableA);
    KERNEL32_FUNC(GetFullPathNameA);
    KERNEL32_FUNC(GetFullPathNameW);
    KERNEL32_FUNC(Sleep);
    KERNEL32_FUNC(LCMapStringW);
    KERNEL32_FUNC(SetFileAttributesW);
    KERNEL32_FUNC(CopyFileExW);
    KERNEL32_FUNC(GetFileType);
    KERNEL32_FUNC(DeviceIoControl);
    KERNEL32_FUNC(ExpandEnvironmentStringsW);
    KERNEL32_FUNC(FlushViewOfFile);
    KERNEL32_FUNC(MapViewOfFileEx);
    KERNEL32_FUNC(MapViewOfFile);
    KERNEL32_FUNC(GetLastError);
    KERNEL32_FUNC(GetFileSizeEx);
    KERNEL32_FUNC(ReadFile);
    KERNEL32_FUNC(WideCharToMultiByte);
    KERNEL32_FUNC(lstrcmpiA);
    KERNEL32_FUNC(MultiByteToWideChar);
    KERNEL32_FUNC(HeapCreate);
    KERNEL32_FUNC(HeapDestroy);
    KERNEL32_FUNC(HeapAlloc);
    KERNEL32_FUNC(GetProcessHeap);
    KERNEL32_FUNC(HeapFree);

    // RPCRT4.dll
    register_function("RPCRT4.dll", "UuidCreate", generic_func(&UuidCreate));

    register_function("WS2_32.dll", "WSAStartup", generic_func(&ws2_32_WSAStartup));
    register_function("WS2_32.dll", "WSACleanup", generic_func(&ws2_32_WSACleanup));
    register_function("WS2_32.dll", "WSAGetLastError", generic_func(&ws2_32_WSAGetLastError));
    register_function("WS2_32.dll", "WSASocketW", generic_func(&ws2_32_WSASocketW));
    register_function("WS2_32.dll", "accept", generic_func(&ws2_32_accept));
    register_function("WS2_32.dll", "bind", generic_func(&ws2_32_bind));
    register_function("WS2_32.dll", "closesocket", generic_func(&ws2_32_closesocket));
    register_function("WS2_32.dll", "connect", generic_func(&ws2_32_connect));
    register_function("WS2_32.dll", "getsockname", generic_func(&ws2_32_getsockname));
    register_function("WS2_32.dll", "getaddrinfo", generic_func(&ws2_32_getaddrinfo));
    register_function("WS2_32.dll", "freeaddrinfo", generic_func(&ws2_32_freeaddrinfo));
    register_function("WS2_32.dll", "htons", generic_func(&ws2_32_htons));
    register_function("WS2_32.dll", "ioctlsocket", generic_func(&ws2_32_ioctlsocket));
    register_function("WS2_32.dll", "inet_pton", generic_func(&ws2_32_inet_pton));
    register_function("WS2_32.dll", "inet_ntop", generic_func(&ws2_32_inet_ntop));
    register_function("WS2_32.dll", "listen", generic_func(&ws2_32_listen));
    register_function("WS2_32.dll", "ntohs", generic_func(&ws2_32_ntohs));
    register_function("WS2_32.dll", "recv", generic_func(&ws2_32_recv));
    register_function("WS2_32.dll", "select", generic_func(&ws2_32_select));
    register_function("WS2_32.dll", "send", generic_func(&ws2_32_send));
    register_function("WS2_32.dll", "setsockopt", generic_func(&ws2_32_setsockopt));
    register_function("WS2_32.dll", "gethostname", generic_func(&ws2_32_gethostname));
    register_function("WS2_32.dll", "__WSAFDIsSet", generic_func(&ws2_32___WSAFDIsSet));
}
