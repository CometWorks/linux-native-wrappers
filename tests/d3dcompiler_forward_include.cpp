// Checks SE_CreateForwardingIncludeHandler against the real d3dcompiler_47.dll:
// the PE compiler must reach an ID3DInclude whose vtable uses the Linux
// calling convention, the way SharpDX's managed Include shadow is built, get
// the included text from it, see the parent buffer on a nested include, and
// close every buffer it opened.
//
// Usage: d3dcompiler_forward_include_test <d3dcompiler_47.dll>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

extern "C" {
void Init(const char *dllPath, const char *sidecarPath);
void *SE_CreateForwardingIncludeHandler(void *include);
void SE_DestroyIncludeHandler(void *pInclude);
int32_t SE_D3DPreprocess(
    const void *pSrcData, uint64_t SrcDataSize,
    const char *pSourceName, const void *pDefines, void *pInclude,
    void **ppCodeText, void **ppErrorMsgs);
void *SE_BlobGetBufferPointer(void *blob);
uint32_t SE_BlobRelease(void *blob);
}

static const char OUTER[] = "#include \"Inner.hlsli\"\nfloat outer_marker;\n";
static const char INNER[] = "float inner_marker;\n";

static int s_opens, s_closes;
static bool s_nestedParentSeen;

static int32_t Open(void *, uint32_t, const char *fileName, const void *parentData,
                    const void **ppData, uint32_t *pBytes)
{
    s_opens++;
    const char *text = nullptr;
    if (strcmp(fileName, "Outer.hlsli") == 0) {
        text = OUTER;
    } else if (strcmp(fileName, "Inner.hlsli") == 0) {
        text = INNER;
        s_nestedParentSeen = parentData == OUTER;
    } else {
        return (int32_t)0x80004005;
    }
    *ppData = text;
    *pBytes = (uint32_t)strlen(text);
    return 0;
}

static int32_t Close(void *, const void *)
{
    s_closes++;
    return 0;
}

struct LinuxInclude {
    void *const *vtbl;
};

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <d3dcompiler_47.dll>\n", argv[0]);
        return 2;
    }
    Init(argv[1], nullptr);

    static void *const vtbl[] = { (void *)Open, (void *)Close };
    LinuxInclude include{ vtbl };
    void *handler = SE_CreateForwardingIncludeHandler(&include);

    const char source[] = "#include \"Outer.hlsli\"\nfloat root_marker;\n";
    void *code = nullptr, *errors = nullptr;
    int32_t hr = SE_D3DPreprocess(source, sizeof source - 1, "Root.hlsl", nullptr,
                                  handler, &code, &errors);
    SE_DestroyIncludeHandler(handler);

    int failed = 0;
    if (hr < 0) {
        printf("FAIL: preprocess HRESULT 0x%08X\n", (uint32_t)hr);
        failed = 1;
    } else {
        const char *text = (const char *)SE_BlobGetBufferPointer(code);
        for (const char *marker : { "inner_marker", "outer_marker", "root_marker" }) {
            if (!strstr(text, marker)) {
                printf("FAIL: %s missing from output\n", marker);
                failed = 1;
            }
        }
    }
    if (s_opens != 2 || s_closes != 2) {
        printf("FAIL: %d opens, %d closes, expected 2 each\n", s_opens, s_closes);
        failed = 1;
    }
    if (!s_nestedParentSeen) {
        printf("FAIL: nested include did not get the outer buffer as parent\n");
        failed = 1;
    }

    if (code)
        SE_BlobRelease(code);
    if (errors)
        SE_BlobRelease(errors);
    if (!failed)
        printf("OK\n");
    return failed;
}
