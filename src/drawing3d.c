#include "slayer3d/drawing3d.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "rasterizer.h"
#include "render_context_internal.h"
#include "texture_internal.h"

#include "gl_renderer.h"
#include "lighting_internal.h"
#include "slayer3d/animation.h"
#include "slayer3d/level.h"

static const int SLAYER3D_MODEL_STACK_INITIAL_CAPACITY = 8;

static float slayer3d_clamp01(float value)
{
    if (value <= 0.0f)
    {
        return 0.0f;
    }
    if (value >= 1.0f)
    {
        return 1.0f;
    }
    return value;
}

static Uint8 slayer3d_color_channel_clamp(float value)
{
    if (value <= 0.0f)
    {
        return 0;
    }
    if (value >= 255.0f)
    {
        return 255;
    }
    return (Uint8)(value + 0.5f);
}

static float slayer3d_overlay_melt_column_random(Uint32 seed, int column)
{
    Uint32 x = seed ^ (Uint32)column * 0x9E3779B9u;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (float)x / 4294967295.0f;
}

static bool slayer3d_overlay_melt_sample(float u, float v, float progress, Uint32 seed, int columns, float *out_u,
                                         float *out_v)
{
    if (out_u == NULL || out_v == NULL)
    {
        return false;
    }
    if (columns <= 0)
    {
        columns = 1;
    }

    const int column = SDL_clamp((int)(u * (float)columns), 0, columns - 1);
    const float rnd = slayer3d_overlay_melt_column_random(seed, column);
    const float delay = rnd * 0.45f;
    const float melt = slayer3d_clamp01((progress - delay) / 0.55f);
    const float wobble = (rnd - 0.5f) * 0.025f * melt;
    const float y_shift = melt * (1.05f + rnd * 0.6f);

    *out_u = u + wobble;
    *out_v = v - y_shift;
    return true;
}

static slayer3d_vec4 slayer3d_color_to_modulate(slayer3d_color color)
{
    slayer3d_vec4 out;
    out.x = (float)color.r / 255.0f;
    out.y = (float)color.g / 255.0f;
    out.z = (float)color.b / 255.0f;
    out.w = (float)color.a / 255.0f;
    return out;
}

static bool slayer3d_overlay_capture_scissor(const slayer3d_render_context *context, bool *out_enabled,
                                             SDL_Rect *out_rect)
{
    if (!out_enabled || !out_rect)
    {
        return SDL_InvalidParamError("out_enabled");
    }

    *out_enabled = slayer3d_is_scissor_enabled(context);
    if (*out_enabled)
    {
        return slayer3d_get_scissor_rect(context, out_rect);
    }

    *out_rect = (SDL_Rect){0, 0, 0, 0};
    return true;
}

static SDL_Rect slayer3d_rect_overlay_intersect_scissor(SDL_Rect rect, bool scissor_enabled,
                                                        const SDL_Rect *scissor_rect)
{
    if (!scissor_enabled || !scissor_rect)
    {
        return rect;
    }

    Sint64 x0 = rect.x > scissor_rect->x ? rect.x : scissor_rect->x;
    Sint64 y0 = rect.y > scissor_rect->y ? rect.y : scissor_rect->y;
    Sint64 x1 = ((Sint64)rect.x + rect.w) < ((Sint64)scissor_rect->x + scissor_rect->w)
                    ? ((Sint64)rect.x + rect.w)
                    : ((Sint64)scissor_rect->x + scissor_rect->w);
    Sint64 y1 = ((Sint64)rect.y + rect.h) < ((Sint64)scissor_rect->y + scissor_rect->h)
                    ? ((Sint64)rect.y + rect.h)
                    : ((Sint64)scissor_rect->y + scissor_rect->h);
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;
    rect.x = (int)x0;
    rect.y = (int)y0;
    rect.w = (int)(x1 - x0);
    rect.h = (int)(y1 - y0);
    return rect;
}

static bool slayer3d_ensure_model_stack_capacity(slayer3d_render_context *context, int required_depth)
{
    if (context->model_stack_capacity >= required_depth)
    {
        return true;
    }

    int new_capacity = context->model_stack_capacity;
    if (new_capacity <= 0)
    {
        new_capacity = SLAYER3D_MODEL_STACK_INITIAL_CAPACITY;
    }

    while (new_capacity < required_depth)
    {
        new_capacity *= 2;
    }

    slayer3d_mat4 *new_stack =
        (slayer3d_mat4 *)SDL_realloc(context->model_stack, (size_t)new_capacity * sizeof(*new_stack));
    if (new_stack == NULL)
    {
        return SDL_OutOfMemory();
    }

    context->model_stack = new_stack;
    context->model_stack_capacity = new_capacity;
    return true;
}

/* Derive the 6 frustum planes (a*x + b*y + c*z + d >= 0 for inside) from
 * a row-major MVP. Caller writes them into context->frustum_planes and
 * sets context->frustum_planes_valid. Used by both the camera frustum
 * (begin_mode_3d) and the shadow-light frustum (begin_shadow_pass) so
 * actor and mesh culling work in shadow passes too. */
void slayer3d_internal_extract_frustum_planes(slayer3d_render_context *context, slayer3d_mat4 view_projection)
{
    const float *m = view_projection.m;
    float raw[6][4] = {
        {m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]},
        {m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]},
        {m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]},
        {m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]},
        {m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]},
        {m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]},
    };
    for (int i = 0; i < 6; ++i)
    {
        float len = SDL_sqrtf(raw[i][0] * raw[i][0] + raw[i][1] * raw[i][1] + raw[i][2] * raw[i][2]);
        if (len > 0.000001f)
        {
            context->frustum_planes[i][0] = raw[i][0] / len;
            context->frustum_planes[i][1] = raw[i][1] / len;
            context->frustum_planes[i][2] = raw[i][2] / len;
            context->frustum_planes[i][3] = raw[i][3] / len;
        }
        else
        {
            context->frustum_planes[i][0] = 0.0f;
            context->frustum_planes[i][1] = 0.0f;
            context->frustum_planes[i][2] = 0.0f;
            context->frustum_planes[i][3] = raw[i][3];
        }
    }
    context->frustum_planes_valid = true;
}

static void slayer3d_update_current_model_matrices(slayer3d_render_context *context)
{
    if (context->model_stack_depth <= 0)
    {
        context->model = slayer3d_mat4_identity();
    }
    else
    {
        context->model = context->model_stack[context->model_stack_depth - 1];
    }

    context->model_view_projection = slayer3d_mat4_multiply(context->view_projection, context->model);
}

bool slayer3d_begin_mode_3d(slayer3d_render_context *context, slayer3d_camera3d camera)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (context->in_mode_3d)
    {
        return SDL_SetError("slayer3d_begin_mode_3d called while already in 3D mode.");
    }

    const int viewport_width = context->viewport_enabled ? context->viewport_rect.w : context->width;
    const int viewport_height = context->viewport_enabled ? context->viewport_rect.h : context->height;
    if (!slayer3d_camera3d_compute_matrices(&camera, viewport_width, viewport_height, context->near_plane,
                                            context->far_plane, &context->view, &context->projection))
    {
        return false;
    }

    context->view_projection = slayer3d_mat4_multiply(context->projection, context->view);
    if (!slayer3d_ensure_model_stack_capacity(context, 1))
    {
        return false;
    }

    context->model_stack_depth = 1;
    context->model_stack[0] = slayer3d_mat4_identity();
    slayer3d_update_current_model_matrices(context);

    /* Cache frustum planes once per frame so per-mesh culling is cheap. */
    slayer3d_internal_extract_frustum_planes(context, context->view_projection);

    context->in_mode_3d = true;
    return true;
}

bool slayer3d_end_mode_3d(slayer3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (!context->in_mode_3d)
    {
        return SDL_SetError("slayer3d_end_mode_3d called while not in 3D mode.");
    }

    context->in_mode_3d = false;
    context->frustum_planes_valid = false;
    context->model_stack_depth = 0;
    context->model = slayer3d_mat4_identity();
    context->model_view_projection = context->view_projection;
    return true;
}

bool slayer3d_is_in_mode_3d(const slayer3d_render_context *context)
{
    if (context == NULL)
    {
        return false;
    }
    return context->in_mode_3d;
}

bool slayer3d_set_backface_culling_enabled(slayer3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->backface_culling_enabled = enabled;
    return true;
}

bool slayer3d_is_backface_culling_enabled(const slayer3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return false;
    }

    return context->backface_culling_enabled;
}

bool slayer3d_set_wireframe_enabled(slayer3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->wireframe_enabled = enabled;
    return true;
}

bool slayer3d_is_wireframe_enabled(const slayer3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return false;
    }

    return context->wireframe_enabled;
}

bool slayer3d_set_depth_planes(slayer3d_render_context *context, float near_plane, float far_plane)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (context->in_mode_3d)
    {
        return SDL_SetError("Depth planes cannot be changed while in 3D mode.");
    }

    if (!(near_plane > 0.0f) || !(far_plane > near_plane))
    {
        return SDL_SetError("Depth planes require 0 < near_plane < far_plane.");
    }

    context->near_plane = near_plane;
    context->far_plane = far_plane;
    return true;
}

static bool slayer3d_require_mode_3d(const slayer3d_render_context *context, const char *function_name)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (!context->in_mode_3d)
    {
        return SDL_SetError("%s called outside slayer3d_begin_mode_3d / slayer3d_end_mode_3d.", function_name);
    }
    return true;
}

static bool slayer3d_validate_texture_for_draw(const slayer3d_texture2d *texture)
{
    if (texture == NULL)
    {
        return true;
    }
    if (texture->pixels == NULL || texture->width <= 0 || texture->height <= 0)
    {
        return SDL_SetError("Texture passed to draw call is empty.");
    }
    return true;
}

static slayer3d_vec4 slayer3d_mesh_vertex_modulate(const slayer3d_mesh *mesh, unsigned int vertex_index,
                                                   slayer3d_vec4 base)
{
    slayer3d_vec4 out = base;

    if (mesh->colors != NULL)
    {
        const float *color = &mesh->colors[(size_t)vertex_index * 4U];
        out.x = slayer3d_clamp01(base.x * color[0]);
        out.y = slayer3d_clamp01(base.y * color[1]);
        out.z = slayer3d_clamp01(base.z * color[2]);
        out.w = slayer3d_clamp01(base.w * color[3]);
    }

    return out;
}

static slayer3d_vec3 slayer3d_mesh_position(const slayer3d_mesh *mesh, unsigned int vertex_index)
{
    const float *position = &mesh->positions[(size_t)vertex_index * 3U];
    return slayer3d_vec3_make(position[0], position[1], position[2]);
}

static slayer3d_vec2 slayer3d_mesh_uv(const slayer3d_mesh *mesh, unsigned int vertex_index)
{
    const float *uv = &mesh->uvs[(size_t)vertex_index * 2U];
    slayer3d_vec2 out;
    out.x = uv[0];
    out.y = uv[1];
    return out;
}

static slayer3d_vec3 slayer3d_mesh_normal(const slayer3d_mesh *mesh, unsigned int vertex_index)
{
    const float *n = &mesh->normals[(size_t)vertex_index * 3U];
    return slayer3d_vec3_make(n[0], n[1], n[2]);
}

/* Apply vertex skinning: transform a position or normal by the weighted
 * sum of up to 4 joint matrices. */
static slayer3d_vec3 slayer3d_skin_position(const slayer3d_mesh *mesh, unsigned int vi,
                                            const slayer3d_mat4 *joint_matrices, slayer3d_vec3 pos)
{
    const unsigned short *ji = &mesh->joint_indices[vi * 4];
    const float *jw = &mesh->joint_weights[vi * 4];
    float rx = 0, ry = 0, rz = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (jw[i] <= 0.0f)
        {
            continue;
        }
        slayer3d_vec4 tp = slayer3d_mat4_transform_vec4(joint_matrices[ji[i]], slayer3d_vec4_from_vec3(pos, 1.0f));
        rx += tp.x * jw[i];
        ry += tp.y * jw[i];
        rz += tp.z * jw[i];
    }
    return slayer3d_vec3_make(rx, ry, rz);
}

