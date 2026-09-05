// Drives the event and semaphore shims through the shared library and checks
// that the ntsync device was opened when it is available. Before the TLS
// dialect change, the std::call_once inlined into CreateEventW handed
// pthread_once a GOT slot instead of the once flag, so init_ntsync never ran
// and every object silently used the pthread fallback.
// Each API runs in a separate process so neither can initialize for the other.
#include <cstdio>
#include <cstring>
#include <string>

#include <dirent.h>
#include <unistd.h>

#include "win_types.h"

extern HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
extern HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCWSTR);
extern BOOL WINAPI SetEvent(HANDLE);
extern BOOL WINAPI ReleaseSemaphore(HANDLE, LONG, LPLONG);
extern DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);
extern BOOL WINAPI CloseHandle(HANDLE);

static constexpr DWORD WAIT_OBJECT_0 = 0;

static bool ntsync_device_open()
{
    DIR *dir = opendir("/proc/self/fd");
    if (!dir)
        return false;
    bool found = false;
    while (dirent *entry = readdir(dir)) {
        std::string link = std::string("/proc/self/fd/") + entry->d_name;
        char target[64];
        ssize_t length = readlink(link.c_str(), target, sizeof target - 1);
        if (length > 0) {
            target[length] = '\0';
            found = found || std::strcmp(target, "/dev/ntsync") == 0;
        }
    }
    closedir(dir);
    return found;
}

int main(int argc, char **argv)
{
    if (argc != 2 || (std::strcmp(argv[1], "event") != 0 &&
                      std::strcmp(argv[1], "semaphore") != 0)) {
        std::fputs("usage: ntsync_init_test <event|semaphore>\n", stderr);
        return 2;
    }
    bool semaphore = std::strcmp(argv[1], "semaphore") == 0;
    HANDLE handle = semaphore ? CreateSemaphoreW(nullptr, 0, 1, nullptr)
                              : CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!handle)
        return 1;
    if (semaphore ? !ReleaseSemaphore(handle, 1, nullptr) : !SetEvent(handle))
        return 2;
    if (WaitForSingleObject(handle, 0) != WAIT_OBJECT_0)
        return 3;
    if (!CloseHandle(handle))
        return 4;
    if (access("/dev/ntsync", R_OK) != 0) {
        std::puts("ntsync device not accessible, only the fallback path was exercised");
        return 0;
    }
    if (!ntsync_device_open()) {
        std::fputs("/dev/ntsync exists but the wrapper never opened it\n", stderr);
        return 5;
    }
    std::printf("ntsync initialized, %s round trip passed\n", argv[1]);
    return 0;
}
