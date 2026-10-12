#include "wgf_shader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "material/wgf_gfx_shader_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"

/* Custom shaders, libwgt's wgt_gfx_shader.c (wgrender's wgr_shader): .wgfshader files from
 * tools/pack_shader.py (the format is written there), through core's load pipeline.
 * Preparing reads the file on a worker; finishing picks the running backend's sources and
 * makes its program: a surface shader's model one, or a screen effect's. A part, installed
 * by the first shader made; its stop, after the materials that hold shaders, frees them. */

#define FORMAT_VERSION 1
#define GLSL_NAME_MAX 128 /* a texture-sampler pair joins two names: longer than the parameters' */
#define NAMES_MAX 64

typedef struct shader_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    wgf_gfx_priv_shader_t shader;      /* READY: its program */
} shader_t;

static bool pool_ready;
static wgf_core_priv_handle_pool_t pool;
static shader_t *shaders; /* grown by the pool: don't hold a pointer across a create */

static shader_t *shader_of(wgf_shader_t shader)
{
    uint16_t index;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve(&pool, shader, &index)) return NULL;
    return &shaders[index];
}

wgf_gfx_priv_shader_t *wgf_gfx_priv_shader_get(wgf_shader_t shader)
{
    shader_t *shader_ptr = shader_of(shader);
    return shader_ptr != NULL && shader_ptr->resource.status == WGF_RESOURCE_STATUS_READY ? &shader_ptr->shader : NULL;
}

bool wgf_gfx_priv_shader_is(wgf_shader_t shader)
{
    return shader_of(shader) != NULL;
}

int wgf_gfx_priv_shader_find_param(const wgf_gfx_priv_shader_t *shader, const char *name)
{
    int i;
    for (i = 0; shader != NULL && name != NULL && i < shader->param_count; i++) {
        if (strcmp(shader->params[i].name, name) == 0) return i;
    }
    return -1;
}

int wgf_gfx_priv_shader_find_texture(const wgf_gfx_priv_shader_t *shader, const char *name)
{
    int i;
    for (i = 0; shader != NULL && name != NULL && i < shader->texture_count; i++) {
        if (strcmp(shader->textures[i], name) == 0) return i;
    }
    return -1;
}

/* ------------------------------------------------------------- parsing ---- */

typedef struct reader_t {
    const char *at, *end;
} reader_t;

/* The next line (without its newline) into `line`; false at the end. */
static bool next_line(reader_t *r, char *line, size_t size)
{
    const char *nl;
    size_t n;
    if (r->at >= r->end) return false;
    nl = (const char *)memchr(r->at, '\n', (size_t)(r->end - r->at));
    n = (size_t)((nl != NULL ? nl : r->end) - r->at);
    snprintf(line, size, "%.*s", (int)(n < size ? n : size - 1), r->at);
    r->at = nl != NULL ? nl + 1 : r->end;
    return true;
}

static sg_shader_stage stage_of(const char *s)
{
    return strcmp(s, "vertex") == 0 ? SG_SHADERSTAGE_VERTEX : SG_SHADERSTAGE_FRAGMENT;
}

/* The name pack_shader.py gives this backend's sources; NULL when there are none. */
static const char *backend_slang(void)
{
    switch (sg_query_backend()) {
        case SG_BACKEND_GLCORE: return "glsl410";
        case SG_BACKEND_GLES3: return "glsl300es";
        case SG_BACKEND_DUMMY: return "glsl410"; /* a headless build: its layout checked only */
        default: return NULL;
    }
}

typedef struct program_desc_t {
    sg_shader_desc desc;
    char *sources[2];                   /* vertex, fragment: NUL-terminated copies */
    char names[NAMES_MAX][GLSL_NAME_MAX]; /* the GLSL names the desc points at */
    int name_count;
    char view_names[SG_MAX_VIEW_BINDSLOTS][WGF_GFX_PRIV_SHADER_NAME_MAX];
    int pair_view[SG_MAX_TEXTURE_SAMPLER_PAIRS];
    int pair_sampler[SG_MAX_TEXTURE_SAMPLER_PAIRS];
    int pair_count;
    bool has_block[WGF_GFX_PRIV_SHADER_BLOCK_COUNT];
    bool found;
} program_desc_t;

