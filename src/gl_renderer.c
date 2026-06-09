/*
 * OpenGL 3.3 Core renderer for SLAYER3D.
 *
 * Implements the slayer3d_backend_interface using streaming VAO/VBO draws,
 * a scene UBO for per-frame data, and an offscreen FBO with a
 * fullscreen-triangle copy pass for letterboxed presentation.
 */

#include "gl_renderer_internal.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include "gl_renderer_programs.h"
#include "gl_renderer_shaders.h"

#include <string.h>

static void draw_entry_capture_viewport(slayer3d_draw_entry *entry, const slayer3d_render_context *context);

static const float k_cube_vertices[] = {
    -1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1,
};
static const unsigned int k_cube_indices[] = {
    0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4,
};

static const char *gl_version_prefix_for_context(const slayer3d_gl_context *ctx)
{
    return (ctx != NULL && ctx->is_es) ? "#version 300 es\nprecision highp float;\n" : "#version 330\n";
}

static bool csm_shadow_pass_enabled(void)
{
    return false;
}

/* ------------------------------------------------------------------ */
/* White color buffer                                                  */
/* ------------------------------------------------------------------ */

static const float *ensure_white_colors(slayer3d_gl_context *ctx, int vertex_count)
{
    int need = vertex_count * 4;
    if (need <= ctx->white_colors_capacity)
        return ctx->white_colors;
    SDL_free(ctx->white_colors);
    ctx->white_colors = SDL_malloc((size_t)need * sizeof(float));
    ctx->white_colors_capacity = need;
    for (int i = 0; i < need; i++)
        ctx->white_colors[i] = 1.0f;
    return ctx->white_colors;
}

static float *copy_floats(const float *src, size_t count);

static slayer3d_vec4 transform_vec4_columns(const float *m, float x, float y, float z, float w)
{
    return slayer3d_vec4_make(
        (m[0] * x) + (m[4] * y) + (m[8] * z) + (m[12] * w), (m[1] * x) + (m[5] * y) + (m[9] * z) + (m[13] * w),
        (m[2] * x) + (m[6] * y) + (m[10] * z) + (m[14] * w), (m[3] * x) + (m[7] * y) + (m[11] * z) + (m[15] * w));
}

static float clip_line_distance(slayer3d_vec4 v, int plane)
{
    switch (plane)
    {
    case 0:
        return v.x + v.w;
    case 1:
        return v.w - v.x;
    case 2:
        return v.y + v.w;
    case 3:
        return v.w - v.y;
    case 4:
        return v.z + v.w;
    default:
        return v.w - v.z;
    }
}

static bool clip_line_to_view(slayer3d_vec4 *a, slayer3d_vec4 *b)
{
    if (a == NULL || b == NULL)
        return false;

    for (int plane = 0; plane < 6; ++plane)
    {
        const float da = clip_line_distance(*a, plane);
        const float db = clip_line_distance(*b, plane);
        if (da < 0.0f && db < 0.0f)
            return false;
        if (da >= 0.0f && db >= 0.0f)
            continue;

        const float denom = da - db;
        if (SDL_fabsf(denom) <= 0.000001f)
            return false;
        const float t = da / denom;
        const slayer3d_vec4 p = slayer3d_vec4_lerp(*a, *b, t);
        if (da < 0.0f)
            *a = p;
        else
            *b = p;
    }

    return a->w > 0.0f && b->w > 0.0f;
}

static bool append_line_quad_vertex(float *positions, float *uvs, float *quad_colors, int *index, slayer3d_vec4 p,
                                    const float *color)
{
    if (positions == NULL || uvs == NULL || quad_colors == NULL || index == NULL || color == NULL)
        return false;
    const int i = *index;
    positions[(i * 3) + 0] = p.x;
    positions[(i * 3) + 1] = p.y;
    positions[(i * 3) + 2] = p.z;
    uvs[(i * 2) + 0] = 0.0f;
    uvs[(i * 2) + 1] = 0.0f;
    quad_colors[(i * 4) + 0] = color[0];
    quad_colors[(i * 4) + 1] = color[1];
    quad_colors[(i * 4) + 2] = color[2];
    quad_colors[(i * 4) + 3] = color[3];
    *index = i + 1;
    return true;
}

bool slayer3d_gl_append_line(slayer3d_gl_context *ctx, const float *positions, const float *colors, const float *mvp)
{
    static const float k_white_tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    if (!ctx || !positions || !colors || !mvp)
    {
        return false;
    }

    slayer3d_vec4 clip_start = transform_vec4_columns(mvp, positions[0], positions[1], positions[2], 1.0f);
    slayer3d_vec4 clip_end = transform_vec4_columns(mvp, positions[3], positions[4], positions[5], 1.0f);
    if (!clip_line_to_view(&clip_start, &clip_end))
        return true;

    slayer3d_vec4 start =
        slayer3d_vec4_make(clip_start.x / clip_start.w, clip_start.y / clip_start.w, clip_start.z / clip_start.w, 1.0f);
    slayer3d_vec4 end =
        slayer3d_vec4_make(clip_end.x / clip_end.w, clip_end.y / clip_end.w, clip_end.z / clip_end.w, 1.0f);

    const int logical_w = ctx->logical_w > 0 ? ctx->logical_w : SDL_max(ctx->world_w, 1);
    const int logical_h = ctx->logical_h > 0 ? ctx->logical_h : SDL_max(ctx->world_h, 1);
    float viewport_w = (float)logical_w;
    float viewport_h = (float)logical_h;
    if (ctx->current_ctx != NULL && ctx->current_ctx->viewport_enabled && ctx->current_ctx->viewport_rect.w > 0 &&
        ctx->current_ctx->viewport_rect.h > 0)
    {
        viewport_w = (float)ctx->current_ctx->viewport_rect.w;
        viewport_h = (float)ctx->current_ctx->viewport_rect.h;
    }
    viewport_w *= (float)SDL_max(ctx->world_w, 1) / (float)logical_w;
    viewport_h *= (float)SDL_max(ctx->world_h, 1) / (float)logical_h;

    const float dx = (end.x - start.x) * viewport_w * 0.5f;
    const float dy = (end.y - start.y) * viewport_h * 0.5f;
    const float len = SDL_sqrtf((dx * dx) + (dy * dy));
    if (len <= 0.0001f)
        return true;

    const float half_width_px = 0.75f;
    const float off_x = (-dy / len) * half_width_px * 2.0f / viewport_w;
    const float off_y = (dx / len) * half_width_px * 2.0f / viewport_h;
    const slayer3d_vec4 start_a = slayer3d_vec4_make(start.x + off_x, start.y + off_y, start.z, 1.0f);
    const slayer3d_vec4 start_b = slayer3d_vec4_make(start.x - off_x, start.y - off_y, start.z, 1.0f);
    const slayer3d_vec4 end_a = slayer3d_vec4_make(end.x + off_x, end.y + off_y, end.z, 1.0f);
    const slayer3d_vec4 end_b = slayer3d_vec4_make(end.x - off_x, end.y - off_y, end.z, 1.0f);

    float quad_positions[18];
    float quad_uvs[12];
    float quad_colors[24];
    int vertex_index = 0;
    (void)append_line_quad_vertex(quad_positions, quad_uvs, quad_colors, &vertex_index, start_a, colors);
    (void)append_line_quad_vertex(quad_positions, quad_uvs, quad_colors, &vertex_index, start_b, colors);
    (void)append_line_quad_vertex(quad_positions, quad_uvs, quad_colors, &vertex_index, end_b, colors + 4);
    (void)append_line_quad_vertex(quad_positions, quad_uvs, quad_colors, &vertex_index, start_a, colors);
    (void)append_line_quad_vertex(quad_positions, quad_uvs, quad_colors, &vertex_index, end_b, colors + 4);
    (void)append_line_quad_vertex(quad_positions, quad_uvs, quad_colors, &vertex_index, end_a, colors + 4);

    slayer3d_draw_entry *e = slayer3d_gl_append_draw_entry(ctx);
    if (!e)
        return false;

    e->lit = false;
    e->primitive_mode = GL_TRIANGLES;
    e->vertex_count = vertex_index;
    e->positions = copy_floats(quad_positions, (size_t)vertex_index * 3u);
    e->uvs = copy_floats(quad_uvs, (size_t)vertex_index * 2u);
    e->colors = copy_floats(quad_colors, (size_t)vertex_index * 4u);
    const slayer3d_mat4 identity = slayer3d_mat4_identity();
    SDL_memcpy(e->mvp, identity.m, 16 * sizeof(float));
    SDL_memcpy(e->tint, k_white_tint, sizeof(k_white_tint));
    e->disable_culling = true;
    e->owns_arrays = true;
    if (ctx->current_ctx != NULL)
        draw_entry_capture_viewport(e, ctx->current_ctx);
    return e->positions != NULL && e->uvs != NULL && e->colors != NULL;
}

/* Check for potential z-fighting: warn if two lit draw entries have
 * overlapping axis-aligned bounding boxes with coplanar faces. */
static void check_z_fighting(slayer3d_gl_context *ctx, const slayer3d_draw_entry *new_entry)
{
    if (!new_entry->lit || new_entry->vertex_count == 0)
        return;

    /* Compute AABB of the new entry from its vertex positions + model matrix translation. */
    float tx = new_entry->model_matrix[12];
    float ty = new_entry->model_matrix[13];
    float tz = new_entry->model_matrix[14];

    float min_x = 1e30f, max_x = -1e30f;
    float min_y = 1e30f, max_y = -1e30f;
    float min_z = 1e30f, max_z = -1e30f;
    for (int i = 0; i < new_entry->vertex_count; i++)
    {
        float x = new_entry->positions[i * 3 + 0] + tx;
        float y = new_entry->positions[i * 3 + 1] + ty;
        float z = new_entry->positions[i * 3 + 2] + tz;
        if (x < min_x)
            min_x = x;
        if (x > max_x)
            max_x = x;
        if (y < min_y)
            min_y = y;
        if (y > max_y)
            max_y = y;
        if (z < min_z)
            min_z = z;
        if (z > max_z)
            max_z = z;
    }

    /* Compare against all previous lit entries. */
    for (int i = 0; i < ctx->draw_count - 1; i++)
    {
        const slayer3d_draw_entry *other = &ctx->draw_list[i];
        if (!other->lit || other->vertex_count == 0)
            continue;

        float otx = other->model_matrix[12];
        float oty = other->model_matrix[13];
        float otz = other->model_matrix[14];

        float o_min_x = 1e30f, o_max_x = -1e30f;
        float o_min_y = 1e30f, o_max_y = -1e30f;
        float o_min_z = 1e30f, o_max_z = -1e30f;
        for (int j = 0; j < other->vertex_count; j++)
        {
            float x = other->positions[j * 3 + 0] + otx;
            float y = other->positions[j * 3 + 1] + oty;
            float z = other->positions[j * 3 + 2] + otz;
            if (x < o_min_x)
                o_min_x = x;
            if (x > o_max_x)
                o_max_x = x;
            if (y < o_min_y)
                o_min_y = y;
            if (y > o_max_y)
                o_max_y = y;
            if (z < o_min_z)
                o_min_z = z;
            if (z > o_max_z)
                o_max_z = z;
        }

        /* Check if AABBs overlap. */
        /* Check if AABBs overlap with at least 0.1 margin (not just touching edges). */
        float margin = 0.1f;
        if (max_x < o_min_x + margin || min_x > o_max_x - margin)
            continue;
        if (max_y < o_min_y + margin || min_y > o_max_y - margin)
            continue;
        if (max_z < o_min_z + margin || min_z > o_max_z - margin)
            continue;

        /* Z-fighting: two thin surfaces overlapping on the same thin axis.
         * A "thin" axis is one where the extent is < 0.5 units (wall thickness).
         * Both entries must be thin on the same axis AND overlap on that axis. */
        float eps = 0.02f;
        float thin = 0.5f;
        float min_thin = 0.05f; /* ignore zero-thickness geometry (skybox, planes) */
        bool zfight = false;
        float ext_x = max_x - min_x, ext_y = max_y - min_y, ext_z = max_z - min_z;
        float o_ext_x = o_max_x - o_min_x, o_ext_y = o_max_y - o_min_y, o_ext_z = o_max_z - o_min_z;

        /* Both thin on X and overlapping on X */
        if (ext_x > min_thin && ext_x < thin && o_ext_x > min_thin && o_ext_x < thin &&
            SDL_fabsf((min_x + max_x) * 0.5f - (o_min_x + o_max_x) * 0.5f) < eps)
            zfight = true;
        /* Both thin on Y and overlapping on Y */
        if (ext_y > min_thin && ext_y < thin && o_ext_y > min_thin && o_ext_y < thin &&
            SDL_fabsf((min_y + max_y) * 0.5f - (o_min_y + o_max_y) * 0.5f) < eps)
            zfight = true;
        /* Both thin on Z and overlapping on Z */
        if (ext_z > min_thin && ext_z < thin && o_ext_z > min_thin && o_ext_z < thin &&
            SDL_fabsf((min_z + max_z) * 0.5f - (o_min_z + o_max_z) * 0.5f) < eps)
            zfight = true;

        if (zfight)
        {
            char msg[512];
            SDL_snprintf(msg, sizeof(msg),
                         "Z-FIGHTING DETECTED\n\n"
                         "Two draw calls have overlapping coplanar geometry:\n\n"
                         "Entry A: (%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f)\n"
                         "Entry B: (%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f)\n\n"
                         "Fix: offset one surface by at least 0.05 units.",
                         min_x, min_y, min_z, max_x, max_y, max_z, o_min_x, o_min_y, o_min_z, o_max_x, o_max_y,
                         o_max_z);
            if (ctx->current_ctx && ctx->current_ctx->zfight_callback)
            {
                ctx->current_ctx->zfight_callback(msg, ctx->current_ctx->zfight_userdata);
            }
        }
    }
}

static float *copy_floats(const float *src, size_t count)
{
    if (!src || count == 0)
        return NULL;
    float *dst = SDL_malloc(count * sizeof(float));
    if (dst)
        SDL_memcpy(dst, src, count * sizeof(float));
    return dst;
}

static unsigned int *copy_indices(const unsigned int *src, size_t count)
{
    if (!src || count == 0)
        return NULL;
    unsigned int *dst = SDL_malloc(count * sizeof(unsigned int));
    if (dst)
        SDL_memcpy(dst, src, count * sizeof(unsigned int));
    return dst;
}

static void custom_shader_cache_free(slayer3d_gl_context *ctx)
{
    slayer3d_custom_shader_cache_entry *entry = ctx->custom_shader_cache;
    slayer3d_gl_funcs *gl = &ctx->gl;
    while (entry != NULL)
    {
        slayer3d_custom_shader_cache_entry *next = entry->next;
        if (entry->program)
            gl->DeleteProgram(entry->program);
        SDL_free(entry->vertex_source);
        SDL_free(entry->fragment_source);
        SDL_free(entry);
        entry = next;
    }
    ctx->custom_shader_cache = NULL;
}

static bool shader_source_matches_cache(const char *a, const char *b)
{
    if (a == NULL || a[0] == '\0')
        return b == NULL || b[0] == '\0';
    if (b == NULL || b[0] == '\0')
        return false;
    return SDL_strcmp(a, b) == 0;
}

static slayer3d_custom_shader_cache_entry *custom_shader_lookup_or_create(slayer3d_gl_context *ctx, bool lit,
                                                                          const char *vertex_source,
                                                                          const char *fragment_source)
{
    slayer3d_gl_funcs *gl = &ctx->gl;
    const char *version = gl_version_prefix_for_context(ctx);
    const char *default_vertex = lit ? k_pbr_vert : k_unlit_vert;

    if (fragment_source == NULL || fragment_source[0] == '\0')
        return NULL;

    for (slayer3d_custom_shader_cache_entry *entry = ctx->custom_shader_cache; entry != NULL; entry = entry->next)
    {
        if (entry->lit != lit)
            continue;
        if (shader_source_matches_cache(entry->vertex_source, vertex_source) &&
            shader_source_matches_cache(entry->fragment_source, fragment_source))
            return entry;
    }

    slayer3d_custom_shader_cache_entry *entry =
        (slayer3d_custom_shader_cache_entry *)SDL_calloc(1, sizeof(slayer3d_custom_shader_cache_entry));
    if (entry == NULL)
        return NULL;

    if (vertex_source != NULL && vertex_source[0] != '\0')
    {
        entry->vertex_source = SDL_strdup(vertex_source);
        if (entry->vertex_source == NULL)
        {
            SDL_free(entry);
            return NULL;
        }
    }
    if (fragment_source != NULL && fragment_source[0] != '\0')
    {
        entry->fragment_source = SDL_strdup(fragment_source);
        if (entry->fragment_source == NULL)
        {
            SDL_free(entry->vertex_source);
            SDL_free(entry);
            return NULL;
        }
    }
    entry->lit = lit;
    entry->program = slayer3d_gl_build_program_from_sources(gl, version, default_vertex, entry->vertex_source,
                                                            entry->fragment_source);
    if (entry->program == 0)
    {
        SDL_free(entry->vertex_source);
        SDL_free(entry->fragment_source);
        SDL_free(entry);
        return NULL;
    }

    entry->mvp_loc = gl->GetUniformLocation(entry->program, "uMVP");
    entry->texture_loc = gl->GetUniformLocation(entry->program, "uTexture");
    entry->has_texture_loc = gl->GetUniformLocation(entry->program, "uHasTexture");
    entry->tint_loc = gl->GetUniformLocation(entry->program, "uTint");
    entry->model_loc = gl->GetUniformLocation(entry->program, "uModel");
    entry->normal_matrix_loc = gl->GetUniformLocation(entry->program, "uNormalMatrix");
    entry->metallic_loc = gl->GetUniformLocation(entry->program, "uMetallic");
    entry->roughness_loc = gl->GetUniformLocation(entry->program, "uRoughness");
    entry->emissive_loc = gl->GetUniformLocation(entry->program, "uEmissive");
    entry->baked_light_mode_loc = gl->GetUniformLocation(entry->program, "uBakedLightMode");
    entry->pbr_texture_loc = gl->GetUniformLocation(entry->program, "uTexture");
    entry->pbr_has_texture_loc = gl->GetUniformLocation(entry->program, "uHasTexture");
    entry->pbr_lightmap_loc = gl->GetUniformLocation(entry->program, "uLightmap");
    entry->pbr_has_lightmap_loc = gl->GetUniformLocation(entry->program, "uHasLightmap");
    entry->pbr_shadow_map_loc = gl->GetUniformLocation(entry->program, "uShadowMap");
    entry->pbr_shadow_vp_loc = gl->GetUniformLocation(entry->program, "uShadowVP");
    entry->pbr_shadow_enabled_loc = gl->GetUniformLocation(entry->program, "uShadowEnabled");
    entry->pbr_shadow_bias_loc = gl->GetUniformLocation(entry->program, "uShadowBias");
    entry->pbr_csm_splits_loc = gl->GetUniformLocation(entry->program, "uCSMSplits");
    entry->pbr_csm_enabled_loc = gl->GetUniformLocation(entry->program, "uCSMEnabled");
    entry->pbr_view_matrix_loc = gl->GetUniformLocation(entry->program, "uViewMatrix");
    for (int c = 0; c < 4; c++)
    {
        char name[32];
        SDL_snprintf(name, sizeof(name), "uCSMVP[%d]", c);
        entry->pbr_csm_vp_loc[c] = gl->GetUniformLocation(entry->program, name);
    }
    for (int ps = 0; ps < SLAYER3D_MAX_POINT_SHADOWS; ps++)
    {
        char name[32];
        SDL_snprintf(name, sizeof(name), "uPointShadowMap[%d]", ps);
        entry->pbr_point_shadow_map_loc[ps] = gl->GetUniformLocation(entry->program, name);
        SDL_snprintf(name, sizeof(name), "uPointShadowLightPos[%d]", ps);
        entry->pbr_point_shadow_light_pos_loc[ps] = gl->GetUniformLocation(entry->program, name);
        SDL_snprintf(name, sizeof(name), "uPointShadowFar[%d]", ps);
        entry->pbr_point_shadow_far_loc[ps] = gl->GetUniformLocation(entry->program, name);
    }
    entry->pbr_point_shadow_count_loc = gl->GetUniformLocation(entry->program, "uPointShadowCount");
    entry->pbr_irradiance_map_loc = gl->GetUniformLocation(entry->program, "uIrradianceMap");
    entry->pbr_prefilter_map_loc = gl->GetUniformLocation(entry->program, "uPrefilterMap");
    entry->pbr_brdf_lut_loc = gl->GetUniformLocation(entry->program, "uBrdfLUT");
    entry->pbr_ibl_enabled_loc = gl->GetUniformLocation(entry->program, "uIBLEnabled");
    entry->pbr_max_reflection_lod_loc = gl->GetUniformLocation(entry->program, "uMaxReflectionLod");
    entry->overlay_effect_loc = gl->GetUniformLocation(entry->program, "uOverlayEffect");
    entry->overlay_effect_progress_loc = gl->GetUniformLocation(entry->program, "uOverlayEffectProgress");
    entry->overlay_effect_seed_loc = gl->GetUniformLocation(entry->program, "uOverlayEffectSeed");
    entry->overlay_effect_columns_loc = gl->GetUniformLocation(entry->program, "uOverlayEffectColumns");

    entry->next = ctx->custom_shader_cache;
    ctx->custom_shader_cache = entry;
    return entry;
}

