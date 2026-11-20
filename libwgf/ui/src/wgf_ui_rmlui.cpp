/* The ui module's way into RmlUi (deps/rmlui), the C side's interface
 * (wgf_ui_rmlui_priv.h) in C++: RmlUi's one context, the documents in it, the elements the
 * program holds, and the interfaces RmlUi asks for -- system, files, rendering, and fonts --
 * each passing what it is asked to the C side (wgf_ui_document.c), which does it through
 * libwgf. C++17 without exceptions or RTTI (RmlUi's own type ids serve its casts). */

#include <RmlUi/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/ElementText.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "wgf_ui_rmlui_priv.h"

namespace {

constexpr int EVENTS_MAX = 2048; /* ints: two an event, 1024 events (wgf_document.h) */

/* ---- the system: time, logs, the clipboard, the cursor ------------------------------ */

class System final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return wgf_ui_priv_doc_seconds(); }

    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override
    {
        int level = 1;
        switch (type) {
        case Rml::Log::LT_ALWAYS:
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT: level = 3; break;
        case Rml::Log::LT_WARNING: level = 2; break;
        case Rml::Log::LT_INFO: level = 1; break;
        default: level = 0; break;
        }
        wgf_ui_priv_doc_log(level, message.c_str());
        return true;
    }

    void SetMouseCursor(const Rml::String &name) override { wgf_ui_priv_doc_set_cursor(name.c_str()); }
    void ActivateKeyboard(Rml::Vector2f caret, float line_height) override
    {
        wgf_ui_priv_doc_keyboard(true, caret.x, caret.y, line_height);
    }
    void DeactivateKeyboard() override { wgf_ui_priv_doc_keyboard(false, 0.0f, 0.0f, 0.0f); }
    void SetClipboardText(const Rml::String &text) override { wgf_ui_priv_doc_set_clipboard(text.c_str()); }
    void GetClipboardText(Rml::String &text) override
    {
        const char *clipboard = wgf_ui_priv_doc_get_clipboard();
        text = clipboard != nullptr ? clipboard : "";
    }
};

/* ---- files: read whole through libwgf, then from memory ----------------------------- */

struct OpenFile {
    unsigned char *data;
    size_t size, at;
};

class Files final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String &path) override
    {
        int size = 0;
        unsigned char *data = wgf_ui_priv_doc_file_read(path.c_str(), &size);
        if (data == nullptr) return 0;
        OpenFile *file = new OpenFile{data, (size_t)size, 0};
        return (Rml::FileHandle)file;
    }

    void Close(Rml::FileHandle handle) override
    {
        OpenFile *file = (OpenFile *)handle;
        if (file == nullptr) return;
        wgf_ui_priv_doc_file_free(file->data);
        delete file;
    }

    size_t Read(void *buffer, size_t size, Rml::FileHandle handle) override
    {
        OpenFile *file = (OpenFile *)handle;
        const size_t left = file->size - file->at, n = size < left ? size : left;
        std::memcpy(buffer, file->data + file->at, n);
        file->at += n;
        return n;
    }

    bool Seek(Rml::FileHandle handle, long offset, int origin) override
    {
        OpenFile *file = (OpenFile *)handle;
        long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? (long)file->at : (long)file->size;
        if (base + offset < 0 || (size_t)(base + offset) > file->size) return false;
        file->at = (size_t)(base + offset);
        return true;
    }

    size_t Tell(Rml::FileHandle handle) override { return ((OpenFile *)handle)->at; }
    size_t Length(Rml::FileHandle handle) override { return ((OpenFile *)handle)->size; }
};

/* ---- rendering: geometry kept here, drawn by the C side ------------------------------ */

struct Geometry {
    std::vector<wgf_ui_priv_vertex_t> vertices;
    std::vector<int> indices;
};

