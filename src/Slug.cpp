#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>

#include "dll_loader.h"

namespace {
pe_image slug_image;
std::mutex slug_mutex;
std::string slug_path;

void initialize(const char *dll_path, const char *sidecar_path);

void ensure_thread_info()
{
    initialize(nullptr, nullptr);
    if (!setup_nt_threadinfo(nullptr))
        std::abort();
    pe_ensure_tls_for_loaded_images();
}

template <typename Function>
Function load_export(const char *name)
{
    auto function = reinterpret_cast<Function>(get_export(name));
    if (!function)
        throw std::runtime_error(name);
    return function;
}

using SetDefaultLayoutDataFunction = void(WINAPI *)(void *);
using GetFontKeyDataFunction = void *(WINAPI *)(void *, unsigned int);
using ExtractTextureFunction = void(WINAPI *)(void *, void *);
using MeasureSlugExFunction = float(WINAPI *)(int, void *, void *, void *, void *, int, int,
                                              unsigned int *, float *, void *, void *);
using SetDefaultFillDataFunction = void(WINAPI *)(void *);
using CountFillFunction = void(WINAPI *)(void *, int, void *, void *, void *, void *, void *,
                                         int *, int *, void *, void *);
using CreateFillFunction = void(WINAPI *)(void *, int, void *, void *, void *, void *, void *, void *);
using SetDefaultStrokeDataFunction = void(WINAPI *)(void *);
using CountStrokeFunction = void(WINAPI *)(void *, unsigned int, int, void *, void *, void *,
                                           int *, int *, void *);
using CreateStrokeFunction = void(WINAPI *)(void *, unsigned int, int, void *, void *, void *, void *);
using CountSlugExFunction = int(WINAPI *)(int, void *, void *, void *, void *, int, int *, int *, void *, void *);
using BuildSlugExFunction = void(WINAPI *)(int, void *, void *, void *, void *, int, void *, void *,
                                           void *, void *, void *, void *);

SetDefaultLayoutDataFunction set_default_layout_data;
GetFontKeyDataFunction get_font_key_data;
ExtractTextureFunction extract_curve_texture;
ExtractTextureFunction extract_band_texture;
MeasureSlugExFunction measure_slug_ex;
SetDefaultFillDataFunction set_default_fill_data;
CountFillFunction count_fill;
CreateFillFunction create_fill;
SetDefaultStrokeDataFunction set_default_stroke_data;
CountStrokeFunction count_stroke;
CreateStrokeFunction create_stroke;
CountSlugExFunction count_slug_ex;
BuildSlugExFunction build_slug_ex;

void initialize(const char *dll_path, const char *sidecar_path)
{
    std::lock_guard<std::mutex> lock(slug_mutex);
    if (slug_image.image)
        return;

    if (dll_path) {
        slug_path = dll_path;
    } else {
        // Playback loads exports directly; deployed wrappers receive the path through Init.
        const char *home = std::getenv("HOME");
        if (!home)
            throw std::runtime_error("HOME is not set; call Init with the Slug DLL path");
        slug_path = std::string(home) + "/Documents/Se2-Game2/VRage.Slug.Native.dll";
    }

    if (!load_dll(&slug_image, slug_path.c_str(), sidecar_path))
        throw std::runtime_error("Failed to load VRage.Slug.Native.dll");

    set_default_layout_data = load_export<SetDefaultLayoutDataFunction>("?SetDefaultLayoutData@Slug@Terathon@@YAXPEAULayoutData@12@@Z");
    get_font_key_data = load_export<GetFontKeyDataFunction>("?GetFontKeyData@Slug@Terathon@@YAPEBXPEBUFontHeader@12@I@Z");
    extract_curve_texture = load_export<ExtractTextureFunction>("?ExtractCurveTexture@Slug@Terathon@@YAXPEBUSlugFileHeader@12@PEAX@Z");
    extract_band_texture = load_export<ExtractTextureFunction>("?ExtractBandTexture@Slug@Terathon@@YAXPEBUSlugFileHeader@12@PEAX@Z");
    measure_slug_ex = load_export<MeasureSlugExFunction>("?MeasureSlugEx@Slug@Terathon@@YAMHPEBUFontDesc@12@PEBUFontMap@12@PEBULayoutData@12@PEBDHHPEBIPEAMPEAU512@PEAUCompiledStorage@12@@Z");
    set_default_fill_data = load_export<SetDefaultFillDataFunction>("?SetDefaultFillData@Slug@Terathon@@YAXPEAUFillData@12@@Z");
    count_fill = load_export<CountFillFunction>("?CountFill@Slug@Terathon@@YAXPEBUFillData@12@HPEBVQuadraticBezier2D@2@AEBVInteger2D@2@PEAV52@23PEAH4PEBUCreateData@12@PEAUFillWorkspace@12@@Z");
    create_fill = load_export<CreateFillFunction>("?CreateFill@Slug@Terathon@@YAXPEBUFillData@12@HPEBVQuadraticBezier2D@2@PEAUTextureBuffer@12@2PEAUGeometryBuffer@12@PEBUCreateData@12@PEAUFillWorkspace@12@@Z");
    set_default_stroke_data = load_export<SetDefaultStrokeDataFunction>("?SetDefaultStrokeData@Slug@Terathon@@YAXPEAUStrokeData@12@@Z");
    count_stroke = load_export<CountStrokeFunction>("?CountStroke@Slug@Terathon@@YAXPEBUStrokeData@12@IHPEBVQuadraticBezier2D@2@AEBVInteger2D@2@PEAV52@PEAH4PEAUStrokeWorkspace@12@@Z");
    create_stroke = load_export<CreateStrokeFunction>("?CreateStroke@Slug@Terathon@@YAXPEBUStrokeData@12@IHPEBVQuadraticBezier2D@2@PEAUTextureBuffer@12@PEAUGeometryBuffer@12@PEAUStrokeWorkspace@12@@Z");
    count_slug_ex = load_export<CountSlugExFunction>("?CountSlugEx@Slug@Terathon@@YAHHPEBUFontDesc@12@PEBUFontMap@12@PEBULayoutData@12@PEBDHPEAH4PEAU512@PEAUCompiledStorage@12@@Z");
    build_slug_ex = load_export<BuildSlugExFunction>("?BuildSlugEx@Slug@Terathon@@YAXHPEBUFontDesc@12@PEBUFontMap@12@PEBULayoutData@12@PEBDHAEBVPoint2D@2@PEAUGeometryBuffer@12@PEAVBox2D@2@PEAV62@PEAU512@PEAUCompiledStorage@12@@Z");
}
}

