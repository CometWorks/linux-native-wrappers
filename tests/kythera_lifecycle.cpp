// Loads the real VRage.KytheraV2.Native.dll with its Kythera_*.dll imports,
// runs the startup, update and teardown sequence of KytheraCore, builds a
// ground navmesh on a test quad to query a path on it, and updates a surface
// agent while GS points at another wrapper's TEB.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include <asm/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" {
void Init(const char *, const char *);
void *KytV2_CreateCore(void *);
void KytV2_Destroy();
void KytV2_CreatePhysicalWorldModel();
void KytV2_DestroyPhysicalWorldModel();
void *KytV2_CreateGroundNavigation();
void KytV2_DestroyGroundNavigation();
void KytV2_InitPathPlanner();
void *KytV2_CreateSurfaceNavigation();
void KytV2_DestroySurfaceNavigation();
void KytV2_InitSurfacePathPlanner();
void *KytV2_CreateFlightNavigation();
void KytV2_InitAvoidance();
void KytV2_InitCover();
void KytV2_DestroyCover();
void KytV2_SetMaxJobThreads(uint32_t);
void KytV2_TerminateJobSystem();
void KytV2_SetUpdateThread(void *);
void KytV2_StartUpdate(void *, double);
void KytV2_SetTestQuad(float, float, float, float);
uint32_t KytV2_CreateNavMesh(void *, float, float, float, float, float, float, int32_t, int32_t,
                             void *);
void KytV2_DestroyNavMesh(void *, uint32_t);
void KytV2_MarkDirty(void *, uint32_t, float, float, float, float, float, float);
int32_t KytV2_IsGenerating(void *, uint32_t);
int32_t KytV2_HasChangesPending(void *, uint32_t);
int32_t KytV2_GetNavMeshTriangleCount(void *, uint32_t);
int32_t KytV2_FindPath(void *, uint32_t, float, float, float, float, float, float, float, int32_t,
                       void *, void *, int32_t);
int32_t KytV2_FindNearestPoint(void *, uint32_t, float, float, float, float, void *, void *,
                               void *);
uint32_t KytV2_CreateSurfaceNavMesh(float, float, float, int32_t, float, int32_t, float, int32_t);
void KytV2_DestroySurfaceNavMesh(uint32_t);
void KytV2_SurfaceMarkDirty(uint32_t, float, float, float, float, float, float);
uint32_t KytV2_CreateSurfaceAgent(float, float, float);
void KytV2_DestroySurfaceAgent(uint32_t);
int32_t KytV2_SurfaceAgent_RequestPath(uint32_t, uint32_t, float, float, float, float, float,
                                       float);
}

