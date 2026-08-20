#include "pe_sidecar.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s INPUT_PE OUTPUT_SO\n", argv[0]);
        return 2;
    }
    if (std::strcmp(argv[1], argv[2]) == 0) {
        std::fprintf(stderr, "%s: input and output paths must differ\n", argv[0]);
        return 2;
    }

    std::string temporary = std::string(argv[2]) + ".tmp.XXXXXX";
    std::vector<char> name(temporary.begin(), temporary.end());
    name.push_back(0);
    int fd = mkstemp(name.data());
    if (fd < 0) {
        std::fprintf(stderr, "%s: cannot create temporary output: %s\n", argv[0], std::strerror(errno));
        return 1;
    }
    std::string error;
    bool generated = fchmod(fd, 0644) == 0;
    if (!generated)
        error = "cannot set output permissions: " + std::string(std::strerror(errno));
    if (generated)
        generated = generate_pe_sidecar(argv[1], fd, &error);
    if (generated && fsync(fd) < 0) {
        generated = false;
        error = "cannot sync temporary output: " + std::string(std::strerror(errno));
    }
    if (close(fd) < 0 && generated) {
        generated = false;
        error = "cannot close temporary output: " + std::string(std::strerror(errno));
    }
    if (generated && rename(name.data(), argv[2]) < 0) {
        generated = false;
        error = "cannot publish output: " + std::string(std::strerror(errno));
    }
    if (!generated) {
        unlink(name.data());
        std::fprintf(stderr, "%s: %s\n", argv[0], error.c_str());
        return 1;
    }
    return 0;
}
