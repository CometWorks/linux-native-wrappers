// Stress harness for SE1-0015: dangling entries in Havok's hkpEndOfStepCallbackUtil.
//
// Drives Havok.dll through libHavok.so exactly the way MyPhysics.StepWorldsParallel does,
// keeps a pile of boxes colliding, and churns bodies with the lifetime sequences the game
// uses (and a few it should not). Before every FinishMtStep it walks the world's end-of-step
// callback list and checks that every registered contact manager is still alive. A dead
// manager in that list is exactly what the game crashes on.
//
// usage: havok_endofstep_test /path/to/Havok.dll /path/to/sidecar SCENARIO [options]
//   SCENARIO: baseline | dispose-first | deferred-remove | callback-remove | detach-toggle | mixed
//             endstep-remove | endstep-dispose | endstep-detach | double-remove | deferred-probe | field
//   field: the sequence recovered from the SE1-0015 core: listeners toggled off while touching,
//          other bodies removed in the game's order.
//   endstep-*: the mutation runs inside a ManifoldAtEndOfStep contact callback, i.e. inside
//              HkWorld_FinishMtStep on the main thread, the way game contact handlers run.
//   --steps N          stop after N steps (default: run until a violation or forever)
//   --seed N           PRNG seed (default 1)
//   --bodies N         dynamic boxes kept alive (default 40)
//   --churn P          per-step probability (percent) of removing a body (default 20)
//   --threads N        Havok worker threads (default 1)
//   --report N         print a status line every N steps (default 5000)
//   --keep-going       count violations instead of exiting on the first one
// exit code: 0 clean, 1 violation found, 2 usage, 3 setup failure

#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dlfcn.h>
#include <random>
#include <set>
#include <string>
#include <vector>

struct Vector3 { float X, Y, Z; };
struct BoundingBox { Vector3 Min, Max; };

extern "C" {
void Init(const char *, const char *);
void HkBaseSystem_Init(int32_t, void *, bool);
void *HkJobThreadPool_CreateWithNumThreads(int32_t);
void HkJobThreadPool_ExecuteJobQueue(void *, void *);
void HkJobThreadPool_WaitForCompletion(void *);
void *HkJobQueue_CreateWithNumThreads(int32_t);
void HkJobQueue_SetWaitPolicy(void *, int32_t);
void HkJobQueue_ProcessAllJobs(void *);
void *HkWorld_CreateCInfo(void *, void *);
void HkWorld_RegisterWithJobQueue(void *, void *);
void HkWorld_MarkForWrite(void *);
void HkWorld_UnmarkForWrite(void *);
void HkWorld_ExecutePendingCriticalOperations(void *);
bool HkWorld_InitMtStep(void *, void *, float);
bool HkWorld_FinishMtStep(void *, void *, void *);
void HkWorld_LockCriticalOperations(void *);
void HkWorld_UnlockCriticalOperations(void *);
void HkWorld_AddEntity(void *, void *);
void HkWorld_RemoveEntity(void *, void *);
void *HkBoxShape_Create(Vector3);
void *HkRigidBodyCinfo_Create();
void HkRigidBodyCinfo_Release(void *);
void HkRigidBodyCinfo_SetShape(void *, void *);
void HkRigidBodyCinfo_SetMotionType(void *, int32_t);
void HkRigidBodyCinfo_SetQualityType(void *, int32_t);
void HkRigidBodyCinfo_SetPosition(void *, Vector3);
void HkRigidBodyCinfo_SetMass(void *, float);
void HkRigidBodyCinfo_SetFriction(void *, float);
void HkRigidBodyCinfo_SetRestitution(void *, float);
void HkRigidBodyCinfo_CalculateBoxInertiaTensor(void *, Vector3, float);
void *HkRigidBody_CreateWithCustomVelocity(void *);
void HkRigidBody_SetLinearVelocity(void *, Vector3);
void HkReferenceObject_RemoveReference(void *);
void *HkContactListener_Create(void *, void *, void *, int32_t);
void HkEntity_SetContactListener(void *, void *, bool);
void HkGlobal_ReleasePtr(void *);
void HkContactPointEvent_GetFieldOffsets(int32_t *, int32_t *, int32_t *, int32_t *, int32_t *, int32_t *, int32_t *);
int32_t HkReferenceObject_ReferenceCount(void *);
void HkRigidBody_RemoveFromWorld(void *);
}