/* ------------------------------------------------------------------ */
/* 4x4 matrix inverse (Cramer's rule, column-major float[16])          */
/* ------------------------------------------------------------------ */

static bool mat4_inverse(const float *m, float *out)
{
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f)
        return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * det;
    return true;
}

/* ------------------------------------------------------------------ */
/* CSM cascade computation                                             */
/* ------------------------------------------------------------------ */

static void compute_csm_matrices(slayer3d_gl_context *ctx, const slayer3d_render_context *rc)
{
    float near_p = rc->near_plane;
    float far_p = rc->far_plane;
    /* Use the camera's far plane so shadows cover the entire visible scene. */

    /* Practical split scheme: lambda=0.5 blend of log and uniform. */
    for (int i = 0; i < SLAYER3D_CSM_CASCADE_COUNT; i++)
    {
        float p = (float)(i + 1) / (float)SLAYER3D_CSM_CASCADE_COUNT;
        float log_split = near_p * SDL_powf(far_p / near_p, p);
        float uni_split = near_p + (far_p - near_p) * p;
        ctx->csm_split_depths[i] = 0.5f * log_split + 0.5f * uni_split;
    }

    /* Extract FOV and aspect from the projection matrix (column-major).
     * proj[5] = m[1][1] = f = 1/tan(fovy/2)
     * proj[0] = m[0][0] = f/aspect                                    */
    float f = rc->projection.m[5];
    float aspect = f / rc->projection.m[0];
    float fovy = 2.0f * SDL_atanf(1.0f / f);

    /* Light direction (normalized). */
    slayer3d_vec3 light_dir = slayer3d_vec3_normalize(rc->lights[0].direction);

    for (int i = 0; i < SLAYER3D_CSM_CASCADE_COUNT; i++)
    {
        float c_near = (i == 0) ? rc->near_plane : ctx->csm_split_depths[i - 1];
        float c_far = ctx->csm_split_depths[i];

        /* Build sub-frustum projection and invert (proj * view). */
        slayer3d_mat4 sub_proj;
        slayer3d_mat4_perspective(fovy, aspect, c_near, c_far, &sub_proj);
        slayer3d_mat4 pv = slayer3d_mat4_multiply(sub_proj, rc->view);

        float inv_pv[16];
        mat4_inverse(pv.m, inv_pv);

        /* 8 NDC corners -> world space. */
        float corners[8][3];
        int ci = 0;
        for (int z = 0; z < 2; z++)
        {
            for (int y = 0; y < 2; y++)
            {
                for (int x = 0; x < 2; x++)
                {
                    slayer3d_vec4 ndc = slayer3d_vec4_make((float)x * 2.0f - 1.0f, (float)y * 2.0f - 1.0f,
                                                           (float)z * 2.0f - 1.0f, 1.0f);
                    slayer3d_mat4 inv_m;
                    SDL_memcpy(inv_m.m, inv_pv, sizeof(inv_pv));
                    slayer3d_vec4 ws = slayer3d_mat4_transform_vec4(inv_m, ndc);
                    corners[ci][0] = ws.x / ws.w;
                    corners[ci][1] = ws.y / ws.w;
                    corners[ci][2] = ws.z / ws.w;
                    ci++;
                }
            }
        }

        /* Frustum center. */
        float cx = 0, cy = 0, cz = 0;
        for (int j = 0; j < 8; j++)
        {
            cx += corners[j][0];
            cy += corners[j][1];
            cz += corners[j][2];
        }
        cx /= 8.0f;
        cy /= 8.0f;
        cz /= 8.0f;

        /* Light view: lookAt(center + lightDir, center, up). */
        slayer3d_vec3 center = slayer3d_vec3_make(cx, cy, cz);
        slayer3d_vec3 eye = slayer3d_vec3_add(center, light_dir);
        slayer3d_vec3 up = slayer3d_vec3_make(0, 1, 0);
        /* If light is nearly vertical, use alternative up. */
        if (SDL_fabsf(slayer3d_vec3_dot(slayer3d_vec3_normalize(light_dir), up)) > 0.99f)
            up = slayer3d_vec3_make(1, 0, 0);

        slayer3d_mat4 light_view;
        slayer3d_mat4_look_at(eye, center, up, &light_view);

        /* Transform corners to light view space, find AABB. */
        float minX = 1e30f, maxX = -1e30f;
        float minY = 1e30f, maxY = -1e30f;
        float minZ = 1e30f, maxZ = -1e30f;
        for (int j = 0; j < 8; j++)
        {
            slayer3d_vec4 lv = slayer3d_mat4_transform_vec4(
                light_view, slayer3d_vec4_make(corners[j][0], corners[j][1], corners[j][2], 1.0f));
            if (lv.x < minX)
                minX = lv.x;
            if (lv.x > maxX)
                maxX = lv.x;
            if (lv.y < minY)
                minY = lv.y;
            if (lv.y > maxY)
                maxY = lv.y;
            if (lv.z < minZ)
                minZ = lv.z;
            if (lv.z > maxZ)
                maxZ = lv.z;
        }

        /* Extend Z range to catch shadow casters behind the frustum. */
        float z_mult = 10.0f;
        if (minZ < 0)
            minZ *= z_mult;
        else
            minZ /= z_mult;
        if (maxZ < 0)
            maxZ /= z_mult;
        else
            maxZ *= z_mult;

        slayer3d_mat4 light_proj;
        slayer3d_mat4_orthographic(minX, maxX, minY, maxY, minZ, maxZ, &light_proj);

        slayer3d_mat4 lp_lv = slayer3d_mat4_multiply(light_proj, light_view);
        SDL_memcpy(ctx->csm_light_vp[i], lp_lv.m, 16 * sizeof(float));
    }

    /* Use cascade 0 VP as the main shadow VP for the fragment shader (layer 0).
     * NOTE: ctx->shadow_light_vp is already set from slayer3d_enable_shadow in gl_clear.
     * We keep that original VP as fallback. CSM fragment path is now active. */
    ctx->csm_fragment_enabled = true;
}

static void compute_point_shadow_matrices(slayer3d_gl_context *ctx, const slayer3d_render_context *rc)
{
    ctx->point_shadow_count = 0;
    for (int i = 0; i < rc->light_count && ctx->point_shadow_count < SLAYER3D_MAX_POINT_SHADOWS; i++)
    {
        if (rc->lights[i].type != SLAYER3D_LIGHT_POINT)
            continue;
        int s = ctx->point_shadow_count;
        ctx->point_shadow_light_index[s] = i;

        const slayer3d_light *l = &rc->lights[i];
        float far = l->range > 0 ? l->range : 25.0f;
        ctx->point_shadow_far_plane[s] = far;

        slayer3d_mat4 proj;
        slayer3d_mat4_perspective(3.14159265f * 0.5f, 1.0f, 0.1f, far, &proj);

        slayer3d_vec3 pos = l->position;
        slayer3d_vec3 targets[6] = {{pos.x + 1, pos.y, pos.z}, {pos.x - 1, pos.y, pos.z}, {pos.x, pos.y + 1, pos.z},
                                    {pos.x, pos.y - 1, pos.z}, {pos.x, pos.y, pos.z + 1}, {pos.x, pos.y, pos.z - 1}};
        slayer3d_vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
        for (int f = 0; f < 6; f++)
        {
            slayer3d_mat4 view;
            slayer3d_mat4_look_at(pos, targets[f], ups[f], &view);
            slayer3d_mat4 vp = slayer3d_mat4_multiply(proj, view);
            SDL_memcpy(ctx->point_shadow_vp[s][f], vp.m, 16 * sizeof(float));
        }
        ctx->point_shadow_count++;
    }
}

/* ------------------------------------------------------------------ */
/* Draw list replay                                                    */
/* ------------------------------------------------------------------ */

static void replay_draw_list_geometry(slayer3d_gl_context *ctx);
static bool draw_entries_can_instance(const slayer3d_gl_context *ctx, const slayer3d_draw_entry *a,
                                      const slayer3d_draw_entry *b);
static bool upload_instance_model_attributes(slayer3d_gl_context *ctx, const slayer3d_draw_entry *entries, int count);
static void upload_joint_palette_uniform(slayer3d_gl_context *ctx, GLint location, const slayer3d_draw_entry *entry);
static void bind_skinning_state(slayer3d_gl_context *ctx, GLint use_skinning_loc, GLint use_palette_loc,
                                GLint palette_offset_loc, GLint joint_matrices_loc, const slayer3d_draw_entry *entry);
static bool draw_entry_matrix_equal(const float *a, const float *b, int count);

static void draw_entry_capture_viewport(slayer3d_draw_entry *entry, const slayer3d_render_context *context)
{
    if (entry == NULL || context == NULL)
        return;
    entry->viewport_enabled = context->viewport_enabled;
    entry->viewport_rect = context->viewport_rect;
    entry->scissor_enabled = context->scissor_enabled;
    entry->scissor_rect = context->scissor_rect;
}

static SDL_Rect logical_rect_to_world_rect(const slayer3d_gl_context *ctx, SDL_Rect logical)
{
    const int logical_w = ctx != NULL && ctx->logical_w > 0 ? ctx->logical_w : 1;
    const int logical_h = ctx != NULL && ctx->logical_h > 0 ? ctx->logical_h : 1;
    const int world_w = ctx != NULL && ctx->world_w > 0 ? ctx->world_w : logical_w;
    const int world_h = ctx != NULL && ctx->world_h > 0 ? ctx->world_h : logical_h;
    const float sx = (float)world_w / (float)logical_w;
    const float sy = (float)world_h / (float)logical_h;
    SDL_Rect out;
    out.x = (int)SDL_floorf((float)logical.x * sx);
    out.y = (int)SDL_floorf((float)(logical_h - logical.y - logical.h) * sy);
    out.w = SDL_max(0, (int)SDL_ceilf((float)logical.w * sx));
    out.h = SDL_max(0, (int)SDL_ceilf((float)logical.h * sy));
    if (out.x < 0)
    {
        out.w += out.x;
        out.x = 0;
    }
    if (out.y < 0)
    {
        out.h += out.y;
        out.y = 0;
    }
    if (out.x > world_w)
        out.x = world_w;
    if (out.y > world_h)
        out.y = world_h;
    if (out.w > world_w - out.x)
        out.w = world_w - out.x;
    if (out.h > world_h - out.y)
        out.h = world_h - out.y;
    if (out.w < 0)
        out.w = 0;
    if (out.h < 0)
        out.h = 0;
    return out;
}

static SDL_Rect rect_intersection(SDL_Rect a, SDL_Rect b)
{
    const int x0 = SDL_max(a.x, b.x);
    const int y0 = SDL_max(a.y, b.y);
    const int x1 = SDL_min(a.x + a.w, b.x + b.w);
    const int y1 = SDL_min(a.y + a.h, b.y + b.h);
    return (SDL_Rect){x0, y0, SDL_max(0, x1 - x0), SDL_max(0, y1 - y0)};
}

static void apply_draw_entry_viewport(slayer3d_gl_context *ctx, const slayer3d_draw_entry *entry)
{
    if (ctx == NULL || entry == NULL)
        return;
    slayer3d_gl_funcs *gl = &ctx->gl;
    SDL_Rect viewport =
        entry->viewport_enabled ? entry->viewport_rect : (SDL_Rect){0, 0, ctx->logical_w, ctx->logical_h};
    SDL_Rect world_viewport = logical_rect_to_world_rect(ctx, viewport);
    if (world_viewport.w <= 0 || world_viewport.h <= 0)
        world_viewport = (SDL_Rect){0, 0, ctx->world_w, ctx->world_h};
    gl->Viewport(world_viewport.x, world_viewport.y, world_viewport.w, world_viewport.h);

    SDL_Rect world_scissor = world_viewport;
    if (entry->scissor_enabled)
    {
        world_scissor = rect_intersection(world_scissor, logical_rect_to_world_rect(ctx, entry->scissor_rect));
    }
    gl->Enable(GL_SCISSOR_TEST);
    gl->Scissor(world_scissor.x, world_scissor.y, world_scissor.w, world_scissor.h);
}

static void replay_draw_list_shadow(slayer3d_gl_context *ctx)
{
    slayer3d_gl_funcs *gl = &ctx->gl;
    gl->UseProgram(ctx->shadow_program);
    /* lightVP uniform is set by the caller per cascade. */
    if (ctx->shadow_use_instancing_loc >= 0)
        gl->Uniform1i(ctx->shadow_use_instancing_loc, 0);

    for (int i = 0; i < ctx->draw_count; i++)
    {
        slayer3d_draw_entry *e = &ctx->draw_list[i];
        if (!e->lit)
            continue;

        gl->UniformMatrix4fv(ctx->shadow_model_loc, 1, GL_FALSE, e->model_matrix);
        bind_skinning_state(ctx, ctx->shadow_use_skinning_loc, ctx->shadow_use_skin_palette_loc,
                            ctx->shadow_joint_palette_offset_loc, ctx->shadow_joint_matrices_loc, e);

        gl->BindVertexArray(e->mesh_cache ? e->mesh_cache->shadow_vao : ctx->shadow_vao);
        if (e->mesh_cache == NULL)
        {
            gl->BindBuffer(GL_ARRAY_BUFFER, ctx->shadow_position_vbo);
            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)), e->positions,
                           GL_DYNAMIC_DRAW);
        }

        if (e->indices && e->index_count > 0)
        {
            if (e->mesh_cache == NULL)
            {
                gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->shadow_ebo);
                gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->index_count * sizeof(unsigned int)),
                               e->indices, GL_DYNAMIC_DRAW);
            }
            gl->DrawElements(GL_TRIANGLES, e->index_count, GL_UNSIGNED_INT, NULL);
        }
        else
        {
            gl->DrawArrays(GL_TRIANGLES, 0, e->vertex_count);
        }
    }
    gl->BindVertexArray(0);
}

static bool draw_entry_depth_prepass_eligible(const slayer3d_draw_entry *entry)
{
    return entry != NULL && entry->depth_prepass_eligible && entry->lit && entry->primitive_mode == GL_TRIANGLES &&
           entry->vertex_count > 0 && entry->mesh_cache != NULL && entry->tint[3] >= 0.999f;
}

static int draw_entry_depth_instance_batch_count(const slayer3d_gl_context *ctx, int start)
{
    if (ctx == NULL || start < 0 || start >= ctx->draw_count)
        return 0;
    const slayer3d_draw_entry *first = &ctx->draw_list[start];
    if (!draw_entry_depth_prepass_eligible(first))
        return 0;

    int count = 1;
    while (start + count < ctx->draw_count && draw_entry_depth_prepass_eligible(&ctx->draw_list[start + count]) &&
           draw_entries_can_instance(ctx, first, &ctx->draw_list[start + count]))
    {
        ++count;
    }
    return count;
}

static Uint64 draw_entry_triangle_count(const slayer3d_draw_entry *entry)
{
    if (entry == NULL)
        return 0u;
    if (entry->indices != NULL && entry->index_count > 0)
        return (Uint64)(entry->index_count / 3);
    return (Uint64)(entry->vertex_count / 3);
}

static void upload_joint_palette_uniform(slayer3d_gl_context *ctx, GLint location, const slayer3d_draw_entry *entry)
{
    if (ctx == NULL || entry == NULL || location < 0 || !entry->gpu_skinned || entry->joint_count <= 0 ||
        entry->joint_matrices == NULL || entry->use_joint_palette_buffer)
        return;

    ctx->gl.UniformMatrix4fv(location, entry->joint_count, GL_FALSE, entry->joint_matrices);
    if (ctx->current_ctx != NULL)
    {
        ctx->current_ctx->stats.gpu_skinning_palette_uploads += 1u;
        ctx->current_ctx->stats.gpu_skinning_palette_matrices_uploaded += (Uint64)entry->joint_count;
    }
}

static void bind_skinning_state(slayer3d_gl_context *ctx, GLint use_skinning_loc, GLint use_palette_loc,
                                GLint palette_offset_loc, GLint joint_matrices_loc, const slayer3d_draw_entry *entry)
{
    const bool use_skinning = entry != NULL && entry->gpu_skinned;
    const bool use_palette = use_skinning && entry->use_joint_palette_buffer;
    slayer3d_gl_funcs *gl = &ctx->gl;

    if (use_skinning_loc >= 0)
        gl->Uniform1i(use_skinning_loc, use_skinning ? 1 : 0);
    if (use_palette_loc >= 0)
        gl->Uniform1i(use_palette_loc, use_palette ? 1 : 0);
    if (palette_offset_loc >= 0)
        gl->Uniform1i(palette_offset_loc, use_palette ? entry->joint_palette_offset : 0);
    upload_joint_palette_uniform(ctx, joint_matrices_loc, entry);

    if (use_palette && ctx->current_ctx != NULL)
        ctx->current_ctx->stats.gpu_skinning_palette_buffer_draws += 1u;
}

static int find_matching_skin_palette_offset(const slayer3d_gl_context *ctx, const slayer3d_draw_entry *entry)
{
    if (ctx == NULL || entry == NULL || entry->joint_matrices == NULL || entry->joint_count <= 0)
        return -1;

    for (int offset = 0; offset + entry->joint_count <= ctx->skin_palette_matrix_count; ++offset)
    {
        if (draw_entry_matrix_equal(&ctx->skin_palette_matrices[offset * 16], entry->joint_matrices,
                                    entry->joint_count * 16))
            return offset;
    }
    return -1;
}

static void prepare_skin_palette_buffer(slayer3d_gl_context *ctx)
{
    if (ctx == NULL || ctx->skin_palette_ubo == 0)
        return;

    ctx->skin_palette_matrix_count = 0;
    for (int i = 0; i < ctx->draw_count; ++i)
    {
        slayer3d_draw_entry *entry = &ctx->draw_list[i];
        entry->use_joint_palette_buffer = false;
        entry->joint_palette_offset = 0;
        if (!entry->gpu_skinned || entry->joint_matrices == NULL || entry->joint_count <= 0)
            continue;

        const int existing_offset = find_matching_skin_palette_offset(ctx, entry);
        if (existing_offset >= 0)
        {
            entry->use_joint_palette_buffer = true;
            entry->joint_palette_offset = existing_offset;
            continue;
        }

        if (ctx->skin_palette_matrix_count + entry->joint_count > SLAYER3D_GPU_SKINNING_PALETTE_MATRICES)
            continue;

        entry->joint_palette_offset = ctx->skin_palette_matrix_count;
        entry->use_joint_palette_buffer = true;
        SDL_memcpy(&ctx->skin_palette_matrices[ctx->skin_palette_matrix_count * 16], entry->joint_matrices,
                   (size_t)entry->joint_count * 16u * sizeof(float));
        ctx->skin_palette_matrix_count += entry->joint_count;
    }

    if (ctx->skin_palette_matrix_count <= 0)
        return;

    slayer3d_gl_funcs *gl = &ctx->gl;
    gl->BindBuffer(GL_UNIFORM_BUFFER, ctx->skin_palette_ubo);
    gl->BufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)(SLAYER3D_GPU_SKINNING_PALETTE_MATRICES * 16u * sizeof(float)),
                   ctx->skin_palette_matrices, GL_DYNAMIC_DRAW);
    gl->BindBufferBase(GL_UNIFORM_BUFFER, SLAYER3D_SKIN_PALETTE_BINDING, ctx->skin_palette_ubo);

    if (ctx->current_ctx != NULL)
    {
        ctx->current_ctx->stats.gpu_skinning_palette_buffer_uploads += 1u;
        ctx->current_ctx->stats.gpu_skinning_palette_buffer_matrices_uploaded += (Uint64)ctx->skin_palette_matrix_count;
    }
}

