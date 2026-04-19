#include "sdl3d/drawing3d.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "rasterizer.h"
#include "render_context_internal.h"
#include "texture_internal.h"

#include "lighting_internal.h"

static const int SDL3D_MODEL_STACK_INITIAL_CAPACITY = 8;

static float sdl3d_clamp01(float value)
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

static Uint8 sdl3d_color_channel_clamp(float value)
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

static sdl3d_vec4 sdl3d_color_to_modulate(sdl3d_color color)
{
    sdl3d_vec4 out;
    out.x = (float)color.r / 255.0f;
    out.y = (float)color.g / 255.0f;
    out.z = (float)color.b / 255.0f;
    out.w = (float)color.a / 255.0f;
    return out;
}

static bool sdl3d_ensure_model_stack_capacity(sdl3d_render_context *context, int required_depth)
{
    if (context->model_stack_capacity >= required_depth)
    {
        return true;
    }

    int new_capacity = context->model_stack_capacity;
    if (new_capacity <= 0)
    {
        new_capacity = SDL3D_MODEL_STACK_INITIAL_CAPACITY;
    }

    while (new_capacity < required_depth)
    {
        new_capacity *= 2;
    }

    sdl3d_mat4 *new_stack = (sdl3d_mat4 *)SDL_realloc(context->model_stack, (size_t)new_capacity * sizeof(*new_stack));
    if (new_stack == NULL)
    {
        return SDL_OutOfMemory();
    }

    context->model_stack = new_stack;
    context->model_stack_capacity = new_capacity;
    return true;
}

static void sdl3d_update_current_model_matrices(sdl3d_render_context *context)
{
    if (context->model_stack_depth <= 0)
    {
        context->model = sdl3d_mat4_identity();
    }
    else
    {
        context->model = context->model_stack[context->model_stack_depth - 1];
    }

    context->model_view_projection = sdl3d_mat4_multiply(context->view_projection, context->model);
}

bool sdl3d_begin_mode_3d(sdl3d_render_context *context, sdl3d_camera3d camera)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (context->in_mode_3d)
    {
        return SDL_SetError("sdl3d_begin_mode_3d called while already in 3D mode.");
    }

    if (!sdl3d_camera3d_compute_matrices(&camera, context->width, context->height, context->near_plane,
                                         context->far_plane, &context->view, &context->projection))
    {
        return false;
    }

    context->view_projection = sdl3d_mat4_multiply(context->projection, context->view);
    if (!sdl3d_ensure_model_stack_capacity(context, 1))
    {
        return false;
    }

    context->model_stack_depth = 1;
    context->model_stack[0] = sdl3d_mat4_identity();
    sdl3d_update_current_model_matrices(context);
    context->in_mode_3d = true;
    return true;
}

bool sdl3d_end_mode_3d(sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (!context->in_mode_3d)
    {
        return SDL_SetError("sdl3d_end_mode_3d called while not in 3D mode.");
    }

    context->in_mode_3d = false;
    context->model_stack_depth = 0;
    context->model = sdl3d_mat4_identity();
    context->model_view_projection = context->view_projection;
    return true;
}

bool sdl3d_is_in_mode_3d(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return false;
    }
    return context->in_mode_3d;
}

bool sdl3d_set_backface_culling_enabled(sdl3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->backface_culling_enabled = enabled;
    return true;
}

bool sdl3d_is_backface_culling_enabled(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return false;
    }

    return context->backface_culling_enabled;
}

bool sdl3d_set_wireframe_enabled(sdl3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->wireframe_enabled = enabled;
    return true;
}

bool sdl3d_is_wireframe_enabled(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return false;
    }

    return context->wireframe_enabled;
}

bool sdl3d_set_depth_planes(sdl3d_render_context *context, float near_plane, float far_plane)
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

static bool sdl3d_require_mode_3d(const sdl3d_render_context *context, const char *function_name)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (!context->in_mode_3d)
    {
        return SDL_SetError("%s called outside sdl3d_begin_mode_3d / sdl3d_end_mode_3d.", function_name);
    }
    return true;
}