#pragma pack(push, 1)
struct WorldCInfo {
    Vector3 Gravity;
    int32_t BroadPhaseQuerySize;
    float ContactRestingVelocity;
    uint8_t BroadPhaseType;
    uint8_t BroadPhaseBorderBehaviour;
    bool MtPostponeAndSortBroadPhaseBorderCallbacks;
    BoundingBox BroadPhaseWorldAabb;
    float CollisionTolerance;
    float ExpectedMaxLinearVelocity;
    int32_t SizeOfToiEventQueue;
    float ExpectedMinPsiDeltaTime;
    int32_t BroadPhaseNumMarkers;
    uint8_t ContactPointGeneration;
    bool AllowToSkipConfirmedCallbacks;
    float SolverTau;
    float SolverDamp;
    int32_t SolverIterations;
    int32_t SolverMicrosteps;
    float MaxConstraintViolation;
    bool ForceCoherentConstraintOrderingInSolver;
    float SnapCollisionToConvexEdgeThreshold;
    float SnapCollisionToConcaveEdgeThreshold;
    bool EnableToiWeldRejection;
    bool EnableDeprecatedWelding;
    float IterativeLinearCastEarlyOutDistance;
    int32_t IterativeLinearCastMaxIterations;
    uint8_t DeactivationNumInactiveFramesSelectFlag0;
    uint8_t DeactivationNumInactiveFramesSelectFlag1;
    uint8_t DeactivationIntegrateCounter;
    bool ShouldActivateOnRigidBodyTransformChange;
    float DeactivationReferenceDistance;
    float ToiCollisionResponseRotateNormal;
    bool UseCompoundSpuElf;
    int32_t MaxSectorsPerMidphaseCollideTask;
    int32_t MaxSectorsPerNarrowphaseCollideTask;
    bool ProcessToisMultithreaded;
    int32_t MaxEntriesPerToiMidphaseCollideTask;
    int32_t MaxEntriesPerToiNarrowphaseCollideTask;
    int32_t MaxNumToiCollisionPairsSinglethreaded;
    float NumToisTillAllowedPenetrationSimplifiedToi;
    float NumToisTillAllowedPenetrationToi;
    float NumToisTillAllowedPenetrationToiHigher;
    float NumToisTillAllowedPenetrationToiForced;
    bool EnableDeactivation;
    uint8_t SimulationType;
    bool EnableSimulationIslands;
    uint32_t MinDesiredIslandSize;
    bool ProcessActionsInSingleThread;
    bool AllowIntegrationOfIslandsWithoutConstraintsInASeparateJob;
    float FrameMarkerPsiSnap;
    bool FireCollisionCallbacks;
};
#pragma pack(pop)
static_assert(sizeof(WorldCInfo) == 172, "CInfo layout drifted from HkWorld.CInfo (Pack=1)");

// Same values as MyPhysics.CreateWorldCInfo on top of HkWorld.CInfo.Create().
static WorldCInfo game_world_cinfo(float broadphase_size)
{
    WorldCInfo c{};
    c.Gravity = {0.f, -9.8f, 0.f};
    c.BroadPhaseQuerySize = 1024;
    c.ContactRestingVelocity = 1.f;
    c.BroadPhaseType = 0;            // SAP
    c.BroadPhaseBorderBehaviour = 2; // REMOVE_ENTITY
    c.BroadPhaseWorldAabb = {{-broadphase_size / 2, -broadphase_size / 2, -broadphase_size / 2},
                             {broadphase_size / 2, broadphase_size / 2, broadphase_size / 2}};
    c.CollisionTolerance = 0.1f;
    c.ExpectedMaxLinearVelocity = 200.f;
    c.SizeOfToiEventQueue = 250;
    c.ExpectedMinPsiDeltaTime = 1.f / 60.f;
    c.ContactPointGeneration = 1; // REJECT_DUBIOUS
    c.SolverTau = 0.6f;
    c.SolverDamp = 1.f;
    c.SolverIterations = 8;
    c.SolverMicrosteps = 2;
    c.MaxConstraintViolation = 1.8446726E+19f;
    c.SnapCollisionToConvexEdgeThreshold = 0.524f;
    c.SnapCollisionToConcaveEdgeThreshold = 0.698f;
    c.IterativeLinearCastEarlyOutDistance = 0.01f;
    c.IterativeLinearCastMaxIterations = 20;
    c.ShouldActivateOnRigidBodyTransformChange = true;
    c.DeactivationReferenceDistance = 0.02f;
    c.ToiCollisionResponseRotateNormal = 0.2f;
    c.MaxSectorsPerMidphaseCollideTask = 2;
    c.MaxSectorsPerNarrowphaseCollideTask = 4;
    c.ProcessToisMultithreaded = true;
    c.MaxEntriesPerToiMidphaseCollideTask = -1;
    c.MaxEntriesPerToiNarrowphaseCollideTask = -1;
    c.NumToisTillAllowedPenetrationSimplifiedToi = 3.f;
    c.NumToisTillAllowedPenetrationToi = 3.f;
    c.NumToisTillAllowedPenetrationToiHigher = 4.f;
    c.NumToisTillAllowedPenetrationToiForced = 20.f;
    c.EnableDeactivation = true;
    c.SimulationType = 3; // MULTITHREADED
    c.EnableSimulationIslands = true;
    c.MinDesiredIslandSize = 2;
    c.ProcessActionsInSingleThread = true;
    c.FrameMarkerPsiSnap = 0.0001f;
    c.FireCollisionCallbacks = true;
    return c;
}