static bool gl_sample_queries_enabled(const slayer3d_gl_context *ctx)
{
    return ctx != NULL && ctx->sample_queries_supported && ctx->current_ctx != NULL &&
           ctx->current_ctx->render_sample_queries_enabled;
}

static Uint64 gl_query_samples_passed(slayer3d_gl_context *ctx, GLuint query)
{
    if (ctx == NULL || query == 0u || ctx->gl.GetQueryObjectuiv == NULL)
        return 0u;
    GLuint samples = 0u;
    ctx->gl.GetQueryObjectuiv(query, GL_QUERY_RESULT, &samples);
    return (Uint64)samples;
}

static void replay_draw_list_geometry_measured(slayer3d_gl_context *ctx)
{
    if (ctx == NULL)
        return;

    slayer3d_gl_funcs *gl = &ctx->gl;
    const bool sample_query = gl_sample_queries_enabled(ctx) && ctx->geometry_query != 0u;
    if (sample_query)
        gl->BeginQuery(GL_SAMPLES_PASSED, ctx->geometry_query);

    replay_draw_list_geometry(ctx);

    if (sample_query)
    {
        gl->EndQuery(GL_SAMPLES_PASSED);
        ctx->current_ctx->stats.geometry_samples_passed += gl_query_samples_passed(ctx, ctx->geometry_query);
    }
}

static void replay_draw_list_depth_prepass(slayer3d_gl_context *ctx)
{
    if (ctx == NULL || ctx->current_ctx == NULL || !ctx->current_ctx->depth_prepass_enabled || !ctx->shadow_program)
        return;

    slayer3d_gl_funcs *gl = &ctx->gl;
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
    gl->Enable(GL_DEPTH_TEST);
    gl->DepthMask(GL_TRUE);
    gl->DepthFunc(GL_LESS);
    gl->ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    gl->UseProgram(ctx->shadow_program);
    const bool sample_query = gl_sample_queries_enabled(ctx) && ctx->depth_prepass_query != 0u;
    if (sample_query)
        gl->BeginQuery(GL_SAMPLES_PASSED, ctx->depth_prepass_query);

    for (int i = 0; i < ctx->draw_count; i++)
    {
        slayer3d_draw_entry *e = &ctx->draw_list[i];
        if (!draw_entry_depth_prepass_eligible(e))
            continue;
        apply_draw_entry_viewport(ctx, e);

        const int instance_batch_count = draw_entry_depth_instance_batch_count(ctx, i);
        if (instance_batch_count > 1)
        {
            gl->UniformMatrix4fv(ctx->shadow_light_vp_loc, 1, GL_FALSE, e->view_projection);
            gl->UniformMatrix4fv(ctx->shadow_model_loc, 1, GL_FALSE, e->model_matrix);
            if (ctx->shadow_use_instancing_loc >= 0)
                gl->Uniform1i(ctx->shadow_use_instancing_loc, 1);
            bind_skinning_state(ctx, ctx->shadow_use_skinning_loc, ctx->shadow_use_skin_palette_loc,
                                ctx->shadow_joint_palette_offset_loc, ctx->shadow_joint_matrices_loc, e);
            gl->BindVertexArray(e->mesh_cache->shadow_vao);
            if (upload_instance_model_attributes(ctx, e, instance_batch_count))
            {
                if (e->indices && e->index_count > 0)
                    gl->DrawElementsInstanced(GL_TRIANGLES, e->index_count, GL_UNSIGNED_INT, NULL,
                                              instance_batch_count);
                else
                    gl->DrawArraysInstanced(GL_TRIANGLES, 0, e->vertex_count, instance_batch_count);

                ctx->current_ctx->stats.depth_prepass_draws += 1u;
                ctx->current_ctx->stats.depth_prepass_triangles +=
                    draw_entry_triangle_count(e) * (Uint64)instance_batch_count;
                i += instance_batch_count - 1;
                continue;
            }
        }

        if (ctx->shadow_use_instancing_loc >= 0)
            gl->Uniform1i(ctx->shadow_use_instancing_loc, 0);
        bind_skinning_state(ctx, ctx->shadow_use_skinning_loc, ctx->shadow_use_skin_palette_loc,
                            ctx->shadow_joint_palette_offset_loc, ctx->shadow_joint_matrices_loc, e);
        gl->UniformMatrix4fv(ctx->shadow_light_vp_loc, 1, GL_FALSE, e->view_projection);
        gl->UniformMatrix4fv(ctx->shadow_model_loc, 1, GL_FALSE, e->model_matrix);

        gl->BindVertexArray(e->mesh_cache ? e->mesh_cache->shadow_vao : ctx->shadow_vao);
        if (e->mesh_cache == NULL)
        {
            gl->BindBuffer(GL_ARRAY_BUFFER, ctx->shadow_position_vbo);
            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)), e->positions,
                           GL_DYNAMIC_DRAW);
        }

        if (e->indices && e->index_count > 0)
        {
            if (e->mesh_cache == NULL)
            {
                gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->shadow_ebo);
                gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->index_count * sizeof(unsigned int)),
                               e->indices, GL_DYNAMIC_DRAW);
            }
            gl->DrawElements(GL_TRIANGLES, e->index_count, GL_UNSIGNED_INT, NULL);
        }
        else
        {
            gl->DrawArrays(GL_TRIANGLES, 0, e->vertex_count);
        }

        ctx->current_ctx->stats.depth_prepass_draws += 1u;
        ctx->current_ctx->stats.depth_prepass_triangles += draw_entry_triangle_count(e);
    }

    if (sample_query)
    {
        gl->EndQuery(GL_SAMPLES_PASSED);
        ctx->current_ctx->stats.depth_prepass_samples_passed += gl_query_samples_passed(ctx, ctx->depth_prepass_query);
    }

    gl->BindVertexArray(0);
    gl->Disable(GL_SCISSOR_TEST);
    gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
    gl->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl->DepthFunc(GL_LEQUAL);
}

static void camera_position_from_view_matrix(const slayer3d_mat4 *view, float out_position[3])
{
    if (view == NULL || out_position == NULL)
        return;
    out_position[0] = -(view->m[0] * view->m[12] + view->m[1] * view->m[13] + view->m[2] * view->m[14]);
    out_position[1] = -(view->m[4] * view->m[12] + view->m[5] * view->m[13] + view->m[6] * view->m[14]);
    out_position[2] = -(view->m[8] * view->m[12] + view->m[9] * view->m[13] + view->m[10] * view->m[14]);
}

static slayer3d_vec3 transform_point3(const float *m, slayer3d_vec3 p)
{
    return slayer3d_vec3_make(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                              m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                              m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]);
}

static void draw_entry_compute_bounds(slayer3d_draw_entry *entry)
{
    if (entry == NULL || entry->positions == NULL || entry->vertex_count <= 0)
        return;

    slayer3d_vec3 minv = transform_point3(
        entry->model_matrix, slayer3d_vec3_make(entry->positions[0], entry->positions[1], entry->positions[2]));
    slayer3d_vec3 maxv = minv;
    for (int i = 1; i < entry->vertex_count; ++i)
    {
        const size_t offset = (size_t)i * 3U;
        const slayer3d_vec3 point = transform_point3(
            entry->model_matrix,
            slayer3d_vec3_make(entry->positions[offset], entry->positions[offset + 1U], entry->positions[offset + 2U]));
        minv.x = SDL_min(minv.x, point.x);
        minv.y = SDL_min(minv.y, point.y);
        minv.z = SDL_min(minv.z, point.z);
        maxv.x = SDL_max(maxv.x, point.x);
        maxv.y = SDL_max(maxv.y, point.y);
        maxv.z = SDL_max(maxv.z, point.z);
    }

    entry->bounds_center = slayer3d_vec3_scale(slayer3d_vec3_add(minv, maxv), 0.5f);
    entry->bounds_radius = 0.0f;
    for (int i = 0; i < entry->vertex_count; ++i)
    {
        const size_t offset = (size_t)i * 3U;
        const slayer3d_vec3 point = transform_point3(
            entry->model_matrix,
            slayer3d_vec3_make(entry->positions[offset], entry->positions[offset + 1U], entry->positions[offset + 2U]));
        entry->bounds_radius =
            SDL_max(entry->bounds_radius, slayer3d_vec3_length(slayer3d_vec3_sub(point, entry->bounds_center)));
    }
    entry->bounds_valid = true;
}

static float light_selection_score(const slayer3d_light *light, const slayer3d_draw_entry *entry, bool *out_relevant)
{
    if (out_relevant != NULL)
        *out_relevant = false;
    if (light == NULL)
        return -1.0f;
    if (light->type == SLAYER3D_LIGHT_DIRECTIONAL)
    {
        if (out_relevant != NULL)
            *out_relevant = true;
        return 1000000.0f + SDL_max(light->intensity, 0.0f);
    }

    const slayer3d_vec3 center =
        (entry != NULL && entry->bounds_valid) ? entry->bounds_center : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const float radius = (entry != NULL && entry->bounds_valid) ? entry->bounds_radius : 0.0f;
    const float distance = slayer3d_vec3_length(slayer3d_vec3_sub(light->position, center));
    const float range = light->range > 0.0f ? light->range : 0.0f;
    if (range > 0.0f && distance > range + radius)
        return -1.0f;

    const float distance_weight =
        range > 0.0f ? SDL_clamp(1.0f - distance / (range + radius + 0.0001f), 0.0f, 1.0f) : 1.0f / (1.0f + distance);
    if (out_relevant != NULL)
        *out_relevant = true;
    return SDL_max(light->intensity, 0.0f) * SDL_max(distance_weight, 0.0001f);
}

static int select_lights_for_draw_entry(slayer3d_render_context *rc, const slayer3d_draw_entry *entry,
                                        const slayer3d_light **out_lights, int out_capacity)
{
    if (rc == NULL || out_lights == NULL || out_capacity <= 0)
        return 0;

    const int capacity = SDL_clamp(rc->per_object_light_limit, 0, SDL_min(out_capacity, SLAYER3D_MAX_SHADER_LIGHTS));
    if (capacity <= 0)
        return 0;

    rc->stats.light_selection_draws += 1u;
    rc->stats.light_candidates += (Uint64)SDL_max(rc->light_count, 0);

    int selected_count = 0;
    float scores[SLAYER3D_MAX_SHADER_LIGHTS] = {0.0f};
    for (int i = 0; i < rc->light_count; ++i)
    {
        bool relevant = true;
        const float score = rc->per_object_light_selection_enabled
                                ? light_selection_score(&rc->lights[i], entry, &relevant)
                                : (float)(SLAYER3D_MAX_LIGHTS - i);
        if (!relevant || score < 0.0f)
            continue;

        int insert = selected_count;
        while (insert > 0 && score > scores[insert - 1])
            --insert;
        if (insert >= capacity)
            continue;

        if (selected_count < capacity)
            ++selected_count;
        for (int move = selected_count - 1; move > insert; --move)
        {
            out_lights[move] = out_lights[move - 1];
            scores[move] = scores[move - 1];
        }
        out_lights[insert] = &rc->lights[i];
        scores[insert] = score;
    }

    rc->stats.lights_selected += (Uint64)selected_count;
    return selected_count;
}

static void flush_scene_ubo_for_draw_entry(slayer3d_gl_context *ctx, const slayer3d_draw_entry *entry)
{
    slayer3d_render_context *rc = ctx->current_ctx;
    if (rc == NULL || entry == NULL)
        return;

    slayer3d_scene_ubo_data ubo;
    SDL_memset(&ubo, 0, sizeof(ubo));
    SDL_memcpy(ubo.view_projection, entry->view_projection, 16 * sizeof(float));
    SDL_memcpy(ubo.camera_pos, entry->camera_pos, 3 * sizeof(float));

    ubo.ambient[0] = rc->ambient[0];
    ubo.ambient[1] = rc->ambient[1];
    ubo.ambient[2] = rc->ambient[2];

    const slayer3d_light *selected_lights[SLAYER3D_MAX_SHADER_LIGHTS] = {0};
    int lc = select_lights_for_draw_entry(rc, entry, selected_lights, SLAYER3D_MAX_SHADER_LIGHTS);
    ubo.light_count = lc;
    for (int i = 0; i < lc; i++)
    {
        const slayer3d_light *l = selected_lights[i];
        ubo.lights[i].type = (int)l->type;
        ubo.lights[i].position[0] = l->position.x;
        ubo.lights[i].position[1] = l->position.y;
        ubo.lights[i].position[2] = l->position.z;
        ubo.lights[i].direction[0] = l->direction.x;
        ubo.lights[i].direction[1] = l->direction.y;
        ubo.lights[i].direction[2] = l->direction.z;
        ubo.lights[i].color[0] = l->color[0];
        ubo.lights[i].color[1] = l->color[1];
        ubo.lights[i].color[2] = l->color[2];
        ubo.lights[i].intensity = l->intensity;
        ubo.lights[i].range = l->range;
        ubo.lights[i].inner_cutoff = l->inner_cutoff;
        ubo.lights[i].outer_cutoff = l->outer_cutoff;
    }

    ubo.fog_mode = (int)rc->fog.mode;
    ubo.fog_start = rc->fog.start;
    ubo.fog_end = rc->fog.end;
    ubo.fog_density = rc->fog.density;
    ubo.fog_color[0] = rc->fog.color[0];
    ubo.fog_color[1] = rc->fog.color[1];
    ubo.fog_color[2] = rc->fog.color[2];
    ubo.tonemap_mode = (int)rc->tonemap_mode;

    slayer3d_gl_funcs *gl = &ctx->gl;
    gl->BindBuffer(GL_UNIFORM_BUFFER, ctx->scene_ubo);
    gl->BufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(ubo), &ubo, GL_DYNAMIC_DRAW);
    gl->BindBufferBase(GL_UNIFORM_BUFFER, 0, ctx->scene_ubo);
}

static void bind_builtin_pbr_entry(slayer3d_gl_context *ctx, const slayer3d_draw_entry *e, GLuint tex,
                                   bool use_instancing)
{
    slayer3d_gl_funcs *gl = &ctx->gl;
    gl->UseProgram(ctx->pbr_program);
    gl->UniformMatrix4fv(ctx->pbr_model_loc, 1, GL_FALSE, e->model_matrix);
    gl->UniformMatrix3fv(ctx->pbr_normal_matrix_loc, 1, GL_FALSE, e->normal_matrix);
    if (ctx->pbr_use_instancing_loc >= 0)
        gl->Uniform1i(ctx->pbr_use_instancing_loc, use_instancing ? 1 : 0);
    bind_skinning_state(ctx, ctx->pbr_use_skinning_loc, ctx->pbr_use_skin_palette_loc,
                        ctx->pbr_joint_palette_offset_loc, ctx->pbr_joint_matrices_loc, e);
    gl->Uniform4f(ctx->pbr_tint_loc, e->tint[0], e->tint[1], e->tint[2], e->tint[3]);
    gl->Uniform1f(ctx->pbr_metallic_loc, e->metallic);
    gl->Uniform1f(ctx->pbr_roughness_loc, e->roughness);
    gl->Uniform3f(ctx->pbr_emissive_loc, e->emissive[0], e->emissive[1], e->emissive[2]);
    gl->Uniform1i(ctx->pbr_baked_light_mode_loc, e->baked_light_mode ? 1 : 0);

    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, tex);
    gl->Uniform1i(ctx->pbr_texture_loc, 0);
    gl->Uniform1i(ctx->pbr_has_texture_loc, e->texture ? 1 : 0);
    gl->ActiveTexture(GL_TEXTURE0 + 7);
    gl->BindTexture(GL_TEXTURE_2D,
                    e->has_lightmap ? slayer3d_gl_resolve_texture(ctx, e->lightmap_texture) : ctx->black_texture);
    gl->Uniform1i(ctx->pbr_lightmap_loc, 7);
    gl->Uniform1i(ctx->pbr_has_lightmap_loc, e->has_lightmap ? 1 : 0);
    gl->ActiveTexture(GL_TEXTURE0);

    /* Shadow uniforms — always bind the texture array to prevent undefined
     * sampler behavior on some drivers. */
    gl->ActiveTexture(GL_TEXTURE0 + 1);
    gl->BindTexture(GL_TEXTURE_2D_ARRAY, ctx->shadow_depth_tex);
    gl->Uniform1i(ctx->pbr_shadow_map_loc, 1);
    if (ctx->shadow_depth_tex && ctx->shadow_bias > 0.0f)
    {
        gl->UniformMatrix4fv(ctx->pbr_shadow_vp_loc, 1, GL_FALSE, ctx->shadow_light_vp);
        gl->Uniform1i(ctx->pbr_shadow_enabled_loc, 0);
        gl->Uniform1f(ctx->pbr_shadow_bias_loc, ctx->shadow_bias);
        if (ctx->csm_fragment_enabled)
        {
            gl->UniformMatrix4fv(ctx->pbr_csm_vp_loc[0], 1, GL_FALSE, ctx->shadow_light_vp);
            for (int c = 1; c < 4; c++)
                gl->UniformMatrix4fv(ctx->pbr_csm_vp_loc[c], 1, GL_FALSE, ctx->csm_light_vp[c]);
            gl->Uniform1fv(ctx->pbr_csm_splits_loc, 4, ctx->csm_split_depths);
            gl->UniformMatrix4fv(ctx->pbr_view_matrix_loc, 1, GL_FALSE, e->view_matrix);
            gl->Uniform1i(ctx->pbr_csm_enabled_loc, 1);
        }
        else
        {
            gl->Uniform1i(ctx->pbr_csm_enabled_loc, 0);
        }
    }
    else
    {
        gl->Uniform1i(ctx->pbr_shadow_enabled_loc, 0);
        gl->Uniform1i(ctx->pbr_csm_enabled_loc, 0);
    }
    gl->ActiveTexture(GL_TEXTURE0);

    for (int ps = 0; ps < SLAYER3D_MAX_POINT_SHADOWS; ps++)
    {
        gl->ActiveTexture(GL_TEXTURE0 + 2 + (GLenum)ps);
        gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->point_shadow_cubemap[ps]);
        gl->Uniform1i(ctx->pbr_point_shadow_map_loc[ps], 2 + ps);
        if (ps < ctx->point_shadow_count)
        {
            const slayer3d_light *pl = &ctx->current_ctx->lights[ctx->point_shadow_light_index[ps]];
            gl->Uniform3f(ctx->pbr_point_shadow_light_pos_loc[ps], pl->position.x, pl->position.y, pl->position.z);
            gl->Uniform1f(ctx->pbr_point_shadow_far_loc[ps], ctx->point_shadow_far_plane[ps]);
        }
    }
    gl->Uniform1i(ctx->pbr_point_shadow_count_loc, ctx->point_shadow_count);
    gl->ActiveTexture(GL_TEXTURE0);

    if (ctx->ibl_ready)
    {
        gl->ActiveTexture(GL_TEXTURE0 + 4);
        gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->ibl_irradiance_map);
        gl->Uniform1i(ctx->pbr_irradiance_map_loc, 4);
        gl->ActiveTexture(GL_TEXTURE0 + 5);
        gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->ibl_prefilter_map);
        gl->Uniform1i(ctx->pbr_prefilter_map_loc, 5);
        gl->ActiveTexture(GL_TEXTURE0 + 6);
        gl->BindTexture(GL_TEXTURE_2D, ctx->ibl_brdf_lut);
        gl->Uniform1i(ctx->pbr_brdf_lut_loc, 6);
        gl->Uniform1i(ctx->pbr_ibl_enabled_loc, 1);
        gl->Uniform1f(ctx->pbr_max_reflection_lod_loc, 4.0f);
        gl->ActiveTexture(GL_TEXTURE0);
    }
    else
    {
        gl->ActiveTexture(GL_TEXTURE0 + 4);
        gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->black_cubemap);
        gl->Uniform1i(ctx->pbr_irradiance_map_loc, 4);
        gl->ActiveTexture(GL_TEXTURE0 + 5);
        gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->black_cubemap);
        gl->Uniform1i(ctx->pbr_prefilter_map_loc, 5);
        gl->ActiveTexture(GL_TEXTURE0 + 6);
        gl->BindTexture(GL_TEXTURE_2D, ctx->black_texture);
        gl->Uniform1i(ctx->pbr_brdf_lut_loc, 6);
        gl->Uniform1i(ctx->pbr_ibl_enabled_loc, 0);
        gl->ActiveTexture(GL_TEXTURE0);
    }
}

