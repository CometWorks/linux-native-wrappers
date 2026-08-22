// CLI harness for the D3DCompiler wrapper.
//
// Preprocesses or compiles a shader through the PE-loaded d3dcompiler_47.dll
// using the same include semantics as the game, writing the result to stdout.
// Used to verify byte-for-byte compatibility with the shipped shader cache.
//
// Usage:
//   d3dcompiler_tool preprocess <d3dcompiler_47.dll> <sidecar|->
//       <shader.hlsl> <shaders-root> [NAME[=VALUE]...]
//   d3dcompiler_tool compile <d3dcompiler_47.dll> <sidecar|->
//       <shader.hlsl> <shaders-root> <entrypoint> <profile> <flags-hex>
//       [NAME[=VALUE]...]

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct D3D_SHADER_MACRO {
    const char *Name;
    const char *Definition;
};

extern "C" {
void Init(const char *dllPath, const char *sidecarPath);
void *SE_CreateIncludeHandler(const char *basePath, const char *includeDir);
void SE_DestroyIncludeHandler(void *pInclude);
int32_t SE_D3DPreprocess(
    const void *pSrcData, uint64_t SrcDataSize,
    const char *pSourceName, const D3D_SHADER_MACRO *pDefines, void *pInclude,
    void **ppCodeText, void **ppErrorMsgs);
int32_t SE_D3DCompile(
    const void *pSrcData, uint64_t SrcDataSize,
    const char *pSourceName, const D3D_SHADER_MACRO *pDefines, void *pInclude,
    const char *pEntrypoint, const char *pTarget,
    uint32_t Flags1, uint32_t Flags2,
    void **ppCode, void **ppErrorMsgs);
void *SE_BlobGetBufferPointer(void *blob);
uint64_t SE_BlobGetBufferSize(void *blob);
uint32_t SE_BlobRelease(void *blob);
}

// Reads the file as text the way File.ReadAllText does for the game:
// raw bytes with a leading UTF-8 BOM removed.
static bool read_text_file(const char *path, std::string &out)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    if (length < 0) {
        fclose(file);
        return false;
    }
    out.resize((size_t)length);
    if (length > 0 && fread(&out[0], 1, (size_t)length, file) != (size_t)length) {
        fclose(file);
        return false;
    }
    fclose(file);
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

static std::string dir_name(const std::string &path)
{
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return ".";
    if (slash == 0)
        return "/";
    return path.substr(0, slash);
}

static void print_errors(void *errorBlob)
{
    if (!errorBlob)
        return;
    const char *message = (const char *)SE_BlobGetBufferPointer(errorBlob);
    uint64_t size = SE_BlobGetBufferSize(errorBlob);
    if (message && size)
        fwrite(message, 1, strlen(message), stderr);
}

int main(int argc, char *argv[])
{
    if (argc < 6) {
        fprintf(stderr, "Usage: %s preprocess|compile <dll> <sidecar|-> <shader> "
                        "<shaders-root> [compile: <entry> <profile> <flags-hex>] "
                        "[NAME[=VALUE]...]\n",
                argv[0]);
        return 2;
    }

    const char *mode = argv[1];
    const char *dllPath = argv[2];
    const char *sidecarPath = strcmp(argv[3], "-") == 0 ? nullptr : argv[3];
    const std::string shaderPath = argv[4];
    const char *shadersRoot = argv[5];
    bool compileMode = strcmp(mode, "compile") == 0;

    int macroStart = compileMode ? 9 : 6;
    if (compileMode && argc < 9) {
        fprintf(stderr, "compile mode needs <entry> <profile> <flags-hex>\n");
        return 2;
    }

    std::vector<D3D_SHADER_MACRO> macros;
    std::vector<std::string> macroStorage;
    macroStorage.reserve(2 * (argc - macroStart));
    for (int i = macroStart; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        if (eq) {
            macroStorage.emplace_back(argv[i], eq - argv[i]);
            macroStorage.emplace_back(eq + 1);
            macros.push_back({ macroStorage[macroStorage.size() - 2].c_str(),
                               macroStorage[macroStorage.size() - 1].c_str() });
        } else {
            macroStorage.emplace_back(argv[i]);
            macros.push_back({ macroStorage.back().c_str(), nullptr });
        }
    }
    macros.push_back({ nullptr, nullptr });

    std::string source;
    if (!read_text_file(shaderPath.c_str(), source)) {
        fprintf(stderr, "Cannot read shader: %s\n", shaderPath.c_str());
        return 1;
    }

    Init(dllPath, sidecarPath);

    void *include = SE_CreateIncludeHandler(dir_name(shaderPath).c_str(), shadersRoot);
    const D3D_SHADER_MACRO *defines = macros.size() > 1 ? macros.data() : nullptr;

    void *codeBlob = nullptr;
    void *errorBlob = nullptr;
    int32_t hr;
    if (compileMode) {
        uint32_t flags = (uint32_t)strtoul(argv[8], nullptr, 16);
        hr = SE_D3DCompile(source.data(), source.size(), shaderPath.c_str(),
                           defines, include, argv[6], argv[7], flags, 0,
                           &codeBlob, &errorBlob);
    } else {
        // SharpDX PreprocessFromFile passes an empty source name.
        hr = SE_D3DPreprocess(source.data(), source.size(), "",
                              defines, include, &codeBlob, &errorBlob);
    }

    int result = 0;
    if (hr < 0) {
        fprintf(stderr, "%s failed: HRESULT 0x%08X\n", mode, (uint32_t)hr);
        print_errors(errorBlob);
        result = 1;
    } else {
        const void *data = SE_BlobGetBufferPointer(codeBlob);
        uint64_t size = SE_BlobGetBufferSize(codeBlob);
        if (!compileMode) {
            // The preprocessed text blob is NUL-terminated; the game consumes
            // it as a string, so emit only the bytes before the terminator.
            size = strnlen((const char *)data, size);
        }
        fwrite(data, 1, size, stdout);
    }

    if (codeBlob)
        SE_BlobRelease(codeBlob);
    if (errorBlob)
        SE_BlobRelease(errorBlob);
    SE_DestroyIncludeHandler(include);
    return result;
}
