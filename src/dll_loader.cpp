// High-level DLL loader: ties together CPU detection, Win32 shim registration,
// PE loading/linking, TEB setup, TLS initialization, and DllMain invocation.

#include <cstdlib>

#include "dll_loader.h"
#include "support.h"
#include "winlibs.h"
#include "cpu_features.h"

// Top-level SEH handler installed for loaded DLLs.
// Aborts on any unhandled exception from PE code.
static WINAPI EXCEPTION_DISPOSITION ExceptionHandler(
    _EXCEPTION_RECORD *ExceptionRecord,
    _EXCEPTION_FRAME *EstablisherFrame,
    PVOID *ContextRecord,
    _EXCEPTION_FRAME **DispatcherContext)
{
    LogMessage("Top-level exception handler caught exception");
    abort();
}

bool load_dll(pe_image *image, const char *name, const char *sidecar_path)
{
    pe_lock_loader();
    struct loader_guard {
        ~loader_guard() { pe_unlock_loader(); }
    } guard;

    if (!parseCPUInfo()) {
        LogMessage("Cannot parse CPU info");
        return false;
    }

    register_windows_library_functions();

    image->name = name;
    if (!pe_load_library(image->name, sidecar_path, image)) {
        LogMessageA("Missing DLL: %s", image->name);
        return false;
    }

    if (link_pe_images(image, 1)) {
        pe_discard_image(image);
        return false;
    }

    if (!setup_nt_threadinfo(&ExceptionHandler)) {
        pe_discard_image(image);
        return false;
    }
    if (!pe_begin_process_attach(image)) {
        pe_discard_image(image);
        return false;
    }
    if (!pe_initialize_tls_for_current_thread(image, DLL_PROCESS_ATTACH)) {
        pe_finish_process_attach(image, false, false);
        pe_discard_image(image);
        return false;
    }

    bool loaded = !image->entry || image->entry(image->image, DLL_PROCESS_ATTACH, nullptr);
    bool finished = pe_finish_process_attach(image, loaded, true);
    if (!loaded || !finished)
        pe_discard_image(image);
    return finished && loaded;
}