static bool draw_entry_float4_equal(const float *a, const float *b)
{
    for (int i = 0; i < 4; ++i)
    {
        if (SDL_fabsf(a[i] - b[i]) > 0.000001f)
            return false;
    }
    return true;
}

static bool draw_entry_float3_equal(const float *a, const float *b)
{
    for (int i = 0; i < 3; ++i)
    {
        if (SDL_fabsf(a[i] - b[i]) > 0.000001f)
            return false;
    }
    return true;
}

static bool draw_entry_matrix_equal(const float *a, const float *b, int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (SDL_fabsf(a[i] - b[i]) > 0.000001f)
            return false;
    }
    return true;
}

static bool draw_entry_skinning_equal(const slayer3d_draw_entry *a, const slayer3d_draw_entry *b)
{
    if (a->gpu_skinned != b->gpu_skinned)
        return false;
    if (!a->gpu_skinned)
        return true;
    return a->joint_count == b->joint_count && a->joint_count > 0 && a->joint_matrices != NULL &&
           b->joint_matrices != NULL &&
           draw_entry_matrix_equal(a->joint_matrices, b->joint_matrices, a->joint_count * 16);
}

static bool draw_entry_instancing_light_safe(const slayer3d_gl_context *ctx)
{
    const slayer3d_render_context *rc = ctx != NULL ? ctx->current_ctx : NULL;
    if (rc == NULL)
        return false;
    const int limit = SDL_clamp(rc->per_object_light_limit, 0, SLAYER3D_MAX_SHADER_LIGHTS);
    return !rc->per_object_light_selection_enabled || rc->light_count <= limit;
}

static bool draw_entries_can_instance(const slayer3d_gl_context *ctx, const slayer3d_draw_entry *a,
                                      const slayer3d_draw_entry *b)
{
    if (ctx == NULL || a == NULL || b == NULL || ctx->gl.DrawElementsInstanced == NULL ||
        ctx->gl.DrawArraysInstanced == NULL || ctx->gl.VertexAttribDivisor == NULL)
        return false;
    if (!draw_entry_instancing_light_safe(ctx))
        return false;
    if (!a->lit || !b->lit || a->mesh_cache == NULL || a->mesh_cache != b->mesh_cache ||
        a->primitive_mode != GL_TRIANGLES || b->primitive_mode != GL_TRIANGLES || a->tint[3] < 0.999f ||
        b->tint[3] < 0.999f || a->shader_vertex_source != NULL || a->shader_fragment_source != NULL ||
        b->shader_vertex_source != NULL || b->shader_fragment_source != NULL)
        return false;
    return a->texture == b->texture && a->lightmap_texture == b->lightmap_texture &&
           a->has_lightmap == b->has_lightmap && a->baked_light_mode == b->baked_light_mode &&
           a->vertex_count == b->vertex_count && a->index_count == b->index_count && a->positions == b->positions &&
           a->normals == b->normals && a->uvs == b->uvs && a->lightmap_uvs == b->lightmap_uvs &&
           a->colors == b->colors && a->indices == b->indices && draw_entry_float4_equal(a->tint, b->tint) &&
           SDL_fabsf(a->metallic - b->metallic) <= 0.000001f && SDL_fabsf(a->roughness - b->roughness) <= 0.000001f &&
           draw_entry_float3_equal(a->emissive, b->emissive) &&
           draw_entry_matrix_equal(a->view_projection, b->view_projection, 16) &&
           draw_entry_float3_equal(a->camera_pos, b->camera_pos) && a->viewport_enabled == b->viewport_enabled &&
           a->scissor_enabled == b->scissor_enabled && a->disable_culling == b->disable_culling &&
           (!a->viewport_enabled || SDL_memcmp(&a->viewport_rect, &b->viewport_rect, sizeof(a->viewport_rect)) == 0) &&
           (!a->scissor_enabled || SDL_memcmp(&a->scissor_rect, &b->scissor_rect, sizeof(a->scissor_rect)) == 0) &&
           draw_entry_skinning_equal(a, b);
}

static int draw_entry_instance_batch_count(const slayer3d_gl_context *ctx, int start)
{
    if (ctx == NULL || start < 0 || start >= ctx->draw_count)
        return 0;
    const slayer3d_draw_entry *first = &ctx->draw_list[start];
    int count = 1;
    while (start + count < ctx->draw_count && draw_entries_can_instance(ctx, first, &ctx->draw_list[start + count]))
        ++count;
    return count;
}

static bool upload_instance_model_attributes(slayer3d_gl_context *ctx, const slayer3d_draw_entry *entries, int count)
{
    if (ctx == NULL || entries == NULL || count <= 0)
        return false;
    float *model_matrices = (float *)SDL_malloc((size_t)count * 16u * sizeof(float));
    if (model_matrices == NULL)
    {
        SDL_OutOfMemory();
        return false;
    }
    for (int i = 0; i < count; ++i)
        SDL_memcpy(&model_matrices[i * 16], entries[i].model_matrix, 16u * sizeof(float));

    slayer3d_gl_funcs *gl = &ctx->gl;
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->instance_model_vbo);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * 16u * sizeof(float)), model_matrices, GL_DYNAMIC_DRAW);
    for (GLuint column = 0; column < 4; ++column)
    {
        const GLuint attrib = 5u + column;
        gl->EnableVertexAttribArray(attrib);
        gl->VertexAttribPointer(attrib, 4, GL_FLOAT, GL_FALSE, 16 * (GLsizei)sizeof(float),
                                (const void *)(size_t)(column * 4u * sizeof(float)));
        gl->VertexAttribDivisor(attrib, 1u);
    }

    SDL_free(model_matrices);
    return true;
}

static bool upload_instance_attributes(slayer3d_gl_context *ctx, const slayer3d_draw_entry *entries, int count)
{
    if (!upload_instance_model_attributes(ctx, entries, count))
        return false;
    float *normal_matrices = (float *)SDL_malloc((size_t)count * 9u * sizeof(float));
    if (normal_matrices == NULL)
    {
        SDL_OutOfMemory();
        return false;
    }
    for (int i = 0; i < count; ++i)
        SDL_memcpy(&normal_matrices[i * 9], entries[i].normal_matrix, 9u * sizeof(float));

    slayer3d_gl_funcs *gl = &ctx->gl;
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->instance_normal_vbo);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * 9u * sizeof(float)), normal_matrices, GL_DYNAMIC_DRAW);
    for (GLuint column = 0; column < 3; ++column)
    {
        const GLuint attrib = 9u + column;
        gl->EnableVertexAttribArray(attrib);
        gl->VertexAttribPointer(attrib, 3, GL_FLOAT, GL_FALSE, 9 * (GLsizei)sizeof(float),
                                (const void *)(size_t)(column * 3u * sizeof(float)));
        gl->VertexAttribDivisor(attrib, 1u);
    }

    SDL_free(normal_matrices);
    return true;
}

static void apply_draw_entry_cull_state(slayer3d_gl_context *ctx, const slayer3d_draw_entry *entry)
{
    if (ctx == NULL || entry == NULL)
        return;
    slayer3d_gl_funcs *gl = &ctx->gl;
    if (entry->disable_culling)
    {
        gl->Disable(GL_CULL_FACE);
    }
    else if (ctx->current_ctx != NULL && ctx->current_ctx->backface_culling_enabled)
    {
        gl->Enable(GL_CULL_FACE);
    }
    else
    {
        gl->Disable(GL_CULL_FACE);
    }
}

static bool draw_static_mesh_instance_batch(slayer3d_gl_context *ctx, int start, int count)
{
    if (ctx == NULL || count <= 1)
        return false;
    slayer3d_gl_funcs *gl = &ctx->gl;
    slayer3d_draw_entry *e = &ctx->draw_list[start];
    GLuint tex = slayer3d_gl_resolve_texture(ctx, e->texture);

    apply_draw_entry_viewport(ctx, e);
    flush_scene_ubo_for_draw_entry(ctx, e);
    bind_builtin_pbr_entry(ctx, e, tex, true);
    gl->BindVertexArray(e->mesh_cache->vao);
    if (!upload_instance_attributes(ctx, e, count))
        return false;

    if (e->indices && e->index_count > 0)
        gl->DrawElementsInstanced(e->primitive_mode, e->index_count, GL_UNSIGNED_INT, NULL, count);
    else
        gl->DrawArraysInstanced(e->primitive_mode, 0, e->vertex_count, count);

    if (ctx->current_ctx != NULL)
    {
        ctx->current_ctx->stats.geometry_draw_calls += 1u;
        ctx->current_ctx->stats.static_mesh_instanced_draw_calls += 1u;
        ctx->current_ctx->stats.static_mesh_instances_batched += (Uint64)count;
        ctx->current_ctx->stats.static_mesh_draw_calls_saved += (Uint64)(count - 1);
    }
    return true;
}

static void replay_draw_list_geometry(slayer3d_gl_context *ctx)
{
    slayer3d_gl_funcs *gl = &ctx->gl;

    for (int i = 0; i < ctx->draw_count; i++)
    {
        slayer3d_draw_entry *e = &ctx->draw_list[i];
        GLuint tex = slayer3d_gl_resolve_texture(ctx, e->texture);
        apply_draw_entry_viewport(ctx, e);
        apply_draw_entry_cull_state(ctx, e);

        if (e->lit)
        {
            const int instance_batch_count = draw_entry_instance_batch_count(ctx, i);
            if (instance_batch_count > 1 && draw_static_mesh_instance_batch(ctx, i, instance_batch_count))
            {
                i += instance_batch_count - 1;
                continue;
            }

            flush_scene_ubo_for_draw_entry(ctx, e);
            slayer3d_custom_shader_cache_entry *custom =
                (e->shader_fragment_source != NULL && e->shader_fragment_source[0] != '\0')
                    ? custom_shader_lookup_or_create(ctx, true, e->shader_vertex_source, e->shader_fragment_source)
                    : NULL;

            if (custom != NULL)
            {
                gl->UseProgram(custom->program);
                if (custom->model_loc >= 0)
                    gl->UniformMatrix4fv(custom->model_loc, 1, GL_FALSE, e->model_matrix);
                if (custom->normal_matrix_loc >= 0)
                    gl->UniformMatrix3fv(custom->normal_matrix_loc, 1, GL_FALSE, e->normal_matrix);
                if (custom->tint_loc >= 0)
                    gl->Uniform4f(custom->tint_loc, e->tint[0], e->tint[1], e->tint[2], e->tint[3]);
                if (custom->metallic_loc >= 0)
                    gl->Uniform1f(custom->metallic_loc, e->metallic);
                if (custom->roughness_loc >= 0)
                    gl->Uniform1f(custom->roughness_loc, e->roughness);
                if (custom->emissive_loc >= 0)
                    gl->Uniform3f(custom->emissive_loc, e->emissive[0], e->emissive[1], e->emissive[2]);
                if (custom->baked_light_mode_loc >= 0)
                    gl->Uniform1i(custom->baked_light_mode_loc, e->baked_light_mode ? 1 : 0);

                gl->ActiveTexture(GL_TEXTURE0);
                gl->BindTexture(GL_TEXTURE_2D, tex);
                if (custom->pbr_texture_loc >= 0)
                    gl->Uniform1i(custom->pbr_texture_loc, 0);
                if (custom->pbr_has_texture_loc >= 0)
                    gl->Uniform1i(custom->pbr_has_texture_loc, e->texture ? 1 : 0);
                gl->ActiveTexture(GL_TEXTURE0 + 7);
                gl->BindTexture(GL_TEXTURE_2D, e->has_lightmap ? slayer3d_gl_resolve_texture(ctx, e->lightmap_texture)
                                                               : ctx->black_texture);
                if (custom->pbr_lightmap_loc >= 0)
                    gl->Uniform1i(custom->pbr_lightmap_loc, 7);
                if (custom->pbr_has_lightmap_loc >= 0)
                    gl->Uniform1i(custom->pbr_has_lightmap_loc, e->has_lightmap ? 1 : 0);
                gl->ActiveTexture(GL_TEXTURE0);

                gl->ActiveTexture(GL_TEXTURE0 + 1);
                gl->BindTexture(GL_TEXTURE_2D_ARRAY, ctx->shadow_depth_tex);
                if (custom->pbr_shadow_map_loc >= 0)
                    gl->Uniform1i(custom->pbr_shadow_map_loc, 1);
                if (ctx->shadow_depth_tex && ctx->shadow_bias > 0.0f)
                {
                    if (custom->pbr_shadow_vp_loc >= 0)
                        gl->UniformMatrix4fv(custom->pbr_shadow_vp_loc, 1, GL_FALSE, ctx->shadow_light_vp);
                    if (custom->pbr_shadow_enabled_loc >= 0)
                        gl->Uniform1i(custom->pbr_shadow_enabled_loc, 0);
                    if (custom->pbr_shadow_bias_loc >= 0)
                        gl->Uniform1f(custom->pbr_shadow_bias_loc, ctx->shadow_bias);
                    if (ctx->csm_fragment_enabled)
                    {
                        if (custom->pbr_csm_vp_loc[0] >= 0)
                            gl->UniformMatrix4fv(custom->pbr_csm_vp_loc[0], 1, GL_FALSE, ctx->shadow_light_vp);
                        for (int c = 1; c < 4; c++)
                        {
                            if (custom->pbr_csm_vp_loc[c] >= 0)
                                gl->UniformMatrix4fv(custom->pbr_csm_vp_loc[c], 1, GL_FALSE, ctx->csm_light_vp[c]);
                        }
                        if (custom->pbr_csm_splits_loc >= 0)
                            gl->Uniform1fv(custom->pbr_csm_splits_loc, 4, ctx->csm_split_depths);
                        if (custom->pbr_view_matrix_loc >= 0)
                            gl->UniformMatrix4fv(custom->pbr_view_matrix_loc, 1, GL_FALSE, e->view_matrix);
                        if (custom->pbr_csm_enabled_loc >= 0)
                            gl->Uniform1i(custom->pbr_csm_enabled_loc, 1);
                    }
                    else if (custom->pbr_csm_enabled_loc >= 0)
                    {
                        gl->Uniform1i(custom->pbr_csm_enabled_loc, 0);
                    }
                }
                gl->ActiveTexture(GL_TEXTURE0);

                for (int ps = 0; ps < SLAYER3D_MAX_POINT_SHADOWS; ps++)
                {
                    gl->ActiveTexture(GL_TEXTURE0 + 2 + (GLenum)ps);
                    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->point_shadow_cubemap[ps]);
                    if (custom->pbr_point_shadow_map_loc[ps] >= 0)
                        gl->Uniform1i(custom->pbr_point_shadow_map_loc[ps], 2 + ps);
                    if (ps < ctx->point_shadow_count && ctx->current_ctx != NULL)
                    {
                        const slayer3d_light *pl = &ctx->current_ctx->lights[ctx->point_shadow_light_index[ps]];
                        if (custom->pbr_point_shadow_light_pos_loc[ps] >= 0)
                            gl->Uniform3f(custom->pbr_point_shadow_light_pos_loc[ps], pl->position.x, pl->position.y,
                                          pl->position.z);
                        if (custom->pbr_point_shadow_far_loc[ps] >= 0)
                            gl->Uniform1f(custom->pbr_point_shadow_far_loc[ps], ctx->point_shadow_far_plane[ps]);
                    }
                }
                if (custom->pbr_point_shadow_count_loc >= 0)
                    gl->Uniform1i(custom->pbr_point_shadow_count_loc, ctx->point_shadow_count);
                gl->ActiveTexture(GL_TEXTURE0);

                if (ctx->ibl_ready)
                {
                    gl->ActiveTexture(GL_TEXTURE0 + 4);
                    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->ibl_irradiance_map);
                    if (custom->pbr_irradiance_map_loc >= 0)
                        gl->Uniform1i(custom->pbr_irradiance_map_loc, 4);
                    gl->ActiveTexture(GL_TEXTURE0 + 5);
                    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->ibl_prefilter_map);
                    if (custom->pbr_prefilter_map_loc >= 0)
                        gl->Uniform1i(custom->pbr_prefilter_map_loc, 5);
                    gl->ActiveTexture(GL_TEXTURE0 + 6);
                    gl->BindTexture(GL_TEXTURE_2D, ctx->ibl_brdf_lut);
                    if (custom->pbr_brdf_lut_loc >= 0)
                        gl->Uniform1i(custom->pbr_brdf_lut_loc, 6);
                    if (custom->pbr_ibl_enabled_loc >= 0)
                        gl->Uniform1i(custom->pbr_ibl_enabled_loc, 1);
                    if (custom->pbr_max_reflection_lod_loc >= 0)
                        gl->Uniform1f(custom->pbr_max_reflection_lod_loc, 4.0f);
                    gl->ActiveTexture(GL_TEXTURE0);
                }
                else
                {
                    gl->ActiveTexture(GL_TEXTURE0 + 4);
                    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->black_cubemap);
                    if (custom->pbr_irradiance_map_loc >= 0)
                        gl->Uniform1i(custom->pbr_irradiance_map_loc, 4);
                    gl->ActiveTexture(GL_TEXTURE0 + 5);
                    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->black_cubemap);
                    if (custom->pbr_prefilter_map_loc >= 0)
                        gl->Uniform1i(custom->pbr_prefilter_map_loc, 5);
                    gl->ActiveTexture(GL_TEXTURE0 + 6);
                    gl->BindTexture(GL_TEXTURE_2D, ctx->black_texture);
                    if (custom->pbr_brdf_lut_loc >= 0)
                        gl->Uniform1i(custom->pbr_brdf_lut_loc, 6);
                    if (custom->pbr_ibl_enabled_loc >= 0)
                        gl->Uniform1i(custom->pbr_ibl_enabled_loc, 0);
                    gl->ActiveTexture(GL_TEXTURE0);
                }
            }
            else
            {
                gl->UseProgram(ctx->pbr_program);
                gl->UniformMatrix4fv(ctx->pbr_model_loc, 1, GL_FALSE, e->model_matrix);
                gl->UniformMatrix3fv(ctx->pbr_normal_matrix_loc, 1, GL_FALSE, e->normal_matrix);
                if (ctx->pbr_use_instancing_loc >= 0)
                    gl->Uniform1i(ctx->pbr_use_instancing_loc, 0);
                bind_skinning_state(ctx, ctx->pbr_use_skinning_loc, ctx->pbr_use_skin_palette_loc,
                                    ctx->pbr_joint_palette_offset_loc, ctx->pbr_joint_matrices_loc, e);
                gl->Uniform4f(ctx->pbr_tint_loc, e->tint[0], e->tint[1], e->tint[2], e->tint[3]);
                gl->Uniform1f(ctx->pbr_metallic_loc, e->metallic);
                gl->Uniform1f(ctx->pbr_roughness_loc, e->roughness);
                gl->Uniform3f(ctx->pbr_emissive_loc, e->emissive[0], e->emissive[1], e->emissive[2]);
                gl->Uniform1i(ctx->pbr_baked_light_mode_loc, e->baked_light_mode ? 1 : 0);

                gl->ActiveTexture(GL_TEXTURE0);
                gl->BindTexture(GL_TEXTURE_2D, tex);
                gl->Uniform1i(ctx->pbr_texture_loc, 0);
                gl->Uniform1i(ctx->pbr_has_texture_loc, e->texture ? 1 : 0);
                gl->ActiveTexture(GL_TEXTURE0 + 7);
                gl->BindTexture(GL_TEXTURE_2D, e->has_lightmap ? slayer3d_gl_resolve_texture(ctx, e->lightmap_texture)
                                                               : ctx->black_texture);
                gl->Uniform1i(ctx->pbr_lightmap_loc, 7);
                gl->Uniform1i(ctx->pbr_has_lightmap_loc, e->has_lightmap ? 1 : 0);
                gl->ActiveTexture(GL_TEXTURE0);

                /* Shadow uniforms — always bind the texture array to prevent
                 * undefined sampler behavior on some drivers. */
                gl->ActiveTexture(GL_TEXTURE0 + 1);
                gl->BindTexture(GL_TEXTURE_2D_ARRAY, ctx->shadow_depth_tex);
                gl->Uniform1i(ctx->pbr_shadow_map_loc, 1);
                if (ctx->shadow_depth_tex && ctx->shadow_bias > 0.0f)
                {
                    gl->UniformMatrix4fv(ctx->pbr_shadow_vp_loc, 1, GL_FALSE, ctx->shadow_light_vp);
                    gl->Uniform1i(ctx->pbr_shadow_enabled_loc, 0); /* CSM disabled */
                    gl->Uniform1f(ctx->pbr_shadow_bias_loc, ctx->shadow_bias);
                    if (ctx->csm_fragment_enabled)
                    {
                        gl->UniformMatrix4fv(ctx->pbr_csm_vp_loc[0], 1, GL_FALSE, ctx->shadow_light_vp);
                        for (int c = 1; c < 4; c++)
                            gl->UniformMatrix4fv(ctx->pbr_csm_vp_loc[c], 1, GL_FALSE, ctx->csm_light_vp[c]);
                        gl->Uniform1fv(ctx->pbr_csm_splits_loc, 4, ctx->csm_split_depths);
                        gl->UniformMatrix4fv(ctx->pbr_view_matrix_loc, 1, GL_FALSE, e->view_matrix);
                        gl->Uniform1i(ctx->pbr_csm_enabled_loc, 1);
                    }
                    else
                    {
                        gl->Uniform1i(ctx->pbr_csm_enabled_loc, 0);
                    }
                }
                else
                {
                    gl->Uniform1i(ctx->pbr_shadow_enabled_loc, 0);
                    gl->Uniform1i(ctx->pbr_csm_enabled_loc, 0);
                }
                gl->ActiveTexture(GL_TEXTURE0);

                /* Point shadow uniforms. */
                for (int ps = 0; ps < SLAYER3D_MAX_POINT_SHADOWS; ps++)
                {
                    gl->ActiveTexture(GL_TEXTURE0 + 2 + (GLenum)ps);
                    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->point_shadow_cubemap[ps]);
                    gl->Uniform1i(ctx->pbr_point_shadow_map_loc[ps], 2 + ps);
                    if (ps < ctx->point_shadow_count)
                    {
                        const slayer3d_light *pl = &ctx->current_ctx->lights[ctx->point_shadow_light_index[ps]];
                        gl->Uniform3f(ctx->pbr_point_shadow_light_pos_loc[ps], pl->position.x, pl->position.y,
                                      pl->position.z);
                        gl->Uniform1f(ctx->pbr_point_shadow_far_loc[ps], ctx->point_shadow_far_plane[ps]);
                    }
                }
                gl->Uniform1i(ctx->pbr_point_shadow_count_loc, ctx->point_shadow_count);
                gl->ActiveTexture(GL_TEXTURE0);

                /* IBL textures. */
                if (ctx->ibl_ready)
                {
                    gl->ActiveTexture(GL_TEXTURE0 + 4);
                    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->ibl_irradiance_map);
                    gl->Uniform1i(ctx->pbr_irradiance_map_loc, 4);
                    gl->ActiveTexture(GL_TEXTURE0 + 5);
                    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->ibl_prefilter_map);
                    gl->Uniform1i(ctx->pbr_prefilter_map_loc, 5);
                    gl->ActiveTexture(GL_TEXTURE0 + 6);
                    gl->BindTexture(GL_TEXTURE_2D, ctx->ibl_brdf_lut);
                    gl->Uniform1i(ctx->pbr_brdf_lut_loc, 6);
                    gl->Uniform1i(ctx->pbr_ibl_enabled_loc, 1);
                    gl->Uniform1f(ctx->pbr_max_reflection_lod_loc, 4.0f);
                    gl->ActiveTexture(GL_TEXTURE0);
                }
                else
                {
                    gl->ActiveTexture(GL_TEXTURE0 + 4);
                    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->black_cubemap);
                    gl->Uniform1i(ctx->pbr_irradiance_map_loc, 4);
                    gl->ActiveTexture(GL_TEXTURE0 + 5);
                    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->black_cubemap);
                    gl->Uniform1i(ctx->pbr_prefilter_map_loc, 5);
                    gl->ActiveTexture(GL_TEXTURE0 + 6);
                    gl->BindTexture(GL_TEXTURE_2D, ctx->black_texture);
                    gl->Uniform1i(ctx->pbr_brdf_lut_loc, 6);
                    gl->Uniform1i(ctx->pbr_ibl_enabled_loc, 0);
                    gl->ActiveTexture(GL_TEXTURE0);
                }
            }

            gl->BindVertexArray(e->mesh_cache ? e->mesh_cache->vao : ctx->lit_vao);
            if (e->mesh_cache == NULL)
            {
                gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_position_vbo);
                gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)), e->positions,
                               GL_DYNAMIC_DRAW);
                gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_normal_vbo);
                gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)), e->normals,
                               GL_DYNAMIC_DRAW);
                gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_uv_vbo);
                gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 2 * sizeof(float)), e->uvs,
                               GL_DYNAMIC_DRAW);
                gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_lightmap_uv_vbo);
                gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 2 * sizeof(float)),
                               e->has_lightmap ? e->lightmap_uvs : e->uvs, GL_DYNAMIC_DRAW);
                gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_color_vbo);
                gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 4 * sizeof(float)), e->colors,
                               GL_DYNAMIC_DRAW);
            }

            if (e->indices && e->index_count > 0)
            {
                if (e->mesh_cache == NULL)
                {
                    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->lit_ebo);
                    gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->index_count * sizeof(unsigned int)),
                                   e->indices, GL_DYNAMIC_DRAW);
                }
                gl->DrawElements(e->primitive_mode, e->index_count, GL_UNSIGNED_INT, NULL);
                if (ctx->current_ctx != NULL)
                    ctx->current_ctx->stats.geometry_draw_calls += 1u;
            }
            else
            {
                gl->DrawArrays(e->primitive_mode, 0, e->vertex_count);
                if (ctx->current_ctx != NULL)
                    ctx->current_ctx->stats.geometry_draw_calls += 1u;
            }
        }
        else
        {
            slayer3d_custom_shader_cache_entry *custom =
                (e->shader_fragment_source != NULL && e->shader_fragment_source[0] != '\0')
                    ? custom_shader_lookup_or_create(ctx, false, e->shader_vertex_source, e->shader_fragment_source)
                    : NULL;

            if (custom != NULL)
            {
                gl->UseProgram(custom->program);
                gl->UniformMatrix4fv(custom->mvp_loc, 1, GL_FALSE, e->mvp);
                gl->Uniform4f(custom->tint_loc, e->tint[0], e->tint[1], e->tint[2], e->tint[3]);
                gl->ActiveTexture(GL_TEXTURE0);
                gl->BindTexture(GL_TEXTURE_2D, tex);
                if (custom->texture_loc >= 0)
                    gl->Uniform1i(custom->texture_loc, 0);
                if (custom->has_texture_loc >= 0)
                    gl->Uniform1i(custom->has_texture_loc, e->texture ? 1 : 0);
                if (custom->overlay_effect_loc >= 0)
                    gl->Uniform1i(custom->overlay_effect_loc, 0);
            }
            else
            {
                gl->UseProgram(ctx->unlit_program);
                gl->UniformMatrix4fv(ctx->unlit_mvp_loc, 1, GL_FALSE, e->mvp);
                gl->Uniform4f(ctx->unlit_tint_loc, e->tint[0], e->tint[1], e->tint[2], e->tint[3]);

                gl->ActiveTexture(GL_TEXTURE0);
                gl->BindTexture(GL_TEXTURE_2D, tex);
                gl->Uniform1i(ctx->unlit_texture_loc, 0);
                gl->Uniform1i(ctx->unlit_has_texture_loc, e->texture ? 1 : 0);
            }

            gl->BindVertexArray(e->mesh_cache ? e->mesh_cache->vao : ctx->unlit_vao);
            if (e->mesh_cache == NULL)
            {
                gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_position_vbo);
                gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)), e->positions,
                               GL_DYNAMIC_DRAW);
                gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_uv_vbo);
                gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 2 * sizeof(float)), e->uvs,
                               GL_DYNAMIC_DRAW);
                gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_color_vbo);
                gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 4 * sizeof(float)), e->colors,
                               GL_DYNAMIC_DRAW);
            }

            if (e->indices && e->index_count > 0)
            {
                if (e->mesh_cache == NULL)
                {
                    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->unlit_ebo);
                    gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->index_count * sizeof(unsigned int)),
                                   e->indices, GL_DYNAMIC_DRAW);
                }
                gl->DrawElements(e->primitive_mode, e->index_count, GL_UNSIGNED_INT, NULL);
                if (ctx->current_ctx != NULL)
                    ctx->current_ctx->stats.geometry_draw_calls += 1u;
            }
            else
            {
                gl->DrawArrays(e->primitive_mode, 0, e->vertex_count);
                if (ctx->current_ctx != NULL)
                    ctx->current_ctx->stats.geometry_draw_calls += 1u;
            }
        }
    }
    gl->Disable(GL_SCISSOR_TEST);
    gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
}