static slayer3d_vec3 slayer3d_skin_normal(const slayer3d_mesh *mesh, unsigned int vi,
                                          const slayer3d_mat4 *joint_matrices, slayer3d_vec3 n)
{
    const unsigned short *ji = &mesh->joint_indices[vi * 4];
    const float *jw = &mesh->joint_weights[vi * 4];
    float rx = 0, ry = 0, rz = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (jw[i] <= 0.0f)
        {
            continue;
        }
        const slayer3d_mat4 *m = &joint_matrices[ji[i]];
        rx += (m->m[0] * n.x + m->m[4] * n.y + m->m[8] * n.z) * jw[i];
        ry += (m->m[1] * n.x + m->m[5] * n.y + m->m[9] * n.z) * jw[i];
        rz += (m->m[2] * n.x + m->m[6] * n.y + m->m[10] * n.z) * jw[i];
    }
    return slayer3d_vec3_normalize(slayer3d_vec3_make(rx, ry, rz));
}

/* Build lighting params from render context state. Material fields
 * (metallic, roughness, emissive) are set by the caller. */
static void slayer3d_build_lighting_params(const slayer3d_render_context *context, slayer3d_lighting_params *lp)
{
    SDL_zerop(lp);
    lp->lights = context->lights;
    lp->light_count = context->light_count;
    lp->ambient[0] = context->ambient[0];
    lp->ambient[1] = context->ambient[1];
    lp->ambient[2] = context->ambient[2];
    lp->emissive[0] = context->emissive[0];
    lp->emissive[1] = context->emissive[1];
    lp->emissive[2] = context->emissive[2];
    lp->fog = context->fog;
    lp->tonemap_mode = context->tonemap_mode;
    lp->shadow_bias = context->shadow_bias;
    for (int i = 0; i < SLAYER3D_MAX_LIGHTS; ++i)
    {
        lp->shadow_depth[i] = context->shadow_depth[i];
        lp->shadow_vp[i] = context->shadow_vp[i];
        lp->shadow_enabled[i] = context->shadow_enabled[i];
    }
    {
        const slayer3d_mat4 v = context->view;
        lp->camera_pos.x = -(v.m[0] * v.m[12] + v.m[1] * v.m[13] + v.m[2] * v.m[14]);
        lp->camera_pos.y = -(v.m[4] * v.m[12] + v.m[5] * v.m[13] + v.m[6] * v.m[14]);
        lp->camera_pos.z = -(v.m[8] * v.m[12] + v.m[9] * v.m[13] + v.m[10] * v.m[14]);
    }
    lp->uv_mode = context->uv_mode;
    lp->fog_eval = context->fog_eval;
    lp->vertex_snap = context->vertex_snap;
    lp->vertex_snap_precision = context->vertex_snap_precision;
    lp->color_quantize = context->color_quantize;
    lp->color_depth = context->color_depth;
}

/* Transform an object-space normal to world space via the model matrix upper-left 3x3. */
static slayer3d_vec3 slayer3d_transform_normal(slayer3d_mat4 m, slayer3d_vec3 n)
{
    slayer3d_vec3 out;
    out.x = m.m[0] * n.x + m.m[4] * n.y + m.m[8] * n.z;
    out.y = m.m[1] * n.x + m.m[5] * n.y + m.m[9] * n.z;
    out.z = m.m[2] * n.x + m.m[6] * n.y + m.m[10] * n.z;
    return slayer3d_vec3_normalize(out);
}

/* Transform an object-space position to world space. */
static slayer3d_vec3 slayer3d_transform_position(slayer3d_mat4 m, slayer3d_vec3 p)
{
    slayer3d_vec4 w = slayer3d_mat4_transform_vec4(m, slayer3d_vec4_from_vec3(p, 1.0f));
    return slayer3d_vec3_make(w.x, w.y, w.z);
}

typedef struct
{
    float a;
    float b;
    float c;
    float d;
} slayer3d_frustum_plane;

static slayer3d_bounding_box slayer3d_transform_bounding_box(slayer3d_mat4 transform,
                                                             slayer3d_bounding_box local_bounds)
{
    const slayer3d_vec3 corners[8] = {
        slayer3d_vec3_make(local_bounds.min.x, local_bounds.min.y, local_bounds.min.z),
        slayer3d_vec3_make(local_bounds.min.x, local_bounds.min.y, local_bounds.max.z),
        slayer3d_vec3_make(local_bounds.min.x, local_bounds.max.y, local_bounds.min.z),
        slayer3d_vec3_make(local_bounds.min.x, local_bounds.max.y, local_bounds.max.z),
        slayer3d_vec3_make(local_bounds.max.x, local_bounds.min.y, local_bounds.min.z),
        slayer3d_vec3_make(local_bounds.max.x, local_bounds.min.y, local_bounds.max.z),
        slayer3d_vec3_make(local_bounds.max.x, local_bounds.max.y, local_bounds.min.z),
        slayer3d_vec3_make(local_bounds.max.x, local_bounds.max.y, local_bounds.max.z),
    };
    slayer3d_bounding_box world_bounds;
    slayer3d_vec3 p = slayer3d_transform_position(transform, corners[0]);
    world_bounds.min = p;
    world_bounds.max = p;

    for (int i = 1; i < 8; ++i)
    {
        p = slayer3d_transform_position(transform, corners[i]);
        if (p.x < world_bounds.min.x)
            world_bounds.min.x = p.x;
        if (p.y < world_bounds.min.y)
            world_bounds.min.y = p.y;
        if (p.z < world_bounds.min.z)
            world_bounds.min.z = p.z;
        if (p.x > world_bounds.max.x)
            world_bounds.max.x = p.x;
        if (p.y > world_bounds.max.y)
            world_bounds.max.y = p.y;
        if (p.z > world_bounds.max.z)
            world_bounds.max.z = p.z;
    }

    return world_bounds;
}

static bool slayer3d_box_intersects_frustum(slayer3d_bounding_box bounds, const slayer3d_frustum_plane planes[6])
{
    for (int i = 0; i < 6; ++i)
    {
        const slayer3d_frustum_plane plane = planes[i];
        const float px = plane.a >= 0.0f ? bounds.max.x : bounds.min.x;
        const float py = plane.b >= 0.0f ? bounds.max.y : bounds.min.y;
        const float pz = plane.c >= 0.0f ? bounds.max.z : bounds.min.z;
        const float distance = plane.a * px + plane.b * py + plane.c * pz + plane.d;
        if (distance < 0.0f)
        {
            return false;
        }
    }
    return true;
}

static bool slayer3d_mesh_is_visible(const slayer3d_render_context *context, const slayer3d_mesh *mesh)
{
    slayer3d_bounding_box world_bounds;

    if (context == NULL || mesh == NULL || !mesh->has_local_bounds)
    {
        return true;
    }

    if (!context->frustum_planes_valid)
    {
        return true;
    }

    slayer3d_frustum_plane planes[6];
    for (int i = 0; i < 6; ++i)
    {
        planes[i].a = context->frustum_planes[i][0];
        planes[i].b = context->frustum_planes[i][1];
        planes[i].c = context->frustum_planes[i][2];
        planes[i].d = context->frustum_planes[i][3];
    }

    world_bounds = slayer3d_transform_bounding_box(context->model, mesh->local_bounds);
    return slayer3d_box_intersects_frustum(world_bounds, planes);
}

/* Evaluate PBR shading at a single point and return the result as an slayer3d_color.
 * Used by FLAT (once per triangle) and GOURAUD (once per vertex). */
static slayer3d_color slayer3d_shade_point(const slayer3d_lighting_params *lp, float albedo_r, float albedo_g,
                                           float albedo_b, float albedo_a, slayer3d_vec3 world_normal,
                                           slayer3d_vec3 world_pos)
{
    float lit_r, lit_g, lit_b;
    slayer3d_color c;
    slayer3d_shade_fragment_pbr(lp, albedo_r, albedo_g, albedo_b, world_normal.x, world_normal.y, world_normal.z,
                                world_pos.x, world_pos.y, world_pos.z, &lit_r, &lit_g, &lit_b);

    /* Fog in linear space, then tonemap+gamma. */
    if (lp->tonemap_mode != SLAYER3D_TONEMAP_NONE)
    {
        if (lp->fog.mode != SLAYER3D_FOG_NONE)
        {
            float dx = world_pos.x - lp->camera_pos.x;
            float dy = world_pos.y - lp->camera_pos.y;
            float dz = world_pos.z - lp->camera_pos.z;
            float dist = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
            float fog_f = slayer3d_compute_fog_factor(&lp->fog, dist);
            lit_r = lit_r * (1.0f - fog_f) + lp->fog.color[0] * fog_f;
            lit_g = lit_g * (1.0f - fog_f) + lp->fog.color[1] * fog_f;
            lit_b = lit_b * (1.0f - fog_f) + lp->fog.color[2] * fog_f;
        }
        slayer3d_tonemap(lp->tonemap_mode, &lit_r, &lit_g, &lit_b);
    }
    else
    {
        slayer3d_tonemap(SLAYER3D_TONEMAP_NONE, &lit_r, &lit_g, &lit_b);
        if (lp->fog.mode != SLAYER3D_FOG_NONE)
        {
            float dx = world_pos.x - lp->camera_pos.x;
            float dy = world_pos.y - lp->camera_pos.y;
            float dz = world_pos.z - lp->camera_pos.z;
            float dist = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
            float fog_f = slayer3d_compute_fog_factor(&lp->fog, dist);
            lit_r = lit_r * (1.0f - fog_f) + lp->fog.color[0] * fog_f;
            lit_g = lit_g * (1.0f - fog_f) + lp->fog.color[1] * fog_f;
            lit_b = lit_b * (1.0f - fog_f) + lp->fog.color[2] * fog_f;
        }
    }

    c.r = slayer3d_color_channel_clamp(lit_r * 255.0f);
    c.g = slayer3d_color_channel_clamp(lit_g * 255.0f);
    c.b = slayer3d_color_channel_clamp(lit_b * 255.0f);
    c.a = slayer3d_color_channel_clamp(albedo_a * 255.0f);
    return c;
}

static slayer3d_vec4 slayer3d_shade_point_retro(const slayer3d_lighting_params *lp, slayer3d_vec4 base_color,
                                                slayer3d_vec3 world_normal, slayer3d_vec3 world_pos)
{
    slayer3d_vec3 n = slayer3d_vec3_normalize(world_normal);
    float lit_r = lp->ambient[0];
    float lit_g = lp->ambient[1];
    float lit_b = lp->ambient[2];
    slayer3d_vec4 out;

    for (int i = 0; i < lp->light_count; ++i)
    {
        const slayer3d_light *light = &lp->lights[i];
        slayer3d_vec3 l;
        float attenuation = 1.0f;

        if (light->type == SLAYER3D_LIGHT_DIRECTIONAL)
        {
            l = slayer3d_vec3_normalize(slayer3d_vec3_scale(light->direction, -1.0f));
        }
        else
        {
            slayer3d_vec3 to_light = slayer3d_vec3_sub(light->position, world_pos);
            float dist = slayer3d_vec3_length(to_light);
            if (dist <= 1e-6f)
            {
                continue;
            }
            l = slayer3d_vec3_scale(to_light, 1.0f / dist);

            if (light->range > 0.0f)
            {
                float ratio = dist / light->range;
                attenuation = SDL_expf(-2.8f * ratio * ratio);
            }
            else
            {
                attenuation = 1.0f / (dist * dist + 1e-4f);
            }

            if (light->type == SLAYER3D_LIGHT_SPOT)
            {
                slayer3d_vec3 spot_dir = slayer3d_vec3_normalize(light->direction);
                float cos_angle = slayer3d_vec3_dot(slayer3d_vec3_scale(l, -1.0f), spot_dir);
                float epsilon = light->inner_cutoff - light->outer_cutoff;
                if (SDL_fabsf(epsilon) < 1e-6f)
                {
                    attenuation *= (cos_angle >= light->outer_cutoff) ? 1.0f : 0.0f;
                }
                else
                {
                    float spot_intensity = slayer3d_clamp01((cos_angle - light->outer_cutoff) / epsilon);
                    spot_intensity = spot_intensity * spot_intensity * (3.0f - 2.0f * spot_intensity);
                    attenuation *= spot_intensity;
                }
            }
        }

        {
            float n_dot_l = SDL_max(slayer3d_vec3_dot(n, l), 0.0f);
            if (n_dot_l <= 0.0f)
            {
                continue;
            }
            lit_r += light->color[0] * light->intensity * attenuation * n_dot_l;
            lit_g += light->color[1] * light->intensity * attenuation * n_dot_l;
            lit_b += light->color[2] * light->intensity * attenuation * n_dot_l;
        }
    }

    out.x = SDL_max(base_color.x * SDL_min(lit_r, 1.0f), 0.0f);
    out.y = SDL_max(base_color.y * SDL_min(lit_g, 1.0f), 0.0f);
    out.z = SDL_max(base_color.z * SDL_min(lit_b, 1.0f), 0.0f);
    out.w = base_color.w;

    /* Apply gamma to the lit color BEFORE fog mixing.  The fog color is
     * specified in sRGB space (to match the visible sky), so it must not
     * be gamma-encoded again. */
    slayer3d_tonemap(SLAYER3D_TONEMAP_NONE, &out.x, &out.y, &out.z);

    if (lp->fog.mode != SLAYER3D_FOG_NONE)
    {
        float dx = world_pos.x - lp->camera_pos.x;
        float dy = world_pos.y - lp->camera_pos.y;
        float dz = world_pos.z - lp->camera_pos.z;
        float dist = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
        float fog_f = slayer3d_compute_fog_factor(&lp->fog, dist);
        out.x = out.x * (1.0f - fog_f) + lp->fog.color[0] * fog_f;
        out.y = out.y * (1.0f - fog_f) + lp->fog.color[1] * fog_f;
        out.z = out.z * (1.0f - fog_f) + lp->fog.color[2] * fog_f;
    }

    return out;
}