// ---- Havok object layouts read from the crash core and the disassembly (SE1 Havok.dll) ----
// hkpWorld: extension array at +0x268 (data) / +0x270 (size); extension id at +0x18.
// Extension 1001 embeds hkpEndOfStepCallbackUtil at +0x28:
//   +0x20 hkArray<Collision>  m_collisions        (24-byte entries: mgr, listener, source, seq)
//   +0x30 hkArray<NewCollision> m_newCollisions   (32-byte entries: mgr, listener, source, seq)
//   +0x40 hkArray<Collision>  m_removedCollisions (24-byte entries)
// hkpCollisionEvent: +0 source, +8 bodyA, +0x10 bodyB, +0x18 contact manager.
// Keen's HkContactListener: +0x20 hkArray of 32-byte hkpCollisionEvent copies, size at +0x28.

static uintptr_t g_pe_base;
static uintptr_t g_pe_end;
static const void *g_mgr_vtable;

static const char *rtti_name(const void *object)
{
    uintptr_t vt = *reinterpret_cast<const uintptr_t *>(object);
    if (vt < g_pe_base || vt >= g_pe_end)
        return nullptr;
    uintptr_t col = *reinterpret_cast<const uintptr_t *>(vt - 8);
    const uintptr_t link_base = 0x180000000ull;
    if (col >= link_base && col < link_base + (g_pe_end - g_pe_base))
        col = g_pe_base + (col - link_base);
    if (col < g_pe_base || col >= g_pe_end)
        return nullptr;
    uint32_t td = *reinterpret_cast<const uint32_t *>(col + 0xc);
    return reinterpret_cast<const char *>(g_pe_base + td + 0x10);
}

static char *endofstep_util(void *world)
{
    char **ext = *reinterpret_cast<char ***>(static_cast<char *>(world) + 0x268);
    int n = *reinterpret_cast<int *>(static_cast<char *>(world) + 0x270);
    for (int i = 0; i < n; ++i)
        if (*reinterpret_cast<int *>(ext[i] + 0x18) == 1001)
            return ext[i] + 0x28;
    return nullptr;
}

struct Entry { void *mgr; void *listener; int source; int seq; };

static bool mgr_alive(const void *mgr)
{
    // A freed block has its first word overwritten by Havok's free list.
    return mgr && *reinterpret_cast<const void *const *>(mgr) == g_mgr_vtable;
}

// ---- harness state ----
struct Body {
    void *body;
    void *listener;
    bool attached;
    bool pending_removal; // RemoveEntity was queued behind a critical-operations lock
    int id;
};

