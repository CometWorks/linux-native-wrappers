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

static DWORD WINAPI thread_proc(LPVOID)
{
    return 0;
}

static BOOL WINAPI dll_entry(PVOID, DWORD reason, PVOID)
{
    if (reason != DLL_THREAD_ATTACH || g_nested_thread_started.exchange(true))
        return TRUE;

    DWORD thread_id = 0;
    HANDLE thread = CreateThread(nullptr, 0, thread_proc, nullptr, 0, &thread_id);
    if (thread) {
        g_nested_thread_id = thread_id;
        g_nested_thread = thread;
    }
    return TRUE;
}

int main()
{
    pe_image image{};
    image.entry = dll_entry;
    image.image = &image;
    pe_register_loaded_image_for_test(&image);

    HANDLE thread = CreateThread(nullptr, 0, thread_proc, nullptr, 0, nullptr);
    if (!thread)
        return 1;

    DWORD wait_result = WaitForSingleObject(thread, 2000);
    CloseHandle(thread);
    if (wait_result != STATUS_WAIT_0)
        return 2;

    HANDLE nested_thread = g_nested_thread.load();
    if (!nested_thread || g_nested_thread_id.load() == 0)
        return 3;

    wait_result = WaitForSingleObject(nested_thread, 2000);
    CloseHandle(nested_thread);
    return wait_result == STATUS_WAIT_0 ? 0 : 4;
}