static bool slayer3d_draw_mesh_internal(slayer3d_render_context *context, const slayer3d_mesh *mesh,
                                        const slayer3d_texture2d *texture, const slayer3d_texture2d *lightmap_texture,
                                        slayer3d_vec4 base_modulate, const slayer3d_lighting_params *lighting,
                                        const slayer3d_mat4 *joint_matrices, int joint_count, bool static_geometry,
                                        const char *shader_vertex_source, const char *shader_fragment_source)
{
    bool indexed;
    int triangle_count;
    bool software_baked_static;
    bool lit;
    bool skinned;
    bool gpu_skinning;
    bool cpu_skinning;
    slayer3d_framebuffer framebuffer;
    float *skinned_positions = NULL;
    float *skinned_normals = NULL;

/* Macro to read a position with optional skinning applied. */
#define SKIN_POS(idx)                                                                                                  \
    (skinned ? slayer3d_skin_position(mesh, (idx), joint_matrices, slayer3d_mesh_position(mesh, (idx)))                \
             : slayer3d_mesh_position(mesh, (idx)))
#define SKIN_NORM(idx)                                                                                                 \
    (skinned ? slayer3d_skin_normal(mesh, (idx), joint_matrices, slayer3d_mesh_normal(mesh, (idx)))                    \
             : slayer3d_mesh_normal(mesh, (idx)))

    if (!slayer3d_require_mode_3d(context, "slayer3d_draw_mesh"))
    {
        return false;
    }
    if (mesh == NULL)
    {
        return SDL_InvalidParamError("mesh");
    }
    if (mesh->positions == NULL || mesh->vertex_count <= 0)
    {
        return SDL_SetError("Mesh must provide positions and at least one vertex.");
    }

    indexed = mesh->indices != NULL;
    triangle_count = indexed ? (mesh->index_count / 3) : (mesh->vertex_count / 3);
    software_baked_static = context->backend == SLAYER3D_BACKEND_SOFTWARE && lighting != NULL &&
                            mesh->colors_are_baked_light && lighting->light_count == 0 &&
                            lighting->fog.mode == SLAYER3D_FOG_NONE;
    lit = lighting != NULL && !software_baked_static &&
          (context->shading_mode == SLAYER3D_SHADING_FLAT || mesh->normals != NULL);
    skinned = joint_matrices != NULL && joint_count > 0 && mesh->joint_indices != NULL && mesh->joint_weights != NULL;
    gpu_skinning = skinned && static_geometry && !mesh->dynamic_geometry &&
                   context->backend == SLAYER3D_BACKEND_OPENGL && lighting != NULL && shader_vertex_source == NULL &&
                   shader_fragment_source == NULL && joint_count <= SLAYER3D_GPU_SKINNING_MAX_JOINTS;
    cpu_skinning = skinned && !gpu_skinning;

    if (!slayer3d_validate_texture_for_draw(texture))
    {
        return false;
    }
    if (texture != NULL && mesh->uvs == NULL)
    {
        return SDL_SetError("Textured mesh draw requires UV coordinates.");
    }
    if (indexed)
    {
        if (mesh->index_count <= 0 || (mesh->index_count % 3) != 0)
        {
            return SDL_SetError("Indexed mesh draw requires index_count > 0 and divisible by 3.");
        }
    }
    else if ((mesh->vertex_count % 3) != 0)
    {
        return SDL_SetError("Non-indexed mesh draw requires vertex_count divisible by 3.");
    }

    if (cpu_skinning)
    {
        skinned_positions = (float *)SDL_malloc((size_t)mesh->vertex_count * 3 * sizeof(float));
        if (skinned_positions == NULL)
        {
            return SDL_OutOfMemory();
        }
        if (mesh->normals != NULL)
        {
            skinned_normals = (float *)SDL_malloc((size_t)mesh->vertex_count * 3 * sizeof(float));
            if (skinned_normals == NULL)
            {
                SDL_free(skinned_positions);
                return SDL_OutOfMemory();
            }
        }

        for (int i = 0; i < mesh->vertex_count; ++i)
        {
            slayer3d_vec3 p = slayer3d_skin_position(mesh, (unsigned int)i, joint_matrices,
                                                     slayer3d_mesh_position(mesh, (unsigned int)i));
            skinned_positions[i * 3 + 0] = p.x;
            skinned_positions[i * 3 + 1] = p.y;
            skinned_positions[i * 3 + 2] = p.z;

            if (skinned_normals != NULL)
            {
                slayer3d_vec3 n = slayer3d_skin_normal(mesh, (unsigned int)i, joint_matrices,
                                                       slayer3d_mesh_normal(mesh, (unsigned int)i));
                skinned_normals[i * 3 + 0] = n.x;
                skinned_normals[i * 3 + 1] = n.y;
                skinned_normals[i * 3 + 2] = n.z;
            }
        }
        context->stats.cpu_skinned_vertices += (Uint64)mesh->vertex_count;
    }

    /* Backend dispatch: try the vtable first. Software returns false to
     * fall through to the per-triangle loop below. */
    if (lit)
    {
        slayer3d_draw_params_lit lp;
        SDL_zero(lp);
        lp.positions = (skinned_positions != NULL) ? skinned_positions : mesh->positions;
        lp.normals = (skinned_normals != NULL) ? skinned_normals : mesh->normals;
        lp.uvs = mesh->uvs;
        lp.lightmap_uvs = mesh->lightmap_uvs;
        lp.colors = mesh->colors;
        lp.shader_vertex_source = shader_vertex_source;
        lp.shader_fragment_source = shader_fragment_source;
        lp.indices = mesh->indices;
        lp.vertex_count = mesh->vertex_count;
        lp.index_count = mesh->index_count;
        lp.texture = texture;
        lp.lightmap = lightmap_texture;
        lp.mvp = context->model_view_projection.m;
        lp.model_matrix = context->model.m;
        lp.normal_matrix[0] = context->model.m[0];
        lp.normal_matrix[1] = context->model.m[1];
        lp.normal_matrix[2] = context->model.m[2];
        lp.normal_matrix[3] = context->model.m[4];
        lp.normal_matrix[4] = context->model.m[5];
        lp.normal_matrix[5] = context->model.m[6];
        lp.normal_matrix[6] = context->model.m[8];
        lp.normal_matrix[7] = context->model.m[9];
        lp.normal_matrix[8] = context->model.m[10];
        lp.tint[0] = base_modulate.x;
        lp.tint[1] = base_modulate.y;
        lp.tint[2] = base_modulate.z;
        lp.tint[3] = base_modulate.w;
        {
            if (lighting != NULL)
            {
                lp.camera_pos[0] = lighting->camera_pos.x;
                lp.camera_pos[1] = lighting->camera_pos.y;
                lp.camera_pos[2] = lighting->camera_pos.z;
            }
            else
            {
                const slayer3d_mat4 v = context->view;
                lp.camera_pos[0] = -(v.m[0] * v.m[12] + v.m[1] * v.m[13] + v.m[2] * v.m[14]);
                lp.camera_pos[1] = -(v.m[4] * v.m[12] + v.m[5] * v.m[13] + v.m[6] * v.m[14]);
                lp.camera_pos[2] = -(v.m[8] * v.m[12] + v.m[9] * v.m[13] + v.m[10] * v.m[14]);
            }
        }
        if (lighting != NULL)
        {
            lp.ambient[0] = lighting->ambient[0];
            lp.ambient[1] = lighting->ambient[1];
            lp.ambient[2] = lighting->ambient[2];
            lp.metallic = lighting->metallic;
            lp.roughness = lighting->roughness;
            lp.emissive[0] = lighting->emissive[0];
            lp.emissive[1] = lighting->emissive[1];
            lp.emissive[2] = lighting->emissive[2];
            lp.lights = lighting->lights;
            lp.light_count = lighting->light_count;
            lp.tonemap_mode = (int)lighting->tonemap_mode;
            lp.fog_mode = (int)lighting->fog.mode;
            lp.fog_color[0] = lighting->fog.color[0];
            lp.fog_color[1] = lighting->fog.color[1];
            lp.fog_color[2] = lighting->fog.color[2];
            lp.fog_start = lighting->fog.start;
            lp.fog_end = lighting->fog.end;
            lp.fog_density = lighting->fog.density;
        }
        else
        {
            lp.ambient[0] = context->ambient[0];
            lp.ambient[1] = context->ambient[1];
            lp.ambient[2] = context->ambient[2];
            lp.metallic = 0.0f;
            lp.roughness = 1.0f;
            lp.lights = context->lights;
            lp.light_count = context->light_count;
            lp.tonemap_mode = (int)context->tonemap_mode;
            lp.fog_mode = (int)context->fog.mode;
            lp.fog_color[0] = context->fog.color[0];
            lp.fog_color[1] = context->fog.color[1];
            lp.fog_color[2] = context->fog.color[2];
            lp.fog_start = context->fog.start;
            lp.fog_end = context->fog.end;
            lp.fog_density = context->fog.density;
        }
        lp.shading_mode = (int)context->shading_mode;
        lp.uv_mode = (int)context->uv_mode;
        lp.baked_light_mode = mesh->colors_are_baked_light;
        lp.vertex_snap = context->vertex_snap;
        lp.vertex_snap_precision = context->vertex_snap_precision;
        lp.texture_filter = (int)context->texture_filter;
        lp.static_geometry = static_geometry && !cpu_skinning && !mesh->dynamic_geometry;
        lp.generation = mesh->generation;
        lp.depth_prepass_eligible = context->depth_prepass_scope_enabled && lp.static_geometry;
        if (gpu_skinning)
        {
            lp.joint_indices = mesh->joint_indices;
            lp.joint_weights = mesh->joint_weights;
            lp.joint_matrices = joint_matrices;
            lp.joint_count = joint_count;
        }

        if (lighting->shadow_enabled[0] && lighting->shadow_depth[0] != NULL)
        {
            lp.shadow_depth_data = lighting->shadow_depth[0];
            lp.shadow_vp = lighting->shadow_vp[0].m;
            lp.shadow_bias = lighting->shadow_bias > 0 ? lighting->shadow_bias : 0.005f;
        }

        if (context->backend_iface.draw_mesh_lit(context, &lp))
        {
            SDL_free(skinned_positions);
            SDL_free(skinned_normals);
            return true;
        }
    }
    else
    {
        slayer3d_draw_params_unlit up;
        SDL_zero(up);
        up.positions = (skinned_positions != NULL) ? skinned_positions : mesh->positions;
        up.uvs = mesh->uvs;
        up.colors = mesh->colors;
        up.shader_vertex_source = shader_vertex_source;
        up.shader_fragment_source = shader_fragment_source;
        up.indices = mesh->indices;
        up.vertex_count = mesh->vertex_count;
        up.index_count = mesh->index_count;
        up.texture = texture;
        up.mvp = context->model_view_projection.m;
        up.tint[0] = base_modulate.x;
        up.tint[1] = base_modulate.y;
        up.tint[2] = base_modulate.z;
        up.tint[3] = base_modulate.w;
        up.texture_filter = (int)context->texture_filter;
        up.static_geometry = static_geometry && !skinned && !mesh->dynamic_geometry;
        up.generation = mesh->generation;

        if (context->backend_iface.draw_mesh_unlit(context, &up))
        {
            SDL_free(skinned_positions);
            SDL_free(skinned_normals);
            return true;
        }
    }

    framebuffer = slayer3d_framebuffer_from_context(context);

    for (int triangle = 0; triangle < triangle_count; ++triangle)
    {
        const unsigned int i0 = indexed ? mesh->indices[triangle * 3 + 0] : (unsigned int)(triangle * 3 + 0);
        const unsigned int i1 = indexed ? mesh->indices[triangle * 3 + 1] : (unsigned int)(triangle * 3 + 1);
        const unsigned int i2 = indexed ? mesh->indices[triangle * 3 + 2] : (unsigned int)(triangle * 3 + 2);
        slayer3d_vec2 uv0 = {0.0f, 0.0f};
        slayer3d_vec2 uv1 = {0.0f, 0.0f};
        slayer3d_vec2 uv2 = {0.0f, 0.0f};

        if ((int)i0 >= mesh->vertex_count || (int)i1 >= mesh->vertex_count || (int)i2 >= mesh->vertex_count)
        {
            return SDL_SetError("Mesh index out of range for vertex_count=%d.", mesh->vertex_count);
        }

        if (texture != NULL)
        {
            uv0 = slayer3d_mesh_uv(mesh, i0);
            uv1 = slayer3d_mesh_uv(mesh, i1);
            uv2 = slayer3d_mesh_uv(mesh, i2);
        }

        if (lit && context->shading_mode == SLAYER3D_SHADING_FLAT)
        {
            /* FLAT: use lit rasterizer with face normal for all three vertices.
             * The lit rasterizer handles tonemapping, fog, and color quantization. */
            slayer3d_vec3 p0 = SKIN_POS(i0);
            slayer3d_vec3 p1 = SKIN_POS(i1);
            slayer3d_vec3 p2 = SKIN_POS(i2);
            slayer3d_vec3 edge1 = slayer3d_vec3_sub(p1, p0);
            slayer3d_vec3 edge2 = slayer3d_vec3_sub(p2, p0);
            slayer3d_vec3 fn = slayer3d_vec3_normalize(slayer3d_vec3_cross(edge1, edge2));
            slayer3d_vec3 wn = slayer3d_transform_normal(context->model, fn);
            slayer3d_vec3 wp0 = slayer3d_transform_position(context->model, p0);
            slayer3d_vec3 wp1 = slayer3d_transform_position(context->model, p1);
            slayer3d_vec3 wp2 = slayer3d_transform_position(context->model, p2);
            slayer3d_rasterize_triangle_lit(&framebuffer, context->model_view_projection, p0, p1, p2, uv0, uv1, uv2, wn,
                                            wn, wn, wp0, wp1, wp2,
                                            slayer3d_mesh_vertex_modulate(mesh, i0, base_modulate),
                                            slayer3d_mesh_vertex_modulate(mesh, i1, base_modulate),
                                            slayer3d_mesh_vertex_modulate(mesh, i2, base_modulate), texture, lighting,
                                            context->backface_culling_enabled, context->wireframe_enabled);
        }
        else if (lit && context->shading_mode == SLAYER3D_SHADING_GOURAUD)
        {
            /* GOURAUD: shade vertices once, then rasterize with the active
             * retro/profile rules (affine UVs, vertex snap, quantization). */
            slayer3d_vec3 p0 = SKIN_POS(i0);
            slayer3d_vec3 p1 = SKIN_POS(i1);
            slayer3d_vec3 p2 = SKIN_POS(i2);
            slayer3d_vec3 wn0, wn1, wn2;
            slayer3d_vec4 m0 = slayer3d_mesh_vertex_modulate(mesh, i0, base_modulate);
            slayer3d_vec4 m1 = slayer3d_mesh_vertex_modulate(mesh, i1, base_modulate);
            slayer3d_vec4 m2 = slayer3d_mesh_vertex_modulate(mesh, i2, base_modulate);
            if (mesh->normals != NULL)
            {
                wn0 = slayer3d_transform_normal(context->model, SKIN_NORM(i0));
                wn1 = slayer3d_transform_normal(context->model, SKIN_NORM(i1));
                wn2 = slayer3d_transform_normal(context->model, SKIN_NORM(i2));
            }
            else
            {
                slayer3d_vec3 fn =
                    slayer3d_vec3_normalize(slayer3d_vec3_cross(slayer3d_vec3_sub(p1, p0), slayer3d_vec3_sub(p2, p0)));
                wn0 = wn1 = wn2 = slayer3d_transform_normal(context->model, fn);
            }
            slayer3d_vec3 wp0 = slayer3d_transform_position(context->model, p0);
            slayer3d_vec3 wp1 = slayer3d_transform_position(context->model, p1);
            slayer3d_vec3 wp2 = slayer3d_transform_position(context->model, p2);
            slayer3d_rasterize_triangle_textured_profiled(
                &framebuffer, context->model_view_projection, p0, p1, p2, uv0, uv1, uv2,
                slayer3d_shade_point_retro(lighting, m0, wn0, wp0), slayer3d_shade_point_retro(lighting, m1, wn1, wp1),
                slayer3d_shade_point_retro(lighting, m2, wn2, wp2), texture, lighting,
                context->backface_culling_enabled, context->wireframe_enabled);
        }
        else if (lit)
        {
            /* PHONG: per-fragment PBR with interpolated normals. */
            const slayer3d_mat4 m = context->model;
            slayer3d_vec3 rn0 = slayer3d_transform_normal(m, SKIN_NORM(i0));
            slayer3d_vec3 rn1 = slayer3d_transform_normal(m, SKIN_NORM(i1));
            slayer3d_vec3 rn2 = slayer3d_transform_normal(m, SKIN_NORM(i2));
            slayer3d_vec3 p0 = SKIN_POS(i0);
            slayer3d_vec3 p1 = SKIN_POS(i1);
            slayer3d_vec3 p2 = SKIN_POS(i2);
            slayer3d_vec3 wp0 = slayer3d_transform_position(m, p0);
            slayer3d_vec3 wp1 = slayer3d_transform_position(m, p1);
            slayer3d_vec3 wp2 = slayer3d_transform_position(m, p2);

            slayer3d_rasterize_triangle_lit(&framebuffer, context->model_view_projection, p0, p1, p2, uv0, uv1, uv2,
                                            rn0, rn1, rn2, wp0, wp1, wp2,
                                            slayer3d_mesh_vertex_modulate(mesh, i0, base_modulate),
                                            slayer3d_mesh_vertex_modulate(mesh, i1, base_modulate),
                                            slayer3d_mesh_vertex_modulate(mesh, i2, base_modulate), texture, lighting,
                                            context->backface_culling_enabled, context->wireframe_enabled);
        }
        else
        {
            slayer3d_rasterize_triangle_textured(&framebuffer, context->model_view_projection, SKIN_POS(i0),
                                                 SKIN_POS(i1), SKIN_POS(i2), uv0, uv1, uv2,
                                                 slayer3d_mesh_vertex_modulate(mesh, i0, base_modulate),
                                                 slayer3d_mesh_vertex_modulate(mesh, i1, base_modulate),
                                                 slayer3d_mesh_vertex_modulate(mesh, i2, base_modulate), texture,
                                                 context->backface_culling_enabled, context->wireframe_enabled);
        }
    }

#undef SKIN_POS
#undef SKIN_NORM
    SDL_free(skinned_positions);
    SDL_free(skinned_normals);
    return true;
}

