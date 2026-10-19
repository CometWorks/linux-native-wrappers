#include <cstdint>

// No-op stand-in for VRage.KytheraV2.Native.dll: exports every entry point the game
// declares so AI navigation is simply inactive on Linux. Handles are dummy non-null pointers
// where the game checks them; everything else returns zero.

namespace {
char core;
char ground_navigation;
char surface_navigation;
char flight_navigation;
}

extern "C" {

void Init(const char *, const char *) {}

void KytV2_Agent_CancelNextLeg(std::uint32_t) {}
int KytV2_Agent_GetPath(std::uint32_t, void *, void *, void *) { return 0; }
int KytV2_Agent_HasNextLeg(std::uint32_t) { return 0; }
int KytV2_Agent_QueueNextLeg(std::uint32_t, void *, std::uint32_t, float, float, float, float, int) { return 0; }
int KytV2_Agent_RequestPath(std::uint32_t, void *, std::uint32_t, float, float, float, float, float, float, float, int, int) { return 0; }
void KytV2_Agent_SetArrivalDeceleration(std::uint32_t, int) {}
void KytV2_Agent_SetSpeed(std::uint32_t, float) {}
void KytV2_Agent_Stop(std::uint32_t) {}
int KytV2_Agent_Tick(std::uint32_t, float, float, float, float, void *) { return 0; }
int KytV2_CanSurfacePathTo(std::uint32_t, float, float, float, float, float, float) { return 0; }
std::uint32_t KytV2_CreateAgent(float, float, float) { return 0; }
void *KytV2_CreateCore(void *, int) { return &core; }
std::uint32_t KytV2_CreateFlightAgent(float, float, float) { return 0; }
std::uint32_t KytV2_CreateFlightNavMesh(double, int, int, int) { return 0; }
void *KytV2_CreateFlightNavigation() { return &flight_navigation; }
void *KytV2_CreateGroundNavigation() { return &ground_navigation; }
std::uint32_t KytV2_CreateNavMesh(void *, float, float, float, float, float, float, int, int, float, float, void *) { return 0; }
void KytV2_CreatePhysicalWorldModel() {}
std::uint32_t KytV2_CreateSurfaceAgent(float, float, float) { return 0; }
std::uint32_t KytV2_CreateSurfaceNavMesh(float, float, float, int, float, int, float, int) { return 0; }
void *KytV2_CreateSurfaceNavigation() { return &surface_navigation; }
void KytV2_Destroy() {}
void KytV2_DestroyAgent(std::uint32_t) {}
void KytV2_DestroyCover() {}
void KytV2_DestroyFlightAgent(std::uint32_t) {}
void KytV2_DestroyFlightNavMesh(std::uint32_t) {}
void KytV2_DestroyGroundNavigation() {}
void KytV2_DestroyNavMesh(void *, std::uint32_t) {}
void KytV2_DestroyPhysicalWorldModel() {}
void KytV2_DestroySurfaceAgent(std::uint32_t) {}
void KytV2_DestroySurfaceNavMesh(std::uint32_t) {}
void KytV2_DestroySurfaceNavigation() {}
int KytV2_DiagnoseCoverState(std::uint32_t, float, float, float, float, void *, int) { return 0; }
int KytV2_DoBackgroundWork(void *) { return 0; }
void KytV2_DumpSurfaceTriangleComparison(std::uint32_t, void *, void *) {}
void KytV2_EndGeometryUploadBatch() {}
void KytV2_EndUpdate(void *) {}
int KytV2_FillNavMeshTriangles(void *, std::uint32_t, void *, int) { return 0; }
int KytV2_FindAvoidanceVelocity(void *, std::uint32_t, float, float, float, float, float, float, float, float, float, float, void *, int, float, float, float, void *, void *, void *) { return 0; }
int KytV2_FindNearestPoint(void *, std::uint32_t, float, float, float, float, void *, void *, void *) { return 0; }
int KytV2_FindPath(void *, std::uint32_t, float, float, float, float, float, float, float, int, void *, void *, int) { return 0; }
int KytV2_FindSurfacePath(std::uint32_t, float, float, float, float, float, float, void *, void *) { return 0; }
int KytV2_FlightAgent_GetPath(std::uint32_t, void *, void *, void *) { return 0; }
int KytV2_FlightAgent_RequestPath(std::uint32_t, std::uint32_t, float, float, float, float, float, float, int) { return 0; }
void KytV2_FlightAgent_SetSpeed(std::uint32_t, float) {}
void KytV2_FlightAgent_Stop(std::uint32_t) {}
int KytV2_FlightAgent_Tick(std::uint32_t, float, float, float, float, void *) { return 0; }
int KytV2_FlightEvictOutside(std::uint32_t, void *, int) { return 0; }
int KytV2_FlightGetCellsLoaded(std::uint32_t) { return 0; }
float KytV2_FlightGetGenerationPercentage(std::uint32_t) { return 0; }
int KytV2_FlightHasChangesPending(std::uint32_t) { return 0; }
int KytV2_FlightIsGenerating(std::uint32_t) { return 0; }
void KytV2_FlightMarkDirty(std::uint32_t, float, float, float, float, float, float) {}
int KytV2_FlightNavMesh_GetTrianglesAtomic(std::uint32_t, int, int, void *) { return 0; }
int KytV2_FlightQueueMarkedCells(std::uint32_t) { return 0; }
float KytV2_GetGenerationPercentage(void *, std::uint32_t) { return 0; }
int KytV2_GetNavMeshTriangleCount(void *, std::uint32_t) { return 0; }
int KytV2_GetNavMeshTrianglesAtomic(void *, std::uint32_t, void *) { return 0; }
int KytV2_GetSurfaceNavMeshTrianglesAtomic(std::uint32_t, void *) { return 0; }
int KytV2_HasChangesPending(void *, std::uint32_t) { return 0; }
void KytV2_InitAvoidance() {}
void KytV2_InitCover() {}
void KytV2_InitPathPlanner() {}
void KytV2_InitSurfacePathPlanner() {}
int KytV2_IsGenerating(void *, std::uint32_t) { return 0; }
void KytV2_MarkDirty(void *, std::uint32_t, float, float, float, float, float, float) {}
void *KytV2_MemDumpOutstanding(std::uint32_t) { return nullptr; }
std::int64_t KytV2_MemGetAllocatedBytes() { return 0; }
std::int64_t KytV2_MemGetAllocationCount() { return 0; }
int KytV2_MemHasAllocationStacks() { return 0; }
std::uint32_t KytV2_MemNextEpoch() { return 0; }
void KytV2_Octree_Create(int, int, double) {}
void KytV2_Octree_Destroy() {}
int KytV2_Octree_GenerateFromGeometry(void *, int, void *, int, double, double, double, double, double, double) { return 0; }
void KytV2_Octree_GetStats(void *, void *, void *) {}
int KytV2_Octree_GetTriangleCount(int) { return 0; }
int KytV2_Octree_GetTrianglesAtomic(void *, int, int) { return 0; }
void KytV2_ProfSetEnabled(int) {}
void KytV2_PushGeometry(void *, int, void *, int, float, float, float, float, float, float) {}
void KytV2_PushInvalidCovers(void *, int) {}
int KytV2_QueryCoverPoints(std::uint32_t, float, float, float, float, std::uint32_t, void *, void *) { return 0; }
int KytV2_QueueMarkedTiles(void *, std::uint32_t) { return 0; }
int KytV2_Raycast(void *, std::uint32_t, float, float, float, float, float, float, void *, void *, void *) { return 0; }
void KytV2_ReleaseSessionState() {}
void KytV2_ReleaseWorkerThread(void *) {}
int KytV2_RemoveTiles(void *, std::uint32_t, float, float, float, float, float, float) { return 0; }
void KytV2_SetMaxJobThreads(std::uint32_t) {}
void KytV2_SetTestQuad(float, float, float, float) {}
void KytV2_SetUpdateThread(void *) {}
void KytV2_StartGeometryUploadBatch() {}
void KytV2_StartUpdate(void *, double) {}
int KytV2_SurfaceAgent_RequestPath(std::uint32_t, std::uint32_t, float, float, float, float, float, float) { return 0; }
void KytV2_SurfaceAgent_SetSpeed(std::uint32_t, float) {}
void KytV2_SurfaceAgent_Stop(std::uint32_t) {}
int KytV2_SurfaceAgent_Tick(std::uint32_t, float, float, float, float, float, float, float, float, float, float, void *) { return 0; }
float KytV2_SurfaceGetGenerationPercentage(std::uint32_t) { return 0; }
int KytV2_SurfaceHasChangesPending(std::uint32_t) { return 0; }
int KytV2_SurfaceIsGenerating(std::uint32_t) { return 0; }
void KytV2_SurfaceMarkDirty(std::uint32_t, float, float, float, float, float, float) {}
int KytV2_SurfaceQueueMarkedTiles(std::uint32_t) { return 0; }
void KytV2_TerminateJobSystem() {}
void KytV2_Update(void *) {}
int KytV2_WorkerThreadCount(void *) { return 0; }

}