static const char *keep_name(program_desc_t *p, const char *name)
{
    if (strcmp(name, "-") == 0 || p->name_count >= NAMES_MAX) return NULL;
    snprintf(p->names[p->name_count], GLSL_NAME_MAX, "%s", name);
    return p->names[p->name_count++];
}

/* One line describing a program (attr, ub, view, sampler, pair); false when it isn't one. */
static bool describe(program_desc_t *p, const char *line)
{
    char a[64], b[GLSL_NAME_MAX], c[64], d[GLSL_NAME_MAX];
    int slot, n1, n2;
    sg_shader_desc *desc = &p->desc;

    if (sscanf(line, "attr %d %127s", &slot, b) == 2) {
        if (slot < 0 || slot >= SG_MAX_VERTEX_ATTRIBUTES) return false;
        desc->attrs[slot].base_type = SG_SHADERATTRBASETYPE_FLOAT;
        desc->attrs[slot].glsl_name = keep_name(p, b);
        return true;
    }
    if (sscanf(line, "ub %d %63s %d %127s %d", &slot, a, &n1, b, &n2) == 5) {
        if (slot < 0 || slot >= WGF_GFX_PRIV_SHADER_BLOCK_COUNT || n1 <= 0) return false;
        desc->uniform_blocks[slot].stage = stage_of(a);
        desc->uniform_blocks[slot].layout = SG_UNIFORMLAYOUT_STD140;
        desc->uniform_blocks[slot].size = (uint32_t)n1;
        if (strcmp(b, "-") != 0) {
            desc->uniform_blocks[slot].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
            desc->uniform_blocks[slot].glsl_uniforms[0].array_count = (uint16_t)n2;
            desc->uniform_blocks[slot].glsl_uniforms[0].glsl_name = keep_name(p, b);
        }
        p->has_block[slot] = true;
        return true;
    }
    if (sscanf(line, "view %d %63s %127s %127s %63s", &slot, a, b, d, c) == 5) {
        const bool cube = strcmp(d, "cube") == 0, array = strcmp(d, "array") == 0;
        const bool unfilterable = strcmp(c, "unfilterable_float") == 0, depth = strcmp(c, "depth") == 0;
        if (slot < 0 || slot >= SG_MAX_VIEW_BINDSLOTS || (!unfilterable && !depth && strcmp(c, "float") != 0) ||
            (!cube && !array && strcmp(d, "2d") != 0)) {
            return false;
        }
        desc->views[slot].texture.stage = stage_of(a);
        desc->views[slot].texture.image_type = cube ? SG_IMAGETYPE_CUBE : array ? SG_IMAGETYPE_ARRAY : SG_IMAGETYPE_2D;
        desc->views[slot].texture.sample_type = unfilterable ? SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT
                                                : depth      ? SG_IMAGESAMPLETYPE_DEPTH
                                                             : SG_IMAGESAMPLETYPE_FLOAT;
        snprintf(p->view_names[slot], WGF_GFX_PRIV_SHADER_NAME_MAX, "%.*s", WGF_GFX_PRIV_SHADER_NAME_MAX - 1, b);
        return true;
    }
    if (sscanf(line, "sampler %d %63s %127s %63s", &slot, a, b, c) == 4) {
        if (slot < 0 || slot >= SG_MAX_SAMPLER_BINDSLOTS) return false;
        desc->samplers[slot].stage = stage_of(a);
        desc->samplers[slot].sampler_type = strcmp(c, "nonfiltering") == 0  ? SG_SAMPLERTYPE_NONFILTERING
                                            : strcmp(c, "comparison") == 0 ? SG_SAMPLERTYPE_COMPARISON
                                                                           : SG_SAMPLERTYPE_FILTERING;
        return true;
    }
    if (sscanf(line, "pair %d %63s %d %d %127s", &slot, a, &n1, &n2, d) == 5) {
        if (slot < 0 || slot >= SG_MAX_TEXTURE_SAMPLER_PAIRS || n1 < 0 || n1 >= SG_MAX_VIEW_BINDSLOTS || n2 < 0 ||
            n2 >= SG_MAX_SAMPLER_BINDSLOTS || p->pair_count >= SG_MAX_TEXTURE_SAMPLER_PAIRS) {
            return false;
        }
        desc->texture_sampler_pairs[slot].stage = stage_of(a);
        desc->texture_sampler_pairs[slot].view_slot = (uint8_t)n1;
        desc->texture_sampler_pairs[slot].sampler_slot = (uint8_t)n2;
        desc->texture_sampler_pairs[slot].glsl_name = keep_name(p, d);
        p->pair_view[p->pair_count] = n1;
        p->pair_sampler[p->pair_count++] = n2;
        return true;
    }
    return false;
}