class Render final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override
    {
        Geometry *geometry = new Geometry;
        geometry->vertices.resize(vertices.size());
        for (size_t i = 0; i < vertices.size(); i++) {
            const Rml::Vertex &from = vertices[i];
            const Rml::Colourb colour = from.colour.ToNonPremultiplied(); /* drawn blended straight, as libwgf draws */
            wgf_ui_priv_vertex_t &to = geometry->vertices[i];
            to.x = from.position.x;
            to.y = from.position.y;
            to.u = from.tex_coord.x;
            to.v = from.tex_coord.y;
            to.r = colour.red;
            to.g = colour.green;
            to.b = colour.blue;
            to.a = colour.alpha;
        }
        geometry->indices.assign(indices.begin(), indices.end());
        return (Rml::CompiledGeometryHandle)geometry;
    }

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override
    {
        const Geometry *geometry = (const Geometry *)handle;
        wgf_ui_priv_doc_draw(geometry->vertices.data(), (int)geometry->vertices.size(), geometry->indices.data(),
                             (int)geometry->indices.size(), translation.x, translation.y, (uintptr_t)texture);
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override
    {
        wgf_ui_priv_doc_geometry_gone((uintptr_t)handle); /* its triangles on the GPU, for a gradient */
        delete (Geometry *)handle;
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override
    {
        int width = 0, height = 0;
        const uintptr_t texture = wgf_ui_priv_doc_texture_load(source.c_str(), &width, &height);
        dimensions = Rml::Vector2i(width, height);
        return (Rml::TextureHandle)texture;
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override
    {
        if ((size_t)dimensions.x * (size_t)dimensions.y * 4 != source.size()) return 0;
        return (Rml::TextureHandle)wgf_ui_priv_doc_texture_generate(source.data(), dimensions.x, dimensions.y);
    }

    void ReleaseTexture(Rml::TextureHandle texture) override { wgf_ui_priv_doc_texture_release((uintptr_t)texture); }

    void EnableScissorRegion(bool enable) override
    {
        scissor_on = enable;
        wgf_ui_priv_doc_scissor(scissor_on, scissor.Left(), scissor.Top(), scissor.Width(), scissor.Height());
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        scissor = region;
        if (scissor_on) wgf_ui_priv_doc_scissor(true, region.Left(), region.Top(), region.Width(), region.Height());
    }

    void SetTransform(const Rml::Matrix4f *transform) override
    {
        if (transform == nullptr) {
            wgf_ui_priv_doc_transform(nullptr);
            return;
        }
        wgf_ui_priv_doc_transform(transform->data()); /* column by column: RmlUi's default layout */
    }

    void EnableClipMask(bool enable) override { wgf_ui_priv_doc_clip_mask(enable); }

    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle handle,
                          Rml::Vector2f translation) override
    {
        const Geometry *geometry = (const Geometry *)handle;
        const int op = operation == Rml::ClipMaskOperation::Set          ? 0
                       : operation == Rml::ClipMaskOperation::SetInverse ? 1
                                                                          : 2;
        wgf_ui_priv_doc_clip_mask_draw(op, geometry->vertices.data(), (int)geometry->vertices.size(),
                                       geometry->indices.data(), (int)geometry->indices.size(), translation.x,
                                       translation.y);
    }

    Rml::LayerHandle PushLayer() override { return (Rml::LayerHandle)wgf_ui_priv_doc_layer_push(); }

    void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
                         Rml::Span<const Rml::CompiledFilterHandle> filters) override
    {
        std::vector<uintptr_t> list;
        for (Rml::CompiledFilterHandle filter : filters) list.push_back((uintptr_t)filter);
        wgf_ui_priv_doc_layer_composite((int)source, (int)destination, blend_mode == Rml::BlendMode::Replace,
                                        list.data(), (int)list.size());
    }

    void PopLayer() override { wgf_ui_priv_doc_layer_pop(); }

    Rml::TextureHandle SaveLayerAsTexture() override
    {
        return (Rml::TextureHandle)wgf_ui_priv_doc_layer_save_texture(scissor.Left(), scissor.Top(), scissor.Width(),
                                                                      scissor.Height());
    }

    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override
    {
        return (Rml::CompiledFilterHandle)wgf_ui_priv_doc_layer_save_mask();
    }

    /* CSS's filters: opacity and the color filters as a matrix on premultiplied colors
       (the Filter Effects spec's, its offsets times alpha), a blur, and a drop shadow */
    Rml::CompiledFilterHandle CompileFilter(const Rml::String &name, const Rml::Dictionary &parameters) override
    {
        float v[16] = {};
        const float value = Rml::Get(parameters, "value", 1.0f);
        if (name == "blur") {
            v[0] = Rml::Get(parameters, "sigma", 0.0f);
            return (Rml::CompiledFilterHandle)wgf_ui_priv_doc_filter_make(WGF_UI_PRIV_FILTER_BLUR, v);
        }
        if (name == "drop-shadow") {
            const Rml::Colourb color = Rml::Get(parameters, "color", Rml::Colourb(0, 0, 0, 255));
            const Rml::Vector2f offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.0f, 0.0f));
            v[0] = Rml::Get(parameters, "sigma", 0.0f);
            v[1] = offset.x;
            v[2] = offset.y;
            v[3] = color.red / 255.0f;
            v[4] = color.green / 255.0f;
            v[5] = color.blue / 255.0f;
            v[6] = color.alpha / 255.0f;
            return (Rml::CompiledFilterHandle)wgf_ui_priv_doc_filter_make(WGF_UI_PRIV_FILTER_SHADOW, v);
        }
        /* rows r, g, b of a 3 by 3 on the color, and an offset each, times alpha */
        float m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
        float alpha = 1.0f;
        const float a = 1.0f - (value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value);
        if (name == "opacity") {
            m[0][0] = m[1][1] = m[2][2] = alpha = value;
        } else if (name == "brightness") {
            m[0][0] = m[1][1] = m[2][2] = value;
        } else if (name == "contrast") {
            m[0][0] = m[1][1] = m[2][2] = value;
            m[0][3] = m[1][3] = m[2][3] = 0.5f - 0.5f * value;
        } else if (name == "invert") {
            const float inverted = 1.0f - a;
            m[0][0] = m[1][1] = m[2][2] = 1.0f - 2.0f * inverted;
            m[0][3] = m[1][3] = m[2][3] = inverted;
        } else if (name == "grayscale") {
            const float g[3][3] = {{0.2126f + 0.7874f * a, 0.7152f - 0.7152f * a, 0.0722f - 0.0722f * a},
                                   {0.2126f - 0.2126f * a, 0.7152f + 0.2848f * a, 0.0722f - 0.0722f * a},
                                   {0.2126f - 0.2126f * a, 0.7152f - 0.7152f * a, 0.0722f + 0.9278f * a}};
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++) m[r][c] = g[r][c];
        } else if (name == "sepia") {
            const float g[3][3] = {{0.393f + 0.607f * a, 0.769f - 0.769f * a, 0.189f - 0.189f * a},
                                   {0.349f - 0.349f * a, 0.686f + 0.314f * a, 0.168f - 0.168f * a},
                                   {0.272f - 0.272f * a, 0.534f - 0.534f * a, 0.131f + 0.869f * a}};
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++) m[r][c] = g[r][c];
        } else if (name == "saturate") {
            const float s = value;
            const float g[3][3] = {{0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s},
                                   {0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s},
                                   {0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s}};
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++) m[r][c] = g[r][c];
        } else if (name == "hue-rotate") {
            const float c = std::cos(value), s = std::sin(value); /* radians */
            const float g[3][3] = {{0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f,
                                    0.072f - c * 0.072f + s * 0.928f},
                                   {0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f,
                                    0.072f - c * 0.072f - s * 0.283f},
                                   {0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f,
                                    0.072f + c * 0.928f + s * 0.072f}};
            for (int r = 0; r < 3; r++)
                for (int k = 0; k < 3; k++) m[r][k] = g[r][k];
        } else {
            return 0; /* not one of CSS's: drawn without it */
        }
        for (int c = 0; c < 4; c++) { /* column by column: v[column * 4 + row] */
            for (int r = 0; r < 3; r++) v[c * 4 + r] = m[r][c];
        }
        v[15] = alpha;
        return (Rml::CompiledFilterHandle)wgf_ui_priv_doc_filter_make(WGF_UI_PRIV_FILTER_MATRIX, v);
    }

    void ReleaseFilter(Rml::CompiledFilterHandle filter) override { wgf_ui_priv_doc_filter_release((uintptr_t)filter); }

    /* gradients, RmlUi's shaders; another (a "shader" decorator of the program's own) isn't drawn */
    Rml::CompiledShaderHandle CompileShader(const Rml::String &name, const Rml::Dictionary &parameters) override
    {
        int kind;
        Rml::Vector2f p(0.0f, 0.0f), v(0.0f, 0.0f);
        if (name == "linear-gradient") {
            kind = 0;
            p = Rml::Get(parameters, "p0", Rml::Vector2f(0.0f, 0.0f));
            v = Rml::Get(parameters, "p1", Rml::Vector2f(0.0f, 0.0f)) - p;
        } else if (name == "radial-gradient") {
            kind = 1;
            p = Rml::Get(parameters, "center", Rml::Vector2f(0.0f, 0.0f));
            const Rml::Vector2f radius = Rml::Get(parameters, "radius", Rml::Vector2f(1.0f, 1.0f));
            v = Rml::Vector2f(radius.x > 0.0f ? 1.0f / radius.x : 0.0f, radius.y > 0.0f ? 1.0f / radius.y : 0.0f);
        } else if (name == "conic-gradient") {
            kind = 2;
            p = Rml::Get(parameters, "center", Rml::Vector2f(0.0f, 0.0f));
            const float angle = Rml::Get(parameters, "angle", 0.0f);
            v = Rml::Vector2f(std::cos(angle), std::sin(angle));
        } else {
            return 0;
        }
        auto found = parameters.find("color_stop_list");
        if (found == parameters.end() || found->second.GetType() != Rml::Variant::COLORSTOPLIST) return 0;
        const Rml::ColorStopList &list = found->second.GetReference<Rml::ColorStopList>();
        std::vector<float> stops;
        for (const Rml::ColorStop &stop : list) {
            if (stops.size() == 16 * 5) break;
            stops.push_back(stop.position.number);
            stops.push_back(stop.color.red / 255.0f); /* premultiplied, as RmlUi gives them */
            stops.push_back(stop.color.green / 255.0f);
            stops.push_back(stop.color.blue / 255.0f);
            stops.push_back(stop.color.alpha / 255.0f);
        }
        if (stops.empty()) return 0;
        return (Rml::CompiledShaderHandle)wgf_ui_priv_doc_gradient_make(
            kind, Rml::Get(parameters, "repeating", false), p.x, p.y, v.x, v.y, stops.data(), (int)(stops.size() / 5));
    }

    void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                      Rml::TextureHandle) override
    {
        const Geometry *geometry = (const Geometry *)handle;
        wgf_ui_priv_doc_gradient_draw((uintptr_t)shader, (uintptr_t)handle, geometry->vertices.data(),
                                      (int)geometry->vertices.size(), geometry->indices.data(),
                                      (int)geometry->indices.size(), translation.x, translation.y);
    }

    void ReleaseShader(Rml::CompiledShaderHandle shader) override { wgf_ui_priv_doc_gradient_release((uintptr_t)shader); }

    /* the frame's drawing starts with no scissor */
    void Begin()
    {
        scissor_on = false;
        scissor = Rml::Rectanglei::FromSize({0, 0});
    }

