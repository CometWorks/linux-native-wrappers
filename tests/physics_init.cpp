#include <cstdio>

extern "C" void Init(const char *, const char *);

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s /path/to/VRage.Physics.Native.dll /path/to/sidecar\n", argv[0]);
        return 2;
    }

    Init(argv[1], argv[2]);
    return 0;
}