static std::vector<Body> g_bodies;
static std::vector<Body> g_zombies;
static std::set<void *> g_live_listeners;
static std::deque<std::string> g_history;
static long g_step;
// Havok fires contact callbacks from the worker thread and from the main thread (which also
// processes jobs), so everything a callback touches is atomic or under g_callback_mutex.
static std::atomic<long> g_collisions_added{0}, g_collisions_removed{0}, g_contacts{0};
static long g_removed_bodies, g_toggles;
static std::mutex g_callback_mutex;
static long g_violations, g_dead_listener_entries;
static void *g_world;
static const char *g_scenario;
static bool g_callback_remove;
static int g_endstep_mode; // 0 none, 1 game-order removal, 2 dispose-first, 3 detach+delete only
static int g_type_offset;
static int g_respawn;
static bool g_probe;
static std::mt19937 g_rng;
static void remove_game_order(Body &b);
static void remove_dispose_first(Body &b);
static void detach_and_delete_listener(Body &b);

static void note(const std::string &s)
{
    g_history.push_back("step " + std::to_string(g_step) + ": " + s);
    if (g_history.size() > 12)
        g_history.pop_front();
}

static int body_index(void *body)
{
    for (size_t i = 0; i < g_bodies.size(); ++i)
        if (g_bodies[i].body == body)
            return static_cast<int>(i);
    return -1;
}

// Callbacks arrive with the Keen listener pointer and the event pointer (bridged by the shim).
static void on_contact(void *listener, void *event)
{
    ++g_contacts;
    int type = *reinterpret_cast<int *>(static_cast<char *>(event) + g_type_offset);
    if (g_endstep_mode && type == 3) { // ManifoldAtEndOfStep, fired from FinishMtStep
        char *e = static_cast<char *>(event);
        void *body = *reinterpret_cast<void **>(e + 8 + 8 * (*reinterpret_cast<int *>(e) & 1));
        int idx = body_index(body);
        if (idx < 0 || g_bodies[idx].listener != listener || g_rng() % 100 >= 5)
            return;
        Body &b = g_bodies[idx];
        note(std::string(g_endstep_mode == 1 ? "endstep-remove" : g_endstep_mode == 2 ? "endstep-dispose" : "endstep-detach") +
             " body " + std::to_string(b.id));
        if (g_endstep_mode == 1) remove_game_order(b);
        else if (g_endstep_mode == 2) remove_dispose_first(b);
        else { detach_and_delete_listener(b); return; }
        g_bodies.erase(g_bodies.begin() + idx);
        ++g_removed_bodies;
        ++g_respawn;
        return;
    }
    if (!g_callback_remove || type == 3)
        return;
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    // Model MyMissile-style removal requested from inside a contact callback: the world is
    // locked during the step, so Havok queues it as a pending critical operation.
    char *e = static_cast<char *>(event);
    void *body = *reinterpret_cast<void **>(e + 8);
    int idx = body_index(body);
    if (idx < 0 || !g_bodies[idx].attached || g_bodies[idx].pending_removal)
        return;
    if (g_bodies[idx].listener != listener)
        return;
    if (g_rng() % 100 >= 5)
        return;
    HkWorld_RemoveEntity(g_world, body);
    g_bodies[idx].pending_removal = true;
    note("callback-remove body " + std::to_string(g_bodies[idx].id));
}

static void on_collision_added(void *, void *event)
{
    ++g_collisions_added;
    if (!g_mgr_vtable) {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        if (g_mgr_vtable)
            return;
        void *mgr = *reinterpret_cast<void **>(static_cast<char *>(event) + 0x18);
        const char *name = rtti_name(mgr);
        if (!name || std::strcmp(name, ".?AVhkpSimpleConstraintContactMgr@@") != 0) {
            std::fprintf(stderr, "contact manager RTTI mismatch: %s\n", name ? name : "(none)");
            std::exit(3);
        }
        g_mgr_vtable = *reinterpret_cast<void **>(mgr);
    }
}

static void on_collision_removed(void *, void *) { ++g_collisions_removed; }
static void on_broadphase_exit(void *, void *) {}

static void dump_entry(const char *list, int i, const Entry &e)
{
    std::printf("  %s[%d]: mgr=%p (%s) listener=%p (%s) source=%d seq=%d\n", list, i, e.mgr,
                mgr_alive(e.mgr) ? "alive" : "DEAD", e.listener,
                g_live_listeners.count(e.listener) ? "alive" : "DELETED", e.source, e.seq);
}

