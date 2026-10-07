#include "havok_endofstep.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "win_types.h"

// Offsets and addresses below are for the Havok.dll shipped with Space Engineers 1
// (image size 0x15cf000). havok_endofstep_init verifies them against the loaded image
// and disables the hook when anything does not match.
namespace {

// hkpEndOfStepCallbackUtil::unregisterCollision(this, mgr, listener, source)
constexpr uintptr_t kUnregisterRva = 0x5bc820;
const unsigned char kUnregisterPrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
    0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x41, 0x4c, 0x48, 0x8d, 0x59, 0x40,
};
// vtable of the hkpWorldPostSimulationListener sub-object at util + 0x10
constexpr uintptr_t kUtilVtableRva = 0xa4a7f0;
// vtable of Havok::HkContactListener (Keen's per-entity listener)
constexpr uintptr_t kListenerVtableRva = 0xa754e8;
constexpr int kEndOfStepExtensionId = 1001;
constexpr uintptr_t kLinkBase = 0x180000000ull;

// hkpWorld
constexpr size_t kWorldExtensionsData = 0x268;
constexpr size_t kWorldExtensionsSize = 0x270;
constexpr size_t kExtensionId = 0x18;
constexpr size_t kExtensionUtil = 0x28;
// hkpWorldObject
constexpr size_t kEntityWorld = 0x10;
// hkpEndOfStepCallbackUtil
constexpr size_t kUtilCollisionsData = 0x20;
constexpr size_t kUtilCollisionsSize = 0x28;
constexpr size_t kUtilNewData = 0x30;
constexpr size_t kUtilNewSize = 0x38;
constexpr size_t kCollisionEntry = 24;
constexpr size_t kNewCollisionEntry = 32;
// Havok::HkContactListener: hkArray of hkpCollisionEvent copies
constexpr size_t kListenerRecordsData = 0x20;
constexpr size_t kListenerRecordsSize = 0x28;
constexpr size_t kRecordSize = 32;
constexpr size_t kEventSource = 0x0;
constexpr size_t kEventManager = 0x18;

typedef void(WINAPI *unregister_fn)(void *util, void *mgr, void *listener, int32_t source);

uintptr_t g_base;
uintptr_t g_end;
bool g_enabled;
bool g_trace;
bool g_in_place; // LNW_HAVOK_ENDOFSTEP_FIX=inplace: compact the lists instead of using the deferred removal list
const void *g_util_vtable;
const void *g_listener_vtable;
unregister_fn g_unregister;
std::atomic<long> g_detaches{0};
std::atomic<long> g_dropped{0};

template <typename T> T rd(uintptr_t address) { return *reinterpret_cast<const T *>(address); }

const char *rtti_name(uintptr_t vtable)
{
    if (vtable < g_base + 8 || vtable >= g_end)
        return nullptr;
    uintptr_t locator = rd<uintptr_t>(vtable - 8);
    if (locator >= kLinkBase && locator < kLinkBase + (g_end - g_base))
        locator = g_base + (locator - kLinkBase); // not relocated
    if (locator < g_base || locator + 16 > g_end)
        return nullptr;
    uint32_t descriptor = rd<uint32_t>(locator + 0xc);
    if (g_base + descriptor + 0x10 >= g_end)
        return nullptr;
    return reinterpret_cast<const char *>(g_base + descriptor + 0x10);
}

bool rtti_is(uintptr_t vtable, const char *expected)
{
    const char *name = rtti_name(vtable);
    return name && std::strcmp(name, expected) == 0;
}

void disable(const char *why)
{
    g_enabled = false;
    std::fprintf(stderr, "[LinuxCompat] Havok end-of-step callback fix disabled: %s\n", why);
}

char *find_util(void *world)
{
    auto base = reinterpret_cast<uintptr_t>(world);
    char **extensions = rd<char **>(base + kWorldExtensionsData);
    int count = rd<int>(base + kWorldExtensionsSize);
    for (int i = 0; i < count; ++i) {
        char *extension = extensions[i];
        if (extension && *reinterpret_cast<int *>(extension + kExtensionId) == kEndOfStepExtensionId) {
            char *util = extension + kExtensionUtil;
            if (*reinterpret_cast<const void **>(util + 0x10) != g_util_vtable) {
                disable("end-of-step util has an unexpected vtable");
                return nullptr;
            }
            return util;
        }
    }
    return nullptr;
}

// Removes every {mgr, listener, source} match from an hkArray, keeping the order. This is
// the algorithm the bugfixes plugin uses, where calling into Havok is not an option.
long remove_entries(char *array, size_t entry_size, void *mgr, void *listener, int source)
{
    char *data = *reinterpret_cast<char **>(array);
    int count = *reinterpret_cast<int *>(array + 8);
    if (!data || count <= 0)
        return 0;
    int kept = 0;
    for (int i = 0; i < count; ++i) {
        char *entry = data + i * entry_size;
        bool matches = *reinterpret_cast<void **>(entry) == mgr && *reinterpret_cast<void **>(entry + 8) == listener &&
                       *reinterpret_cast<int *>(entry + 16) == source;
        if (matches)
            continue;
        if (kept != i)
            std::memmove(data + kept * entry_size, entry, entry_size);
        ++kept;
    }
    *reinterpret_cast<int *>(array + 8) = kept;
    return count - kept;
}