static void apply_geometry_cull_state(slayer3d_gl_context *ctx)
{
    slayer3d_gl_funcs *gl = &ctx->gl;
    gl->Enable(GL_DEPTH_TEST);
    gl->Enable(GL_BLEND);
    gl->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl->CullFace(GL_BACK);
    gl->FrontFace(GL_CCW);

    if (ctx->current_ctx && ctx->current_ctx->backface_culling_enabled)
    {
        gl->Enable(GL_CULL_FACE);
    }
    else
    {
        gl->Disable(GL_CULL_FACE);
    }
}

/* ------------------------------------------------------------------ */
/* Lazy UBO upload                                                     */
/* ------------------------------------------------------------------ */

static void flush_scene_ubo(slayer3d_gl_context *ctx)
{
    if (!ctx->ubo_dirty)
        return;
    ctx->ubo_dirty = false;

    slayer3d_render_context *rc = ctx->current_ctx;
    slayer3d_scene_ubo_data ubo;
    SDL_memset(&ubo, 0, sizeof(ubo));

    SDL_memcpy(ubo.view_projection, rc->view_projection.m, 16 * sizeof(float));

    /* Extract camera position from view matrix inverse translation. */
    const slayer3d_mat4 v = rc->view;
    ubo.camera_pos[0] = -(v.m[0] * v.m[12] + v.m[1] * v.m[13] + v.m[2] * v.m[14]);
    ubo.camera_pos[1] = -(v.m[4] * v.m[12] + v.m[5] * v.m[13] + v.m[6] * v.m[14]);
    ubo.camera_pos[2] = -(v.m[8] * v.m[12] + v.m[9] * v.m[13] + v.m[10] * v.m[14]);

    ubo.ambient[0] = rc->ambient[0];
    ubo.ambient[1] = rc->ambient[1];
    ubo.ambient[2] = rc->ambient[2];

    int lc = rc->light_count;
    if (lc > SLAYER3D_MAX_SHADER_LIGHTS)
        lc = SLAYER3D_MAX_SHADER_LIGHTS;
    ubo.light_count = lc;
    for (int i = 0; i < lc; i++)
    {
        const slayer3d_light *l = &rc->lights[i];
        ubo.lights[i].type = (int)l->type;
        ubo.lights[i].position[0] = l->position.x;
        ubo.lights[i].position[1] = l->position.y;
        ubo.lights[i].position[2] = l->position.z;
        ubo.lights[i].direction[0] = l->direction.x;
        ubo.lights[i].direction[1] = l->direction.y;
        ubo.lights[i].direction[2] = l->direction.z;
        ubo.lights[i].color[0] = l->color[0];
        ubo.lights[i].color[1] = l->color[1];
        ubo.lights[i].color[2] = l->color[2];
        ubo.lights[i].intensity = l->intensity;
        ubo.lights[i].range = l->range;
        ubo.lights[i].inner_cutoff = l->inner_cutoff;
        ubo.lights[i].outer_cutoff = l->outer_cutoff;
    }

    ubo.fog_mode = (int)rc->fog.mode;
    ubo.fog_start = rc->fog.start;
    ubo.fog_end = rc->fog.end;
    ubo.fog_density = rc->fog.density;
    ubo.fog_color[0] = rc->fog.color[0];
    ubo.fog_color[1] = rc->fog.color[1];
    ubo.fog_color[2] = rc->fog.color[2];
    ubo.tonemap_mode = (int)rc->tonemap_mode;

    slayer3d_gl_funcs *gl = &ctx->gl;
    gl->BindBuffer(GL_UNIFORM_BUFFER, ctx->scene_ubo);
    gl->BufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(ubo), &ubo, GL_DYNAMIC_DRAW);
    gl->BindBufferBase(GL_UNIFORM_BUFFER, 0, ctx->scene_ubo);

    (void)ctx; /* UBO flush logging removed — enable SDL_LOG_PRIORITY_DEBUG to see GL internals */
}

/* ------------------------------------------------------------------ */
/* Create / Destroy                                                    */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* IBL: load HDRI and generate irradiance + prefilter + BRDF LUT       */
/* ------------------------------------------------------------------ */

static void ibl_render_cube(slayer3d_gl_funcs *gl, GLuint vao)
{
    gl->BindVertexArray(vao);
    gl->DrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, NULL);
}

extern float *stbi_loadf(const char *, int *, int *, int *, int);
extern void stbi_image_free(void *);

bool slayer3d_gl_load_environment_map(slayer3d_gl_context *ctx, const char *hdr_path)
{
    slayer3d_gl_funcs *gl = &ctx->gl;
    int w, h, nc;

    float *data = stbi_loadf(hdr_path, &w, &h, &nc, 0);
    if (!data)
    {
        return SDL_SetError("IBL: failed to load HDR '%s'", hdr_path);
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D IBL: loaded HDR %dx%d (%d ch)", w, h, nc);

    /* Upload equirectangular HDR texture */
    GLuint hdr_tex;
    gl->GenTextures(1, &hdr_tex);
    gl->BindTexture(GL_TEXTURE_2D, hdr_tex);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, nc == 4 ? GL_RGBA : GL_RGB, GL_FLOAT, data);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F /* GL_CLAMP_TO_EDGE */);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);

    /* Capture FBO */
    GLuint cap_fbo, cap_rbo;
    gl->GenFramebuffers(1, &cap_fbo);
    gl->GenRenderbuffers(1, &cap_rbo);

    /* Cube VAO */
    GLuint cube_vao, cube_vbo, cube_ebo;
    gl->GenVertexArrays(1, &cube_vao);
    gl->GenBuffers(1, &cube_vbo);
    gl->GenBuffers(1, &cube_ebo);
    gl->BindVertexArray(cube_vao);
    gl->BindBuffer(GL_ARRAY_BUFFER, cube_vbo);
    gl->BufferData(GL_ARRAY_BUFFER, sizeof(k_cube_vertices), k_cube_vertices, GL_STATIC_DRAW);
    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_ebo);
    gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(k_cube_indices), k_cube_indices, GL_STATIC_DRAW);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), NULL);

    const char *ver = ctx->is_es ? "#version 300 es\nprecision highp float;\n" : "#version 330 core\n";

    /* Capture projection + 6 view matrices */
    float cap_proj[16];
    {
        slayer3d_mat4 pm;
        slayer3d_mat4_perspective(3.14159265f * 0.5f, 1.0f, 0.1f, 10.0f, &pm);
        SDL_memcpy(cap_proj, pm.m, sizeof(cap_proj));
    }
    slayer3d_vec3 o = {0, 0, 0};
    slayer3d_vec3 tgts[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    slayer3d_vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    float cap_views[6][16];
    for (int i = 0; i < 6; i++)
    {
        slayer3d_mat4 v;
        slayer3d_mat4_look_at(o, tgts[i], ups[i], &v);
        SDL_memcpy(cap_views[i], v.m, sizeof(cap_views[i]));
    }

    gl->Disable(GL_CULL_FACE);
    gl->Enable(GL_DEPTH_TEST);

    /* ---- Step 1: Equirect -> Cubemap (512) ---- */
    GLuint eq_prog = slayer3d_gl_build_program(gl, ver, k_cube_vert, k_equirect_to_cube_frag);
    GLuint env_cubemap;
    gl->GenTextures(1, &env_cubemap);
    gl->BindTexture(GL_TEXTURE_CUBE_MAP, env_cubemap);
    for (int i = 0; i < 6; i++)
        gl->TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i, 0, GL_RGB16F, 512, 512, 0, GL_RGB, GL_FLOAT, NULL);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, 0x812F);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, 0x812F);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, 0x812F);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    gl->UseProgram(eq_prog);
    gl->Uniform1i(gl->GetUniformLocation(eq_prog, "uEquirectMap"), 0);
    gl->UniformMatrix4fv(gl->GetUniformLocation(eq_prog, "uProjection"), 1, GL_FALSE, cap_proj);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, hdr_tex);

    gl->BindFramebuffer(GL_FRAMEBUFFER, cap_fbo);
    gl->BindRenderbuffer(GL_RENDERBUFFER, cap_rbo);
    gl->RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    gl->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, cap_rbo);
    gl->Viewport(0, 0, 512, 512);
    GLint eq_view_loc = gl->GetUniformLocation(eq_prog, "uView");
    for (int i = 0; i < 6; i++)
    {
        gl->UniformMatrix4fv(eq_view_loc, 1, GL_FALSE, cap_views[i]);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i,
                                 env_cubemap, 0);
        gl->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ibl_render_cube(gl, cube_vao);
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D IBL: equirect -> cubemap done");

    /* ---- Step 2: Irradiance convolution (32x32) ---- */
    GLuint irr_prog = slayer3d_gl_build_program(gl, ver, k_cube_vert, k_irradiance_frag);
    gl->GenTextures(1, &ctx->ibl_irradiance_map);
    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->ibl_irradiance_map);
    for (int i = 0; i < 6; i++)
        gl->TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i, 0, GL_RGB16F, 32, 32, 0, GL_RGB, GL_FLOAT, NULL);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, 0x812F);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, 0x812F);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, 0x812F);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    gl->UseProgram(irr_prog);
    gl->Uniform1i(gl->GetUniformLocation(irr_prog, "uEnvironmentMap"), 0);
    gl->UniformMatrix4fv(gl->GetUniformLocation(irr_prog, "uProjection"), 1, GL_FALSE, cap_proj);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_CUBE_MAP, env_cubemap);
    gl->RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 32, 32);
    gl->Viewport(0, 0, 32, 32);
    GLint irr_view_loc = gl->GetUniformLocation(irr_prog, "uView");
    for (int i = 0; i < 6; i++)
    {
        gl->UniformMatrix4fv(irr_view_loc, 1, GL_FALSE, cap_views[i]);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i,
                                 ctx->ibl_irradiance_map, 0);
        gl->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ibl_render_cube(gl, cube_vao);
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D IBL: irradiance done");

    /* ---- Step 3: Prefilter (128, 5 mip levels) ---- */
    GLuint pf_prog = slayer3d_gl_build_program(gl, ver, k_cube_vert, k_prefilter_frag);
    gl->GenTextures(1, &ctx->ibl_prefilter_map);
    gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->ibl_prefilter_map);
    for (int i = 0; i < 6; i++)
        gl->TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i, 0, GL_RGB16F, 128, 128, 0, GL_RGB, GL_FLOAT, NULL);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, 0x812F);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, 0x812F);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, 0x812F);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, 0x2703 /* GL_LINEAR_MIPMAP_LINEAR */);
    gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->GenerateMipmap(GL_TEXTURE_CUBE_MAP);

    gl->UseProgram(pf_prog);
    gl->Uniform1i(gl->GetUniformLocation(pf_prog, "uEnvironmentMap"), 0);
    gl->UniformMatrix4fv(gl->GetUniformLocation(pf_prog, "uProjection"), 1, GL_FALSE, cap_proj);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_CUBE_MAP, env_cubemap);
    GLint pf_view_loc = gl->GetUniformLocation(pf_prog, "uView");
    GLint pf_rough_loc = gl->GetUniformLocation(pf_prog, "uRoughness");
    for (int mip = 0; mip < 5; mip++)
    {
        int mw = (int)(128 * SDL_powf(0.5f, (float)mip));
        gl->RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mw, mw);
        gl->Viewport(0, 0, mw, mw);
        gl->Uniform1f(pf_rough_loc, (float)mip / 4.0f);
        for (int i = 0; i < 6; i++)
        {
            gl->UniformMatrix4fv(pf_view_loc, 1, GL_FALSE, cap_views[i]);
            gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i,
                                     ctx->ibl_prefilter_map, mip);
            gl->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            ibl_render_cube(gl, cube_vao);
        }
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D IBL: prefilter done");

    /* ---- Step 4: BRDF LUT (512x512) ---- */
    GLuint brdf_prog = slayer3d_gl_build_program(gl, ver, k_brdf_vert, k_brdf_frag);
    gl->GenTextures(1, &ctx->ibl_brdf_lut);
    gl->BindTexture(GL_TEXTURE_2D, ctx->ibl_brdf_lut);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, NULL);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    gl->RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->ibl_brdf_lut, 0);
    gl->Viewport(0, 0, 512, 512);
    gl->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl->UseProgram(brdf_prog);
    gl->BindVertexArray(ctx->fullscreen_vao);
    gl->DrawArrays(GL_TRIANGLES, 0, 3);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D IBL: BRDF LUT done");

    /* Cleanup */
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->DeleteTextures(1, &hdr_tex);
    gl->DeleteTextures(1, &env_cubemap);
    gl->DeleteVertexArrays(1, &cube_vao);
    gl->DeleteBuffers(1, &cube_vbo);
    gl->DeleteBuffers(1, &cube_ebo);
    gl->DeleteFramebuffers(1, &cap_fbo);
    gl->DeleteRenderbuffers(1, &cap_rbo);
    gl->DeleteProgram(eq_prog);
    gl->DeleteProgram(irr_prog);
    gl->DeleteProgram(pf_prog);
    gl->DeleteProgram(brdf_prog);

    /* Restore state */
    gl->Enable(GL_CULL_FACE);
    gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);

    ctx->ibl_ready = true;
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D IBL: environment map ready");
    return true;
}