private:
    bool scissor_on = false;
    Rml::Rectanglei scissor = Rml::Rectanglei::FromSize({0, 0});
};

/* ---- fonts: libwgf's (fontstash's), by family ---------------------------------------- */

class Fonts final : public Rml::FontEngineInterface {
public:
    Fonts() : atlas(atlas_texture()) {}

    /* the atlas's texture let go of before RmlUi's render managers are */
    void Shutdown() override
    {
        atlas = Rml::CallbackTextureSource();
        metrics.clear();
    }

    /* faces are libwgf's fonts, which the program sets by family (wgf_ui_set_document_font) */
    bool LoadFontFace(const Rml::String &, int, bool, Rml::Style::FontWeight) override { return true; }

    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String &family, Rml::Style::FontStyle style,
                                          Rml::Style::FontWeight weight, int size) override
    {
        const int face = wgf_ui_priv_doc_font_face(family.c_str(), size, (int)weight,
                                                   style == Rml::Style::FontStyle::Italic);
        if (face <= 0) return 0;
        if ((size_t)face >= metrics.size()) metrics.resize((size_t)face + 1);
        Rml::FontMetrics &m = metrics[(size_t)face];
        float ascent = 0.0f, descent = 0.0f, line = 0.0f, x_height = 0.0f;
        wgf_ui_priv_doc_font_metrics(face, &ascent, &descent, &line, &x_height);
        m.size = size;
        m.ascent = ascent;
        m.descent = descent;
        m.line_spacing = line;
        m.x_height = x_height;
        m.underline_position = descent * 0.5f;
        m.underline_thickness = size >= 28 ? (float)size / 14.0f : 1.0f;
        m.has_ellipsis = false;
        return (Rml::FontFaceHandle)face;
    }

    Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle, const Rml::FontEffectList &) override { return 0; }

    const Rml::FontMetrics &GetFontMetrics(Rml::FontFaceHandle handle) override
    {
        static const Rml::FontMetrics none = {};
        return (size_t)handle < metrics.size() ? metrics[(size_t)handle] : none;
    }

    int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext &,
                       Rml::Character) override
    {
        const float width = wgf_ui_priv_doc_font_width((int)handle, string.begin(), (int)string.size());
        return (int)(width + 0.5f);
    }

    int GenerateString(Rml::RenderManager &render_manager, Rml::FontFaceHandle handle, Rml::FontEffectsHandle,
                       Rml::StringView string, Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity,
                       const Rml::TextShapingContext &, Rml::TexturedMeshList &mesh_list) override
    {
        const int length = (int)string.size();
        const Rml::ColourbPremultiplied tint = opacity < 1.0f ? colour * opacity : colour;
        quads.resize((size_t)(length > 0 ? length : 1) * 8);
        const int count = wgf_ui_priv_doc_font_quads((int)handle, string.begin(), length, position.x, position.y,
                                                     quads.data(), length);
        Rml::TexturedMesh textured;
        textured.texture = atlas.GetTexture(render_manager);
        Rml::Mesh &mesh = textured.mesh;
        mesh.vertices.reserve((size_t)count * 4);
        mesh.indices.reserve((size_t)count * 6);
        for (int i = 0; i < count; i++) {
            const float *q = &quads[(size_t)i * 8];
            const int base = (int)mesh.vertices.size();
            const float xs[4] = {q[0], q[2], q[2], q[0]}, ys[4] = {q[1], q[1], q[3], q[3]};
            const float us[4] = {q[4], q[6], q[6], q[4]}, vs[4] = {q[5], q[5], q[7], q[7]};
            for (int c = 0; c < 4; c++) {
                Rml::Vertex vertex;
                vertex.position = Rml::Vector2f(xs[c], ys[c]);
                vertex.colour = tint;
                vertex.tex_coord = Rml::Vector2f(us[c], vs[c]);
                mesh.vertices.push_back(vertex);
            }
            const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int k = 0; k < 6; k++) mesh.indices.push_back(base + order[k]);
        }
        if (count > 0) mesh_list.push_back(std::move(textured));
        return GetStringWidth(handle, string, Rml::TextShapingContext{Rml::String()}, Rml::Character::Null);
    }

    int GetVersion(Rml::FontFaceHandle) override { return wgf_ui_priv_doc_font_version(); }

    void ReleaseFontResources() override { metrics.clear(); }

