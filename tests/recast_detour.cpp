#include <cstddef>
#include <cstdio>

extern "C" void Init(const char *, const char *);
extern "C" void *RecastDetour_Create();
extern "C" void RecastDetour_Destroy(void *);
extern "C" void RecastDetour_Init(void *, float, float, float[], float[]);
extern "C" void RecastDetour_Clear(void *);
extern "C" std::size_t RecastDetour_GetAllocatedMemory();

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s /path/to/RecastDetour.dll\n", argv[0]);
        return 2;
    }

    Init(argv[1], nullptr);
    void *mesh = RecastDetour_Create();
    if (!mesh)
        return 1;

    float minimum[3]{};
    float maximum[3]{2.0f, 2.0f, 2.0f};
    RecastDetour_Init(mesh, 0.2f, 2.0f, minimum, maximum);
    (void)RecastDetour_GetAllocatedMemory();
    RecastDetour_Clear(mesh);
    RecastDetour_Destroy(mesh);
    return 0;
}