static bool check_world(const char *when)
{
    char *util = endofstep_util(g_world);
    if (!util) {
        std::fprintf(stderr, "end-of-step util not found in world\n");
        std::exit(3);
    }
    if (!g_mgr_vtable)
        return true;
    Entry *coll = *reinterpret_cast<Entry **>(util + 0x20);
    int ncoll = *reinterpret_cast<int *>(util + 0x28);
    Entry *removed = *reinterpret_cast<Entry **>(util + 0x40);
    int nremoved = *reinterpret_cast<int *>(util + 0x48);
    char *fresh = *reinterpret_cast<char **>(util + 0x30);
    int nfresh = *reinterpret_cast<int *>(util + 0x38);

    auto is_removed = [&](const Entry &e) {
        for (int i = 0; i < nremoved; ++i)
            if (removed[i].mgr == e.mgr && removed[i].listener == e.listener && removed[i].source == e.source)
                return true;
        return false;
    };

    bool bad = false;
    for (int i = 0; i < ncoll; ++i) {
        if (!mgr_alive(coll[i].mgr) && !is_removed(coll[i])) {
            if (!bad)
                std::printf("VIOLATION (%s, step %ld, scenario %s): dead contact manager in m_collisions\n",
                            when, g_step, g_scenario);
            bad = true;
            dump_entry("m_collisions", i, coll[i]);
        } else if (!g_live_listeners.count(coll[i].listener)) {
            ++g_dead_listener_entries;
        }
    }
    for (int i = 0; i < nfresh; ++i) {
        Entry e = *reinterpret_cast<Entry *>(fresh + i * 32);
        if (!mgr_alive(e.mgr) && !is_removed(e)) {
            if (!bad)
                std::printf("VIOLATION (%s, step %ld, scenario %s): dead contact manager in m_newCollisions\n",
                            when, g_step, g_scenario);
            bad = true;
            dump_entry("m_newCollisions", i, e);
        }
    }
    if (bad) {
        ++g_violations;
        std::printf("  list sizes: collisions=%d new=%d removed=%d, bodies=%zu\n", ncoll, nfresh, nremoved,
                    g_bodies.size());
        std::printf("  recent mutations:\n");
        for (const auto &h : g_history)
            std::printf("    %s\n", h.c_str());
        std::fflush(stdout);
    }
    return !bad;
}

static void *g_box_shape;
static int g_next_id;

static Body spawn_body(Vector3 pos)
{
    void *ci = HkRigidBodyCinfo_Create();
    HkRigidBodyCinfo_SetShape(ci, g_box_shape);
    HkRigidBodyCinfo_SetMotionType(ci, 3); // Box_Inertia
    HkRigidBodyCinfo_SetQualityType(ci, 4); // Moving
    HkRigidBodyCinfo_SetPosition(ci, pos);
    HkRigidBodyCinfo_SetMass(ci, 100.f);
    HkRigidBodyCinfo_CalculateBoxInertiaTensor(ci, {0.5f, 0.5f, 0.5f}, 100.f);
    HkRigidBodyCinfo_SetFriction(ci, 0.5f);
    HkRigidBodyCinfo_SetRestitution(ci, 0.1f);
    void *body = HkRigidBody_CreateWithCustomVelocity(ci);
    HkRigidBodyCinfo_Release(ci);
    // HkEntity.Init: one listener per body, attached like MyGridPhysics does.
    void *listener = HkContactListener_Create(reinterpret_cast<void *>(&on_contact),
                                              reinterpret_cast<void *>(&on_collision_added),
                                              reinterpret_cast<void *>(&on_collision_removed), 0);
    g_live_listeners.insert(listener);
    HkEntity_SetContactListener(body, listener, true);
    HkWorld_AddEntity(g_world, body);
    return {body, listener, true, false, g_next_id++};
}

static void detach_and_delete_listener(Body &b)
{
    if (b.attached)
        HkEntity_SetContactListener(b.body, b.listener, false);
    b.attached = false;
    g_live_listeners.erase(b.listener);
    HkGlobal_ReleasePtr(b.listener);
    b.listener = nullptr;
}

// The order MyPhysicsBody.Close uses: Deactivate (RemoveRigidBody) then CloseRigidBody (Dispose).
static void remove_game_order(Body &b)
{
    if (!b.pending_removal)
        HkWorld_RemoveEntity(g_world, b.body);
    detach_and_delete_listener(b);
    HkReferenceObject_RemoveReference(b.body);
}

