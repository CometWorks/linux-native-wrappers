#ifndef PE_SIDECAR_H
#define PE_SIDECAR_H

#include <cstdint>
#include <string>

constexpr uint64_t PE_SIDECAR_VERSION = 3;

bool generate_pe_sidecar(const char *pe_path, int output_fd, std::string *error);

#endif