slayer3d_gl_context *slayer3d_gl_create(SDL_Window *window, int width, int height)
{
    /* Request GL 3.3 Core. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    if (!glctx)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL: failed to create context: %s", SDL_GetError());
        return NULL;
    }
    SDL_GL_MakeCurrent(window, glctx);

    /* Adaptive vsync: allows tearing only when a frame is late,
     * preventing stutter. Falls back to regular vsync if unsupported. */
    if (!SDL_GL_SetSwapInterval(-1))
    {
        SDL_GL_SetSwapInterval(1);
    }

    slayer3d_gl_context *ctx = SDL_calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        SDL_GL_DestroyContext(glctx);
        return NULL;
    }

    ctx->window = window;
    ctx->gl_context = glctx;
    ctx->frame_index = 1;

    if (!slayer3d_gl_load_funcs(&ctx->gl))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL: failed to load GL functions");
        SDL_GL_DestroyContext(glctx);
        SDL_free(ctx);
        return NULL;
    }

    slayer3d_gl_funcs *gl = &ctx->gl;

    /* Detect ES. */
    ctx->is_es = false;
    ctx->sample_queries_supported = !ctx->is_es && gl->GenQueries != NULL && gl->DeleteQueries != NULL &&
                                    gl->BeginQuery != NULL && gl->EndQuery != NULL && gl->GetQueryObjectuiv != NULL;
    if (ctx->sample_queries_supported)
    {
        gl->GenQueries(1, &ctx->depth_prepass_query);
        gl->GenQueries(1, &ctx->geometry_query);
        ctx->sample_queries_supported = ctx->depth_prepass_query != 0u && ctx->geometry_query != 0u;
    }

    const char *version_prefix = ctx->is_es ? "#version 300 es\nprecision highp float;\n" : "#version 330\n";

    /* Compile shader programs. */
    /* PBR frag is split into chunks to stay under C99 string length limits. */
    {
        GLuint vs = slayer3d_gl_compile_shader(gl, GL_VERTEX_SHADER, version_prefix, k_pbr_vert);
        const char *frag_srcs[5] = {version_prefix, k_pbr_frag_decl, k_pbr_frag_main, k_pbr_frag_shadow_post,
                                    k_pbr_frag_light_post};
        GLuint fs = vs ? slayer3d_gl_compile_shader_multi(gl, GL_FRAGMENT_SHADER, 5, frag_srcs) : 0;
        ctx->pbr_program = (vs && fs) ? slayer3d_gl_link_program(gl, vs, fs) : 0;
        if (vs)
            gl->DeleteShader(vs);
        if (fs)
            gl->DeleteShader(fs);
    }
    ctx->unlit_program = slayer3d_gl_build_program(gl, version_prefix, k_unlit_vert, k_unlit_frag);
    ctx->copy_program = slayer3d_gl_build_program(gl, version_prefix, k_fullscreen_vert, k_copy_frag);
    ctx->transition_program = slayer3d_gl_build_program(gl, version_prefix, k_fullscreen_vert, k_transition_frag);

    if (!ctx->pbr_program || !ctx->unlit_program || !ctx->copy_program || !ctx->transition_program)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL: shader compilation failed");
        slayer3d_gl_destroy(ctx);
        return NULL;
    }

    /* PBR uniform locations. */
    ctx->pbr_model_loc = gl->GetUniformLocation(ctx->pbr_program, "uModel");
    ctx->pbr_normal_matrix_loc = gl->GetUniformLocation(ctx->pbr_program, "uNormalMatrix");
    ctx->pbr_use_instancing_loc = gl->GetUniformLocation(ctx->pbr_program, "uUseInstancing");
    ctx->pbr_use_skinning_loc = gl->GetUniformLocation(ctx->pbr_program, "uUseSkinning");
    ctx->pbr_use_skin_palette_loc = gl->GetUniformLocation(ctx->pbr_program, "uUseSkinPalette");
    ctx->pbr_joint_palette_offset_loc = gl->GetUniformLocation(ctx->pbr_program, "uJointPaletteOffset");
    ctx->pbr_joint_matrices_loc = gl->GetUniformLocation(ctx->pbr_program, "uJointMatrices[0]");
    ctx->pbr_texture_loc = gl->GetUniformLocation(ctx->pbr_program, "uTexture");
    ctx->pbr_has_texture_loc = gl->GetUniformLocation(ctx->pbr_program, "uHasTexture");
    ctx->pbr_lightmap_loc = gl->GetUniformLocation(ctx->pbr_program, "uLightmap");
    ctx->pbr_has_lightmap_loc = gl->GetUniformLocation(ctx->pbr_program, "uHasLightmap");
    ctx->pbr_tint_loc = gl->GetUniformLocation(ctx->pbr_program, "uTint");
    ctx->pbr_metallic_loc = gl->GetUniformLocation(ctx->pbr_program, "uMetallic");
    ctx->pbr_roughness_loc = gl->GetUniformLocation(ctx->pbr_program, "uRoughness");
    ctx->pbr_emissive_loc = gl->GetUniformLocation(ctx->pbr_program, "uEmissive");
    ctx->pbr_baked_light_mode_loc = gl->GetUniformLocation(ctx->pbr_program, "uBakedLightMode");

    /* IBL uniform locations. */
    ctx->pbr_irradiance_map_loc = gl->GetUniformLocation(ctx->pbr_program, "uIrradianceMap");
    ctx->pbr_prefilter_map_loc = gl->GetUniformLocation(ctx->pbr_program, "uPrefilterMap");
    ctx->pbr_brdf_lut_loc = gl->GetUniformLocation(ctx->pbr_program, "uBrdfLUT");
    ctx->pbr_ibl_enabled_loc = gl->GetUniformLocation(ctx->pbr_program, "uIBLEnabled");
    ctx->pbr_max_reflection_lod_loc = gl->GetUniformLocation(ctx->pbr_program, "uMaxReflectionLod");

    /* PBR shadow uniform locations. */
    ctx->pbr_shadow_map_loc = gl->GetUniformLocation(ctx->pbr_program, "uShadowMap");
    ctx->pbr_shadow_vp_loc = gl->GetUniformLocation(ctx->pbr_program, "uShadowVP");
    ctx->pbr_shadow_enabled_loc = gl->GetUniformLocation(ctx->pbr_program, "uShadowEnabled");
    ctx->pbr_shadow_bias_loc = gl->GetUniformLocation(ctx->pbr_program, "uShadowBias");

    /* CSM uniform locations. */
    {
        char name[32];
        for (int i = 0; i < 4; i++)
        {
            SDL_snprintf(name, sizeof(name), "uCSMVP[%d]", i);
            ctx->pbr_csm_vp_loc[i] = gl->GetUniformLocation(ctx->pbr_program, name);
        }
    }
    ctx->pbr_csm_splits_loc = gl->GetUniformLocation(ctx->pbr_program, "uCSMSplits");
    ctx->pbr_view_matrix_loc = gl->GetUniformLocation(ctx->pbr_program, "uViewMatrix");
    ctx->pbr_csm_enabled_loc = gl->GetUniformLocation(ctx->pbr_program, "uCSMEnabled");

    /* Point shadow PBR uniform locations. */
    {
        char name[64];
        for (int s = 0; s < SLAYER3D_MAX_POINT_SHADOWS; s++)
        {
            SDL_snprintf(name, sizeof(name), "uPointShadowMap[%d]", s);
            ctx->pbr_point_shadow_map_loc[s] = gl->GetUniformLocation(ctx->pbr_program, name);
            SDL_snprintf(name, sizeof(name), "uPointShadowLightPos[%d]", s);
            ctx->pbr_point_shadow_light_pos_loc[s] = gl->GetUniformLocation(ctx->pbr_program, name);
            SDL_snprintf(name, sizeof(name), "uPointShadowFar[%d]", s);
            ctx->pbr_point_shadow_far_loc[s] = gl->GetUniformLocation(ctx->pbr_program, name);
        }
    }
    ctx->pbr_point_shadow_count_loc = gl->GetUniformLocation(ctx->pbr_program, "uPointShadowCount");

    /* Unlit uniform locations. */
    ctx->unlit_mvp_loc = gl->GetUniformLocation(ctx->unlit_program, "uMVP");
    ctx->unlit_texture_loc = gl->GetUniformLocation(ctx->unlit_program, "uTexture");
    ctx->unlit_has_texture_loc = gl->GetUniformLocation(ctx->unlit_program, "uHasTexture");
    ctx->unlit_tint_loc = gl->GetUniformLocation(ctx->unlit_program, "uTint");
    ctx->unlit_overlay_effect_loc = gl->GetUniformLocation(ctx->unlit_program, "uOverlayEffect");
    ctx->unlit_overlay_effect_progress_loc = gl->GetUniformLocation(ctx->unlit_program, "uOverlayEffectProgress");
    ctx->unlit_overlay_effect_seed_loc = gl->GetUniformLocation(ctx->unlit_program, "uOverlayEffectSeed");
    ctx->unlit_overlay_effect_columns_loc = gl->GetUniformLocation(ctx->unlit_program, "uOverlayEffectColumns");

    /* Copy uniform locations. */
    ctx->copy_texture_loc = gl->GetUniformLocation(ctx->copy_program, "uScene");

    /* Transition uniform locations and lookup texture. */
    ctx->transition_scene_loc = gl->GetUniformLocation(ctx->transition_program, "uScene");
    ctx->transition_type_loc = gl->GetUniformLocation(ctx->transition_program, "uType");
    ctx->transition_direction_loc = gl->GetUniformLocation(ctx->transition_program, "uDirection");
    ctx->transition_progress_loc = gl->GetUniformLocation(ctx->transition_program, "uProgress");
    ctx->transition_color_loc = gl->GetUniformLocation(ctx->transition_program, "uColor");
    ctx->transition_resolution_loc = gl->GetUniformLocation(ctx->transition_program, "uResolution");
    ctx->transition_melt_offsets_loc = gl->GetUniformLocation(ctx->transition_program, "uMeltOffsets");
    {
        gl->GenTextures(1, &ctx->transition_melt_offsets_tex);
        gl->BindTexture(GL_TEXTURE_2D, ctx->transition_melt_offsets_tex);
        gl->TexImage2D(GL_TEXTURE_2D, 0, GL_R32F, SLAYER3D_TRANSITION_MELT_COLUMNS, 1, 0, GL_RED, GL_FLOAT, NULL);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    /* Scene UBO — create, allocate, bind to point 0. */
    gl->GenBuffers(1, &ctx->scene_ubo);
    gl->BindBuffer(GL_UNIFORM_BUFFER, ctx->scene_ubo);
    gl->BufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(slayer3d_scene_ubo_data), NULL, GL_DYNAMIC_DRAW);
    gl->BindBufferBase(GL_UNIFORM_BUFFER, 0, ctx->scene_ubo);

    gl->GenBuffers(1, &ctx->skin_palette_ubo);
    gl->BindBuffer(GL_UNIFORM_BUFFER, ctx->skin_palette_ubo);
    gl->BufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)(SLAYER3D_GPU_SKINNING_PALETTE_MATRICES * 16u * sizeof(float)), NULL,
                   GL_DYNAMIC_DRAW);
    gl->BindBufferBase(GL_UNIFORM_BUFFER, SLAYER3D_SKIN_PALETTE_BINDING, ctx->skin_palette_ubo);

    /* Bind SceneUBO block in PBR program to binding point 0. */
    GLuint block_idx = gl->GetUniformBlockIndex(ctx->pbr_program, "SceneUBO");
    if (block_idx != 0xFFFFFFFFu)
    {
        gl->UniformBlockBinding(ctx->pbr_program, block_idx, 0);
    }
    block_idx = gl->GetUniformBlockIndex(ctx->pbr_program, "SkinPaletteUBO");
    if (block_idx != 0xFFFFFFFFu)
    {
        gl->UniformBlockBinding(ctx->pbr_program, block_idx, SLAYER3D_SKIN_PALETTE_BINDING);
    }

    /* ---- Lit streaming VAO/VBOs ---- */
    gl->GenVertexArrays(1, &ctx->lit_vao);
    gl->BindVertexArray(ctx->lit_vao);

    gl->GenBuffers(1, &ctx->lit_position_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_position_vbo);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->lit_normal_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_normal_vbo);
    gl->EnableVertexAttribArray(1);
    gl->VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->lit_uv_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_uv_vbo);
    gl->EnableVertexAttribArray(2);
    gl->VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->lit_lightmap_uv_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_lightmap_uv_vbo);
    gl->EnableVertexAttribArray(4);
    gl->VertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->lit_color_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->lit_color_vbo);
    gl->EnableVertexAttribArray(3);
    gl->VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->lit_ebo);
    gl->GenBuffers(1, &ctx->instance_model_vbo);
    gl->GenBuffers(1, &ctx->instance_normal_vbo);

    /* ---- Unlit streaming VAO/VBOs ---- */
    gl->GenVertexArrays(1, &ctx->unlit_vao);
    gl->BindVertexArray(ctx->unlit_vao);

    gl->GenBuffers(1, &ctx->unlit_position_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_position_vbo);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->unlit_uv_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_uv_vbo);
    gl->EnableVertexAttribArray(1);
    gl->VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->unlit_color_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_color_vbo);
    gl->EnableVertexAttribArray(2);
    gl->VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &ctx->unlit_ebo);

    /* ---- Fullscreen VAO (empty, vertex ID driven) ---- */
    gl->GenVertexArrays(1, &ctx->fullscreen_vao);

    /* Shadow map: 1024x1024 depth-only FBO with its own VAO. */
    gl->GenFramebuffers(1, &ctx->shadow_fbo);
    gl->GenTextures(1, &ctx->shadow_depth_tex);
    gl->BindTexture(GL_TEXTURE_2D_ARRAY, ctx->shadow_depth_tex);
    gl->TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, 2048, 2048, 4, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    gl->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    gl->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    {
        float border_color[] = {1.0f, 1.0f, 1.0f, 1.0f};
        gl->TexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border_color);
    }
    /* Attach layer 0 for now — CSM will render to each layer separately. */
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
    gl->FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, ctx->shadow_depth_tex, 0, 0);
    gl->DrawBuffer(GL_NONE);
    gl->ReadBuffer(GL_NONE);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo); /* restore */

    /* Shadow VAO: position only (simple depth shader). */
    gl->GenVertexArrays(1, &ctx->shadow_vao);
    gl->BindVertexArray(ctx->shadow_vao);
    gl->GenBuffers(1, &ctx->shadow_position_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, ctx->shadow_position_vbo);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    gl->GenBuffers(1, &ctx->shadow_ebo);
    gl->BindVertexArray(0);

    /* Compile shadow program. */
    ctx->shadow_program = slayer3d_gl_build_program(gl, version_prefix, k_shadow_vert, k_shadow_frag);
    if (ctx->shadow_program)
    {
        ctx->shadow_light_vp_loc = gl->GetUniformLocation(ctx->shadow_program, "uLightVP");
        ctx->shadow_model_loc = gl->GetUniformLocation(ctx->shadow_program, "uModel");
        ctx->shadow_use_instancing_loc = gl->GetUniformLocation(ctx->shadow_program, "uUseInstancing");
        ctx->shadow_use_skinning_loc = gl->GetUniformLocation(ctx->shadow_program, "uUseSkinning");
        ctx->shadow_use_skin_palette_loc = gl->GetUniformLocation(ctx->shadow_program, "uUseSkinPalette");
        ctx->shadow_joint_palette_offset_loc = gl->GetUniformLocation(ctx->shadow_program, "uJointPaletteOffset");
        ctx->shadow_joint_matrices_loc = gl->GetUniformLocation(ctx->shadow_program, "uJointMatrices[0]");
        GLuint shadow_skin_block_idx = gl->GetUniformBlockIndex(ctx->shadow_program, "SkinPaletteUBO");
        if (shadow_skin_block_idx != 0xFFFFFFFFu)
        {
            gl->UniformBlockBinding(ctx->shadow_program, shadow_skin_block_idx, SLAYER3D_SKIN_PALETTE_BINDING);
        }
    }

    /* Point light shadow: 1024x1024 depth cubemaps. */
    for (int s = 0; s < SLAYER3D_MAX_POINT_SHADOWS; s++)
        ctx->point_shadow_light_index[s] = -1;
    gl->GenFramebuffers(1, &ctx->point_shadow_fbo);
    for (int s = 0; s < SLAYER3D_MAX_POINT_SHADOWS; s++)
    {
        gl->GenTextures(1, &ctx->point_shadow_cubemap[s]);
        gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->point_shadow_cubemap[s]);
        for (int i = 0; i < 6; i++)
        {
            gl->TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i, 0, GL_DEPTH_COMPONENT24, 1024, 1024, 0,
                           GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        }
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }

    ctx->point_shadow_program = slayer3d_gl_build_program(gl, version_prefix, k_point_shadow_vert, k_point_shadow_frag);
    if (ctx->point_shadow_program)
    {
        ctx->point_shadow_model_loc = gl->GetUniformLocation(ctx->point_shadow_program, "model");
        ctx->point_shadow_light_vp_loc = gl->GetUniformLocation(ctx->point_shadow_program, "lightVP");
        ctx->point_shadow_light_pos_loc = gl->GetUniformLocation(ctx->point_shadow_program, "lightPos");
        ctx->point_shadow_far_loc = gl->GetUniformLocation(ctx->point_shadow_program, "farPlane");
        ctx->point_shadow_use_skinning_loc = gl->GetUniformLocation(ctx->point_shadow_program, "uUseSkinning");
        ctx->point_shadow_use_skin_palette_loc = gl->GetUniformLocation(ctx->point_shadow_program, "uUseSkinPalette");
        ctx->point_shadow_joint_palette_offset_loc =
            gl->GetUniformLocation(ctx->point_shadow_program, "uJointPaletteOffset");
        ctx->point_shadow_joint_matrices_loc = gl->GetUniformLocation(ctx->point_shadow_program, "uJointMatrices[0]");
        GLuint point_shadow_skin_block_idx = gl->GetUniformBlockIndex(ctx->point_shadow_program, "SkinPaletteUBO");
        if (point_shadow_skin_block_idx != 0xFFFFFFFFu)
        {
            gl->UniformBlockBinding(ctx->point_shadow_program, point_shadow_skin_block_idx,
                                    SLAYER3D_SKIN_PALETTE_BINDING);
        }
    }

    gl->BindVertexArray(0);

    /* ---- 1×1 white texture ---- */
    {
        Uint8 white[4] = {255, 255, 255, 255};
        gl->GenTextures(1, &ctx->white_texture);
        gl->BindTexture(GL_TEXTURE_2D, ctx->white_texture);
        gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    /* Fallback textures keep inactive IBL samplers on distinct, valid units.
     * Without these bindings, some drivers treat the mixed sampler types as
     * undefined even when uIBLEnabled is 0. */
    {
        Uint8 black[4] = {0, 0, 0, 255};
        gl->GenTextures(1, &ctx->black_texture);
        gl->BindTexture(GL_TEXTURE_2D, ctx->black_texture);
        gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        gl->GenTextures(1, &ctx->black_cubemap);
        gl->BindTexture(GL_TEXTURE_CUBE_MAP, ctx->black_cubemap);
        for (int face = 0; face < 6; face++)
        {
            gl->TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                           GL_UNSIGNED_BYTE, black);
        }
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }

    ctx->logical_w = width;
    ctx->logical_h = height;
    ctx->world_render_scale = 1.0f;

    /* ---- Main FBO ---- */
    if (!slayer3d_gl_create_world_targets(ctx, width, height))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL: FBO creation failed");
        slayer3d_gl_destroy(ctx);
        return NULL;
    }

    /* ---- Post-process shaders ---- */
    ctx->bloom_program = slayer3d_gl_build_program(gl, version_prefix, k_fullscreen_vert, k_bloom_threshold_frag);
    ctx->bloom_blur_program = slayer3d_gl_build_program(gl, version_prefix, k_fullscreen_vert, k_blur_frag);
    ctx->composite_program = slayer3d_gl_build_program(gl, version_prefix, k_fullscreen_vert, k_composite_frag);
    if (!ctx->bloom_program || !ctx->bloom_blur_program || !ctx->composite_program)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL: post-process shader compilation failed");
        slayer3d_gl_destroy(ctx);
        return NULL;
    }
    ctx->bloom_scene_loc = gl->GetUniformLocation(ctx->bloom_program, "uScene");
    ctx->bloom_threshold_loc = gl->GetUniformLocation(ctx->bloom_program, "uThreshold");
    ctx->blur_image_loc = gl->GetUniformLocation(ctx->bloom_blur_program, "uImage");
    ctx->blur_horizontal_loc = gl->GetUniformLocation(ctx->bloom_blur_program, "uHorizontal");
    ctx->comp_scene_loc = gl->GetUniformLocation(ctx->composite_program, "uScene");
    ctx->comp_bloom_loc = gl->GetUniformLocation(ctx->composite_program, "uBloom");
    ctx->comp_vignette_loc = gl->GetUniformLocation(ctx->composite_program, "uVignetteStrength");
    ctx->comp_contrast_loc = gl->GetUniformLocation(ctx->composite_program, "uContrast");
    ctx->comp_saturation_loc = gl->GetUniformLocation(ctx->composite_program, "uSaturation");

    /* ---- Retro profile shader ---- */
    {
        GLuint vs = slayer3d_gl_compile_shader(gl, GL_VERTEX_SHADER, version_prefix, k_fullscreen_vert);
        const char *frag_srcs[3] = {version_prefix, k_retro_frag_helpers, k_retro_frag_main};
        GLuint fs = vs ? slayer3d_gl_compile_shader_multi(gl, GL_FRAGMENT_SHADER, 3, frag_srcs) : 0;
        ctx->retro_program = (vs && fs) ? slayer3d_gl_link_program(gl, vs, fs) : 0;
        if (vs)
            gl->DeleteShader(vs);
        if (fs)
            gl->DeleteShader(fs);
    }
    if (!ctx->retro_program)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL: retro shader compilation failed");
        slayer3d_gl_destroy(ctx);
        return NULL;
    }
    ctx->retro_scene_loc = gl->GetUniformLocation(ctx->retro_program, "uScene");
    ctx->retro_profile_loc = gl->GetUniformLocation(ctx->retro_program, "uProfile");
    ctx->retro_virtual_resolution_loc = gl->GetUniformLocation(ctx->retro_program, "uVirtualResolution");
    ctx->retro_output_resolution_loc = gl->GetUniformLocation(ctx->retro_program, "uOutputResolution");

    /* ---- SSAO shader ---- */
    ctx->ssao_program = slayer3d_gl_build_program(gl, version_prefix, k_fullscreen_vert, k_ssao_frag);
    if (!ctx->ssao_program)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL: SSAO shader compilation failed");
        slayer3d_gl_destroy(ctx);
        return NULL;
    }
    ctx->ssao_scene_loc = gl->GetUniformLocation(ctx->ssao_program, "uScene");
    ctx->ssao_depth_loc = gl->GetUniformLocation(ctx->ssao_program, "uDepth");
    ctx->ssao_texel_size_loc = gl->GetUniformLocation(ctx->ssao_program, "uTexelSize");
    ctx->ssao_near_loc = gl->GetUniformLocation(ctx->ssao_program, "uNear");
    ctx->ssao_far_loc = gl->GetUniformLocation(ctx->ssao_program, "uFar");

    /* ---- Initial GL state ---- */
    gl->Enable(GL_DEPTH_TEST);
    gl->DepthFunc(GL_LEQUAL);
    gl->Enable(GL_CULL_FACE);
    gl->CullFace(GL_BACK);
    gl->FrontFace(GL_CCW);

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL create: ctx=%p logical=%dx%d fbo=%u", (void *)ctx, width,
                 height, ctx->fbo);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                 "SLAYER3D GL renderer created: %dx%d pbr=%u unlit=%u copy=%u fbo=%u ubo=%u", width, height,
                 ctx->pbr_program, ctx->unlit_program, ctx->copy_program, ctx->fbo, ctx->scene_ubo);

    return ctx;
}