#define SLUG_EXPORT(name) __asm__("\"" name "\"")

extern "C" {

void Init(const char *dll_path, const char *sidecar_path)
{
    initialize(dll_path, sidecar_path);
}

void SetDefaultLayoutData(void *data) SLUG_EXPORT("?SetDefaultLayoutData$Slug$Terathon$$YAXPEAULayoutData$12$$Z");
void *GetFontKeyData(void *header, unsigned int key) SLUG_EXPORT("?GetFontKeyData$Slug$Terathon$$YAPEBXPEBUFontHeader$12$I$Z");
void ExtractCurveTexture(void *header, void *texture) SLUG_EXPORT("?ExtractCurveTexture$Slug$Terathon$$YAXPEBUSlugFileHeader$12$PEAX$Z");
void ExtractBandTexture(void *header, void *texture) SLUG_EXPORT("?ExtractBandTexture$Slug$Terathon$$YAXPEBUSlugFileHeader$12$PEAX$Z");
float MeasureSlugEx(int font_count, void *font_desc, void *font_map, void *layout_data, void *text,
                    int max_length, int trim_count, unsigned int *trim_array, float *trim_span,
                    void *exit_layout_data, void *compiled_storage)
    SLUG_EXPORT("?MeasureSlugEx$Slug$Terathon$$YAMHPEBUFontDesc$12$PEBUFontMap$12$PEBULayoutData$12$PEBDHHPEBIPEAMPEAU512$PEAUCompiledStorage$12$$Z");
void SetDefaultFillData(void *data) SLUG_EXPORT("?SetDefaultFillData$Slug$Terathon$$YAXPEAUFillData$12$$Z");
void CountFill(void *fill_data, int curve_count, void *curve_array, void *curve_texture_size,
               void *curve_write_location, void *band_texture_size, void *band_write_location,
               int *vertex_count, int *triangle_count, void *create_data, void *workspace)
    SLUG_EXPORT("?CountFill$Slug$Terathon$$YAXPEBUFillData$12$HPEBVQuadraticBezier2D$2$AEBVInteger2D$2$PEAV52$23PEAH4PEBUCreateData$12$PEAUFillWorkspace$12$$Z");
void CreateFill(void *fill_data, int curve_count, void *curve_array, void *curve_texture_buffer,
                void *band_texture_buffer, void *geometry_buffer, void *create_data, void *workspace)
    SLUG_EXPORT("?CreateFill$Slug$Terathon$$YAXPEBUFillData$12$HPEBVQuadraticBezier2D$2$PEAUTextureBuffer$12$2PEAUGeometryBuffer$12$PEBUCreateData$12$PEAUFillWorkspace$12$$Z");
void SetDefaultStrokeData(void *data) SLUG_EXPORT("?SetDefaultStrokeData$Slug$Terathon$$YAXPEAUStrokeData$12$$Z");
void CountStroke(void *stroke_data, unsigned int stroke_flags, int curve_count, void *curve_array,
                 void *curve_texture_size, void *curve_write_location, int *vertex_count,
                 int *triangle_count, void *workspace)
    SLUG_EXPORT("?CountStroke$Slug$Terathon$$YAXPEBUStrokeData$12$IHPEBVQuadraticBezier2D$2$AEBVInteger2D$2$PEAV52$PEAH4PEAUStrokeWorkspace$12$$Z");
void CreateStroke(void *stroke_data, unsigned int stroke_flags, int curve_count, void *curve_array,
                  void *curve_texture_buffer, void *geometry_buffer, void *workspace)
    SLUG_EXPORT("?CreateStroke$Slug$Terathon$$YAXPEBUStrokeData$12$IHPEBVQuadraticBezier2D$2$PEAUTextureBuffer$12$PEAUGeometryBuffer$12$PEAUStrokeWorkspace$12$$Z");
int CountSlugEx(int font_count, void *font_desc, void *font_map, void *layout_data, void *text,
                int max_length, int *vertex_count, int *triangle_count, void *exit_layout_data,
                void *compiled_storage)
    SLUG_EXPORT("?CountSlugEx$Slug$Terathon$$YAHHPEBUFontDesc$12$PEBUFontMap$12$PEBULayoutData$12$PEBDHPEAH4PEAU512$PEAUCompiledStorage$12$$Z");
void BuildSlugEx(int font_count, void *font_desc, void *font_map, void *layout_data, void *text,
                 int max_length, void *position, void *geometry_buffer, void *text_box,
                 void *exit_position, void *exit_layout_data, void *compiled_storage)
    SLUG_EXPORT("?BuildSlugEx$Slug$Terathon$$YAXHPEBUFontDesc$12$PEBUFontMap$12$PEBULayoutData$12$PEBDHAEBVPoint2D$2$PEAUGeometryBuffer$12$PEAVBox2D$2$PEAV62$PEAU512$PEAUCompiledStorage$12$$Z");

void SetDefaultLayoutData(void *data)
{
    ensure_thread_info();
    set_default_layout_data(data);
}
void *GetFontKeyData(void *header, unsigned int key) { ensure_thread_info(); return get_font_key_data(header, key); }
void ExtractCurveTexture(void *header, void *texture) { ensure_thread_info(); extract_curve_texture(header, texture); }
void ExtractBandTexture(void *header, void *texture) { ensure_thread_info(); extract_band_texture(header, texture); }
float MeasureSlugEx(int font_count, void *font_desc, void *font_map, void *layout_data, void *text,
                    int max_length, int trim_count, unsigned int *trim_array, float *trim_span,
                    void *exit_layout_data, void *compiled_storage)
{
    ensure_thread_info();
    return measure_slug_ex(font_count, font_desc, font_map, layout_data, text, max_length, trim_count,
                           trim_array, trim_span, exit_layout_data, compiled_storage);
}
void SetDefaultFillData(void *data) { ensure_thread_info(); set_default_fill_data(data); }
void CountFill(void *fill_data, int curve_count, void *curve_array, void *curve_texture_size,
               void *curve_write_location, void *band_texture_size, void *band_write_location,
               int *vertex_count, int *triangle_count, void *create_data, void *workspace)
{
    ensure_thread_info();
    count_fill(fill_data, curve_count, curve_array, curve_texture_size, curve_write_location,
               band_texture_size, band_write_location, vertex_count, triangle_count, create_data, workspace);
}
void CreateFill(void *fill_data, int curve_count, void *curve_array, void *curve_texture_buffer,
                void *band_texture_buffer, void *geometry_buffer, void *create_data, void *workspace)
{
    ensure_thread_info();
    create_fill(fill_data, curve_count, curve_array, curve_texture_buffer, band_texture_buffer,
                geometry_buffer, create_data, workspace);
}
void SetDefaultStrokeData(void *data) { ensure_thread_info(); set_default_stroke_data(data); }
void CountStroke(void *stroke_data, unsigned int stroke_flags, int curve_count, void *curve_array,
                 void *curve_texture_size, void *curve_write_location, int *vertex_count,
                 int *triangle_count, void *workspace)
{
    ensure_thread_info();
    count_stroke(stroke_data, stroke_flags, curve_count, curve_array, curve_texture_size,
                 curve_write_location, vertex_count, triangle_count, workspace);
}
void CreateStroke(void *stroke_data, unsigned int stroke_flags, int curve_count, void *curve_array,
                  void *curve_texture_buffer, void *geometry_buffer, void *workspace)
{
    ensure_thread_info();
    create_stroke(stroke_data, stroke_flags, curve_count, curve_array, curve_texture_buffer,
                  geometry_buffer, workspace);
}
int CountSlugEx(int font_count, void *font_desc, void *font_map, void *layout_data, void *text,
                int max_length, int *vertex_count, int *triangle_count, void *exit_layout_data,
                void *compiled_storage)
{
    ensure_thread_info();
    return count_slug_ex(font_count, font_desc, font_map, layout_data, text, max_length,
                         vertex_count, triangle_count, exit_layout_data, compiled_storage);
}
void BuildSlugEx(int font_count, void *font_desc, void *font_map, void *layout_data, void *text,
                 int max_length, void *position, void *geometry_buffer, void *text_box,
                 void *exit_position, void *exit_layout_data, void *compiled_storage)
{
    ensure_thread_info();
    build_slug_ex(font_count, font_desc, font_map, layout_data, text, max_length, position,
                  geometry_buffer, text_box, exit_position, exit_layout_data, compiled_storage);
}

}
