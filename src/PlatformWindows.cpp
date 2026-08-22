#include <cstdint>

#define WINAPI __attribute__((ms_abi))

namespace {
struct Presenter {};

template <typename Function>
Function vtable_function(void *instance, std::size_t index)
{
    return reinterpret_cast<Function>((*reinterpret_cast<void ***>(instance))[index]);
}
}

extern "C" {

void Init(const char *, const char *) {}

Presenter *StartPresenter() { return new Presenter; }
void DisposePresenter(Presenter *presenter) { delete presenter; }

void Present(Presenter *, void *swap_chain, std::int32_t sync_interval, std::int32_t flags)
{
    using PresentFunction = std::int32_t(WINAPI *)(void *, std::int32_t, std::int32_t);
    vtable_function<PresentFunction>(swap_chain, 8)(swap_chain, sync_interval, flags);
}

std::int32_t ResizeBuffers(Presenter *, void *swap_chain, std::uint32_t buffer_count,
                           std::uint32_t width, std::uint32_t height, std::int32_t format,
                           std::uint32_t flags)
{
    using ResizeFunction = std::int32_t(WINAPI *)(void *, std::uint32_t, std::uint32_t,
                                                  std::uint32_t, std::int32_t, std::uint32_t);
    return vtable_function<ResizeFunction>(swap_chain, 13)(
        swap_chain, buffer_count, width, height, format, flags);
}

bool IsRunningWine() { return false; }
const char *WineGetVersion() { return nullptr; }
const char *WineGetBuildId() { return nullptr; }
void DereferenceNullPtr() { *static_cast<volatile std::int32_t *>(nullptr) = 5; }

}