static bool sdl3d_validate_texture_for_draw(const sdl3d_texture2d *texture)
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

static sdl3d_vec4 sdl3d_mesh_vertex_modulate(const sdl3d_mesh *mesh, unsigned int vertex_index, sdl3d_vec4 base)
{
    sdl3d_vec4 out = base;

    if (mesh->colors != NULL)
    {
        const float *color = &mesh->colors[(size_t)vertex_index * 4U];
        out.x = sdl3d_clamp01(base.x * color[0]);
        out.y = sdl3d_clamp01(base.y * color[1]);
        out.z = sdl3d_clamp01(base.z * color[2]);
        out.w = sdl3d_clamp01(base.w * color[3]);
    }

    return out;
}

static sdl3d_vec3 sdl3d_mesh_position(const sdl3d_mesh *mesh, unsigned int vertex_index)
{
    const float *position = &mesh->positions[(size_t)vertex_index * 3U];
    return sdl3d_vec3_make(position[0], position[1], position[2]);
}

static sdl3d_vec2 sdl3d_mesh_uv(const sdl3d_mesh *mesh, unsigned int vertex_index)
{
    const float *uv = &mesh->uvs[(size_t)vertex_index * 2U];
    sdl3d_vec2 out;
    out.x = uv[0];
    out.y = uv[1];
    return out;
}

static sdl3d_vec3 sdl3d_mesh_normal(const sdl3d_mesh *mesh, unsigned int vertex_index)
{
    const float *n = &mesh->normals[(size_t)vertex_index * 3U];
    return sdl3d_vec3_make(n[0], n[1], n[2]);
}

/* Build lighting params from render context state. Material fields
 * (metallic, roughness, emissive) are set by the caller. */