void slayer3d_gl_destroy(slayer3d_gl_context *ctx)
{
    if (!ctx)
        return;
    slayer3d_gl_funcs *gl = &ctx->gl;

    slayer3d_gl_free_draw_list(ctx);
    SDL_free(ctx->draw_list);
    slayer3d_gl_mesh_cache_free(ctx);
    custom_shader_cache_free(ctx);

    slayer3d_gl_free_overlay_list(ctx);
    SDL_free(ctx->overlay_list);
    for (int i = 0; i < ctx->overlay_atlas_count; i++)
        gl->DeleteTextures(1, &ctx->overlay_atlases[i].gl_tex);
    SDL_free(ctx->overlay_atlases);

    slayer3d_gl_tex_cache_free(ctx);
    SDL_free(ctx->white_colors);

    if (ctx->pbr_program)
        gl->DeleteProgram(ctx->pbr_program);
    if (ctx->unlit_program)
        gl->DeleteProgram(ctx->unlit_program);
    if (ctx->copy_program)
        gl->DeleteProgram(ctx->copy_program);
    if (ctx->transition_program)
        gl->DeleteProgram(ctx->transition_program);
    if (ctx->transition_melt_offsets_tex)
        gl->DeleteTextures(1, &ctx->transition_melt_offsets_tex);

    if (ctx->scene_ubo)
        gl->DeleteBuffers(1, &ctx->scene_ubo);
    if (ctx->skin_palette_ubo)
        gl->DeleteBuffers(1, &ctx->skin_palette_ubo);
    if (gl->DeleteQueries != NULL)
    {
        if (ctx->depth_prepass_query)
            gl->DeleteQueries(1, &ctx->depth_prepass_query);
        if (ctx->geometry_query)
            gl->DeleteQueries(1, &ctx->geometry_query);
    }

    GLuint lit_bufs[] = {ctx->lit_position_vbo, ctx->lit_normal_vbo, ctx->lit_uv_vbo,         ctx->lit_lightmap_uv_vbo,
                         ctx->lit_color_vbo,    ctx->lit_ebo,        ctx->instance_model_vbo, ctx->instance_normal_vbo};
    gl->DeleteBuffers(8, lit_bufs);
    if (ctx->lit_vao)
        gl->DeleteVertexArrays(1, &ctx->lit_vao);

    GLuint unlit_bufs[] = {ctx->unlit_position_vbo, ctx->unlit_uv_vbo, ctx->unlit_color_vbo, ctx->unlit_ebo};
    gl->DeleteBuffers(4, unlit_bufs);
    if (ctx->unlit_vao)
        gl->DeleteVertexArrays(1, &ctx->unlit_vao);

    if (ctx->fullscreen_vao)
        gl->DeleteVertexArrays(1, &ctx->fullscreen_vao);

    /* Shadow resources. */
    if (ctx->shadow_program)
        gl->DeleteProgram(ctx->shadow_program);
    if (ctx->shadow_fbo)
        gl->DeleteFramebuffers(1, &ctx->shadow_fbo);
    if (ctx->shadow_depth_tex)
        gl->DeleteTextures(1, &ctx->shadow_depth_tex);
    {
        GLuint shadow_bufs[] = {ctx->shadow_position_vbo, ctx->shadow_ebo};
        gl->DeleteBuffers(2, shadow_bufs);
    }
    if (ctx->shadow_vao)
        gl->DeleteVertexArrays(1, &ctx->shadow_vao);

    /* Point shadow resources. */
    if (ctx->point_shadow_program)
        gl->DeleteProgram(ctx->point_shadow_program);
    if (ctx->point_shadow_fbo)
        gl->DeleteFramebuffers(1, &ctx->point_shadow_fbo);
    gl->DeleteTextures(SLAYER3D_MAX_POINT_SHADOWS, ctx->point_shadow_cubemap);

    /* Post-process resources. */
    if (ctx->retro_program)
        gl->DeleteProgram(ctx->retro_program);
    if (ctx->ssao_program)
        gl->DeleteProgram(ctx->ssao_program);
    if (ctx->bloom_program)
        gl->DeleteProgram(ctx->bloom_program);
    if (ctx->bloom_blur_program)
        gl->DeleteProgram(ctx->bloom_blur_program);
    if (ctx->composite_program)
        gl->DeleteProgram(ctx->composite_program);

    if (ctx->white_texture)
        gl->DeleteTextures(1, &ctx->white_texture);
    if (ctx->black_texture)
        gl->DeleteTextures(1, &ctx->black_texture);
    if (ctx->black_cubemap)
        gl->DeleteTextures(1, &ctx->black_cubemap);

    slayer3d_gl_destroy_world_targets(ctx);

    if (ctx->gl_context)
        SDL_GL_DestroyContext(ctx->gl_context);
    SDL_free(ctx);
}

/* ================================================================== */
/* Backend interface implementations                                   */
/* ================================================================== */

static bool gl_clear(slayer3d_render_context *context, slayer3d_color color)
{
    slayer3d_gl_context *ctx = context->gl;
    slayer3d_gl_funcs *gl = &ctx->gl;

    slayer3d_gl_free_draw_list(ctx);

    ctx->frame_index++;
    ctx->current_ctx = context;
    ctx->ubo_dirty = true;

    ctx->active_retro_profile = (int)context->display_profile;
    if (ctx->active_retro_profile < (int)SLAYER3D_DISPLAY_PROFILE_MODERN ||
        ctx->active_retro_profile > (int)SLAYER3D_DISPLAY_PROFILE_GAMEBOY)
        ctx->active_retro_profile = (int)SLAYER3D_DISPLAY_PROFILE_MODERN;
    ctx->active_retro_virtual_w = context->display_width;
    ctx->active_retro_virtual_h = context->display_height;
    ctx->active_retro_filter = (int)context->display_filter;

    /* Sync shadow state from render context so deferred replay works. */
    if (context->shadow_enabled[0])
    {
        SDL_memcpy(ctx->shadow_light_vp, context->shadow_vp[0].m, 16 * sizeof(float));
        ctx->shadow_bias = context->shadow_bias > 0 ? context->shadow_bias : 0.005f;
    }

    SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);

    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
    gl->ClearColor((float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f, (float)color.a / 255.0f);
    gl->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return true;
}

static bool gl_draw_mesh_unlit(slayer3d_render_context *context, const slayer3d_draw_params_unlit *params)
{
    slayer3d_gl_context *ctx = context->gl;

    const float *colors = params->colors ? params->colors : ensure_white_colors(ctx, params->vertex_count);

    slayer3d_draw_entry *e = slayer3d_gl_append_draw_entry(ctx);
    if (!e)
        return false;

    e->lit = false;
    e->vertex_count = params->vertex_count;
    e->index_count = (params->indices && params->index_count > 0) ? params->index_count : 0;
    e->texture = params->texture;
    e->primitive_mode = GL_TRIANGLES;
    e->shader_vertex_source = params->shader_vertex_source;
    e->shader_fragment_source = params->shader_fragment_source;
    SDL_memcpy(e->mvp, params->mvp, 16 * sizeof(float));
    SDL_memcpy(e->tint, params->tint, 4 * sizeof(float));
    e->owns_arrays = !params->static_geometry;
    e->generation = params->generation;
    draw_entry_capture_viewport(e, context);

    if (params->static_geometry)
    {
        e->positions = params->positions;
        e->uvs = params->uvs;
        e->colors = colors;
        e->indices = params->indices;
        e->mesh_cache = slayer3d_gl_mesh_cache_lookup_or_create(
            ctx, false, e->primitive_mode, e->positions, NULL, e->uvs, NULL, e->colors, e->indices, NULL, NULL,
            e->vertex_count, e->index_count, e->generation, false, false);
        return e->mesh_cache != NULL;
    }

    e->positions = copy_floats(params->positions, (size_t)params->vertex_count * 3);
    e->uvs = copy_floats(params->uvs, (size_t)params->vertex_count * 2);
    e->colors = copy_floats(colors, (size_t)params->vertex_count * 4);
    e->indices = copy_indices(params->indices, (size_t)e->index_count);
    return true;
}

static bool gl_draw_mesh_lit(slayer3d_render_context *context, const slayer3d_draw_params_lit *params)
{
    slayer3d_gl_context *ctx = context->gl;

    const float *colors = params->colors ? params->colors : ensure_white_colors(ctx, params->vertex_count);

    slayer3d_draw_entry *e = slayer3d_gl_append_draw_entry(ctx);
    if (!e)
        return false;

    e->lit = true;
    e->vertex_count = params->vertex_count;
    e->index_count = (params->indices && params->index_count > 0) ? params->index_count : 0;
    e->texture = params->texture;
    e->lightmap_texture = params->lightmap;
    e->primitive_mode = GL_TRIANGLES;
    e->shader_vertex_source = params->shader_vertex_source;
    e->shader_fragment_source = params->shader_fragment_source;
    SDL_memcpy(e->view_matrix, context->view.m, 16 * sizeof(float));
    SDL_memcpy(e->view_projection, context->view_projection.m, 16 * sizeof(float));
    camera_position_from_view_matrix(&context->view, e->camera_pos);
    SDL_memcpy(e->model_matrix, params->model_matrix, 16 * sizeof(float));
    SDL_memcpy(e->normal_matrix, params->normal_matrix, 9 * sizeof(float));
    SDL_memcpy(e->tint, params->tint, 4 * sizeof(float));
    e->metallic = params->metallic;
    e->roughness = params->roughness;
    SDL_memcpy(e->emissive, params->emissive, 3 * sizeof(float));
    e->baked_light_mode = params->baked_light_mode;
    e->has_lightmap = params->lightmap_uvs != NULL && params->lightmap != NULL;
    e->owns_arrays = !params->static_geometry;
    e->generation = params->generation;
    draw_entry_capture_viewport(e, context);
    e->depth_prepass_eligible = params->depth_prepass_eligible;
    e->gpu_skinned = params->static_geometry && params->joint_matrices != NULL && params->joint_indices != NULL &&
                     params->joint_weights != NULL && params->joint_count > 0 &&
                     params->joint_count <= SLAYER3D_GPU_SKINNING_MAX_JOINTS;
    if (e->gpu_skinned)
    {
        e->joint_indices = params->joint_indices;
        e->joint_weights = params->joint_weights;
        e->joint_count = params->joint_count;
        e->joint_matrices = (float *)SDL_malloc((size_t)e->joint_count * 16u * sizeof(float));
        if (e->joint_matrices == NULL)
            return SDL_OutOfMemory();
        for (int joint = 0; joint < e->joint_count; ++joint)
            SDL_memcpy(&e->joint_matrices[joint * 16], params->joint_matrices[joint].m, 16u * sizeof(float));
        context->stats.gpu_skinned_draws += 1u;
        context->stats.gpu_skinned_vertices += (Uint64)params->vertex_count;
    }

    if (params->static_geometry)
    {
        e->positions = params->positions;
        e->normals = params->normals;
        e->uvs = params->uvs;
        e->lightmap_uvs = params->lightmap_uvs;
        e->colors = colors;
        e->indices = params->indices;
        draw_entry_compute_bounds(e);
        e->mesh_cache = slayer3d_gl_mesh_cache_lookup_or_create(
            ctx, true, e->primitive_mode, e->positions, e->normals, e->uvs, e->lightmap_uvs, e->colors, e->indices,
            e->joint_indices, e->joint_weights, e->vertex_count, e->index_count, e->generation, e->has_lightmap,
            e->gpu_skinned);
        return e->mesh_cache != NULL;
    }

    e->positions = copy_floats(params->positions, (size_t)params->vertex_count * 3);
    e->normals = copy_floats(params->normals, (size_t)params->vertex_count * 3);
    e->uvs = copy_floats(params->uvs, (size_t)params->vertex_count * 2);
    e->lightmap_uvs = copy_floats(params->lightmap_uvs, (size_t)params->vertex_count * 2);
    e->colors = copy_floats(colors, (size_t)params->vertex_count * 4);
    e->indices = copy_indices(params->indices, (size_t)e->index_count);
    draw_entry_compute_bounds(e);

    /* check_z_fighting(ctx, e); — disabled: triggers on authored model geometry */
    (void)check_z_fighting;

    return true;
}

/* ------------------------------------------------------------------ */
/* Shadow pass                                                         */
/* ------------------------------------------------------------------ */

void slayer3d_gl_begin_shadow_pass(slayer3d_gl_context *ctx, const float *light_vp, float bias)
{
    if (!ctx || !light_vp || !ctx->shadow_fbo)
        return;
    SDL_memcpy(ctx->shadow_light_vp, light_vp, 16 * sizeof(float));
    ctx->shadow_bias = bias;
    ctx->in_shadow_pass = true;

    slayer3d_gl_funcs *gl = &ctx->gl;
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
    gl->Viewport(0, 0, 1024, 1024);
    gl->Clear(GL_DEPTH_BUFFER_BIT);
    gl->Enable(GL_DEPTH_TEST);
    gl->Disable(GL_CULL_FACE);
}

void slayer3d_gl_end_shadow_pass(slayer3d_gl_context *ctx)
{
    if (!ctx)
        return;
    ctx->in_shadow_pass = false;

    slayer3d_gl_funcs *gl = &ctx->gl;
    /* Restore main FBO and normal culling. */
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
    gl->Disable(GL_CULL_FACE);
    gl->CullFace(GL_BACK);
}

/* ------------------------------------------------------------------ */
/* Overlay replay — renders directly to the current framebuffer with  */
/* alpha blending, no depth test, no post-processing.                  */
/* ------------------------------------------------------------------ */