namespace {
struct BridgeCallbacks {
    void *allocate;
    void *deallocate;
    void *reallocate;
    void *allocate_and_clear;
    void *request_background_work;
};

// KytheraCoverConfig.Default
struct CoverConfig {
    float values[10] = {1.5f, 0.5f, 1.5f, 0.3f, 0.2f, 0.25f, 1.0f, 1.0f, 1.0f, 1.5f};
};

std::atomic<int> background_work_requests;

void request_background_work() { ++background_work_requests; }

// What another wrapper library leaves behind after a call on this thread: GS
// on its own TEB, whose TLS array has no slots for the Kythera images.
void point_gs_at_foreign_teb()
{
    static thread_local void *foreign_tls[1024];
    static thread_local unsigned char foreign_teb[0x1000];
    *reinterpret_cast<void **>(foreign_teb + 0x30) = foreign_teb;
    *reinterpret_cast<void **>(foreign_teb + 0x58) = foreign_tls;
    syscall(SYS_arch_prctl, ARCH_SET_GS, foreign_teb);
}

int fail(const char *message)
{
    std::fprintf(stderr, "kythera_lifecycle: %s\n", message);
    return 1;
}
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s /path/to/VRage.KytheraV2.Native.dll /path/to/sidecar\n",
                     argv[0]);
        return 2;
    }
    std::filesystem::create_directories(std::filesystem::path(argv[2]).parent_path());
    Init(argv[1], argv[2]);

    // KytheraCore.Initialize
    BridgeCallbacks callbacks{};
    callbacks.request_background_work = reinterpret_cast<void *>(&request_background_work);
    void *core = KytV2_CreateCore(&callbacks);
    if (!core)
        return fail("KytV2_CreateCore returned null");
    KytV2_CreatePhysicalWorldModel();
    void *ground = KytV2_CreateGroundNavigation();
    if (!ground)
        return fail("KytV2_CreateGroundNavigation returned null");
    KytV2_InitPathPlanner();
    void *surface = KytV2_CreateSurfaceNavigation();
    if (surface)
        KytV2_InitSurfacePathPlanner();
    KytV2_CreateFlightNavigation();
    KytV2_InitAvoidance();
    KytV2_InitCover();
    KytV2_SetMaxJobThreads(2);

    // KytheraCore.StartUpdate without agents, as every server world does.
    KytV2_SetUpdateThread(core);
    for (int i = 0; i < 10; ++i)
        KytV2_StartUpdate(core, 1.0 / 60.0);

    // KytheraNavMeshConfig.Default on a 20 m flat quad.
    CoverConfig cover;
    KytV2_SetTestQuad(0, 0, 0, 10);
    uint32_t navmesh = KytV2_CreateNavMesh(ground, 0.12f, 0.04f, 1.8f, 0.6f, 0.25f, 45, 128, 8,
                                           &cover);
    if (navmesh == UINT32_MAX)
        return fail("KytV2_CreateNavMesh failed");
    KytV2_MarkDirty(ground, navmesh, -10, -1, -10, 10, 1, 10);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    do {
        KytV2_StartUpdate(core, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (std::chrono::steady_clock::now() > deadline)
            return fail("navmesh generation did not finish");
    } while (KytV2_IsGenerating(ground, navmesh) || KytV2_HasChangesPending(ground, navmesh));

    int triangles = KytV2_GetNavMeshTriangleCount(ground, navmesh);
    if (triangles <= 0)
        return fail("navmesh has no triangles");

    float x = 0, y = 0, z = 0;
    if (!KytV2_FindNearestPoint(ground, navmesh, 1, 0.5f, 1, 2, &x, &y, &z))
        return fail("KytV2_FindNearestPoint found nothing");

    float *waypoints = nullptr;
    int waypoint_count = 0;
    int status = KytV2_FindPath(ground, navmesh, -5, 0, -5, 5, 0, 5, 1, 2048, &waypoints,
                                &waypoint_count, 0);
    std::printf("navmesh triangles %d, nearest (%.2f %.2f %.2f), path status %d with %d "
                "waypoints, %d background work requests\n",
                triangles, x, y, z, status, waypoint_count, background_work_requests.load());
    if (waypoint_count < 2 || !waypoints)
        return fail("KytV2_FindPath returned no path");
    // Allocated with CoTaskMemAlloc, which .NET frees with free().
    std::free(waypoints);
    KytV2_DestroyNavMesh(ground, navmesh);

    // KytheraSurfaceNavConfig.Default; the surface update reads the bridge's TLS.
    uint32_t surface_navmesh = KytV2_CreateSurfaceNavMesh(0.32f, 0.2f, 4, 100, 2, 100, 2000, 2500);
    if (surface_navmesh == UINT32_MAX)
        return fail("KytV2_CreateSurfaceNavMesh failed");
    KytV2_SurfaceMarkDirty(surface_navmesh, -10, -1, -10, 10, 1, 10);
    uint32_t agent = KytV2_CreateSurfaceAgent(3, 0.5f, 2);
    KytV2_SurfaceAgent_RequestPath(agent, surface_navmesh, -5, 0, -5, 5, 0, 5);
    for (int i = 0; i < 60; ++i) {
        point_gs_at_foreign_teb();
        KytV2_StartUpdate(core, 1.0 / 60.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    KytV2_DestroySurfaceAgent(agent);
    KytV2_DestroySurfaceNavMesh(surface_navmesh);

    // KytheraCore.Dispose
    KytV2_TerminateJobSystem();
    KytV2_DestroyCover();
    if (surface)
        KytV2_DestroySurfaceNavigation();
    KytV2_DestroyGroundNavigation();
    KytV2_DestroyPhysicalWorldModel();
    KytV2_Destroy();
    return 0;
}