// HkEntity.Dispose before the body left the world (finalizer thread, or a Dispose without Deactivate).
static void remove_dispose_first(Body &b)
{
    detach_and_delete_listener(b);
    if (!b.pending_removal)
        HkWorld_RemoveEntity(g_world, b.body);
    HkReferenceObject_RemoveReference(b.body);
}

// RemoveEntity while critical operations are locked: Havok queues it, the wrapper is disposed
// right away, and the queued removal runs at the next ExecutePendingCriticalOperations.
static void probe(const char *stage, const Body &b)
{
    if (!g_probe)
        return;
    std::printf("  probe body %d %s: refcount=%d world=%p\n", b.id, stage, HkReferenceObject_ReferenceCount(b.body),
                *reinterpret_cast<void **>(static_cast<char *>(b.body) + 0x10));
    check_world(stage);
}

static void remove_deferred(Body &b)
{
    probe("start", b);
    if (!b.pending_removal) {
        HkWorld_LockCriticalOperations(g_world);
        HkWorld_RemoveEntity(g_world, b.body);
        probe("after queued RemoveEntity", b);
        HkWorld_UnlockCriticalOperations(g_world);
        probe("after Unlock", b);
        b.pending_removal = true;
    }
    detach_and_delete_listener(b);
    probe("after detach+delete listener", b);
    HkReferenceObject_RemoveReference(b.body);
    if (g_probe)
        std::printf("  probe body %d after RemoveReference: world=%p\n", b.id,
                    *reinterpret_cast<void **>(static_cast<char *>(b.body) + 0x10));
    check_world("after RemoveReference");
}

