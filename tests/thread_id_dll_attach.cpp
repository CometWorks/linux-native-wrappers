#include <atomic>

#include "pe_loader.h"

extern HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T,
                                  DWORD (WINAPI *)(LPVOID), LPVOID, DWORD,
                                  LPDWORD);
extern DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);
extern BOOL WINAPI CloseHandle(HANDLE);

static std::atomic<bool> g_nested_thread_started{false};
static std::atomic<DWORD> g_nested_thread_id{0};
static std::atomic<HANDLE> g_nested_thread{nullptr};
static std::atomic<int> g_attach_count{0};
static std::atomic<int> g_detach_count{0};
static std::atomic<int> g_disabled_notifications{0};
static std::atomic<int> g_fls_callbacks{0};
static std::atomic<int> g_tls_attach_count{0};
static std::atomic<int> g_tls_detach_count{0};
static std::atomic<int> g_tls_dll_attach_count{0};
static std::atomic<int> g_tls_dll_detach_count{0};
static DWORD g_fls_index;

extern DWORD WINAPI FlsAlloc(PFLS_CALLBACK_FUNCTION);
extern BOOL WINAPI FlsFree(DWORD);
extern BOOL WINAPI FlsSetValue(DWORD, LPVOID);
extern BOOL WINAPI DisableThreadLibraryCalls(HMODULE);
extern DWORD WINAPI TlsAlloc();
extern BOOL WINAPI TlsFree(DWORD);
extern void WINAPI SetLastError(DWORD);
extern DWORD WINAPI GetLastError();

static void WINAPI fls_callback(PVOID value)
{
    if (value == reinterpret_cast<PVOID>(0x1234))
        ++g_fls_callbacks;
}

static DWORD WINAPI thread_proc(LPVOID)
{
    FlsSetValue(g_fls_index, reinterpret_cast<PVOID>(0x1234));
    return 0;
}

static BOOL WINAPI dll_entry(PVOID, DWORD reason, PVOID)
{
    if (reason == DLL_THREAD_DETACH) {
        ++g_detach_count;
        FlsSetValue(g_fls_index, reinterpret_cast<PVOID>(0x1234));
        return TRUE;
    }
    if (reason != DLL_THREAD_ATTACH)
        return TRUE;
    ++g_attach_count;
    if (g_nested_thread_started.exchange(true))
        return TRUE;

    DWORD thread_id = 0;
    HANDLE thread = CreateThread(nullptr, 0, thread_proc, nullptr, 0, &thread_id);
    if (thread) {
        g_nested_thread_id = thread_id;
        g_nested_thread = thread;
    }
    return TRUE;
}

static BOOL WINAPI disabled_dll_entry(PVOID, DWORD reason, PVOID)
{
    if (reason == DLL_THREAD_ATTACH || reason == DLL_THREAD_DETACH)
        ++g_disabled_notifications;
    return TRUE;
}

static BOOL WINAPI tls_callback(PVOID, DWORD reason, PVOID)
{
    if (reason == DLL_THREAD_ATTACH)
        ++g_tls_attach_count;
    else if (reason == DLL_THREAD_DETACH)
        ++g_tls_detach_count;
    return TRUE;
}

static BOOL WINAPI tls_dll_entry(PVOID, DWORD reason, PVOID)
{
    if (reason == DLL_THREAD_ATTACH)
        ++g_tls_dll_attach_count;
    else if (reason == DLL_THREAD_DETACH)
        ++g_tls_dll_detach_count;
    return TRUE;
}

int main()
{
    pe_image image{};
    image.entry = dll_entry;
    image.image = &image;
    pe_register_loaded_image_for_test(&image);

    pe_image disabled_image{};
    disabled_image.entry = disabled_dll_entry;
    disabled_image.image = &disabled_image;
    pe_register_loaded_image_for_test(&disabled_image);
    if (!DisableThreadLibraryCalls(disabled_image.image))
        return 1;

    DllEntry_t tls_callbacks[] = {tls_callback, nullptr};
    IMAGE_TLS_DIRECTORY tls_directory{};
    tls_directory.AddressOfCallbacks = tls_callbacks;
    pe_image tls_image{};
    tls_image.entry = tls_dll_entry;
    tls_image.image = &tls_image;
    tls_image.tls_directory = &tls_directory;
    tls_image.tls_total_size = 1;
    tls_image.tls_index = TlsAlloc();
    if (tls_image.tls_index == 0xffffffffu)
        return 2;
    pe_register_loaded_image_for_test(&tls_image);
    SetLastError(0);
    if (DisableThreadLibraryCalls(tls_image.image) || GetLastError() != 87)
        return 3;

    g_fls_index = FlsAlloc(fls_callback);
    if (g_fls_index == 0xffffffffu)
        return 4;

    HANDLE thread = CreateThread(nullptr, 0, thread_proc, nullptr, 0, nullptr);
    if (!thread)
        return 5;

    DWORD wait_result = WaitForSingleObject(thread, 2000);
    CloseHandle(thread);
    if (wait_result != STATUS_WAIT_0)
        return 6;

    HANDLE nested_thread = g_nested_thread.load();
    if (!nested_thread || g_nested_thread_id.load() == 0)
        return 7;

    wait_result = WaitForSingleObject(nested_thread, 2000);
    CloseHandle(nested_thread);
    if (wait_result != STATUS_WAIT_0)
        return 8;

    for (int i = 0; i < 32; ++i) {
        thread = CreateThread(nullptr, 0, thread_proc, nullptr, 0, nullptr);
        if (!thread || WaitForSingleObject(thread, 2000) != STATUS_WAIT_0)
            return 9;
        CloseHandle(thread);
    }

    if (g_attach_count != 34 || g_detach_count != 34 ||
        g_disabled_notifications != 0 || g_fls_callbacks != 68 ||
        g_tls_attach_count != 34 || g_tls_detach_count != 34 ||
        g_tls_dll_attach_count != 34 || g_tls_dll_detach_count != 34)
        return 10;

    if (!FlsSetValue(g_fls_index, reinterpret_cast<PVOID>(0x1234)) ||
        !FlsFree(g_fls_index) || g_fls_callbacks != 69)
        return 11;
    if (!TlsFree(tls_image.tls_index))
        return 12;
    return 0;
}