static void sdl3d_build_lighting_params(const sdl3d_render_context *context, sdl3d_lighting_params *lp)
{
    SDL_zerop(lp);
    lp->lights = context->lights;
    lp->light_count = context->light_count;
    lp->ambient[0] = context->ambient[0];
    lp->ambient[1] = context->ambient[1];
    lp->ambient[2] = context->ambient[2];
    lp->fog = context->fog;
    lp->tonemap_mode = context->tonemap_mode;
    lp->shadow_bias = context->shadow_bias;
    for (int i = 0; i < SDL3D_MAX_LIGHTS; ++i)
    {
        lp->shadow_depth[i] = context->shadow_depth[i];
        lp->shadow_vp[i] = context->shadow_vp[i];
        lp->shadow_enabled[i] = context->shadow_enabled[i];
    }
    {
        const sdl3d_mat4 v = context->view;
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
static sdl3d_vec3 sdl3d_transform_normal(sdl3d_mat4 m, sdl3d_vec3 n)
{
    sdl3d_vec3 out;
    out.x = m.m[0] * n.x + m.m[4] * n.y + m.m[8] * n.z;
    out.y = m.m[1] * n.x + m.m[5] * n.y + m.m[9] * n.z;
    out.z = m.m[2] * n.x + m.m[6] * n.y + m.m[10] * n.z;
    return sdl3d_vec3_normalize(out);
}

/* Transform an object-space position to world space. */
static sdl3d_vec3 sdl3d_transform_position(sdl3d_mat4 m, sdl3d_vec3 p)
{
    sdl3d_vec4 w = sdl3d_mat4_transform_vec4(m, sdl3d_vec4_from_vec3(p, 1.0f));
    return sdl3d_vec3_make(w.x, w.y, w.z);
}

/* Evaluate PBR shading at a single point and return the result as an sdl3d_color.
 * Used by FLAT (once per triangle) and GOURAUD (once per vertex). */
static sdl3d_color sdl3d_shade_point(const sdl3d_lighting_params *lp, float albedo_r, float albedo_g, float albedo_b,
                                     float albedo_a, sdl3d_vec3 world_normal, sdl3d_vec3 world_pos)
{
    float lit_r, lit_g, lit_b;
    sdl3d_color c;
    sdl3d_shade_fragment_pbr(lp, albedo_r, albedo_g, albedo_b, world_normal.x, world_normal.y, world_normal.z,
                             world_pos.x, world_pos.y, world_pos.z, &lit_r, &lit_g, &lit_b);
    sdl3d_tonemap(lp->tonemap_mode, &lit_r, &lit_g, &lit_b);

    /* Fog. */
    if (lp->fog.mode != SDL3D_FOG_NONE)
    {
        float dx = world_pos.x - lp->camera_pos.x;
        float dy = world_pos.y - lp->camera_pos.y;
        float dz = world_pos.z - lp->camera_pos.z;
        float dist = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
        float fog_f = sdl3d_compute_fog_factor(&lp->fog, dist);
        lit_r = lit_r * (1.0f - fog_f) + lp->fog.color[0] * fog_f;
        lit_g = lit_g * (1.0f - fog_f) + lp->fog.color[1] * fog_f;
        lit_b = lit_b * (1.0f - fog_f) + lp->fog.color[2] * fog_f;
    }

    c.r = sdl3d_color_channel_clamp(lit_r * 255.0f);
    c.g = sdl3d_color_channel_clamp(lit_g * 255.0f);
    c.b = sdl3d_color_channel_clamp(lit_b * 255.0f);
    c.a = sdl3d_color_channel_clamp(albedo_a * 255.0f);
    return c;
}

static bool sdl3d_draw_mesh_internal(sdl3d_render_context *context, const sdl3d_mesh *mesh,
                                     const sdl3d_texture2d *texture, sdl3d_vec4 base_modulate,
                                     const sdl3d_lighting_params *lighting)
{
    const bool indexed = mesh->indices != NULL;
    const int triangle_count = indexed ? (mesh->index_count / 3) : (mesh->vertex_count / 3);
    const bool lit = lighting != NULL && (context->shading_mode == SDL3D_SHADING_FLAT || mesh->normals != NULL);
    sdl3d_framebuffer framebuffer;

    if (!sdl3d_require_mode_3d(context, "sdl3d_draw_mesh"))
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
    if (!sdl3d_validate_texture_for_draw(texture))
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

    framebuffer = sdl3d_framebuffer_from_context(context);

    for (int triangle = 0; triangle < triangle_count; ++triangle)
    {
        const unsigned int i0 = indexed ? mesh->indices[triangle * 3 + 0] : (unsigned int)(triangle * 3 + 0);
        const unsigned int i1 = indexed ? mesh->indices[triangle * 3 + 1] : (unsigned int)(triangle * 3 + 1);
        const unsigned int i2 = indexed ? mesh->indices[triangle * 3 + 2] : (unsigned int)(triangle * 3 + 2);
        sdl3d_vec2 uv0 = {0.0f, 0.0f};
        sdl3d_vec2 uv1 = {0.0f, 0.0f};
        sdl3d_vec2 uv2 = {0.0f, 0.0f};

        if ((int)i0 >= mesh->vertex_count || (int)i1 >= mesh->vertex_count || (int)i2 >= mesh->vertex_count)
        {
            return SDL_SetError("Mesh index out of range for vertex_count=%d.", mesh->vertex_count);
        }

        if (texture != NULL)
        {
            uv0 = sdl3d_mesh_uv(mesh, i0);
            uv1 = sdl3d_mesh_uv(mesh, i1);
            uv2 = sdl3d_mesh_uv(mesh, i2);
        }

        if (lit && context->shading_mode == SDL3D_SHADING_FLAT)
        {
            /* FLAT: one PBR eval per triangle using face normal at centroid. */
            sdl3d_vec3 p0 = sdl3d_mesh_position(mesh, i0);
            sdl3d_vec3 p1 = sdl3d_mesh_position(mesh, i1);
            sdl3d_vec3 p2 = sdl3d_mesh_position(mesh, i2);
            sdl3d_vec3 edge1 = sdl3d_vec3_sub(p1, p0);
            sdl3d_vec3 edge2 = sdl3d_vec3_sub(p2, p0);
            sdl3d_vec3 fn = sdl3d_vec3_normalize(sdl3d_vec3_cross(edge1, edge2));
            sdl3d_vec3 wn = sdl3d_transform_normal(context->model, fn);
            sdl3d_vec3 wp0 = sdl3d_transform_position(context->model, p0);
            sdl3d_vec3 wp1 = sdl3d_transform_position(context->model, p1);
            sdl3d_vec3 wp2 = sdl3d_transform_position(context->model, p2);
            sdl3d_vec3 centroid = sdl3d_vec3_scale(sdl3d_vec3_add(sdl3d_vec3_add(wp0, wp1), wp2), 1.0f / 3.0f);
            sdl3d_vec4 mod0 = sdl3d_mesh_vertex_modulate(mesh, i0, base_modulate);
            sdl3d_color flat_color = sdl3d_shade_point(lighting, mod0.x, mod0.y, mod0.z, mod0.w, wn, centroid);
            sdl3d_rasterize_triangle(&framebuffer, context->model_view_projection, p0, p1, p2, flat_color,
                                     context->backface_culling_enabled, context->wireframe_enabled);
        }
        else if (lit && context->shading_mode == SDL3D_SHADING_GOURAUD)
        {
            /* GOURAUD: PBR eval at each vertex, interpolate colors. */
            sdl3d_vec3 p0 = sdl3d_mesh_position(mesh, i0);
            sdl3d_vec3 p1 = sdl3d_mesh_position(mesh, i1);
            sdl3d_vec3 p2 = sdl3d_mesh_position(mesh, i2);
            sdl3d_vec3 wn0, wn1, wn2;
            if (mesh->normals != NULL)
            {
                wn0 = sdl3d_transform_normal(context->model, sdl3d_mesh_normal(mesh, i0));
                wn1 = sdl3d_transform_normal(context->model, sdl3d_mesh_normal(mesh, i1));
                wn2 = sdl3d_transform_normal(context->model, sdl3d_mesh_normal(mesh, i2));
            }
            else
            {
                sdl3d_vec3 fn = sdl3d_vec3_normalize(sdl3d_vec3_cross(sdl3d_vec3_sub(p1, p0), sdl3d_vec3_sub(p2, p0)));
                wn0 = wn1 = wn2 = sdl3d_transform_normal(context->model, fn);
            }
            sdl3d_vec3 wp0 = sdl3d_transform_position(context->model, p0);
            sdl3d_vec3 wp1 = sdl3d_transform_position(context->model, p1);
            sdl3d_vec3 wp2 = sdl3d_transform_position(context->model, p2);
            sdl3d_vec4 mod0 = sdl3d_mesh_vertex_modulate(mesh, i0, base_modulate);
            sdl3d_vec4 mod1 = sdl3d_mesh_vertex_modulate(mesh, i1, base_modulate);
            sdl3d_vec4 mod2 = sdl3d_mesh_vertex_modulate(mesh, i2, base_modulate);
            sdl3d_color c0 = sdl3d_shade_point(lighting, mod0.x, mod0.y, mod0.z, mod0.w, wn0, wp0);
            sdl3d_color c1 = sdl3d_shade_point(lighting, mod1.x, mod1.y, mod1.z, mod1.w, wn1, wp1);
            sdl3d_color c2 = sdl3d_shade_point(lighting, mod2.x, mod2.y, mod2.z, mod2.w, wn2, wp2);
            sdl3d_rasterize_triangle_colored(&framebuffer, context->model_view_projection, p0, p1, p2, c0, c1, c2,
                                             context->backface_culling_enabled, context->wireframe_enabled);
        }
        else if (lit)
        {
            /* PHONG: per-fragment PBR with interpolated normals. */
            const sdl3d_mat4 m = context->model;
            sdl3d_vec3 rn0 = sdl3d_transform_normal(m, sdl3d_mesh_normal(mesh, i0));
            sdl3d_vec3 rn1 = sdl3d_transform_normal(m, sdl3d_mesh_normal(mesh, i1));
            sdl3d_vec3 rn2 = sdl3d_transform_normal(m, sdl3d_mesh_normal(mesh, i2));
            sdl3d_vec3 p0 = sdl3d_mesh_position(mesh, i0);
            sdl3d_vec3 p1 = sdl3d_mesh_position(mesh, i1);
            sdl3d_vec3 p2 = sdl3d_mesh_position(mesh, i2);
            sdl3d_vec3 wp0 = sdl3d_transform_position(m, p0);
            sdl3d_vec3 wp1 = sdl3d_transform_position(m, p1);
            sdl3d_vec3 wp2 = sdl3d_transform_position(m, p2);

            sdl3d_rasterize_triangle_lit(&framebuffer, context->model_view_projection, p0, p1, p2, uv0, uv1, uv2, rn0,
                                         rn1, rn2, wp0, wp1, wp2, sdl3d_mesh_vertex_modulate(mesh, i0, base_modulate),
                                         sdl3d_mesh_vertex_modulate(mesh, i1, base_modulate),
                                         sdl3d_mesh_vertex_modulate(mesh, i2, base_modulate), texture, lighting,
                                         context->backface_culling_enabled, context->wireframe_enabled);
        }
        else
        {
            sdl3d_rasterize_triangle_textured(&framebuffer, context->model_view_projection,
                                              sdl3d_mesh_position(mesh, i0), sdl3d_mesh_position(mesh, i1),
                                              sdl3d_mesh_position(mesh, i2), uv0, uv1, uv2,
                                              sdl3d_mesh_vertex_modulate(mesh, i0, base_modulate),
                                              sdl3d_mesh_vertex_modulate(mesh, i1, base_modulate),
                                              sdl3d_mesh_vertex_modulate(mesh, i2, base_modulate), texture,
                                              context->backface_culling_enabled, context->wireframe_enabled);
        }
    }

    return true;
}

bool sdl3d_push_matrix(sdl3d_render_context *context)
{
    if (!sdl3d_require_mode_3d(context, "sdl3d_push_matrix"))
    {
        return false;
    }

    if (!sdl3d_ensure_model_stack_capacity(context, context->model_stack_depth + 1))
    {
        return false;
    }

    context->model_stack[context->model_stack_depth] = context->model_stack[context->model_stack_depth - 1];
    context->model_stack_depth += 1;
    sdl3d_update_current_model_matrices(context);
    return true;
}

bool sdl3d_pop_matrix(sdl3d_render_context *context)
{
    if (!sdl3d_require_mode_3d(context, "sdl3d_pop_matrix"))
    {
        return false;
    }

    if (context->model_stack_depth <= 1)
    {
        return SDL_SetError("sdl3d_pop_matrix cannot pop the root model matrix.");
    }

    context->model_stack_depth -= 1;
    sdl3d_update_current_model_matrices(context);
    return true;
}

bool sdl3d_translate(sdl3d_render_context *context, float x, float y, float z)
{
    if (!sdl3d_require_mode_3d(context, "sdl3d_translate"))
    {
        return false;
    }

    const sdl3d_mat4 translation = sdl3d_mat4_translate(sdl3d_vec3_make(x, y, z));
    context->model_stack[context->model_stack_depth - 1] =
        sdl3d_mat4_multiply(context->model_stack[context->model_stack_depth - 1], translation);
    sdl3d_update_current_model_matrices(context);
    return true;
}

bool sdl3d_rotate(sdl3d_render_context *context, sdl3d_vec3 axis, float angle_radians)
{
    if (!sdl3d_require_mode_3d(context, "sdl3d_rotate"))
    {
        return false;
    }

    if (!(sdl3d_vec3_length_squared(axis) > 0.0f))
    {
        return SDL_SetError("sdl3d_rotate requires a non-zero rotation axis.");
    }

    const sdl3d_mat4 rotation = sdl3d_mat4_rotate(axis, angle_radians);
    context->model_stack[context->model_stack_depth - 1] =
        sdl3d_mat4_multiply(context->model_stack[context->model_stack_depth - 1], rotation);
    sdl3d_update_current_model_matrices(context);
    return true;
}

bool sdl3d_scale(sdl3d_render_context *context, float x, float y, float z)
{
    if (!sdl3d_require_mode_3d(context, "sdl3d_scale"))
    {
        return false;
    }

    const sdl3d_mat4 scale = sdl3d_mat4_scale(sdl3d_vec3_make(x, y, z));
    context->model_stack[context->model_stack_depth - 1] =
        sdl3d_mat4_multiply(context->model_stack[context->model_stack_depth - 1], scale);
    sdl3d_update_current_model_matrices(context);
    return true;
}

bool sdl3d_draw_triangle_3d(sdl3d_render_context *context, sdl3d_vec3 v0, sdl3d_vec3 v1, sdl3d_vec3 v2,
                            sdl3d_color color)
{
    sdl3d_framebuffer framebuffer;
    sdl3d_shading_mode mode;

    if (!sdl3d_require_mode_3d(context, "sdl3d_draw_triangle_3d"))
    {
        return false;
    }

    framebuffer = sdl3d_framebuffer_from_context(context);
    mode = context->shading_mode;

    if (mode != SDL3D_SHADING_UNLIT && context->light_count > 0)
    {
        sdl3d_vec3 edge1 = sdl3d_vec3_sub(v1, v0);
        sdl3d_vec3 edge2 = sdl3d_vec3_sub(v2, v0);
        sdl3d_vec3 face_normal = sdl3d_vec3_normalize(sdl3d_vec3_cross(edge1, edge2));
        sdl3d_vec3 wn = sdl3d_transform_normal(context->model, face_normal);
        sdl3d_vec3 wp0 = sdl3d_transform_position(context->model, v0);
        sdl3d_vec3 wp1 = sdl3d_transform_position(context->model, v1);
        sdl3d_vec3 wp2 = sdl3d_transform_position(context->model, v2);
        sdl3d_vec4 modulate = sdl3d_color_to_modulate(color);

        sdl3d_lighting_params lp;
        sdl3d_build_lighting_params(context, &lp);
        lp.roughness = 1.0f;

        if (mode == SDL3D_SHADING_FLAT)
        {
            /* One PBR eval at centroid with face normal. */
            sdl3d_vec3 centroid = sdl3d_vec3_scale(sdl3d_vec3_add(sdl3d_vec3_add(wp0, wp1), wp2), 1.0f / 3.0f);
            sdl3d_color lit = sdl3d_shade_point(&lp, modulate.x, modulate.y, modulate.z, modulate.w, wn, centroid);
            sdl3d_rasterize_triangle(&framebuffer, context->model_view_projection, v0, v1, v2, lit,
                                     context->backface_culling_enabled, context->wireframe_enabled);
        }
        else if (mode == SDL3D_SHADING_GOURAUD)
        {
            /* PBR eval at each vertex, interpolate colors. */
            sdl3d_color c0 = sdl3d_shade_point(&lp, modulate.x, modulate.y, modulate.z, modulate.w, wn, wp0);
            sdl3d_color c1 = sdl3d_shade_point(&lp, modulate.x, modulate.y, modulate.z, modulate.w, wn, wp1);
            sdl3d_color c2 = sdl3d_shade_point(&lp, modulate.x, modulate.y, modulate.z, modulate.w, wn, wp2);
            sdl3d_rasterize_triangle_colored(&framebuffer, context->model_view_projection, v0, v1, v2, c0, c1, c2,
                                             context->backface_culling_enabled, context->wireframe_enabled);
        }
        else /* SDL3D_SHADING_PHONG */
        {
            sdl3d_vec2 uv0 = {0.0f, 0.0f};
            sdl3d_rasterize_triangle_lit(&framebuffer, context->model_view_projection, v0, v1, v2, uv0, uv0, uv0, wn,
                                         wn, wn, wp0, wp1, wp2, modulate, modulate, modulate, NULL, &lp,
                                         context->backface_culling_enabled, context->wireframe_enabled);
        }
    }
    else
    {
        sdl3d_rasterize_triangle(&framebuffer, context->model_view_projection, v0, v1, v2, color,
                                 context->backface_culling_enabled, context->wireframe_enabled);
    }

    return true;
}

bool sdl3d_draw_triangle_3d_ex(sdl3d_render_context *context, sdl3d_vec3 v0, sdl3d_vec3 v1, sdl3d_vec3 v2,
                               sdl3d_color c0, sdl3d_color c1, sdl3d_color c2)
{
    if (!sdl3d_require_mode_3d(context, "sdl3d_draw_triangle_3d_ex"))
    {
        return false;
    }

    sdl3d_framebuffer framebuffer = sdl3d_framebuffer_from_context(context);
    sdl3d_rasterize_triangle_colored(&framebuffer, context->model_view_projection, v0, v1, v2, c0, c1, c2,
                                     context->backface_culling_enabled, context->wireframe_enabled);
    return true;
}

bool sdl3d_draw_line_3d(sdl3d_render_context *context, sdl3d_vec3 start, sdl3d_vec3 end, sdl3d_color color)
{
    if (!sdl3d_require_mode_3d(context, "sdl3d_draw_line_3d"))
    {
        return false;
    }

    sdl3d_framebuffer framebuffer = sdl3d_framebuffer_from_context(context);
    sdl3d_rasterize_line(&framebuffer, context->model_view_projection, start, end, color);
    return true;
}

bool sdl3d_draw_point_3d(sdl3d_render_context *context, sdl3d_vec3 position, sdl3d_color color)
{
    if (!sdl3d_require_mode_3d(context, "sdl3d_draw_point_3d"))
    {
        return false;
    }

    sdl3d_framebuffer framebuffer = sdl3d_framebuffer_from_context(context);
    sdl3d_rasterize_point(&framebuffer, context->model_view_projection, position, color);
    return true;
}

bool sdl3d_draw_mesh(sdl3d_render_context *context, const sdl3d_mesh *mesh, const sdl3d_texture2d *texture,
                     sdl3d_color tint)
{
    return sdl3d_draw_mesh_internal(context, mesh, texture, sdl3d_color_to_modulate(tint), NULL);
}

bool sdl3d_draw_model(sdl3d_render_context *context, const sdl3d_model *model, sdl3d_vec3 position, float scale,
                      sdl3d_color tint)
{
    return sdl3d_draw_model_ex(context, model, position, sdl3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f,
                               sdl3d_vec3_make(scale, scale, scale), tint);
}

bool sdl3d_draw_model_ex(sdl3d_render_context *context, const sdl3d_model *model, sdl3d_vec3 position,
                         sdl3d_vec3 rotation_axis, float rotation_angle_radians, sdl3d_vec3 scale, sdl3d_color tint)
{
    const sdl3d_vec4 tint_modulate = sdl3d_color_to_modulate(tint);
    bool ok = false;

    if (!sdl3d_require_mode_3d(context, "sdl3d_draw_model_ex"))
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

    if (!sdl3d_push_matrix(context))
    {
        return false;
    }

    ok = sdl3d_translate(context, position.x, position.y, position.z);
    if (ok && rotation_angle_radians != 0.0f)
    {
        ok = sdl3d_rotate(context, rotation_axis, rotation_angle_radians);
    }
    if (ok)
    {
        ok = sdl3d_scale(context, scale.x, scale.y, scale.z);
    }

    for (int mesh_index = 0; ok && mesh_index < model->mesh_count; ++mesh_index)
    {
        const sdl3d_mesh *mesh = &model->meshes[mesh_index];
        const sdl3d_texture2d *texture = NULL;
        const sdl3d_material *material = NULL;
        sdl3d_vec4 mesh_modulate = tint_modulate;

        if (mesh->material_index < -1)
        {
            ok = SDL_SetError("Mesh material index %d is invalid.", mesh->material_index);
            break;
        }

        if (mesh->material_index >= 0)
        {
            if (mesh->material_index >= model->material_count || model->materials == NULL)
            {
                ok = SDL_SetError("Mesh material index %d is outside material_count=%d.", mesh->material_index,
                                  model->material_count);
                break;
            }

            material = &model->materials[mesh->material_index];
            mesh_modulate.x = sdl3d_clamp01(mesh_modulate.x * material->albedo[0]);
            mesh_modulate.y = sdl3d_clamp01(mesh_modulate.y * material->albedo[1]);
            mesh_modulate.z = sdl3d_clamp01(mesh_modulate.z * material->albedo[2]);
            mesh_modulate.w = sdl3d_clamp01(mesh_modulate.w * material->albedo[3]);

            if (material->albedo_map != NULL && material->albedo_map[0] != '\0')
            {
                ok = sdl3d_texture_cache_get_or_load(&context->texture_cache, model->source_path, material->albedo_map,
                                                     &texture);
                if (!ok)
                {
                    break;
                }
            }
        }

        {
            sdl3d_lighting_params lp_storage;
            const sdl3d_lighting_params *lp_ptr = NULL;

            if (context->shading_mode != SDL3D_SHADING_UNLIT && context->light_count > 0)
            {
                lp_storage.lights = context->lights;
                lp_storage.light_count = context->light_count;
                lp_storage.ambient[0] = context->ambient[0];
                lp_storage.ambient[1] = context->ambient[1];
                lp_storage.ambient[2] = context->ambient[2];

                /* Camera position: extract from the inverse of the view matrix.
                 * For a look-at view matrix, the camera position is the negated
                 * translation of the transpose of the upper-left 3x3 applied to
                 * the translation column. Simpler: we stored it during begin_mode_3d
                 * — but we didn't. Instead, recover from view matrix: the camera
                 * world position is -(R^T * t) where R is upper-left 3x3 and t is
                 * column 3. */
                {
                    const sdl3d_mat4 v = context->view;
                    lp_storage.camera_pos.x = -(v.m[0] * v.m[12] + v.m[1] * v.m[13] + v.m[2] * v.m[14]);
                    lp_storage.camera_pos.y = -(v.m[4] * v.m[12] + v.m[5] * v.m[13] + v.m[6] * v.m[14]);
                    lp_storage.camera_pos.z = -(v.m[8] * v.m[12] + v.m[9] * v.m[13] + v.m[10] * v.m[14]);
                }

                if (material != NULL)
                {
                    lp_storage.metallic = material->metallic;
                    lp_storage.roughness = material->roughness;
                    lp_storage.emissive[0] = material->emissive[0];
                    lp_storage.emissive[1] = material->emissive[1];
                    lp_storage.emissive[2] = material->emissive[2];
                }
                else
                {
                    lp_storage.metallic = 0.0f;
                    lp_storage.roughness = 1.0f;
                    lp_storage.emissive[0] = 0.0f;
                    lp_storage.emissive[1] = 0.0f;
                    lp_storage.emissive[2] = 0.0f;
                }

                /* Fog, tonemapping, shadows. */
                lp_storage.fog = context->fog;
                lp_storage.tonemap_mode = context->tonemap_mode;
                lp_storage.shadow_bias = context->shadow_bias;
                for (int si = 0; si < SDL3D_MAX_LIGHTS; ++si)
                {
                    lp_storage.shadow_depth[si] = context->shadow_depth[si];
                    lp_storage.shadow_vp[si] = context->shadow_vp[si];
                    lp_storage.shadow_enabled[si] = context->shadow_enabled[si];
                }

                lp_ptr = &lp_storage;
            }

            ok = sdl3d_draw_mesh_internal(context, mesh, texture, mesh_modulate, lp_ptr);
        }
    }

    if (!sdl3d_pop_matrix(context))
    {
        return false;
    }

    return ok;
}

bool sdl3d_get_framebuffer_pixel(const sdl3d_render_context *context, int x, int y, sdl3d_color *out_color)
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

bool sdl3d_get_framebuffer_depth(const sdl3d_render_context *context, int x, int y, float *out_depth)
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