static int param_size(wgf_gfx_priv_shader_param_type_t type)
{
    return type == WGF_GFX_PRIV_SHADER_PARAM_VEC2 ? 8
           : type == WGF_GFX_PRIV_SHADER_PARAM_VEC3 ? 12
           : type == WGF_GFX_PRIV_SHADER_PARAM_VEC4 ? 16
                                                    : 4;
}

/* A parameter line's type; false when it isn't one. */
static bool param_type(const char *s, wgf_gfx_priv_shader_param_type_t *out)
{
    static const char *const names[] = {"float", "int", "vec2", "vec3", "vec4"};
    int i;
    for (i = 0; i < 5; i++) {
        if (strcmp(s, names[i]) == 0) {
            *out = (wgf_gfx_priv_shader_param_type_t)i;
            return true;
        }
    }
    return false;
}

/* The program a shader of its kind has: a screen effect's, or a surface's model one. */
static int wanted_program(const wgf_gfx_priv_shader_t *shader)
{
    return shader->screen ? WGF_GFX_PRIV_SHADER_PROGRAM_SCREEN : WGF_GFX_PRIV_SHADER_PROGRAM_MODEL;
}

/* Read a .wgfshader: its parameters and textures into `out`, and its programs for `slang`
 * into `programs`. False (with *error) when the file can't be used. */
static bool parse(const unsigned char *bytes, size_t size, const char *slang, wgf_gfx_priv_shader_t *out,
                  program_desc_t programs[WGF_GFX_PRIV_SHADER_PROGRAM_COUNT], const char **error)
{
    reader_t r;
    char line[512], a[64], b[64], c[64];
    int version = 0, n;
    program_desc_t *current = NULL;
    const program_desc_t *wanted;

    r.at = (const char *)bytes;
    r.end = (const char *)bytes + size;
    if (!next_line(&r, line, sizeof(line)) || sscanf(line, "wgfshader %d", &version) != 1) {
        *error = "not a .wgfshader file";
        return false;
    }
    if (version != FORMAT_VERSION) {
        *error = "made by another version of tools/pack_shader.py (pack it again)";
        return false;
    }
    while (next_line(&r, line, sizeof(line))) {
        if (strcmp(line, "end") == 0) {
            break;
        } else if (sscanf(line, "param %63s %63s %63s %d", a, b, c, &n) == 4) {
            wgf_gfx_priv_shader_param_t *param = &out->params[out->param_count];
            int end;
            if (out->param_count >= WGF_GFX_PRIV_SHADER_MAX_PARAMS || strlen(a) >= WGF_GFX_PRIV_SHADER_NAME_MAX) {
                *error = "too many parameters, or a name too long";
                return false;
            }
            if (!param_type(b, &param->type) || n < 0) {
                *error = "a parameter of a type it doesn't know";
                return false;
            }
            memcpy(param->name, a, strlen(a) + 1);
            param->block = strcmp(c, "vs") == 0 ? WGF_GFX_PRIV_SHADER_BLOCK_VS_PARAMS : WGF_GFX_PRIV_SHADER_BLOCK_FS_PARAMS;
            param->offset = n;
            end = (n + param_size(param->type) + 15) / 16 * 16;
            if (end > out->block_size[param->block]) out->block_size[param->block] = end;
            out->param_count++;
        } else if (sscanf(line, "kind %63s", a) == 1 && (strcmp(a, "screen") == 0 || strcmp(a, "surface") == 0)) {
            out->screen = strcmp(a, "screen") == 0;
        } else if (sscanf(line, "texture %63s", a) == 1) {
            if (out->texture_count >= WGF_GFX_PRIV_SHADER_MAX_TEXTURES || strlen(a) >= WGF_GFX_PRIV_SHADER_NAME_MAX) {
                *error = "too many textures, or a name too long";
                return false;
            }
            memcpy(out->textures[out->texture_count++], a, strlen(a) + 1);
        } else if (sscanf(line, "program %63s %63s", a, b) == 2) {
            const int which = strcmp(a, "model") == 0    ? WGF_GFX_PRIV_SHADER_PROGRAM_MODEL
                              : strcmp(a, "screen") == 0 ? WGF_GFX_PRIV_SHADER_PROGRAM_SCREEN
                                                         : -1;
            current = which >= 0 && strcmp(b, slang) == 0 ? &programs[which] : NULL;
            if (current != NULL) current->found = true;
        } else if (sscanf(line, "source %63s %d", a, &n) == 2) {
            if (n < 0 || (size_t)n > (size_t)(r.end - r.at)) {
                *error = "a source is cut short";
                return false;
            }
            if (current != NULL) {
                char **source = &current->sources[strcmp(a, "vs") == 0 ? 0 : 1];
                free(*source);
                *source = (char *)malloc((size_t)n + 1);
                if (*source == NULL) {
                    *error = "out of memory";
                    return false;
                }
                memcpy(*source, r.at, (size_t)n);
                (*source)[n] = '\0';
            }
            r.at += n;
            if (r.at < r.end && *r.at == '\n') r.at++;
        } else if (current != NULL && !describe(current, line)) {
            *error = "a line it doesn't understand";
            return false;
        }
    }
    wanted = &programs[wanted_program(out)];
    if (!wanted->found || wanted->sources[0] == NULL || wanted->sources[1] == NULL) {
        *error = "no program for this graphics backend";
        return false;
    }
    return true;
}

