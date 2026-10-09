// Calls VRage.Voxels.Native.dll, then Slug, then Voxels again on one thread,
// as the game does. Every wrapper library points GS at its own TEB, and the
// planet shape's GetValue reads a thread_local of the Voxels DLL through
// gs:0x58. Slug has no TLS, so a Voxels call left on Slug's TEB finds an empty
// slot and crashes.

#include <asm/prctl.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {
struct Vector2 {
    float x, y;
};
struct Vector3 {
    float x, y, z;
};

// VrPlanetShape.MapSet and VrPlanetShape.DetailMapData
struct MapSet {
    uint16_t *faces[6];
    int32_t resolution;
};
struct DetailMapData {
    uint8_t *data;
    int32_t resolution;
    float factor, size, scale, min, max, in, out, in_recip, out_recip, mid;
};
static_assert(sizeof(MapSet) == 56);
static_assert(sizeof(DetailMapData) == 56);

using Init_t = void (*)(const char *, const char *);
using Create_t = void *(*)(Vector3, float, float, float, MapSet, DetailMapData, int32_t);
using GetValue_t = float (*)(void *, Vector2, int32_t, Vector3 *);
using Release_t = void (*)(void *);
using SetDefaultLayoutData_t = void (*)(void *);

void *gs_base()
{
    void *base = nullptr;
    syscall(SYS_arch_prctl, ARCH_GET_GS, &base);
    return base;
}

void *open_library(const char *path)
{
    // RTLD_LOCAL like the .NET runtime, so each wrapper keeps its own loader.
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        std::fprintf(stderr, "voxels_teb: %s\n", dlerror());
    return handle;
}

template <typename Function>
Function symbol(void *handle, const char *name)
{
    auto function = reinterpret_cast<Function>(dlsym(handle, name));
    if (!function)
        std::fprintf(stderr, "voxels_teb: missing %s\n", name);
    return function;
}

int fail(const char *message)
{
    std::fprintf(stderr, "voxels_teb: %s\n", message);
    return 1;
}
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        std::fprintf(stderr,
                     "usage: %s libVRage.Voxels.Native.so libVRage.Slug.Native.so "
                     "VRage.Voxels.Native.dll VRage.Slug.Native.dll sidecar-dir\n",
                     argv[0]);
        return 2;
    }
    std::filesystem::path sidecars = argv[5];
    std::filesystem::create_directories(sidecars);

    void *voxels = open_library(argv[1]);
    void *slug = open_library(argv[2]);
    if (!voxels || !slug)
        return 1;
    auto voxels_init = symbol<Init_t>(voxels, "Init");
    auto create = symbol<Create_t>(voxels, "VrPlanetShape_Create");
    auto get_value = symbol<GetValue_t>(voxels, "VrPlanetShape_GetValue");
    auto release = symbol<Release_t>(voxels, "VrPlanetShape_Release");
    auto slug_init = symbol<Init_t>(slug, "Init");
    auto set_default_layout_data = symbol<SetDefaultLayoutData_t>(
            slug, "?SetDefaultLayoutData@Slug@Terathon@@YAXPEAULayoutData@12@@Z");
    if (!voxels_init || !create || !get_value || !release || !slug_init ||
        !set_default_layout_data)
        return 1;

    voxels_init(argv[3], (sidecars / "VRage.Voxels.Native.dll").c_str());
    slug_init(argv[4], (sidecars / "VRage.Slug.Native.dll").c_str());

    // A flat 1 km planet with a constant height map and detail map.
    constexpr int resolution = 64;
    std::vector<uint16_t> heights(resolution * resolution, 0x8000);
    MapSet maps{};
    for (auto &face : maps.faces)
        face = heights.data();
    maps.resolution = resolution;
    std::vector<uint8_t> detail(resolution * resolution, 0x80);
    DetailMapData detail_map{detail.data(), resolution, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0.5f};
    void *shape = create({0, 0, 0}, 1000, 0, 50, maps, detail_map, 0);
    if (!shape)
        return fail("VrPlanetShape_Create returned null");

    Vector3 normal{};
    float first = get_value(shape, {0.25f, 0.5f}, 0, &normal);
    void *voxels_teb = gs_base();

    std::vector<uint8_t> layout(4096);
    set_default_layout_data(layout.data());
    if (gs_base() == voxels_teb)
        return fail("Slug did not switch GS, the test proves nothing");

    float second = get_value(shape, {0.25f, 0.5f}, 0, &normal);
    if (gs_base() != voxels_teb)
        return fail("Voxels ran on another library's TEB");
    if (second != first)
        return fail("GetValue changed after the Slug call");

    std::printf("GetValue %f on both calls\n", first);
    release(shape);
    return 0;
}
