#include <cstdint>

namespace {
char core;
char ground_navigation;
char surface_navigation;
char flight_navigation;
}

extern "C" {

void Init(const char *, const char *) {}

void *KytV2_CreateCore(void *) { return &core; }
void KytV2_Destroy() {}
void KytV2_CreatePhysicalWorldModel() {}
void KytV2_DestroyPhysicalWorldModel() {}
void *KytV2_CreateGroundNavigation() { return &ground_navigation; }
void KytV2_DestroyGroundNavigation() {}
void KytV2_InitPathPlanner() {}
void *KytV2_CreateSurfaceNavigation() { return &surface_navigation; }
void KytV2_DestroySurfaceNavigation() {}
void KytV2_InitSurfacePathPlanner() {}
void *KytV2_CreateFlightNavigation() { return &flight_navigation; }
void KytV2_InitAvoidance() {}
void KytV2_InitCover() {}
void KytV2_DestroyCover() {}
void KytV2_SetMaxJobThreads(std::uint32_t) {}
void KytV2_TerminateJobSystem() {}
void KytV2_SetUpdateThread(void *) {}
void KytV2_StartUpdate(void *, double) {}

}