bool slayer3d_push_matrix(slayer3d_render_context *context)
{
    if (!slayer3d_require_mode_3d(context, "slayer3d_push_matrix"))
    {
        return false;
    }

    if (!slayer3d_ensure_model_stack_capacity(context, context->model_stack_depth + 1))
    {
        return false;
    }

    context->model_stack[context->model_stack_depth] = context->model_stack[context->model_stack_depth - 1];
    context->model_stack_depth += 1;
    slayer3d_update_current_model_matrices(context);
    return true;
}

bool slayer3d_pop_matrix(slayer3d_render_context *context)
{
    if (!slayer3d_require_mode_3d(context, "slayer3d_pop_matrix"))
    {
        return false;
    }

    if (context->model_stack_depth <= 1)
    {
        return SDL_SetError("slayer3d_pop_matrix cannot pop the root model matrix.");
    }

    context->model_stack_depth -= 1;
    slayer3d_update_current_model_matrices(context);
    return true;
}

bool slayer3d_multiply_matrix(slayer3d_render_context *context, slayer3d_mat4 matrix)
{
    if (!slayer3d_require_mode_3d(context, "slayer3d_multiply_matrix"))
    {
        return false;
    }

    context->model_stack[context->model_stack_depth - 1] =
        slayer3d_mat4_multiply(context->model_stack[context->model_stack_depth - 1], matrix);
    slayer3d_update_current_model_matrices(context);
    return true;
}

bool slayer3d_translate(slayer3d_render_context *context, float x, float y, float z)
{
    if (!slayer3d_require_mode_3d(context, "slayer3d_translate"))
    {
        return false;
    }

    const slayer3d_mat4 translation = slayer3d_mat4_translate(slayer3d_vec3_make(x, y, z));
    context->model_stack[context->model_stack_depth - 1] =
        slayer3d_mat4_multiply(context->model_stack[context->model_stack_depth - 1], translation);
    slayer3d_update_current_model_matrices(context);
    return true;
}

bool slayer3d_rotate(slayer3d_render_context *context, slayer3d_vec3 axis, float angle_radians)
{
    if (!slayer3d_require_mode_3d(context, "slayer3d_rotate"))
    {
        return false;
    }

    if (!(slayer3d_vec3_length_squared(axis) > 0.0f))
    {
        return SDL_SetError("slayer3d_rotate requires a non-zero rotation axis.");
    }

    const slayer3d_mat4 rotation = slayer3d_mat4_rotate(axis, angle_radians);
    context->model_stack[context->model_stack_depth - 1] =
        slayer3d_mat4_multiply(context->model_stack[context->model_stack_depth - 1], rotation);
    slayer3d_update_current_model_matrices(context);
    return true;
}

bool slayer3d_scale(slayer3d_render_context *context, float x, float y, float z)
{
    if (!slayer3d_require_mode_3d(context, "slayer3d_scale"))
    {
        return false;
    }

    const slayer3d_mat4 scale = slayer3d_mat4_scale(slayer3d_vec3_make(x, y, z));
    context->model_stack[context->model_stack_depth - 1] =
        slayer3d_mat4_multiply(context->model_stack[context->model_stack_depth - 1], scale);
    slayer3d_update_current_model_matrices(context);
    return true;
}