private:
    /* the font atlas, which the C side binds as it is now each time it is drawn */
    static Rml::CallbackTextureFunction atlas_texture()
    {
        return [](const Rml::CallbackTextureInterface &texture) {
            texture.SetTextureHandle((Rml::TextureHandle)WGF_UI_PRIV_ATLAS, Rml::Vector2i(1, 1));
            return true;
        };
    }

    Rml::CallbackTextureSource atlas;
    std::vector<Rml::FontMetrics> metrics;
    std::vector<float> quads;
};

/* ---- documents, their events, and the elements the program holds --------------------- */

struct Document;

class Listener final : public Rml::EventListener {
public:
    explicit Listener(Document *document) : document(document) {}
    void ProcessEvent(Rml::Event &event) override;

private:
    Document *document;
};

struct Document {
    Rml::ElementDocument *element = nullptr;
    uint32_t owner = 0;
    bool visible = false;
    std::unique_ptr<Listener> listener;
    std::deque<int> events;
};

struct Slot {
    Rml::ObserverPtr<Rml::Element> element;
    uint32_t handle = 0;
    int document = 0;
};

struct State {
    System system;
    Files files;
    Render render;
    Fonts fonts;
    Rml::Context *context = nullptr;
    std::vector<std::unique_ptr<Document>> documents; /* by number less one; null: unloaded */
    std::vector<Slot> slots;                          /* by slot less one */
    std::vector<int> free_slots;
    std::unordered_map<Rml::Element *, int> slot_of;
    std::string text; /* what a call returning text returns */
};

State *state = nullptr;

Document *document_at(int document)
{
    if (state == nullptr || document <= 0 || (size_t)document > state->documents.size()) return nullptr;
    return state->documents[(size_t)document - 1].get();
}

int number_of(Rml::ElementDocument *element)
{
    for (size_t i = 0; i < state->documents.size(); i++) {
        if (state->documents[i] && state->documents[i]->element == element) return (int)i + 1;
    }
    return 0;
}

void free_slot(int slot)
{
    Slot &s = state->slots[(size_t)slot - 1];
    if (s.handle != 0) wgf_ui_priv_doc_element_gone(s.handle);
    if (Rml::Element *element = s.element.get()) state->slot_of.erase(element);
    s = Slot();
    state->free_slots.push_back(slot);
}

