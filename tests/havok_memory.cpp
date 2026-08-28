#include <cstdint>
#include <cstdio>

struct Vector3 { float X, Y, Z; };

extern "C" void Init(const char *, const char *);
extern "C" void HkBaseSystem_Init(int32_t, void *, bool);
extern "C" void HkBaseSystem_Quit();
extern "C" int64_t HkBaseSystem_GetCurrentMemoryConsumption();
extern "C" void *HkBoxShape_Create(Vector3);
extern "C" int32_t HkReferenceObject_ReferenceCount(void *);
extern "C" void HkReferenceObject_RemoveReference(void *);

static bool run_batch(int count)
{
    for (int i = 0; i < count; ++i) {
        void *shape = HkBoxShape_Create({1, 1, 1});
        if (!shape || HkReferenceObject_ReferenceCount(shape) <= 0)
            return false;
        HkReferenceObject_RemoveReference(shape);
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s /path/to/Havok.dll /path/to/sidecar\n", argv[0]);
        return 2;
    }

    Init(argv[1], argv[2]);
    HkBaseSystem_Init(16 * 1024 * 1024, nullptr, false);
    int result = 0;
    const int64_t initial = HkBaseSystem_GetCurrentMemoryConsumption();
    if (!run_batch(10000)) {
        result = 3;
    } else {
        const int64_t warm = HkBaseSystem_GetCurrentMemoryConsumption();
        for (int batch = 1; batch < 10 && !result; ++batch) {
            if (!run_batch(10000)) {
                result = 4;
            } else if (HkBaseSystem_GetCurrentMemoryConsumption() > warm) {
                result = 5;
            }
        }
        const int64_t final = HkBaseSystem_GetCurrentMemoryConsumption();
        std::printf("initial=%lld warm=%lld final=%lld\n",
                    static_cast<long long>(initial), static_cast<long long>(warm),
                    static_cast<long long>(final));
        if (!result && final > warm)
            result = 6;
    }
    HkBaseSystem_Quit();
    return result;
}