bool slayer3d_draw_triangle_3d(slayer3d_render_context *context, slayer3d_vec3 v0, slayer3d_vec3 v1, slayer3d_vec3 v2,
                               slayer3d_color color)
{
    slayer3d_framebuffer framebuffer;
    slayer3d_shading_mode mode;

    if (!slayer3d_require_mode_3d(context, "slayer3d_draw_triangle_3d"))
    {
        return false;
    }

    mode = context->shading_mode;

    /* Backend dispatch: match the software path's lit/unlit split. */
    {
        float positions[9] = {v0.x, v0.y, v0.z, v1.x, v1.y, v1.z, v2.x, v2.y, v2.z};

        if (mode != SLAYER3D_SHADING_UNLIT && context->light_count > 0)
        {
            slayer3d_vec3 edge1 = slayer3d_vec3_sub(v1, v0);
            slayer3d_vec3 edge2 = slayer3d_vec3_sub(v2, v0);
            slayer3d_vec3 face_normal = slayer3d_vec3_normalize(slayer3d_vec3_cross(edge1, edge2));
            float normals[9] = {face_normal.x, face_normal.y, face_normal.z, face_normal.x, face_normal.y,
                                face_normal.z, face_normal.x, face_normal.y, face_normal.z};
            slayer3d_lighting_params lighting;
            slayer3d_draw_params_lit lp;

            slayer3d_build_lighting_params(context, &lighting);
            lighting.roughness = 1.0f;

            SDL_zero(lp);
            lp.positions = positions;
            lp.normals = normals;
            lp.vertex_count = 3;
            lp.mvp = context->model_view_projection.m;
            lp.model_matrix = context->model.m;
            lp.normal_matrix[0] = context->model.m[0];
            lp.normal_matrix[1] = context->model.m[1];
            lp.normal_matrix[2] = context->model.m[2];
            lp.normal_matrix[3] = context->model.m[4];
            lp.normal_matrix[4] = context->model.m[5];
            lp.normal_matrix[5] = context->model.m[6];
            lp.normal_matrix[6] = context->model.m[8];
            lp.normal_matrix[7] = context->model.m[9];
            lp.normal_matrix[8] = context->model.m[10];
            lp.tint[0] = (float)color.r / 255.0f;
            lp.tint[1] = (float)color.g / 255.0f;
            lp.tint[2] = (float)color.b / 255.0f;
            lp.tint[3] = (float)color.a / 255.0f;
            lp.camera_pos[0] = lighting.camera_pos.x;
            lp.camera_pos[1] = lighting.camera_pos.y;
            lp.camera_pos[2] = lighting.camera_pos.z;
            lp.ambient[0] = lighting.ambient[0];
            lp.ambient[1] = lighting.ambient[1];
            lp.ambient[2] = lighting.ambient[2];
            lp.metallic = 0.0f;
            lp.roughness = lighting.roughness;
            lp.emissive[0] = lighting.emissive[0];
            lp.emissive[1] = lighting.emissive[1];
            lp.emissive[2] = lighting.emissive[2];
            lp.lights = lighting.lights;
            lp.light_count = lighting.light_count;
            lp.tonemap_mode = (int)lighting.tonemap_mode;
            lp.fog_mode = (int)lighting.fog.mode;
            lp.fog_color[0] = lighting.fog.color[0];
            lp.fog_color[1] = lighting.fog.color[1];
            lp.fog_color[2] = lighting.fog.color[2];
            lp.fog_start = lighting.fog.start;
            lp.fog_end = lighting.fog.end;
            lp.fog_density = lighting.fog.density;
            lp.shading_mode = (int)context->shading_mode;
            lp.uv_mode = (int)context->uv_mode;
            lp.vertex_snap = context->vertex_snap;
            lp.vertex_snap_precision = context->vertex_snap_precision;
            lp.texture_filter = (int)context->texture_filter;

            if (context->backend_iface.draw_mesh_lit(context, &lp))
            {
                return true;
            }
        }
        else
        {
            slayer3d_draw_params_unlit up;

            SDL_zero(up);
            up.positions = positions;
            up.vertex_count = 3;
            up.mvp = context->model_view_projection.m;
            up.tint[0] = (float)color.r / 255.0f;
            up.tint[1] = (float)color.g / 255.0f;
            up.tint[2] = (float)color.b / 255.0f;
            up.tint[3] = (float)color.a / 255.0f;
            up.texture_filter = (int)context->texture_filter;

            if (context->backend_iface.draw_mesh_unlit(context, &up))
            {
                return true;
            }
        }
    }

    framebuffer = slayer3d_framebuffer_from_context(context);

    if (mode != SLAYER3D_SHADING_UNLIT && context->light_count > 0)
    {
        slayer3d_vec3 edge1 = slayer3d_vec3_sub(v1, v0);
        slayer3d_vec3 edge2 = slayer3d_vec3_sub(v2, v0);
        slayer3d_vec3 face_normal = slayer3d_vec3_normalize(slayer3d_vec3_cross(edge1, edge2));
        slayer3d_vec3 wn = slayer3d_transform_normal(context->model, face_normal);
        slayer3d_vec3 wp0 = slayer3d_transform_position(context->model, v0);
        slayer3d_vec3 wp1 = slayer3d_transform_position(context->model, v1);
        slayer3d_vec3 wp2 = slayer3d_transform_position(context->model, v2);
        slayer3d_vec4 modulate = slayer3d_color_to_modulate(color);

        slayer3d_lighting_params lp;
        slayer3d_build_lighting_params(context, &lp);
        lp.roughness = 1.0f;

        if (mode == SLAYER3D_SHADING_FLAT)
        {
            /* One PBR eval at centroid with face normal. */
            slayer3d_vec3 centroid =
                slayer3d_vec3_scale(slayer3d_vec3_add(slayer3d_vec3_add(wp0, wp1), wp2), 1.0f / 3.0f);
            slayer3d_color lit =
                slayer3d_shade_point(&lp, modulate.x, modulate.y, modulate.z, modulate.w, wn, centroid);
            slayer3d_rasterize_triangle(&framebuffer, context->model_view_projection, v0, v1, v2, lit,
                                        context->backface_culling_enabled, context->wireframe_enabled);
        }
        else if (mode == SLAYER3D_SHADING_GOURAUD)
        {
            slayer3d_vec2 uv0 = {0.0f, 0.0f};
            slayer3d_rasterize_triangle_textured_profiled(
                &framebuffer, context->model_view_projection, v0, v1, v2, uv0, uv0, uv0,
                slayer3d_shade_point_retro(&lp, modulate, wn, wp0), slayer3d_shade_point_retro(&lp, modulate, wn, wp1),
                slayer3d_shade_point_retro(&lp, modulate, wn, wp2), NULL, &lp, context->backface_culling_enabled,
                context->wireframe_enabled);
        }
        else /* SLAYER3D_SHADING_PHONG */
        {
            slayer3d_vec2 uv0 = {0.0f, 0.0f};
            slayer3d_rasterize_triangle_lit(&framebuffer, context->model_view_projection, v0, v1, v2, uv0, uv0, uv0, wn,
                                            wn, wn, wp0, wp1, wp2, modulate, modulate, modulate, NULL, &lp,
                                            context->backface_culling_enabled, context->wireframe_enabled);
        }
    }
    else
    {
        slayer3d_rasterize_triangle(&framebuffer, context->model_view_projection, v0, v1, v2, color,
                                    context->backface_culling_enabled, context->wireframe_enabled);
    }

    return true;
}

bool slayer3d_draw_triangle_3d_ex(slayer3d_render_context *context, slayer3d_vec3 v0, slayer3d_vec3 v1,
                                  slayer3d_vec3 v2, slayer3d_color c0, slayer3d_color c1, slayer3d_color c2)
{
    if (!slayer3d_require_mode_3d(context, "slayer3d_draw_triangle_3d_ex"))
    {
        return false;
    }

    slayer3d_framebuffer framebuffer = slayer3d_framebuffer_from_context(context);
    slayer3d_rasterize_triangle_colored(&framebuffer, context->model_view_projection, v0, v1, v2, c0, c1, c2,
                                        context->backface_culling_enabled, context->wireframe_enabled);
    return true;
}

bool slayer3d_draw_line_3d(slayer3d_render_context *context, slayer3d_vec3 start, slayer3d_vec3 end,
                           slayer3d_color color)
{
    if (!slayer3d_require_mode_3d(context, "slayer3d_draw_line_3d"))
    {
        return false;
    }

    if (context->gl != NULL)
    {
        const float positions[6] = {start.x, start.y, start.z, end.x, end.y, end.z};
        const float c = 1.0f / 255.0f;
        const float colors[8] = {color.r * c, color.g * c, color.b * c, color.a * c,
                                 color.r * c, color.g * c, color.b * c, color.a * c};
        return slayer3d_gl_append_line(context->gl, positions, colors, context->model_view_projection.m);
    }

    slayer3d_framebuffer framebuffer = slayer3d_framebuffer_from_context(context);
    slayer3d_rasterize_line(&framebuffer, context->model_view_projection, start, end, color);
    return true;
}

bool slayer3d_draw_point_3d(slayer3d_render_context *context, slayer3d_vec3 position, slayer3d_color color)
{
    if (!slayer3d_require_mode_3d(context, "slayer3d_draw_point_3d"))
    {
        return false;
    }

    slayer3d_framebuffer framebuffer = slayer3d_framebuffer_from_context(context);
    slayer3d_rasterize_point(&framebuffer, context->model_view_projection, position, color);
    return true;
}

static slayer3d_vec3 slayer3d_view_camera_right(const slayer3d_render_context *context)
{
    return slayer3d_vec3_normalize(slayer3d_vec3_make(context->view.m[0], context->view.m[4], context->view.m[8]));
}

static slayer3d_vec3 slayer3d_view_camera_up(const slayer3d_render_context *context)
{
    return slayer3d_vec3_normalize(slayer3d_vec3_make(context->view.m[1], context->view.m[5], context->view.m[9]));
}

static slayer3d_vec3 slayer3d_view_camera_forward(const slayer3d_render_context *context)
{
    return slayer3d_vec3_normalize(slayer3d_vec3_make(-context->view.m[2], -context->view.m[6], -context->view.m[10]));
}