/* Where each of libwgf's textures (by its name in wgf.glsl) and its sampler go in `p`'s
 * program, and the shader's own textures. */
static void find_slots(const wgf_gfx_priv_shader_t *shader, const program_desc_t *p,
                       wgf_gfx_priv_shader_program_t *program)
{
    static const char *const names[WGF_GFX_PRIV_SHADER_VIEW_COUNT] = {"wgf_env_tex", "wgf_brdf_tex", "wgf_shadow_tex",
                                                                       "wgf_instance_tex", "wgf_screen_tex"};
    int n, v, k, t;
    for (n = 0; n < WGF_GFX_PRIV_SHADER_VIEW_COUNT; n++) {
        program->library_view[n] = program->library_sampler[n] = -1;
        for (v = 0; v < SG_MAX_VIEW_BINDSLOTS; v++) {
            if (strcmp(p->view_names[v], names[n]) != 0) continue;
            program->library_view[n] = v;
            for (k = 0; k < p->pair_count; k++) {
                if (p->pair_view[k] == v) program->library_sampler[n] = p->pair_sampler[k];
            }
        }
    }
    for (t = 0; t < WGF_GFX_PRIV_SHADER_MAX_TEXTURES; t++) {
        program->view_slot[t] = program->sampler_slot[t] = -1;
    }
    for (t = 0; t < shader->texture_count; t++) {
        for (v = 0; v < WGF_GFX_PRIV_SHADER_MAX_TEXTURES; v++) {
            if (strcmp(p->view_names[v], shader->textures[t]) == 0) program->view_slot[t] = v;
        }
        for (k = 0; k < p->pair_count; k++) {
            if (program->view_slot[t] >= 0 && p->pair_view[k] == program->view_slot[t]) {
                program->sampler_slot[t] = p->pair_sampler[k];
                break;
            }
        }
    }
}

static void destroy_gpu(wgf_gfx_priv_shader_t *shader)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (shader->pipelines[i].id != SG_INVALID_ID) sg_destroy_pipeline(shader->pipelines[i]);
    }
    for (i = 0; i < 2; i++) {
        if (shader->screen_pipelines[i].id != SG_INVALID_ID) sg_destroy_pipeline(shader->screen_pipelines[i]);
    }
    for (i = 0; i < WGF_GFX_PRIV_SHADER_PROGRAM_COUNT; i++) {
        if (shader->programs[i].shader.id != SG_INVALID_ID) sg_destroy_shader(shader->programs[i].shader);
    }
}

/* -------------------------------------------------------------- loader ---- */

typedef struct prepared_t {
    unsigned char *bytes;
    int size;
} prepared_t;

static void *prepare(const char *path)
{
    prepared_t *prepared = (prepared_t *)calloc(1, sizeof(prepared_t));
    if (prepared == NULL) return NULL;
    if (!wgf_core_priv_fs_read(path, &prepared->bytes, &prepared->size)) {
        free(prepared);
        return NULL;
    }
    return prepared;
}