static void replay_overlay_list(slayer3d_gl_context *ctx, int vp_x, int vp_y, int vp_w, int vp_h)
{
    slayer3d_gl_funcs *gl = &ctx->gl;
    if (ctx->overlay_count == 0)
        return;

    gl->Disable(GL_DEPTH_TEST);
    gl->Disable(GL_CULL_FACE);
    gl->Enable(GL_BLEND);
    gl->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (int i = 0; i < ctx->overlay_count; i++)
    {
        slayer3d_overlay_entry *e = &ctx->overlay_list[i];

        if (e->scissor_enabled)
        {
            /* Map logical scissor rect to the letterbox viewport in window pixels. */
            float sx_scale = (float)vp_w / (float)ctx->logical_w;
            float sy_scale = (float)vp_h / (float)ctx->logical_h;
            int sc_x = vp_x + (int)((float)e->scissor_rect.x * sx_scale);
            int sc_y = vp_y + (int)((float)(ctx->logical_h - e->scissor_rect.y - e->scissor_rect.h) * sy_scale);
            int sc_w = (int)((float)e->scissor_rect.w * sx_scale);
            int sc_h = (int)((float)e->scissor_rect.h * sy_scale);
            gl->Enable(GL_SCISSOR_TEST);
            gl->Scissor(sc_x, sc_y, sc_w, sc_h);
        }
        else
        {
            gl->Disable(GL_SCISSOR_TEST);
        }

        slayer3d_custom_shader_cache_entry *custom =
            (e->shader_fragment_source != NULL && e->shader_fragment_source[0] != '\0')
                ? custom_shader_lookup_or_create(ctx, false, e->shader_vertex_source, e->shader_fragment_source)
                : NULL;

        if (custom != NULL)
        {
            gl->UseProgram(custom->program);
            if (custom->mvp_loc >= 0)
                gl->UniformMatrix4fv(custom->mvp_loc, 1, GL_FALSE, e->mvp);
            if (custom->tint_loc >= 0)
                gl->Uniform4f(custom->tint_loc, e->tint[0], e->tint[1], e->tint[2], e->tint[3]);
            if (custom->overlay_effect_loc >= 0)
                gl->Uniform1i(custom->overlay_effect_loc, (int)e->effect);
            if (custom->overlay_effect_progress_loc >= 0)
                gl->Uniform1f(custom->overlay_effect_progress_loc, e->effect_progress);
            if (custom->overlay_effect_seed_loc >= 0)
                gl->Uniform1f(custom->overlay_effect_seed_loc, e->effect_seed);
            if (custom->overlay_effect_columns_loc >= 0)
                gl->Uniform1f(custom->overlay_effect_columns_loc, e->effect_columns);
        }
        else
        {
            gl->UseProgram(ctx->unlit_program);
            gl->UniformMatrix4fv(ctx->unlit_mvp_loc, 1, GL_FALSE, e->mvp);
            gl->Uniform4f(ctx->unlit_tint_loc, e->tint[0], e->tint[1], e->tint[2], e->tint[3]);
            gl->Uniform1i(ctx->unlit_overlay_effect_loc, (int)e->effect);
            gl->Uniform1f(ctx->unlit_overlay_effect_progress_loc, e->effect_progress);
            gl->Uniform1f(ctx->unlit_overlay_effect_seed_loc, e->effect_seed);
            gl->Uniform1f(ctx->unlit_overlay_effect_columns_loc, e->effect_columns);
        }
        if (e->atlas_index >= 0)
        {
            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, ctx->overlay_atlases[e->atlas_index].gl_tex);
            if (custom != NULL)
            {
                if (custom->texture_loc >= 0)
                    gl->Uniform1i(custom->texture_loc, 0);
                if (custom->has_texture_loc >= 0)
                    gl->Uniform1i(custom->has_texture_loc, 1);
            }
            else
            {
                gl->Uniform1i(ctx->unlit_texture_loc, 0);
                gl->Uniform1i(ctx->unlit_has_texture_loc, 1);
            }
        }
        else
        {
            gl->BindTexture(GL_TEXTURE_2D, ctx->white_texture);
            if (custom != NULL)
            {
                if (custom->has_texture_loc >= 0)
                    gl->Uniform1i(custom->has_texture_loc, 0);
            }
            else
            {
                gl->Uniform1i(ctx->unlit_has_texture_loc, 0);
            }
        }

        gl->BindVertexArray(ctx->unlit_vao);
        gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_position_vbo);
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)), e->positions,
                       GL_DYNAMIC_DRAW);
        gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_uv_vbo);
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 2 * sizeof(float)), e->uvs,
                       GL_DYNAMIC_DRAW);

        const float *whites = ensure_white_colors(ctx, e->vertex_count * 4);
        gl->BindBuffer(GL_ARRAY_BUFFER, ctx->unlit_color_vbo);
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 4 * sizeof(float)), whites,
                       GL_DYNAMIC_DRAW);

        gl->DrawArrays(GL_TRIANGLES, 0, e->vertex_count);
    }

    gl->Disable(GL_SCISSOR_TEST);
    gl->Disable(GL_BLEND);
    gl->Enable(GL_DEPTH_TEST);
}

static bool gl_present(slayer3d_render_context *context)
{
    slayer3d_gl_context *ctx = context->gl;
    slayer3d_gl_funcs *gl = &ctx->gl;

    /* Flush UBO before any replay. */
    flush_scene_ubo(ctx);
    prepare_skin_palette_buffer(ctx);

    /* Shadow pass: render original VP into layer 0 (backward compatible),
     * then CSM cascades 1-3 into layers 1-3 for Slice 3. */
    if (ctx->shadow_program && csm_shadow_pass_enabled())
    {
        compute_csm_matrices(ctx, context);

        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
        gl->Viewport(0, 0, 1024, 1024);
        gl->Enable(GL_DEPTH_TEST);
        gl->Disable(GL_CULL_FACE);

        for (int cascade = 0; cascade < SLAYER3D_CSM_CASCADE_COUNT; cascade++)
        {
            gl->FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, ctx->shadow_depth_tex, 0, cascade);
            gl->Clear(GL_DEPTH_BUFFER_BIT);
            gl->UseProgram(ctx->shadow_program);
            gl->UniformMatrix4fv(ctx->shadow_light_vp_loc, 1, GL_FALSE,
                                 (cascade == 0) ? ctx->shadow_light_vp : ctx->csm_light_vp[cascade]);
            replay_draw_list_shadow(ctx);
        }

        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
        gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
        gl->Disable(GL_CULL_FACE);
        gl->CullFace(GL_BACK);
    }

    /* Point shadow pass: 6-face cubemap per light. */
    compute_point_shadow_matrices(ctx, context);
    if (ctx->current_ctx->point_shadows_enabled && ctx->point_shadow_program && ctx->point_shadow_count > 0)
    {
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->point_shadow_fbo);
        gl->Viewport(0, 0, 1024, 1024);
        gl->Enable(GL_DEPTH_TEST);
        gl->Disable(GL_CULL_FACE);
        gl->UseProgram(ctx->point_shadow_program);

        for (int s = 0; s < ctx->point_shadow_count; s++)
        {
            const slayer3d_light *pl = &context->lights[ctx->point_shadow_light_index[s]];
            gl->Uniform3f(ctx->point_shadow_light_pos_loc, pl->position.x, pl->position.y, pl->position.z);
            gl->Uniform1f(ctx->point_shadow_far_loc, ctx->point_shadow_far_plane[s]);

            for (int face = 0; face < 6; face++)
            {
                gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                         GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, ctx->point_shadow_cubemap[s],
                                         0);
                gl->Clear(GL_DEPTH_BUFFER_BIT);
                gl->UniformMatrix4fv(ctx->point_shadow_light_vp_loc, 1, GL_FALSE, ctx->point_shadow_vp[s][face]);
                for (int i = 0; i < ctx->draw_count; i++)
                {
                    slayer3d_draw_entry *e = &ctx->draw_list[i];
                    if (!e->lit)
                        continue;
                    gl->UniformMatrix4fv(ctx->point_shadow_model_loc, 1, GL_FALSE, e->model_matrix);
                    bind_skinning_state(ctx, ctx->point_shadow_use_skinning_loc, ctx->point_shadow_use_skin_palette_loc,
                                        ctx->point_shadow_joint_palette_offset_loc,
                                        ctx->point_shadow_joint_matrices_loc, e);
                    gl->BindVertexArray(e->mesh_cache ? e->mesh_cache->shadow_vao : ctx->shadow_vao);
                    if (e->mesh_cache == NULL)
                    {
                        gl->BindBuffer(GL_ARRAY_BUFFER, ctx->shadow_position_vbo);
                        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)),
                                       e->positions, GL_DYNAMIC_DRAW);
                    }
                    if (e->indices && e->index_count > 0)
                    {
                        if (e->mesh_cache == NULL)
                        {
                            gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->shadow_ebo);
                            gl->BufferData(GL_ELEMENT_ARRAY_BUFFER,
                                           (GLsizeiptr)((size_t)e->index_count * sizeof(unsigned int)), e->indices,
                                           GL_DYNAMIC_DRAW);
                        }
                        gl->DrawElements(GL_TRIANGLES, e->index_count, GL_UNSIGNED_INT, NULL);
                    }
                    else
                    {
                        gl->DrawArrays(GL_TRIANGLES, 0, e->vertex_count);
                    }
                }
            }
        }
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
        gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
        gl->Enable(GL_CULL_FACE);
        gl->CullFace(GL_BACK);
    }

    /* Geometry pass: replay all entries into main FBO using the caller's
     * configured backface-culling state. Post-process passes disable culling,
     * so this must be restored explicitly every frame. */
    replay_draw_list_depth_prepass(ctx);
    apply_geometry_cull_state(ctx);
    replay_draw_list_geometry_measured(ctx);

    /* ---- Post-process pipeline ---- */
    gl->Disable(GL_DEPTH_TEST);
    gl->Disable(GL_CULL_FACE);
    gl->BindVertexArray(ctx->fullscreen_vao);

    /* SSAO pass: darken pixels where nearby depth samples indicate occlusion.
     * Reads fbo_color + fbo_depth, writes to pp_fbo_a, then copies back. */
    if (context->ssao_enabled)
    {
        float near_p = ctx->current_ctx->near_plane;
        float far_p = ctx->current_ctx->far_plane;
        if (near_p > 0.0f && far_p > near_p)
        {
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->pp_fbo_a);
            gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
            gl->Clear(GL_COLOR_BUFFER_BIT);
            gl->UseProgram(ctx->ssao_program);
            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
            gl->Uniform1i(ctx->ssao_scene_loc, 0);
            gl->ActiveTexture(GL_TEXTURE0 + 1);
            gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_depth);
            gl->Uniform1i(ctx->ssao_depth_loc, 1);
            gl->Uniform2f(ctx->ssao_texel_size_loc, 1.0f / (float)ctx->world_w, 1.0f / (float)ctx->world_h);
            gl->Uniform1f(ctx->ssao_near_loc, near_p);
            gl->Uniform1f(ctx->ssao_far_loc, far_p);
            gl->DrawArrays(GL_TRIANGLES, 0, 3);
            gl->ActiveTexture(GL_TEXTURE0);

            /* Copy SSAO result back to fbo_color. */
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
            gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
            gl->UseProgram(ctx->copy_program);
            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, ctx->pp_tex_a);
            gl->Uniform1i(ctx->copy_texture_loc, 0);
            gl->DrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

    /* Step 1: Extract bright pixels from main FBO into pp_tex_a. */
    if (context->bloom_enabled)
    {
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->pp_fbo_a);
        gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
        gl->Clear(GL_COLOR_BUFFER_BIT);
        gl->UseProgram(ctx->bloom_program);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
        gl->Uniform1i(ctx->bloom_scene_loc, 0);
        gl->Uniform1f(ctx->bloom_threshold_loc, 1.2f);
        gl->DrawArrays(GL_TRIANGLES, 0, 3);

        /* Step 2: Blur bright pixels (ping-pong, 10 passes = 5 iterations). */
        gl->UseProgram(ctx->bloom_blur_program);
        for (int i = 0; i < 6; i++)
        {
            bool horizontal = (i % 2 == 0);
            gl->BindFramebuffer(GL_FRAMEBUFFER, horizontal ? ctx->pp_fbo_b : ctx->pp_fbo_a);
            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, horizontal ? ctx->pp_tex_a : ctx->pp_tex_b);
            gl->Uniform1i(ctx->blur_image_loc, 0);
            gl->Uniform1i(ctx->blur_horizontal_loc, horizontal ? 1 : 0);
            gl->DrawArrays(GL_TRIANGLES, 0, 3);
        }
        /* After 10 passes (even count), last write was to pp_fbo_b with horizontal=false reading pp_tex_b...
         * Actually: i=9 -> horizontal=false -> writes to pp_fbo_a, reads pp_tex_b.
         * So final blurred result is in pp_tex_a. */

        /* Step 3: Copy fbo_color to pp_tex_b to avoid read-write conflict. */
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->pp_fbo_b);
        gl->UseProgram(ctx->copy_program);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
        gl->Uniform1i(ctx->copy_texture_loc, 0);
        gl->DrawArrays(GL_TRIANGLES, 0, 3);

        /* Step 4: Composite (bloom + vignette + grading) into main FBO.
         * Read scene from pp_tex_b, bloom from pp_tex_a, write to fbo. */
        gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
        gl->UseProgram(ctx->composite_program);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->BindTexture(GL_TEXTURE_2D, ctx->pp_tex_b);
        gl->ActiveTexture(GL_TEXTURE0 + 1);
        gl->BindTexture(GL_TEXTURE_2D, ctx->pp_tex_a);
        gl->Uniform1i(ctx->comp_scene_loc, 0);
        gl->Uniform1i(ctx->comp_bloom_loc, 1);
        gl->Uniform1f(ctx->comp_vignette_loc, 0.4f);
        gl->Uniform1f(ctx->comp_contrast_loc, 1.1f);
        gl->Uniform1f(ctx->comp_saturation_loc, 1.15f);
        gl->DrawArrays(GL_TRIANGLES, 0, 3);
        gl->ActiveTexture(GL_TEXTURE0);
    } /* bloom_enabled */

    slayer3d_gl_apply_transition_pass(ctx);

    gl->Disable(GL_CULL_FACE);
    gl->Enable(GL_DEPTH_TEST);

    /* Compute letterbox viewport. */
    int win_w, win_h;
    SDL_GetWindowSizeInPixels(ctx->window, &win_w, &win_h);

    float scale_x = (float)win_w / (float)ctx->logical_w;
    float scale_y = (float)win_h / (float)ctx->logical_h;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    int vp_w = (int)((float)ctx->logical_w * scale);
    int vp_h = (int)((float)ctx->logical_h * scale);
    int vp_x = (win_w - vp_w) / 2;
    int vp_y = (win_h - vp_h) / 2;

    /* Bind default framebuffer, clear to black, set letterbox viewport. */
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl->Clear(GL_COLOR_BUFFER_BIT);
    gl->Viewport(vp_x, vp_y, vp_w, vp_h);

    /* Draw fullscreen triangle sampling the FBO color attachment. Retro
     * presentation profiles are applied here so the final scene, including
     * bloom/SSAO/transitions, receives the display treatment while the UI
     * overlay remains crisp and readable afterward. */
    gl->Disable(GL_DEPTH_TEST);
    gl->Disable(GL_CULL_FACE);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
    {
        const GLint profile_filter =
            ctx->active_retro_filter == (int)SLAYER3D_DISPLAY_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR;
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, profile_filter);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, profile_filter);
    }
    gl->BindVertexArray(ctx->fullscreen_vao);
    if (ctx->active_retro_profile > 0)
    {
        gl->UseProgram(ctx->retro_program);
        gl->Uniform1i(ctx->retro_scene_loc, 0);
        gl->Uniform1i(ctx->retro_profile_loc, ctx->active_retro_profile);
        gl->Uniform2f(ctx->retro_virtual_resolution_loc, (float)ctx->active_retro_virtual_w,
                      (float)ctx->active_retro_virtual_h);
        gl->Uniform2f(ctx->retro_output_resolution_loc, (float)vp_w, (float)vp_h);
    }
    else
    {
        gl->UseProgram(ctx->copy_program);
        gl->Uniform1i(ctx->copy_texture_loc, 0);
    }
    gl->DrawArrays(GL_TRIANGLES, 0, 3);
    gl->Disable(GL_CULL_FACE);
    gl->Enable(GL_DEPTH_TEST);

    /* Overlay pass: UI text rendered directly to the default framebuffer,
     * after the FBO blit, bypassing all post-processing. */
    replay_overlay_list(ctx, vp_x, vp_y, vp_w, vp_h);
    slayer3d_gl_free_overlay_list(ctx);

    SDL_GL_SwapWindow(ctx->window);
    return true;
}

/* ------------------------------------------------------------------ */
/* Lifecycle adapter                                                   */
/* ------------------------------------------------------------------ */

static void gl_destroy_adapter(slayer3d_render_context *context)
{
    slayer3d_gl_destroy(context->gl);
    context->gl = NULL;
}

/* ------------------------------------------------------------------ */
/* Backend interface initializer                                       */
/* ------------------------------------------------------------------ */

void slayer3d_gl_read_pixel(slayer3d_gl_context *ctx, int x, int y, unsigned char *rgba)
{
    if (ctx == NULL || rgba == NULL)
    {
        return;
    }
    /* Flush any pending draw list so pixels are up to date. */
    if (ctx->draw_count > 0)
    {
        slayer3d_gl_funcs *gl = &ctx->gl;
        flush_scene_ubo(ctx);
        prepare_skin_palette_buffer(ctx);
        if (ctx->shadow_program && csm_shadow_pass_enabled())
        {
            compute_csm_matrices(ctx, ctx->current_ctx);

            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
            gl->Viewport(0, 0, 1024, 1024);
            gl->Enable(GL_DEPTH_TEST);
            gl->Disable(GL_CULL_FACE);

            for (int cascade = 0; cascade < SLAYER3D_CSM_CASCADE_COUNT; cascade++)
            {
                gl->FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, ctx->shadow_depth_tex, 0, cascade);
                gl->Clear(GL_DEPTH_BUFFER_BIT);
                gl->UseProgram(ctx->shadow_program);
                gl->UniformMatrix4fv(ctx->shadow_light_vp_loc, 1, GL_FALSE,
                                     (cascade == 0) ? ctx->shadow_light_vp : ctx->csm_light_vp[cascade]);
                replay_draw_list_shadow(ctx);
            }

            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
            gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
            gl->Disable(GL_CULL_FACE);
            gl->CullFace(GL_BACK);
        }
        compute_point_shadow_matrices(ctx, ctx->current_ctx);
        if (ctx->current_ctx->point_shadows_enabled && ctx->point_shadow_program && ctx->point_shadow_count > 0)
        {
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->point_shadow_fbo);
            gl->Viewport(0, 0, 1024, 1024);
            gl->Enable(GL_DEPTH_TEST);
            gl->Disable(GL_CULL_FACE);
            gl->UseProgram(ctx->point_shadow_program);
            for (int s = 0; s < ctx->point_shadow_count; s++)
            {
                const slayer3d_light *pl = &ctx->current_ctx->lights[ctx->point_shadow_light_index[s]];
                gl->Uniform3f(ctx->point_shadow_light_pos_loc, pl->position.x, pl->position.y, pl->position.z);
                gl->Uniform1f(ctx->point_shadow_far_loc, ctx->point_shadow_far_plane[s]);
                for (int face = 0; face < 6; face++)
                {
                    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                             GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face,
                                             ctx->point_shadow_cubemap[s], 0);
                    gl->Clear(GL_DEPTH_BUFFER_BIT);
                    gl->UniformMatrix4fv(ctx->point_shadow_light_vp_loc, 1, GL_FALSE, ctx->point_shadow_vp[s][face]);
                    for (int i = 0; i < ctx->draw_count; i++)
                    {
                        slayer3d_draw_entry *e = &ctx->draw_list[i];
                        if (!e->lit)
                            continue;
                        if (e->model_matrix[12] == 0.0f && e->model_matrix[13] == 0.0f && e->model_matrix[14] == 0.0f &&
                            e->model_matrix[0] == 1.0f && e->model_matrix[5] == 1.0f && e->model_matrix[10] == 1.0f)
                            continue;
                        gl->UniformMatrix4fv(ctx->point_shadow_model_loc, 1, GL_FALSE, e->model_matrix);
                        bind_skinning_state(
                            ctx, ctx->point_shadow_use_skinning_loc, ctx->point_shadow_use_skin_palette_loc,
                            ctx->point_shadow_joint_palette_offset_loc, ctx->point_shadow_joint_matrices_loc, e);
                        gl->BindVertexArray(e->mesh_cache ? e->mesh_cache->shadow_vao : ctx->shadow_vao);
                        if (e->mesh_cache == NULL)
                        {
                            gl->BindBuffer(GL_ARRAY_BUFFER, ctx->shadow_position_vbo);
                            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)e->vertex_count * 3 * sizeof(float)),
                                           e->positions, GL_DYNAMIC_DRAW);
                        }
                        if (e->indices && e->index_count > 0)
                        {
                            if (e->mesh_cache == NULL)
                            {
                                gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->shadow_ebo);
                                gl->BufferData(GL_ELEMENT_ARRAY_BUFFER,
                                               (GLsizeiptr)((size_t)e->index_count * sizeof(unsigned int)), e->indices,
                                               GL_DYNAMIC_DRAW);
                            }
                            gl->DrawElements(GL_TRIANGLES, e->index_count, GL_UNSIGNED_INT, NULL);
                        }
                        else
                        {
                            gl->DrawArrays(GL_TRIANGLES, 0, e->vertex_count);
                        }
                    }
                }
            }
            gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
            gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
            gl->Disable(GL_CULL_FACE);
            gl->CullFace(GL_BACK);
        }
        replay_draw_list_depth_prepass(ctx);
        apply_geometry_cull_state(ctx);
        replay_draw_list_geometry_measured(ctx);
    }
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
    SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);
    ctx->gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    ctx->gl.ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void slayer3d_gl_backend_init(slayer3d_backend_interface *iface)
{
    iface->destroy = gl_destroy_adapter;
    iface->clear = gl_clear;
    iface->present = gl_present;
    iface->draw_mesh_unlit = gl_draw_mesh_unlit;
    iface->draw_mesh_lit = gl_draw_mesh_lit;
}