/* Slots whose element is gone, or whose document is `document` (0: none), let go of. */
void sweep(int document)
{
    for (size_t i = 0; i < state->slots.size(); i++) {
        Slot &s = state->slots[i];
        if (s.handle == 0) continue;
        if (!s.element || (document != 0 && s.document == document)) free_slot((int)i + 1);
    }
    for (auto it = state->slot_of.begin(); it != state->slot_of.end();) {
        const Slot &s = state->slots[(size_t)it->second - 1];
        if (s.handle == 0 || s.element.get() != it->first) it = state->slot_of.erase(it);
        else ++it;
    }
}

/* The element's handle, made the first time; 0 when there is no room. */
uint32_t handle_of(Rml::Element *element, int document)
{
    auto found = state->slot_of.find(element);
    if (found != state->slot_of.end()) {
        Slot &s = state->slots[(size_t)found->second - 1];
        if (s.element.get() == element && s.handle != 0) return s.handle;
        free_slot(found->second); /* an element at a dead one's address */
    }
    int slot;
    if (!state->free_slots.empty()) {
        slot = state->free_slots.back();
        state->free_slots.pop_back();
    } else {
        state->slots.emplace_back();
        slot = (int)state->slots.size();
    }
    Slot &s = state->slots[(size_t)slot - 1];
    s.element = element->GetObserverPtr();
    s.document = document;
    s.handle = wgf_ui_priv_doc_element_made(slot, document_at(document)->owner);
    if (s.handle == 0) {
        s = Slot();
        state->free_slots.push_back(slot);
        return 0;
    }
    state->slot_of[element] = slot;
    return s.handle;
}

Rml::Element *element_at(int slot)
{
    if (state == nullptr || slot <= 0 || (size_t)slot > state->slots.size()) return nullptr;
    return state->slots[(size_t)slot - 1].element.get();
}

void Listener::ProcessEvent(Rml::Event &event)
{
    int kind = 0;
    switch (event.GetId()) {
    case Rml::EventId::Click: kind = 1; break;
    case Rml::EventId::Change: kind = 2; break;
    case Rml::EventId::Submit: kind = 3; break;
    case Rml::EventId::Focus: kind = 4; break;
    case Rml::EventId::Blur: kind = 5; break;
    default: return;
    }
    Rml::Element *target = event.GetTargetElement();
    if (target == nullptr || target == document->element) return;
    Rml::Element *named = target;
    while (named != nullptr && named != document->element && named->GetId().empty()) named = named->GetParentNode();
    if (named == nullptr || named == document->element) named = target;
    const uint32_t handle = handle_of(named, number_of(document->element));
    if (handle == 0) return;
    if ((int)document->events.size() >= EVENTS_MAX) {
        document->events.pop_front();
        document->events.pop_front();
    }
    document->events.push_back(kind);
    document->events.push_back((int)handle);
}

const char *keep(const Rml::String &text)
{
    state->text = text;
    return state->text.c_str();
}

/* libwgf's keys (wgf_keyboard.h, GLFW's numbers) as RmlUi's. */
Rml::Input::KeyIdentifier key_of(int key)
{
    using namespace Rml::Input;
    if (key >= 65 && key <= 90) return (KeyIdentifier)(KI_A + (key - 65));          /* WGF_KEY_A..Z */
    if (key >= 48 && key <= 57) return (KeyIdentifier)(KI_0 + (key - 48));          /* WGF_KEY_0..9 */
    if (key >= 290 && key <= 313) return (KeyIdentifier)(KI_F1 + (key - 290));      /* WGF_KEY_F1..F24 */
    if (key >= 320 && key <= 329) return (KeyIdentifier)(KI_NUMPAD0 + (key - 320)); /* WGF_KEY_KP_0..9 */
    switch (key) {
    case 32: return KI_SPACE;
    case 39: return KI_OEM_7;
    case 44: return KI_OEM_COMMA;
    case 45: return KI_OEM_MINUS;
    case 46: return KI_OEM_PERIOD;
    case 47: return KI_OEM_2;
    case 59: return KI_OEM_1;
    case 61: return KI_OEM_PLUS;
    case 91: return KI_OEM_4;
    case 92: return KI_OEM_5;
    case 93: return KI_OEM_6;
    case 96: return KI_OEM_3;
    case 256: return KI_ESCAPE;
    case 257: return KI_RETURN;
    case 258: return KI_TAB;
    case 259: return KI_BACK;
    case 260: return KI_INSERT;
    case 261: return KI_DELETE;
    case 262: return KI_RIGHT;
    case 263: return KI_LEFT;
    case 264: return KI_DOWN;
    case 265: return KI_UP;
    case 266: return KI_PRIOR;
    case 267: return KI_NEXT;
    case 268: return KI_HOME;
    case 269: return KI_END;
    case 280: return KI_CAPITAL;
    case 281: return KI_SCROLL;
    case 282: return KI_NUMLOCK;
    case 283: return KI_SNAPSHOT;
    case 284: return KI_PAUSE;
    case 330: return KI_DECIMAL;
    case 331: return KI_DIVIDE;
    case 332: return KI_MULTIPLY;
    case 333: return KI_SUBTRACT;
    case 334: return KI_ADD;
    case 335: return KI_NUMPADENTER;
    case 336: return KI_OEM_NEC_EQUAL;
    case 340: return KI_LSHIFT;
    case 341: return KI_LCONTROL;
    case 342: return KI_LMENU;
    case 343: return KI_LMETA;
    case 344: return KI_RSHIFT;
    case 345: return KI_RCONTROL;
    case 346: return KI_RMENU;
    case 347: return KI_RMETA;
    case 348: return KI_APPS;
    default: return KI_UNKNOWN;
    }
}

} // namespace