static void discard(void *data)
{
    prepared_t *prepared = (prepared_t *)data;
    wgf_core_priv_fs_read_free(prepared->bytes);
    free(prepared);
}

static wgf_core_priv_load_step_t finish(void *data, wgf_handle_t resource)
{
    static const char *const labels[WGF_GFX_PRIV_SHADER_PROGRAM_COUNT] = {"wgf-custom-model", "wgf-custom-screen"};
    const prepared_t *prepared = (const prepared_t *)data;
    shader_t *shader_ptr = shader_of(resource);
    const char *slang, *error = NULL;
    program_desc_t *programs, *p;
    wgf_gfx_priv_shader_t shader;
    wgf_gfx_priv_shader_program_t *program;
    bool ok;
    int i;

    if (shader_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (!wgf_gfx_priv_render_is_running()) return WGF_CORE_PRIV_LOAD_MORE; /* no GPU yet: wait for one */
    slang = backend_slang();
    if (slang == NULL) {
        wgf_log_warn("wgf_gfx_shader: %s: no custom shaders on this graphics backend", shader_ptr->resource.path);
        return WGF_CORE_PRIV_LOAD_FAILED;
    }
    programs = (program_desc_t *)calloc(WGF_GFX_PRIV_SHADER_PROGRAM_COUNT, sizeof(program_desc_t));
    memset(&shader, 0, sizeof(shader));
    ok = programs != NULL && parse(prepared->bytes, (size_t)prepared->size, slang, &shader, programs, &error);
    if (ok) {
        i = wanted_program(&shader);
        p = &programs[i];
        program = &shader.programs[i];
        p->desc.vertex_func.source = p->sources[0];
        p->desc.vertex_func.entry = "main";
        p->desc.fragment_func.source = p->sources[1];
        p->desc.fragment_func.entry = "main";
        p->desc.label = labels[i];
        memcpy(program->has_block, p->has_block, sizeof(program->has_block));
        find_slots(&shader, p, program);
        program->shader = sg_make_shader(&p->desc);
        if (sg_query_shader_state(program->shader) != SG_RESOURCESTATE_VALID) {
            error = "the graphics backend refused it (the error above says why)";
            ok = false;
        }
    }
    for (i = 0; programs != NULL && i < WGF_GFX_PRIV_SHADER_PROGRAM_COUNT; i++) {
        free(programs[i].sources[0]);
        free(programs[i].sources[1]);
    }
    free(programs);
    if (!ok) {
        wgf_log_warn("wgf_gfx_shader: %s: %s", shader_ptr->resource.path, error != NULL ? error : "out of memory");
        destroy_gpu(&shader);
        return WGF_CORE_PRIV_LOAD_FAILED;
    }
    shader_ptr->shader = shader;
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"shader", prepare, finish, discard, fail, NULL, false, NULL};

/* ----------------------------------------------------------- the part ---- */

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

/* What a record holds past the header: its program and pipelines, once READY. */
static void free_shader(wgf_handle_t shader, void *record)
{
    shader_t *shader_ptr = (shader_t *)record;
    (void)shader;
    if (shader_ptr->resource.status == WGF_RESOURCE_STATUS_READY) destroy_gpu(&shader_ptr->shader);
}

static const wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_shader_create",
                                                            .loader = loader_of,
                                                            .free = free_shader};

static void stop(void)
{
    if (!pool_ready) return;
    wgf_core_priv_resource_unregister(&pool);
    wgf_core_priv_handle_pool_destroy(&pool);
    shaders = NULL;
    pool_ready = false;
}

static wgf_core_priv_part_t part = {.name = "shaders",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_SHADERS,
                                    .stop = stop};

/* ---------------------------------------------------------- public API ---- */

wgf_shader_t wgf_shader_create(const char *path)
{
    if (!pool_ready) {
        pool_ready = wgf_core_priv_handle_pool_init(&pool, WGF_CORE_PRIV_HANDLE_KIND_SHADER, (void **)&shaders,
                                                    sizeof(shader_t), 8, 4096);
        if (!pool_ready) return 0;
        wgf_core_priv_resource_register(&pool, &resource_kind);
    }
    wgf_core_priv_part_install(&part);
    return wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_SHADER, path);
}