bool registered(const char *util, void *mgr, void *listener, int source)
{
    const char *data = *reinterpret_cast<char *const *>(util + kUtilCollisionsData);
    int count = *reinterpret_cast<const int *>(util + kUtilCollisionsSize);
    for (int i = 0; i < count; ++i) {
        const char *entry = data + i * kCollisionEntry;
        if (*reinterpret_cast<void *const *>(entry) == mgr && *reinterpret_cast<void *const *>(entry + 8) == listener &&
            *reinterpret_cast<const int *>(entry + 16) == source)
            return true;
    }
    data = *reinterpret_cast<char *const *>(util + kUtilNewData);
    count = *reinterpret_cast<const int *>(util + kUtilNewSize);
    for (int i = 0; i < count; ++i) {
        const char *entry = data + i * kNewCollisionEntry;
        if (*reinterpret_cast<void *const *>(entry) == mgr && *reinterpret_cast<void *const *>(entry + 8) == listener &&
            *reinterpret_cast<const int *>(entry + 16) == source)
            return true;
    }
    return false;
}

} // namespace

void havok_endofstep_init(void *image, size_t image_size)
{
    g_base = reinterpret_cast<uintptr_t>(image);
    g_end = g_base + image_size;
    // Opt-in: the Bugfixes plugin ships the same workaround for both platforms, so the
    // hook stays off unless a test or an experiment asks for it.
    const char *setting = std::getenv("LNW_HAVOK_ENDOFSTEP_FIX");
    if (!setting || std::strcmp(setting, "0") == 0)
        return;
    g_trace = std::strcmp(setting, "trace") == 0;
    g_in_place = std::strcmp(setting, "inplace") == 0;
    if (g_end - g_base < kUnregisterRva + sizeof(kUnregisterPrologue) || g_end - g_base < kListenerVtableRva + 8) {
        disable("Havok.dll image is smaller than expected");
        return;
    }
    if (std::memcmp(reinterpret_cast<const void *>(g_base + kUnregisterRva), kUnregisterPrologue,
                    sizeof(kUnregisterPrologue)) != 0) {
        disable("unregisterCollision code does not match");
        return;
    }
    if (!rtti_is(g_base + kUtilVtableRva, ".?AVhkpEndOfStepCallbackUtil@@")) {
        disable("hkpEndOfStepCallbackUtil vtable does not match");
        return;
    }
    if (!rtti_is(g_base + kListenerVtableRva, ".?AVHkContactListener@Havok@@")) {
        disable("Havok::HkContactListener vtable does not match");
        return;
    }
    g_util_vtable = reinterpret_cast<const void *>(g_base + kUtilVtableRva);
    g_listener_vtable = reinterpret_cast<const void *>(g_base + kListenerVtableRva);
    g_unregister = reinterpret_cast<unregister_fn>(g_base + kUnregisterRva);
    g_enabled = true;
}

void havok_endofstep_before_set_contact_listener(void *entity, void *listener, bool value)
{
    if (!g_enabled || value || !entity || !listener)
        return;
    if (*reinterpret_cast<const void *const *>(listener) != g_listener_vtable)
        return; // the sound listener and others keep no registrations
    auto records = *reinterpret_cast<const char *const *>(static_cast<char *>(listener) + kListenerRecordsData);
    int count = *reinterpret_cast<const int *>(static_cast<char *>(listener) + kListenerRecordsSize);
    if (count <= 0 || !records)
        return;
    void *world = *reinterpret_cast<void **>(static_cast<char *>(entity) + kEntityWorld);
    if (!world)
        return; // out of the world: no agents, so nothing can still be registered
    char *util = find_util(world);
    if (!util)
        return;
    long dropped = 0;
    for (int i = 0; i < count; ++i) {
        const char *record = records + i * kRecordSize;
        void *mgr = *reinterpret_cast<void *const *>(record + kEventManager);
        int source = *reinterpret_cast<const int *>(record + kEventSource);
        if (g_in_place) {
            dropped += remove_entries(util + kUtilCollisionsData, kCollisionEntry, mgr, listener, source);
            dropped += remove_entries(util + kUtilNewData, kNewCollisionEntry, mgr, listener, source);
        } else if (registered(util, mgr, listener, source)) {
            g_unregister(util, mgr, listener, source);
            ++dropped;
        }
    }
    ++g_detaches;
    g_dropped += dropped;
    if (g_trace && dropped)
        std::fprintf(stderr, "[LinuxCompat] Havok end-of-step fix: listener %p detached from entity %p with %ld live "
                             "registrations dropped (total detaches %ld, dropped %ld)\n",
                     listener, entity, dropped, g_detaches.load(), g_dropped.load());
}