static void toggle_listener(Body &b)
{
    HkEntity_SetContactListener(b.body, b.listener, !b.attached);
    b.attached = !b.attached;
    ++g_toggles;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s Havok.dll sidecar SCENARIO [--steps N] [--seed N] [--bodies N] "
                             "[--churn P] [--threads N] [--report N] [--keep-going]\n", argv[0]);
        return 2;
    }
    g_scenario = argv[3];
    long max_steps = -1, report_every = 5000;
    int seed = 1, body_count = 40, churn = 20, threads = 1;
    bool keep_going = false;
    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::atol(argv[++i]) : 0; };
        if (a == "--steps") max_steps = next();
        else if (a == "--seed") seed = static_cast<int>(next());
        else if (a == "--bodies") body_count = static_cast<int>(next());
        else if (a == "--churn") churn = static_cast<int>(next());
        else if (a == "--threads") threads = static_cast<int>(next());
        else if (a == "--report") report_every = next();
        else if (a == "--keep-going") keep_going = true;
        else { std::fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    std::string scenario = g_scenario;
    const char *known[] = {"baseline", "dispose-first", "deferred-remove", "callback-remove", "detach-toggle", "mixed",
                           "endstep-remove", "endstep-dispose", "endstep-detach", "double-remove", "deferred-probe",
                           "field"};
    if (std::find(std::begin(known), std::end(known), scenario) == std::end(known)) {
        std::fprintf(stderr, "unknown scenario %s\n", g_scenario);
        return 2;
    }
    g_callback_remove = scenario == "callback-remove";
    g_endstep_mode = scenario == "endstep-remove" ? 1 : scenario == "endstep-dispose" ? 2 : scenario == "endstep-detach" ? 3 : 0;
    g_probe = scenario == "deferred-probe";
    if (g_endstep_mode)
        churn = 0; // all mutations happen inside callbacks
    g_rng.seed(seed);

    Init(argv[1], argv[2]);
    void *pe = dlopen(argv[2], RTLD_NOW | RTLD_NOLOAD);
    if (!pe) {
        std::fprintf(stderr, "sidecar %s is not loaded\n", argv[2]);
        return 3;
    }
    // __lnw_pe_image_start has value 0, which glibc's dlsym treats as undefined, so take the
    // load base from the end symbol instead.
    g_pe_end = reinterpret_cast<uintptr_t>(dlsym(pe, "__lnw_pe_image_end"));
    Dl_info info{};
    if (g_pe_end && dladdr(reinterpret_cast<void *>(g_pe_end), &info))
        g_pe_base = reinterpret_cast<uintptr_t>(info.dli_fbase);
    if (!g_pe_base || !g_pe_end || std::memcmp(reinterpret_cast<void *>(g_pe_base), "MZ", 2) != 0) {
        std::fprintf(stderr, "PE image bounds not exported by the sidecar\n");
        return 3;
    }

    HkBaseSystem_Init(16 * 1024 * 1024, nullptr, false);
    {
        int32_t sv, pr, cp, f1, f2, f3;
        HkContactPointEvent_GetFieldOffsets(&sv, &g_type_offset, &pr, &cp, &f1, &f2, &f3);
    }
    void *pool = HkJobThreadPool_CreateWithNumThreads(threads);
    void *queue = HkJobQueue_CreateWithNumThreads(threads + 1);
    WorldCInfo cinfo = game_world_cinfo(1000.f);
    g_world = HkWorld_CreateCInfo(&cinfo, reinterpret_cast<void *>(&on_broadphase_exit));
    if (!g_world)
        return 3;
    HkWorld_MarkForWrite(g_world);
    HkWorld_RegisterWithJobQueue(g_world, queue);
    char *util = endofstep_util(g_world);
    const char *util_name = util ? rtti_name(util + 0x10) : nullptr;
    if (!util_name || std::strcmp(util_name, ".?AVhkpEndOfStepCallbackUtil@@") != 0) {
        std::fprintf(stderr, "end-of-step util layout check failed: %s\n", util_name ? util_name : "(none)");
        return 3;
    }

    // Ground: a fixed slab; the pile sits in a 6x6 m footprint so boxes keep touching.
    void *ground_shape = HkBoxShape_Create({20.f, 0.5f, 20.f});
    void *gci = HkRigidBodyCinfo_Create();
    HkRigidBodyCinfo_SetShape(gci, ground_shape);
    HkRigidBodyCinfo_SetMotionType(gci, 5); // Fixed
    HkRigidBodyCinfo_SetPosition(gci, {0.f, -0.5f, 0.f});
    void *ground = HkRigidBody_CreateWithCustomVelocity(gci);
    HkRigidBodyCinfo_Release(gci);
    HkWorld_AddEntity(g_world, ground);
    // Walls keep the pile together.
    for (int w = 0; w < 4; ++w) {
        void *ws = HkBoxShape_Create(w < 2 ? Vector3{0.5f, 5.f, 4.f} : Vector3{4.f, 5.f, 0.5f});
        void *wci = HkRigidBodyCinfo_Create();
        HkRigidBodyCinfo_SetShape(wci, ws);
        HkRigidBodyCinfo_SetMotionType(wci, 5);
        float s = (w % 2) ? 3.5f : -3.5f;
        HkRigidBodyCinfo_SetPosition(wci, w < 2 ? Vector3{s, 5.f, 0.f} : Vector3{0.f, 5.f, s});
        void *wall = HkRigidBody_CreateWithCustomVelocity(wci);
        HkRigidBodyCinfo_Release(wci);
        HkWorld_AddEntity(g_world, wall);
    }
    g_box_shape = HkBoxShape_Create({0.5f, 0.5f, 0.5f});

    std::uniform_real_distribution<float> xz(-2.5f, 2.5f);
    auto spawn_position = [&]() { return Vector3{xz(g_rng), 3.f + (g_rng() % 40) * 0.25f, xz(g_rng)}; };
    for (int i = 0; i < body_count; ++i)
        g_bodies.push_back(spawn_body(spawn_position()));

    std::printf("scenario=%s seed=%d bodies=%d churn=%d%% threads=%d\n", g_scenario, seed, body_count, churn, threads);
    std::fflush(stdout);

    const float dt = 1.f / 60.f;
    while (max_steps < 0 || g_step < max_steps) {
        ++g_step;
        // Mutations between steps, on the main thread, like the game's update.
        if (static_cast<int>(g_rng() % 100) < churn && !g_bodies.empty()) {
            size_t pick = g_rng() % g_bodies.size();
            Body &b = g_bodies[pick];
            std::string kind = scenario;
            if (scenario == "field") // door panel toggles its callbacks off, missile is removed next
                kind = (g_rng() % 2) ? "detach-toggle" : "baseline";
            if (scenario == "mixed") {
                const char *opts[] = {"baseline", "dispose-first", "deferred-remove", "detach-toggle"};
                kind = opts[g_rng() % 4];
            }
            if (scenario == "callback-remove")
                kind = b.pending_removal ? "dispose-first" : "baseline";
            if (scenario == "deferred-probe") {
                kind = "deferred-remove";
                if (g_removed_bodies >= 3) { std::printf("probe done\n"); return 0; }
            }
            if (scenario == "double-remove")
                kind = "double-remove";
            if (kind == "detach-toggle") {
                toggle_listener(b);
                note("toggle listener of body " + std::to_string(b.id) + (b.attached ? " on" : " off"));
            } else {
                note(kind + " body " + std::to_string(b.id) + (b.pending_removal ? " (removal pending)" : ""));
                if (kind == "baseline") remove_game_order(b);
                else if (kind == "dispose-first") remove_dispose_first(b);
                else if (kind == "double-remove") {
                    // Two removals queued for the same body while locked; both execute next step.
                    HkWorld_LockCriticalOperations(g_world);
                    HkWorld_RemoveEntity(g_world, b.body);
                    HkRigidBody_RemoveFromWorld(b.body);
                    HkWorld_UnlockCriticalOperations(g_world);
                    b.pending_removal = true;
                    HkReferenceObject_RemoveReference(b.body);
                    g_zombies.push_back(b); // listener stays attached until the removal ran
                } else remove_deferred(b);
                g_bodies.erase(g_bodies.begin() + static_cast<long>(pick));
                ++g_removed_bodies;
                g_bodies.push_back(spawn_body(spawn_position()));
            }
            if (!check_world("after mutation") && !keep_going)
                return 1;
        }
        // Occasionally poke a body so the pile never settles into full deactivation.
        if (g_step % 120 == 0 && !g_bodies.empty()) {
            Body &b = g_bodies[g_rng() % g_bodies.size()];
            HkRigidBody_SetLinearVelocity(b.body, {xz(g_rng), 4.f, xz(g_rng)});
        }

        // MyPhysics.StepWorldsParallel
        HkJobQueue_SetWaitPolicy(queue, 1); // WAIT_INDEFINITELY
        HkJobThreadPool_ExecuteJobQueue(pool, queue);
        HkWorld_ExecutePendingCriticalOperations(g_world);
        for (auto &b : g_bodies)
            if (b.pending_removal && *reinterpret_cast<void **>(static_cast<char *>(b.body) + 0x10) == nullptr)
                b.pending_removal = false;
        HkWorld_UnmarkForWrite(g_world);
        HkWorld_InitMtStep(g_world, queue, dt);
        HkJobQueue_SetWaitPolicy(queue, 0); // WAIT_UNTIL_ALL_WORK_COMPLETE
        HkJobQueue_ProcessAllJobs(queue);
        HkJobThreadPool_WaitForCompletion(pool);
        if (!check_world("before FinishMtStep") && !keep_going)
            return 1;
        HkWorld_FinishMtStep(g_world, queue, pool);
        HkWorld_MarkForWrite(g_world);
        while (g_respawn > 0) {
            g_bodies.push_back(spawn_body(spawn_position()));
            --g_respawn;
        }
        for (auto &z : g_zombies)
            if (*reinterpret_cast<void **>(static_cast<char *>(z.body) + 0x10) == nullptr && z.listener) {
                HkEntity_SetContactListener(z.body, z.listener, false);
                g_live_listeners.erase(z.listener);
                HkGlobal_ReleasePtr(z.listener);
                z.listener = nullptr;
            }
        g_zombies.erase(std::remove_if(g_zombies.begin(), g_zombies.end(), [](const Body &z) { return !z.listener; }),
                        g_zombies.end());
        if (!check_world("after FinishMtStep") && !keep_going)
            return 1;

        if (report_every > 0 && g_step % report_every == 0) {
            std::printf("step %ld: collisions +%ld -%ld contacts=%ld removed=%ld toggles=%ld "
                        "dead-listener-entries=%ld violations=%ld\n",
                        g_step, g_collisions_added.load(), g_collisions_removed.load(), g_contacts.load(), g_removed_bodies,
                        g_toggles, g_dead_listener_entries, g_violations);
            std::fflush(stdout);
        }
    }
    std::printf("done: %ld steps, violations=%ld\n", g_step, g_violations);
    return g_violations ? 1 : 0;
}