bool slayer3d_draw_billboard_ex(slayer3d_render_context *context, const slayer3d_texture2d *texture,
                                slayer3d_vec3 position, slayer3d_vec2 size, slayer3d_vec2 anchor,
                                slayer3d_billboard_mode mode, slayer3d_color tint)
{
    slayer3d_vec3 right;
    slayer3d_vec3 up;
    slayer3d_vec3 normal;
    const float left = -anchor.x * size.x;
    const float bottom = -anchor.y * size.y;
    const float sprite_right = left + size.x;
    const float sprite_top = bottom + size.y;
    slayer3d_mesh mesh;
    float positions[12];
    float normals[12];
    float uvs[8];
    unsigned int indices[12] = {0, 1, 2, 2, 1, 3, 2, 1, 0, 3, 1, 2};

    if (!slayer3d_require_mode_3d(context, "slayer3d_draw_billboard_ex"))
    {
        return false;
    }
    if (texture == NULL)
    {
        return SDL_InvalidParamError("texture");
    }
    if (size.x <= 0.0f)
    {
        return SDL_SetError("Billboard size.x must be positive.");
    }
    if (size.y <= 0.0f)
    {
        return SDL_SetError("Billboard size.y must be positive.");
    }
    if (anchor.x < 0.0f || anchor.x > 1.0f || anchor.y < 0.0f || anchor.y > 1.0f)
    {
        return SDL_SetError("Billboard anchor must be in [0, 1] for both axes.");
    }

    right = slayer3d_view_camera_right(context);
    if (mode == SLAYER3D_BILLBOARD_SPHERICAL)
    {
        up = slayer3d_view_camera_up(context);
    }
    else
    {
        const slayer3d_vec3 world_up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
        slayer3d_vec3 forward = slayer3d_view_camera_forward(context);
        forward.y = 0.0f;
        if (slayer3d_vec3_length_squared(forward) <= 0.000001f)
        {
            forward = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
        }
        else
        {
            forward = slayer3d_vec3_normalize(forward);
        }
        right = slayer3d_vec3_normalize(slayer3d_vec3_cross(forward, world_up));
        if (slayer3d_vec3_length_squared(right) <= 0.000001f)
        {
            right = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        }
        up = world_up;
    }
    normal = slayer3d_vec3_normalize(slayer3d_vec3_cross(right, up));

    {
        const slayer3d_vec3 bl = slayer3d_vec3_add(
            position, slayer3d_vec3_add(slayer3d_vec3_scale(right, left), slayer3d_vec3_scale(up, bottom)));
        const slayer3d_vec3 tl = slayer3d_vec3_add(
            position, slayer3d_vec3_add(slayer3d_vec3_scale(right, left), slayer3d_vec3_scale(up, sprite_top)));
        const slayer3d_vec3 br = slayer3d_vec3_add(
            position, slayer3d_vec3_add(slayer3d_vec3_scale(right, sprite_right), slayer3d_vec3_scale(up, bottom)));
        const slayer3d_vec3 tr = slayer3d_vec3_add(
            position, slayer3d_vec3_add(slayer3d_vec3_scale(right, sprite_right), slayer3d_vec3_scale(up, sprite_top)));
        const slayer3d_vec3 verts[4] = {bl, tl, br, tr};

        for (int i = 0; i < 4; ++i)
        {
            positions[i * 3 + 0] = verts[i].x;
            positions[i * 3 + 1] = verts[i].y;
            positions[i * 3 + 2] = verts[i].z;
            normals[i * 3 + 0] = normal.x;
            normals[i * 3 + 1] = normal.y;
            normals[i * 3 + 2] = normal.z;
        }
    }

    uvs[0] = 0.0f;
    uvs[1] = 1.0f;
    uvs[2] = 0.0f;
    uvs[3] = 0.0f;
    uvs[4] = 1.0f;
    uvs[5] = 1.0f;
    uvs[6] = 1.0f;
    uvs[7] = 0.0f;

    SDL_zero(mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 12;

    if (context->shading_mode != SLAYER3D_SHADING_UNLIT && context->light_count > 0)
    {
        slayer3d_lighting_params lp;
        slayer3d_build_lighting_params(context, &lp);
        lp.roughness = 1.0f;
        return slayer3d_draw_mesh_internal(context, &mesh, texture, NULL, slayer3d_color_to_modulate(tint), &lp, NULL,
                                           0, false, NULL, NULL);
    }

    return slayer3d_draw_mesh_internal(context, &mesh, texture, NULL, slayer3d_color_to_modulate(tint), NULL, NULL, 0,
                                       false, NULL, NULL);
}

bool slayer3d_draw_billboard_shader_ex(slayer3d_render_context *context, const slayer3d_texture2d *texture,
                                       slayer3d_vec3 position, slayer3d_vec2 size, slayer3d_vec2 anchor,
                                       slayer3d_billboard_mode mode, slayer3d_color tint, bool lighting,
                                       const char *shader_vertex_source, const char *shader_fragment_source)
{
    slayer3d_vec3 right;
    slayer3d_vec3 up;
    slayer3d_vec3 normal;
    slayer3d_lighting_params lp;
    const float left = -anchor.x * size.x;
    const float bottom = -anchor.y * size.y;
    const float sprite_right = left + size.x;
    const float sprite_top = bottom + size.y;
    slayer3d_mesh mesh;
    float positions[12];
    float normals[12];
    float uvs[8];
    unsigned int indices[12] = {0, 1, 2, 2, 1, 3, 2, 1, 0, 3, 1, 2};

    if (shader_fragment_source == NULL || shader_fragment_source[0] == '\0')
        return slayer3d_draw_billboard_ex(context, texture, position, size, anchor, mode, tint);
    if (!slayer3d_require_mode_3d(context, "slayer3d_draw_billboard_shader_ex"))
        return false;
    if (size.x <= 0.0f)
        return SDL_SetError("Billboard size.x must be positive.");
    if (size.y <= 0.0f)
        return SDL_SetError("Billboard size.y must be positive.");
    if (anchor.x < 0.0f || anchor.x > 1.0f || anchor.y < 0.0f || anchor.y > 1.0f)
        return SDL_SetError("Billboard anchor must be in [0, 1] for both axes.");

    right = slayer3d_view_camera_right(context);
    if (mode == SLAYER3D_BILLBOARD_SPHERICAL)
    {
        up = slayer3d_view_camera_up(context);
    }
    else
    {
        const slayer3d_vec3 world_up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
        slayer3d_vec3 forward = slayer3d_view_camera_forward(context);
        forward.y = 0.0f;
        if (slayer3d_vec3_length_squared(forward) <= 0.000001f)
            forward = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
        else
            forward = slayer3d_vec3_normalize(forward);
        right = slayer3d_vec3_normalize(slayer3d_vec3_cross(forward, world_up));
        if (slayer3d_vec3_length_squared(right) <= 0.000001f)
            right = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        up = world_up;
    }
    normal = slayer3d_vec3_normalize(slayer3d_vec3_cross(right, up));

    {
        const slayer3d_vec3 bl = slayer3d_vec3_add(
            position, slayer3d_vec3_add(slayer3d_vec3_scale(right, left), slayer3d_vec3_scale(up, bottom)));
        const slayer3d_vec3 tl = slayer3d_vec3_add(
            position, slayer3d_vec3_add(slayer3d_vec3_scale(right, left), slayer3d_vec3_scale(up, sprite_top)));
        const slayer3d_vec3 br = slayer3d_vec3_add(
            position, slayer3d_vec3_add(slayer3d_vec3_scale(right, sprite_right), slayer3d_vec3_scale(up, bottom)));
        const slayer3d_vec3 tr = slayer3d_vec3_add(
            position, slayer3d_vec3_add(slayer3d_vec3_scale(right, sprite_right), slayer3d_vec3_scale(up, sprite_top)));
        const slayer3d_vec3 verts[4] = {bl, tl, br, tr};

        for (int i = 0; i < 4; ++i)
        {
            positions[i * 3 + 0] = verts[i].x;
            positions[i * 3 + 1] = verts[i].y;
            positions[i * 3 + 2] = verts[i].z;
            normals[i * 3 + 0] = normal.x;
            normals[i * 3 + 1] = normal.y;
            normals[i * 3 + 2] = normal.z;
        }
    }

    uvs[0] = 0.0f;
    uvs[1] = 1.0f;
    uvs[2] = 0.0f;
    uvs[3] = 0.0f;
    uvs[4] = 1.0f;
    uvs[5] = 1.0f;
    uvs[6] = 1.0f;
    uvs[7] = 0.0f;

    SDL_zero(mesh);
    mesh.positions = positions;
    mesh.normals = lighting ? normals : NULL;
    mesh.uvs = uvs;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 12;

    if (lighting)
    {
        slayer3d_build_lighting_params(context, &lp);
        lp.roughness = 1.0f;
        return slayer3d_draw_mesh_internal(context, &mesh, texture, NULL, slayer3d_color_to_modulate(tint), &lp, NULL,
                                           0, false, shader_vertex_source, shader_fragment_source);
    }

    return slayer3d_draw_mesh_internal(context, &mesh, texture, NULL, slayer3d_color_to_modulate(tint), NULL, NULL, 0,
                                       false, shader_vertex_source, shader_fragment_source);
}

bool slayer3d_draw_billboard(slayer3d_render_context *context, const slayer3d_texture2d *texture,
                             slayer3d_vec3 position, slayer3d_vec2 size, slayer3d_color tint)
{
    return slayer3d_draw_billboard_ex(context, texture, position, size, (slayer3d_vec2){0.5f, 0.0f},
                                      SLAYER3D_BILLBOARD_UPRIGHT, tint);
}

bool slayer3d_draw_mesh(slayer3d_render_context *context, const slayer3d_mesh *mesh, const slayer3d_texture2d *texture,
                        slayer3d_color tint)
{
    if (context != NULL && context->shading_mode != SLAYER3D_SHADING_UNLIT && mesh != NULL && mesh->normals != NULL)
    {
        slayer3d_lighting_params lp;
        slayer3d_build_lighting_params(context, &lp);
        lp.roughness = 1.0f;
        return slayer3d_draw_mesh_internal(context, mesh, texture, NULL, slayer3d_color_to_modulate(tint), &lp, NULL, 0,
                                           false, NULL, NULL);
    }
    return slayer3d_draw_mesh_internal(context, mesh, texture, NULL, slayer3d_color_to_modulate(tint), NULL, NULL, 0,
                                       false, NULL, NULL);
}

bool slayer3d_draw_static_mesh(slayer3d_render_context *context, const slayer3d_mesh *mesh,
                               const slayer3d_texture2d *texture, slayer3d_color tint)
{
    if (context != NULL && context->shading_mode != SLAYER3D_SHADING_UNLIT && mesh != NULL && mesh->normals != NULL)
    {
        slayer3d_lighting_params lp;
        slayer3d_build_lighting_params(context, &lp);
        lp.roughness = 1.0f;
        return slayer3d_draw_mesh_internal(context, mesh, texture, NULL, slayer3d_color_to_modulate(tint), &lp, NULL, 0,
                                           true, NULL, NULL);
    }
    return slayer3d_draw_mesh_internal(context, mesh, texture, NULL, slayer3d_color_to_modulate(tint), NULL, NULL, 0,
                                       true, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* Model node hierarchy helpers                                        */
/* ------------------------------------------------------------------ */

static slayer3d_mat4 slayer3d_mat4_from_trs_node(const float *t, const float *r, const float *s)
{
    float x = r[0], y = r[1], z = r[2], w = r[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;
    slayer3d_mat4 m;
    m.m[0] = (1.0f - (yy + zz)) * s[0];
    m.m[1] = (xy + wz) * s[0];
    m.m[2] = (xz - wy) * s[0];
    m.m[3] = 0.0f;
    m.m[4] = (xy - wz) * s[1];
    m.m[5] = (1.0f - (xx + zz)) * s[1];
    m.m[6] = (yz + wx) * s[1];
    m.m[7] = 0.0f;
    m.m[8] = (xz + wy) * s[2];
    m.m[9] = (yz - wx) * s[2];
    m.m[10] = (1.0f - (xx + yy)) * s[2];
    m.m[11] = 0.0f;
    m.m[12] = t[0];
    m.m[13] = t[1];
    m.m[14] = t[2];
    m.m[15] = 1.0f;
    return m;
}

/**
 * Draw a single mesh by index, resolving material/texture/lighting.
 * joint_matrices may be NULL for non-skinned draws.
 */
static char *slayer3d_embedded_texture_cache_key(const slayer3d_model *model, const char *embedded_ref);

static bool slayer3d_draw_model_mesh(slayer3d_render_context *context, const slayer3d_asset_resolver *assets,
                                     const slayer3d_model *model, int mesh_index,
                                     const slayer3d_texture2d *lightmap_texture, slayer3d_vec4 tint_modulate,
                                     const slayer3d_mat4 *joint_matrices, int joint_count)
{
    const slayer3d_mesh *mesh = &model->meshes[mesh_index];
    const slayer3d_texture2d *texture = NULL;
    const slayer3d_material *material = NULL;
    slayer3d_vec4 mesh_modulate = tint_modulate;
    bool ok = true;

    ++context->stats.model_mesh_submissions;
    if (!slayer3d_mesh_is_visible(context, mesh))
    {
        ++context->stats.model_mesh_culled;
        return true;
    }

    if (mesh->material_index < -1)
    {
        return SDL_SetError("Mesh material index %d is invalid.", mesh->material_index);
    }

    if (mesh->material_index >= 0)
    {
        if (mesh->material_index >= model->material_count || model->materials == NULL)
        {
            return SDL_SetError("Mesh material index %d is outside material_count=%d.", mesh->material_index,
                                model->material_count);
        }

        material = &model->materials[mesh->material_index];
        mesh_modulate.x = slayer3d_clamp01(mesh_modulate.x * material->albedo[0]);
        mesh_modulate.y = slayer3d_clamp01(mesh_modulate.y * material->albedo[1]);
        mesh_modulate.z = slayer3d_clamp01(mesh_modulate.z * material->albedo[2]);
        mesh_modulate.w = slayer3d_clamp01(mesh_modulate.w * material->albedo[3]);

        if (material->albedo_map != NULL && material->albedo_map[0] != '\0')
        {
            if (material->albedo_map[0] == '#' && model->embedded_textures != NULL)
            {
                int tex_idx = SDL_atoi(&material->albedo_map[1]);
                if (tex_idx >= 0 && tex_idx < model->embedded_texture_count &&
                    model->embedded_textures[tex_idx].pixels != NULL)
                {
                    char *embedded_key = slayer3d_embedded_texture_cache_key(model, material->albedo_map);
                    slayer3d_texture_cache_entry *entry = NULL;
                    for (entry = context->texture_cache; embedded_key != NULL && entry != NULL; entry = entry->next)
                    {
                        if (SDL_strcmp(entry->path, embedded_key) == 0)
                        {
                            texture = &entry->texture;
                            break;
                        }
                    }
                    if (texture == NULL && embedded_key != NULL)
                    {
                        entry = (slayer3d_texture_cache_entry *)SDL_calloc(1, sizeof(*entry));
                        if (entry != NULL)
                        {
                            entry->path = embedded_key;
                            if (slayer3d_create_texture_from_image(&model->embedded_textures[tex_idx], &entry->texture))
                            {
                                entry->texture.wrap_u = SLAYER3D_TEXTURE_WRAP_REPEAT;
                                entry->texture.wrap_v = SLAYER3D_TEXTURE_WRAP_REPEAT;
                                entry->next = context->texture_cache;
                                context->texture_cache = entry;
                                texture = &entry->texture;
                                embedded_key = NULL;
                            }
                            else
                            {
                                SDL_free(entry->path);
                                SDL_free(entry);
                            }
                        }
                    }
                    SDL_free(embedded_key);
                }
            }
            else
            {
                ok = slayer3d_texture_cache_get_or_load_asset(&context->texture_cache, assets, model->source_path,
                                                              material->albedo_map, &texture);
                if (!ok)
                {
                    return false;
                }
            }
        }
    }

    {
        slayer3d_lighting_params lp_storage;
        const slayer3d_lighting_params *lp_ptr = NULL;

        if (context->shading_mode != SLAYER3D_SHADING_UNLIT && mesh->normals != NULL)
        {
            slayer3d_build_lighting_params(context, &lp_storage);
            lp_storage.baked_light_mode = mesh->colors_are_baked_light;
            if (material != NULL)
            {
                lp_storage.metallic = material->metallic;
                lp_storage.roughness = material->roughness;
                /* Until emissive texture sampling is supported, do not apply
                 * the factor uniformly when the material expects a mask. */
                if (material->emissive_map == NULL || material->emissive_map[0] == '\0')
                {
                    lp_storage.emissive[0] = material->emissive[0];
                    lp_storage.emissive[1] = material->emissive[1];
                    lp_storage.emissive[2] = material->emissive[2];
                }
            }
            else
            {
                lp_storage.roughness = 1.0f;
            }
            lp_ptr = &lp_storage;
        }

        ok = slayer3d_draw_mesh_internal(context, mesh, texture, lightmap_texture, mesh_modulate, lp_ptr,
                                         joint_matrices, joint_count, true, NULL, NULL);
    }
    if (ok)
    {
        ++context->stats.model_mesh_draws;
        context->stats.model_triangles_submitted +=
            (Uint64)((mesh->index_count > 0 ? mesh->index_count : mesh->vertex_count) / 3);
    }
    return ok;
}

static char *slayer3d_embedded_texture_cache_key(const slayer3d_model *model, const char *embedded_ref)
{
    const char *source = model != NULL && model->source_path != NULL ? model->source_path : "<memory-model>";
    const char *ref = embedded_ref != NULL ? embedded_ref : "";
    const size_t source_len = SDL_strlen(source);
    const size_t ref_len = SDL_strlen(ref);
    char *key = (char *)SDL_malloc(source_len + ref_len + 3U);
    if (key == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }
    SDL_memcpy(key, source, source_len);
    key[source_len] = ':';
    key[source_len + 1U] = ':';
    SDL_memcpy(key + source_len + 2U, ref, ref_len + 1U);
    return key;
}

/**
 * Recursively draw a node and its children, applying local TRS transforms
 * via the matrix stack.
 */
static bool slayer3d_draw_model_node(slayer3d_render_context *context, const slayer3d_asset_resolver *assets,
                                     const slayer3d_model *model, int node_index, slayer3d_vec4 tint_modulate,
                                     const slayer3d_mat4 *joint_matrices, int joint_count)
{
    if (node_index < 0 || node_index >= model->node_count)
    {
        return true;
    }

    const slayer3d_model_node *node = &model->nodes[node_index];

    if (!slayer3d_push_matrix(context))
    {
        return false;
    }

    /* Apply this node's local TRS transform. */
    slayer3d_mat4 local;
    if (node->has_matrix)
    {
        SDL_memcpy(local.m, node->local_matrix, sizeof(local.m));
    }
    else
    {
        local = slayer3d_mat4_from_trs_node(node->translation, node->rotation, node->scale);
    }
    context->model_stack[context->model_stack_depth - 1] =
        slayer3d_mat4_multiply(context->model_stack[context->model_stack_depth - 1], local);
    slayer3d_update_current_model_matrices(context);

    bool ok = true;

    /* Draw this node's mesh if it has one. */
    if (node->mesh_index >= 0 && node->mesh_index < model->mesh_count)
    {
        ok = slayer3d_draw_model_mesh(context, assets, model, node->mesh_index, NULL, tint_modulate, joint_matrices,
                                      joint_count);
    }

    /* Recurse into children. */
    for (int c = 0; ok && node->children != NULL && c < node->child_count; ++c)
    {
        ok = slayer3d_draw_model_node(context, assets, model, node->children[c], tint_modulate, joint_matrices,
                                      joint_count);
    }

    if (!slayer3d_pop_matrix(context))
    {
        return false;
    }

    return ok;
}

static bool slayer3d_model_has_node_roots(const slayer3d_model *model)
{
    return model != NULL && model->nodes != NULL && model->node_count > 0 && model->root_nodes != NULL &&
           model->root_count > 0;
}

bool slayer3d_draw_model(slayer3d_render_context *context, const slayer3d_model *model, slayer3d_vec3 position,
                         float scale, slayer3d_color tint)
{
    return slayer3d_draw_model_ex(context, model, position, slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f,
                                  slayer3d_vec3_make(scale, scale, scale), tint);
}

bool slayer3d_draw_model_ex(slayer3d_render_context *context, const slayer3d_model *model, slayer3d_vec3 position,
                            slayer3d_vec3 rotation_axis, float rotation_angle_radians, slayer3d_vec3 scale,
                            slayer3d_color tint)
{
    return slayer3d_draw_model_ex_with_assets(context, NULL, model, position, rotation_axis, rotation_angle_radians,
                                              scale, tint);
}

bool slayer3d_draw_model_ex_with_assets(slayer3d_render_context *context, const slayer3d_asset_resolver *assets,
                                        const slayer3d_model *model, slayer3d_vec3 position,
                                        slayer3d_vec3 rotation_axis, float rotation_angle_radians, slayer3d_vec3 scale,
                                        slayer3d_color tint)
{
    const slayer3d_vec4 tint_modulate = slayer3d_color_to_modulate(tint);
    bool ok = false;

    if (!slayer3d_require_mode_3d(context, "slayer3d_draw_model_ex"))
    {
        return false;
    }
    if (model == NULL)
    {
        return SDL_InvalidParamError("model");
    }
    if (model->meshes == NULL || model->mesh_count <= 0)
    {
        return SDL_SetError("Model draw requires at least one mesh.");
    }
    if (!slayer3d_push_matrix(context))
    {
        return false;
    }

    ok = slayer3d_translate(context, position.x, position.y, position.z);
    if (ok && rotation_angle_radians != 0.0f)
    {
        ok = slayer3d_rotate(context, rotation_axis, rotation_angle_radians);
    }
    if (ok)
    {
        ok = slayer3d_scale(context, scale.x, scale.y, scale.z);
    }

    if (ok && slayer3d_model_has_node_roots(model))
    {
        for (int r = 0; ok && r < model->root_count; ++r)
        {
            ok = slayer3d_draw_model_node(context, assets, model, model->root_nodes[r], tint_modulate, NULL, 0);
        }
    }
    else
    {
        for (int mesh_index = 0; ok && mesh_index < model->mesh_count; ++mesh_index)
        {
            ok = slayer3d_draw_model_mesh(context, assets, model, mesh_index, NULL, tint_modulate, NULL, 0);
        }
    }

    if (!slayer3d_pop_matrix(context))
    {
        return false;
    }

    return ok;
}

bool slayer3d_draw_model_euler_with_assets(slayer3d_render_context *context, const slayer3d_asset_resolver *assets,
                                           const slayer3d_model *model, slayer3d_vec3 position,
                                           slayer3d_vec3 rotation_radians, slayer3d_vec3 scale, slayer3d_color tint)
{
    const slayer3d_vec4 tint_modulate = slayer3d_color_to_modulate(tint);
    bool ok = false;

    if (!slayer3d_require_mode_3d(context, "slayer3d_draw_model_euler_with_assets"))
        return false;
    if (model == NULL)
        return SDL_InvalidParamError("model");
    if (model->meshes == NULL || model->mesh_count <= 0)
        return SDL_SetError("Model draw requires at least one mesh.");

    if (!slayer3d_push_matrix(context))
        return false;

    ok = slayer3d_translate(context, position.x, position.y, position.z);
    if (ok && rotation_radians.y != 0.0f)
        ok = slayer3d_rotate(context, slayer3d_vec3_make(0.0f, 1.0f, 0.0f), rotation_radians.y);
    if (ok && rotation_radians.x != 0.0f)
        ok = slayer3d_rotate(context, slayer3d_vec3_make(1.0f, 0.0f, 0.0f), rotation_radians.x);
    if (ok && rotation_radians.z != 0.0f)
        ok = slayer3d_rotate(context, slayer3d_vec3_make(0.0f, 0.0f, 1.0f), rotation_radians.z);
    if (ok)
        ok = slayer3d_scale(context, scale.x, scale.y, scale.z);

    if (ok && slayer3d_model_has_node_roots(model))
    {
        for (int r = 0; ok && r < model->root_count; ++r)
            ok = slayer3d_draw_model_node(context, assets, model, model->root_nodes[r], tint_modulate, NULL, 0);
    }
    else
    {
        for (int mesh_index = 0; ok && mesh_index < model->mesh_count; ++mesh_index)
            ok = slayer3d_draw_model_mesh(context, assets, model, mesh_index, NULL, tint_modulate, NULL, 0);
    }

    if (!slayer3d_pop_matrix(context))
        return false;

    return ok;
}

bool slayer3d_draw_model_skinned(slayer3d_render_context *context, const slayer3d_model *model, slayer3d_vec3 position,
                                 slayer3d_vec3 rotation_axis, float rotation_angle_radians, slayer3d_vec3 scale,
                                 slayer3d_color tint, const slayer3d_mat4 *joint_matrices)
{
    return slayer3d_draw_model_skinned_with_assets(context, NULL, model, position, rotation_axis,
                                                   rotation_angle_radians, scale, tint, joint_matrices);
}

bool slayer3d_draw_model_skinned_with_assets(slayer3d_render_context *context, const slayer3d_asset_resolver *assets,
                                             const slayer3d_model *model, slayer3d_vec3 position,
                                             slayer3d_vec3 rotation_axis, float rotation_angle_radians,
                                             slayer3d_vec3 scale, slayer3d_color tint,
                                             const slayer3d_mat4 *joint_matrices)
{
    /* Re-use draw_model_ex logic but pass joint_matrices through.
     * For now, call draw_model_ex for the non-skinned parts and
     * handle skinning by replacing the NULL. */
    /* This is a simplified version — we duplicate the draw_model_ex
     * body but pass joint_matrices to draw_mesh_internal. */
    const slayer3d_vec4 tint_modulate = slayer3d_color_to_modulate(tint);
    bool ok = false;

    if (!slayer3d_require_mode_3d(context, "slayer3d_draw_model_skinned"))
    {
        return false;
    }
    if (model == NULL)
    {
        return SDL_InvalidParamError("model");
    }
    if (model->meshes == NULL || model->mesh_count <= 0)
    {
        return SDL_SetError("Model draw requires at least one mesh.");
    }
    const int joint_count = (joint_matrices != NULL && model->skeleton != NULL) ? model->skeleton->joint_count : 0;

    if (!slayer3d_push_matrix(context))
    {
        return false;
    }

    ok = slayer3d_translate(context, position.x, position.y, position.z);
    if (ok && rotation_angle_radians != 0.0f)
    {
        ok = slayer3d_rotate(context, rotation_axis, rotation_angle_radians);
    }
    if (ok)
    {
        ok = slayer3d_scale(context, scale.x, scale.y, scale.z);
    }

    if (ok && slayer3d_model_has_node_roots(model))
    {
        for (int r = 0; ok && r < model->root_count; ++r)
        {
            ok = slayer3d_draw_model_node(context, assets, model, model->root_nodes[r], tint_modulate, joint_matrices,
                                          joint_count);
        }
    }
    else
    {
        for (int mesh_index = 0; ok && mesh_index < model->mesh_count; ++mesh_index)
        {
            ok = slayer3d_draw_model_mesh(context, assets, model, mesh_index, NULL, tint_modulate, joint_matrices,
                                          joint_count);
        }
    }

    if (!slayer3d_pop_matrix(context))
    {
        return false;
    }

    return ok;
}

bool slayer3d_draw_rect_overlay(slayer3d_render_context *context, float x, float y, float w, float h,
                                slayer3d_color color)
{
    SDL_Rect scissor_rect = {0, 0, 0, 0};
    bool scissor_enabled = false;

    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (w <= 0.0f || h <= 0.0f)
    {
        return true;
    }
    if (slayer3d_is_in_mode_3d(context))
    {
        return SDL_SetError(
            "slayer3d_draw_rect_overlay must be called outside slayer3d_begin_mode_3d / slayer3d_end_mode_3d");
    }
    if (!slayer3d_overlay_capture_scissor(context, &scissor_enabled, &scissor_rect))
    {
        return false;
    }

    if (!context->gl)
    {
        SDL_Rect rect = {(int)x, (int)y, (int)w, (int)h};
        rect = slayer3d_rect_overlay_intersect_scissor(rect, scissor_enabled, &scissor_rect);
        if (rect.w <= 0 || rect.h <= 0)
        {
            return true;
        }
        return slayer3d_clear_render_context_rect(context, &rect, color);
    }

    const int ctx_w = slayer3d_get_render_context_width(context);
    const int ctx_h = slayer3d_get_render_context_height(context);
    if (ctx_w <= 0 || ctx_h <= 0)
    {
        return SDL_SetError("Invalid render context dimensions");
    }

    const float hx = (float)ctx_w * 0.5f;
    const float hy = (float)ctx_h * 0.5f;
    const float wx0 = x - hx;
    const float wx1 = x + w - hx;
    const float wy0 = hy - y;
    const float wy1 = hy - (y + h);

    float positions[18] = {
        wx0, wy0, 0.0f, wx0, wy1, 0.0f, wx1, wy1, 0.0f, wx0, wy0, 0.0f, wx1, wy1, 0.0f, wx1, wy0, 0.0f,
    };
    float uvs[12] = {0};
    float mvp[16] = {0};
    float tint[4] = {(float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f,
                     (float)color.a / 255.0f};

    mvp[0] = 1.0f / hx;
    mvp[5] = 1.0f / hy;
    mvp[10] = -1.0f;
    mvp[15] = 1.0f;

    return slayer3d_gl_append_overlay(context->gl, positions, uvs, 6, mvp, tint, NULL, scissor_enabled,
                                      scissor_enabled ? &scissor_rect : NULL, SLAYER3D_OVERLAY_EFFECT_NONE, 0.0f, 0u,
                                      NULL, NULL);
}

static Uint8 overlay_to_u8(float value)
{
    return (Uint8)SDL_clamp((int)(value * 255.0f + 0.5f), 0, 255);
}

static void overlay_blend_pixel(Uint8 *pixel, slayer3d_color color)
{
    if (pixel == NULL || color.a == 0)
    {
        return;
    }
    if (color.a == 255)
    {
        pixel[0] = color.r;
        pixel[1] = color.g;
        pixel[2] = color.b;
        pixel[3] = color.a;
        return;
    }

    const float src_a = (float)color.a / 255.0f;
    const float dst_a = (float)pixel[3] / 255.0f;
    const float out_a = src_a + dst_a * (1.0f - src_a);
    float out_r = (float)color.r * src_a;
    float out_g = (float)color.g * src_a;
    float out_b = (float)color.b * src_a;
    if (out_a > 0.0f)
    {
        out_r += (float)pixel[0] * dst_a * (1.0f - src_a);
        out_g += (float)pixel[1] * dst_a * (1.0f - src_a);
        out_b += (float)pixel[2] * dst_a * (1.0f - src_a);
        out_r /= out_a;
        out_g /= out_a;
        out_b /= out_a;
    }
    pixel[0] = (Uint8)SDL_clamp((int)(out_r + 0.5f), 0, 255);
    pixel[1] = (Uint8)SDL_clamp((int)(out_g + 0.5f), 0, 255);
    pixel[2] = (Uint8)SDL_clamp((int)(out_b + 0.5f), 0, 255);
    pixel[3] = (Uint8)SDL_clamp((int)(out_a * 255.0f + 0.5f), 0, 255);
}

static bool draw_texture_overlay_software(slayer3d_render_context *context, const slayer3d_texture2d *texture, float x,
                                          float y, float w, float h, slayer3d_color tint, bool scissor_enabled,
                                          const SDL_Rect *scissor_rect, slayer3d_overlay_effect effect,
                                          float effect_progress, Uint32 effect_seed)
{
    SDL_Rect rect = {(int)x, (int)y, (int)w, (int)h};
    rect = slayer3d_rect_overlay_intersect_scissor(rect, scissor_enabled, scissor_rect);
    if (rect.w <= 0 || rect.h <= 0)
    {
        return true;
    }

    const float inv_w = 1.0f / w;
    const float inv_h = 1.0f / h;
    const int columns = SDL_max(24, texture->width / 16);
    for (int py = rect.y; py < rect.y + rect.h; ++py)
    {
        for (int px = rect.x; px < rect.x + rect.w; ++px)
        {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            float a = 0.0f;
            float u = ((float)px + 0.5f - x) * inv_w;
            float v = ((float)py + 0.5f - y) * inv_h;
            if (effect == SLAYER3D_OVERLAY_EFFECT_MELT)
            {
                float melt_u = u;
                float melt_v = v;
                if (!slayer3d_overlay_melt_sample(u, v, effect_progress, effect_seed, columns, &melt_u, &melt_v))
                {
                    continue;
                }
                u = melt_u;
                v = melt_v;
                if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
                {
                    continue;
                }
            }
            slayer3d_texture_sample_rgba(texture, u, v, 0.0f, &r, &g, &b, &a);
            const slayer3d_color color = {
                overlay_to_u8(r * ((float)tint.r / 255.0f)),
                overlay_to_u8(g * ((float)tint.g / 255.0f)),
                overlay_to_u8(b * ((float)tint.b / 255.0f)),
                overlay_to_u8(a * ((float)tint.a / 255.0f)),
            };
            overlay_blend_pixel(&context->color_buffer[((size_t)py * (size_t)context->width + (size_t)px) * 4u], color);
        }
    }
    return true;
}

bool slayer3d_draw_texture_overlay(slayer3d_render_context *context, const slayer3d_texture2d *texture, float x,
                                   float y, float w, float h, slayer3d_color tint, slayer3d_overlay_effect effect,
                                   float effect_progress, Uint32 effect_seed)
{
    return slayer3d_draw_texture_overlay_shader(context, texture, x, y, w, h, tint, effect, effect_progress,
                                                effect_seed, NULL, NULL);
}

bool slayer3d_draw_texture_overlay_shader(slayer3d_render_context *context, const slayer3d_texture2d *texture, float x,
                                          float y, float w, float h, slayer3d_color tint,
                                          slayer3d_overlay_effect effect, float effect_progress, Uint32 effect_seed,
                                          const char *shader_vertex_source, const char *shader_fragment_source)
{
    SDL_Rect scissor_rect = {0, 0, 0, 0};
    bool scissor_enabled = false;

    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (texture == NULL || texture->pixels == NULL || texture->width <= 0 || texture->height <= 0)
    {
        return SDL_InvalidParamError("texture");
    }
    if (w <= 0.0f || h <= 0.0f || tint.a == 0)
    {
        return true;
    }
    if (slayer3d_is_in_mode_3d(context))
    {
        return SDL_SetError("slayer3d_draw_texture_overlay must be called outside slayer3d_begin_mode_3d / "
                            "slayer3d_end_mode_3d");
    }
    if (!slayer3d_overlay_capture_scissor(context, &scissor_enabled, &scissor_rect))
    {
        return false;
    }

    if (!context->gl)
    {
        return draw_texture_overlay_software(context, texture, x, y, w, h, tint, scissor_enabled, &scissor_rect, effect,
                                             effect_progress, effect_seed);
    }

    const int ctx_w = slayer3d_get_render_context_width(context);
    const int ctx_h = slayer3d_get_render_context_height(context);
    if (ctx_w <= 0 || ctx_h <= 0)
    {
        return SDL_SetError("Invalid render context dimensions");
    }

    const float hx = (float)ctx_w * 0.5f;
    const float hy = (float)ctx_h * 0.5f;
    const float wx0 = x - hx;
    const float wx1 = x + w - hx;
    const float wy0 = hy - y;
    const float wy1 = hy - (y + h);

    float positions[18] = {
        wx0, wy0, 0.0f, wx0, wy1, 0.0f, wx1, wy1, 0.0f, wx0, wy0, 0.0f, wx1, wy1, 0.0f, wx1, wy0, 0.0f,
    };
    float uvs[12] = {
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
    };
    float mvp[16] = {0};
    float gl_tint[4] = {(float)tint.r / 255.0f, (float)tint.g / 255.0f, (float)tint.b / 255.0f, (float)tint.a / 255.0f};

    mvp[0] = 1.0f / hx;
    mvp[5] = 1.0f / hy;
    mvp[10] = -1.0f;
    mvp[15] = 1.0f;

    return slayer3d_gl_append_overlay(context->gl, positions, uvs, 6, mvp, gl_tint, texture, scissor_enabled,
                                      scissor_enabled ? &scissor_rect : NULL, effect, effect_progress, effect_seed,
                                      shader_vertex_source, shader_fragment_source);
}

bool slayer3d_get_framebuffer_pixel(const slayer3d_render_context *context, int x, int y, slayer3d_color *out_color)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (out_color == NULL)
    {
        return SDL_InvalidParamError("out_color");
    }
    if (x < 0 || x >= context->width || y < 0 || y >= context->height)
    {
        return SDL_SetError("Framebuffer pixel (%d, %d) is outside the %dx%d backbuffer.", x, y, context->width,
                            context->height);
    }

    const int index = (y * context->width) + x;
    const Uint8 *pixel = &context->color_buffer[index * 4];
    out_color->r = pixel[0];
    out_color->g = pixel[1];
    out_color->b = pixel[2];
    out_color->a = pixel[3];
    return true;
}

bool slayer3d_get_framebuffer_depth(const slayer3d_render_context *context, int x, int y, float *out_depth)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (out_depth == NULL)
    {
        return SDL_InvalidParamError("out_depth");
    }
    if (x < 0 || x >= context->width || y < 0 || y >= context->height)
    {
        return SDL_SetError("Framebuffer depth (%d, %d) is outside the %dx%d backbuffer.", x, y, context->width,
                            context->height);
    }

    *out_depth = context->depth_buffer[(y * context->width) + x];
    return true;
}

/* ------------------------------------------------------------------ */
/* Level drawing with portal visibility                                */
/* ------------------------------------------------------------------ */

bool slayer3d_draw_level(slayer3d_render_context *context, const slayer3d_level *level,
                         const slayer3d_visibility_result *vis, slayer3d_color tint)
{
    return slayer3d_draw_level_with_assets(context, NULL, level, vis, tint);
}

bool slayer3d_draw_level_with_assets(slayer3d_render_context *context, const slayer3d_asset_resolver *assets,
                                     const slayer3d_level *level, const slayer3d_visibility_result *vis,
                                     slayer3d_color tint)
{
    if (!slayer3d_require_mode_3d(context, "slayer3d_draw_level"))
        return false;
    if (!level)
        return SDL_InvalidParamError("level");

    const slayer3d_model *model = &level->model;
    if (!model->meshes || model->mesh_count <= 0)
        return SDL_SetError("Level has no meshes.");

    const slayer3d_vec4 tint_modulate = slayer3d_color_to_modulate(tint);
    bool ok = true;

    for (int i = 0; ok && i < model->mesh_count; ++i)
    {
        /* Portal visibility: skip meshes in hidden sectors. */
        if (vis && vis->sector_visible && level->mesh_sector_ids)
        {
            int sid = level->mesh_sector_ids[i];
            if (sid >= 0 && sid < level->sector_count && !vis->sector_visible[sid])
                continue;
        }
        ok = slayer3d_draw_model_mesh(context, assets, model, i,
                                      level->lightmap_texture.pixels != NULL ? &level->lightmap_texture : NULL,
                                      tint_modulate, NULL, 0);
    }

    return ok;
}