/* ---- the C side's calls ------------------------------------------------------------- */

extern "C" {

bool wgf_ui_priv_rml_start(void)
{
    if (state != nullptr) return true;
    state = new State;
    Rml::SetSystemInterface(&state->system);
    Rml::SetFileInterface(&state->files);
    Rml::SetRenderInterface(&state->render);
    Rml::SetFontEngineInterface(&state->fonts);
    if (!Rml::Initialise()) {
        wgf_ui_priv_doc_log(3, "wgf_document: RmlUi didn't start");
        delete state;
        state = nullptr;
        return false;
    }
    state->context = Rml::CreateContext("wgf", Rml::Vector2i(1, 1), &state->render);
    if (state->context == nullptr) {
        wgf_ui_priv_doc_log(3, "wgf_document: RmlUi made no context");
        Rml::Shutdown();
        delete state;
        state = nullptr;
        return false;
    }
    return true;
}

void wgf_ui_priv_rml_stop(void)
{
    if (state == nullptr) return;
    for (size_t i = 0; i < state->slots.size(); i++) {
        if (state->slots[i].handle != 0) free_slot((int)i + 1);
    }
    Rml::Shutdown(); /* the context and its documents with it */
    delete state;
    state = nullptr;
}

int wgf_ui_priv_rml_load(const char *rml, int size, const char *path, uint32_t owner)
{
    if (state == nullptr) return 0;
    Rml::ElementDocument *element = state->context->LoadDocumentFromMemory(Rml::String(rml, (size_t)size), path);
    if (element == nullptr) return 0;
    std::unique_ptr<Document> document(new Document);
    document->element = element;
    document->owner = owner;
    document->listener.reset(new Listener(document.get()));
    const Rml::EventId kinds[5] = {Rml::EventId::Click, Rml::EventId::Change, Rml::EventId::Submit,
                                   Rml::EventId::Focus, Rml::EventId::Blur};
    for (Rml::EventId id : kinds) element->AddEventListener(id, document->listener.get(), true);
    element->Hide();
    for (size_t i = 0; i < state->documents.size(); i++) {
        if (!state->documents[i]) {
            state->documents[i] = std::move(document);
            return (int)i + 1;
        }
    }
    state->documents.push_back(std::move(document));
    return (int)state->documents.size();
}

void wgf_ui_priv_rml_unload(int number)
{
    Document *document = document_at(number);
    if (document == nullptr) return;
    sweep(number);
    const Rml::EventId kinds[5] = {Rml::EventId::Click, Rml::EventId::Change, Rml::EventId::Submit,
                                   Rml::EventId::Focus, Rml::EventId::Blur};
    for (Rml::EventId id : kinds) document->element->RemoveEventListener(id, document->listener.get(), true);
    document->element->Close(); /* gone at the context's next update */
    state->documents[(size_t)number - 1].reset();
}

void wgf_ui_priv_rml_set_visible(int number, bool visible)
{
    Document *document = document_at(number);
    if (document == nullptr) return;
    if (visible) {
        document->element->Show(Rml::ModalFlag::None, Rml::FocusFlag::Auto);
        document->element->PullToFront();
    } else {
        document->element->Hide();
    }
    document->visible = visible;
}

bool wgf_ui_priv_rml_is_visible(int number)
{
    Document *document = document_at(number);
    return document != nullptr && document->visible;
}

uint32_t wgf_ui_priv_rml_find(int number, const char *id)
{
    Document *document = document_at(number);
    if (document == nullptr || id == nullptr || id[0] == '\0') return 0;
    Rml::Element *element = document->element->GetElementById(id);
    return element != nullptr ? handle_of(element, number) : 0;
}

uint32_t wgf_ui_priv_rml_get_focus(int number)
{
    Document *document = document_at(number);
    if (document == nullptr) return 0;
    Rml::Element *focus = state->context->GetFocusElement();
    if (focus == nullptr || focus == document->element || focus->GetOwnerDocument() != document->element) return 0;
    return handle_of(focus, number);
}

bool wgf_ui_priv_rml_set_focus(int number, int slot)
{
    Document *document = document_at(number);
    if (document == nullptr) return false;
    if (slot == 0) {
        Rml::Element *focus = state->context->GetFocusElement();
        if (focus != nullptr && focus->GetOwnerDocument() == document->element) focus->Blur();
        return true;
    }
    Rml::Element *element = element_at(slot);
    if (element == nullptr || element->GetOwnerDocument() != document->element) return false;
    return element->Focus(true);
}

int wgf_ui_priv_rml_event_count(int number)
{
    Document *document = document_at(number);
    return document != nullptr ? (int)document->events.size() / 2 : 0;
}

int wgf_ui_priv_rml_take_events(int number, int *out, int count)
{
    Document *document = document_at(number);
    int n = 0;
    if (document == nullptr || out == nullptr) return 0;
    while (n + 2 <= count && document->events.size() >= 2) {
        out[n++] = document->events.front();
        document->events.pop_front();
        out[n++] = document->events.front();
        document->events.pop_front();
    }
    return n;
}

bool wgf_ui_priv_rml_element_alive(int slot)
{
    if (element_at(slot) != nullptr) return true;
    if (state != nullptr && slot > 0 && (size_t)slot <= state->slots.size() && state->slots[(size_t)slot - 1].handle != 0)
        free_slot(slot); /* gone since: its handle let go of now */
    return false;
}

int wgf_ui_priv_rml_element_document(int slot)
{
    return element_at(slot) != nullptr ? state->slots[(size_t)slot - 1].document : 0;
}

/* The text of every text node inside `element`, in order. */
static void gather_text(Rml::Element *element, Rml::String &out)
{
    if (Rml::ElementText *text = rmlui_dynamic_cast<Rml::ElementText *>(element)) out += text->GetText();
    for (int i = 0; i < element->GetNumChildren(); i++) gather_text(element->GetChild(i), out);
}

const char *wgf_ui_priv_rml_get_text(int slot, wgf_ui_priv_rml_text_t which)
{
    Rml::Element *element = element_at(slot);
    if (element == nullptr) return nullptr;
    switch (which) {
    case WGF_UI_PRIV_RML_ID: return keep(element->GetId());
    case WGF_UI_PRIV_RML_RML: return keep(element->GetInnerRML());
    case WGF_UI_PRIV_RML_VALUE: {
        Rml::ElementFormControl *control = rmlui_dynamic_cast<Rml::ElementFormControl *>(element);
        return control != nullptr ? keep(control->GetValue()) : nullptr;
    }
    case WGF_UI_PRIV_RML_TEXT:
    default: {
        Rml::String text;
        gather_text(element, text);
        return keep(text);
    }
    }
}

bool wgf_ui_priv_rml_set_text(int slot, wgf_ui_priv_rml_text_t which, const char *text)
{
    Rml::Element *element = element_at(slot);
    if (element == nullptr || text == nullptr) return false;
    switch (which) {
    case WGF_UI_PRIV_RML_TEXT:
        element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
        sweep(0); /* the elements it held are gone, and their handles with them */
        return true;
    case WGF_UI_PRIV_RML_RML:
        element->SetInnerRML(text);
        sweep(0);
        return true;
    case WGF_UI_PRIV_RML_VALUE: {
        Rml::ElementFormControl *control = rmlui_dynamic_cast<Rml::ElementFormControl *>(element);
        if (control == nullptr) return false;
        control->SetValue(text);
        return true;
    }
    default: return false;
    }
}

const char *wgf_ui_priv_rml_get_attribute(int slot, const char *name, bool *has)
{
    Rml::Element *element = element_at(slot);
    *has = false;
    if (element == nullptr || name == nullptr || name[0] == '\0') return nullptr;
    *has = element->HasAttribute(name);
    return keep(element->GetAttribute<Rml::String>(name, ""));
}

bool wgf_ui_priv_rml_set_attribute(int slot, const char *name, const char *value)
{
    Rml::Element *element = element_at(slot);
    if (element == nullptr || name == nullptr || name[0] == '\0') return false;
    if (value == nullptr) {
        if (!element->HasAttribute(name)) return false;
        element->RemoveAttribute(name);
        return true;
    }
    element->SetAttribute(name, Rml::String(value));
    return true;
}

bool wgf_ui_priv_rml_has_class(int slot, const char *name)
{
    Rml::Element *element = element_at(slot);
    return element != nullptr && name != nullptr && name[0] != '\0' && element->IsClassSet(name);
}

bool wgf_ui_priv_rml_set_class(int slot, const char *name, bool on)
{
    Rml::Element *element = element_at(slot);
    if (element == nullptr || name == nullptr || name[0] == '\0') return false;
    element->SetClass(name, on);
    return true;
}

const char *wgf_ui_priv_rml_get_property(int slot, const char *name)
{
    Rml::Element *element = element_at(slot);
    if (element == nullptr || name == nullptr || name[0] == '\0') return nullptr;
    const Rml::Property *property = element->GetProperty(name);
    return keep(property != nullptr ? property->ToString() : Rml::String());
}

bool wgf_ui_priv_rml_set_property(int slot, const char *name, const char *value)
{
    Rml::Element *element = element_at(slot);
    if (element == nullptr || name == nullptr || name[0] == '\0') return false;
    if (value == nullptr || value[0] == '\0') {
        if (Rml::StyleSheetSpecification::GetPropertyId(name) == Rml::PropertyId::Invalid &&
            Rml::StyleSheetSpecification::GetShorthandId(name) == Rml::ShorthandId::Invalid)
            return false;
        element->RemoveProperty(name);
        return true;
    }
    return element->SetProperty(name, value);
}

bool wgf_ui_priv_rml_is_form_control(int slot)
{
    return rmlui_dynamic_cast<Rml::ElementFormControl *>(element_at(slot)) != nullptr;
}

bool wgf_ui_priv_rml_get_number(int slot, float *value)
{
    Rml::ElementFormControl *control = rmlui_dynamic_cast<Rml::ElementFormControl *>(element_at(slot));
    *value = 0.0f;
    if (control == nullptr) return false;
    const Rml::String text = control->GetValue();
    char *end = nullptr;
    const float number = std::strtof(text.c_str(), &end);
    if (end != text.c_str() && number == number) *value = number;
    return true;
}

bool wgf_ui_priv_rml_set_number(int slot, float value)
{
    Rml::ElementFormControl *control = rmlui_dynamic_cast<Rml::ElementFormControl *>(element_at(slot));
    if (control == nullptr || value != value) return false;
    char text[64];
    std::snprintf(text, sizeof(text), "%.9g", (double)value);
    control->SetValue(text);
    return true;
}

static Rml::ElementFormControlInput *checkable(int slot)
{
    Rml::ElementFormControlInput *input = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(element_at(slot));
    if (input == nullptr) return nullptr;
    const Rml::String type = input->GetAttribute<Rml::String>("type", "");
    return type == "checkbox" || type == "radio" ? input : nullptr;
}

bool wgf_ui_priv_rml_is_checkable(int slot)
{
    return checkable(slot) != nullptr;
}

bool wgf_ui_priv_rml_is_checked(int slot)
{
    Rml::ElementFormControlInput *input = checkable(slot);
    return input != nullptr && input->HasAttribute("checked");
}

bool wgf_ui_priv_rml_set_checked(int slot, bool checked)
{
    Rml::ElementFormControlInput *input = checkable(slot);
    if (input == nullptr) return false;
    if (checked) input->SetAttribute("checked", Rml::String());
    else input->RemoveAttribute("checked");
    return true;
}

bool wgf_ui_priv_rml_is_disabled(int slot)
{
    Rml::ElementFormControl *control = rmlui_dynamic_cast<Rml::ElementFormControl *>(element_at(slot));
    return control != nullptr && control->IsDisabled();
}

bool wgf_ui_priv_rml_set_disabled(int slot, bool disabled)
{
    Rml::ElementFormControl *control = rmlui_dynamic_cast<Rml::ElementFormControl *>(element_at(slot));
    if (control == nullptr) return false;
    control->SetDisabled(disabled);
    return true;
}

bool wgf_ui_priv_rml_element_box(int slot, float *x, float *y, float *width, float *height)
{
    Rml::Element *element = element_at(slot);
    if (element == nullptr) return false;
    const Rml::Vector2f at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
    *x = at.x;
    *y = at.y;
    *width = size.x;
    *height = size.y;
    return true;
}

void wgf_ui_priv_rml_frame(const wgf_ui_priv_rml_input_t *input, bool *pointer, bool *keyboard)
{
    *pointer = *keyboard = false;
    if (state == nullptr) return;
    Rml::Context *context = state->context;
    if (input != nullptr) {
        const int w = input->width > 1.0f ? (int)(input->width + 0.5f) : 1;
        const int h = input->height > 1.0f ? (int)(input->height + 0.5f) : 1;
        if (context->GetDimensions() != Rml::Vector2i(w, h)) context->SetDimensions(Rml::Vector2i(w, h));
        const int modifiers = (input->ctrl ? Rml::Input::KM_CTRL : 0) | (input->shift ? Rml::Input::KM_SHIFT : 0) |
                              (input->alt ? Rml::Input::KM_ALT : 0) | (input->super ? Rml::Input::KM_META : 0);
        if (input->touch_count > 0) {
            /* fingers as RmlUi's touches, which press and move its pointer themselves and
               scroll what they drag */
            Rml::TouchList phases[4];
            for (int i = 0; i < input->touch_count && i < 8; i++) {
                const int phase = input->touch_phase[i];
                if (phase < 0 || phase > 3) continue;
                Rml::Touch touch;
                touch.identifier = (Rml::TouchId)(input->touch_ids[i] + 1);
                touch.position = Rml::Vector2f(input->touch_x[i], input->touch_y[i]);
                phases[phase].push_back(touch);
            }
            if (!phases[0].empty()) context->ProcessTouchStart(phases[0], modifiers);
            if (!phases[1].empty()) context->ProcessTouchMove(phases[1], modifiers);
            if (!phases[2].empty()) context->ProcessTouchEnd(phases[2], modifiers);
            if (!phases[3].empty()) context->ProcessTouchCancel(phases[3]);
        }
        if (!input->touching) { /* the mouse, unless it is touch's */
            if (input->mouse_inside) {
                context->ProcessMouseMove((int)input->mouse_x, (int)input->mouse_y, modifiers);
            } else {
                context->ProcessMouseLeave();
            }
            for (int b = 0; b < 3; b++) {
                if (input->buttons_pressed & (1 << b)) context->ProcessMouseButtonDown(b, modifiers);
                if (input->buttons_released & (1 << b)) context->ProcessMouseButtonUp(b, modifiers);
            }
        }
        if (input->wheel_x != 0.0f || input->wheel_y != 0.0f)
            context->ProcessMouseWheel(Rml::Vector2f(input->wheel_x, input->wheel_y), modifiers);
        for (int i = 0; i < input->key_down_count; i++) {
            const Rml::Input::KeyIdentifier key = key_of(input->keys_down[i]);
            if (key != Rml::Input::KI_UNKNOWN) context->ProcessKeyDown(key, modifiers);
        }
        if (input->text != nullptr && input->text[0] != '\0') {
            Rml::String typed;
            for (const char *c = input->text; *c != '\0'; c++) {
                if ((unsigned char)*c >= 32 && *c != 127) typed += *c; /* control characters are keys */
            }
            if (!typed.empty()) context->ProcessTextInput(typed);
        }
        for (int i = 0; i < input->key_up_count; i++) {
            const Rml::Input::KeyIdentifier key = key_of(input->keys_up[i]);
            if (key != Rml::Input::KI_UNKNOWN) context->ProcessKeyUp(key, modifiers);
        }
    }
    context->Update();
    sweep(0);
    state->render.Begin();
    context->Render();
    wgf_ui_priv_doc_scissor(false, 0, 0, 0, 0);
    wgf_ui_priv_doc_transform(nullptr);

    Rml::Element *hover = context->GetHoverElement();
    if (hover != nullptr && hover != context->GetRootElement()) {
        Rml::ElementDocument *owner = hover->GetOwnerDocument();
        *pointer = owner != nullptr && hover != owner && owner->IsVisible();
    }
    Rml::Element *focus = context->GetFocusElement();
    if (focus != nullptr) {
        Rml::ElementDocument *owner = focus->GetOwnerDocument();
        *keyboard = owner != nullptr && focus != owner && owner->IsVisible();
    }
}

} /* extern "C" */
