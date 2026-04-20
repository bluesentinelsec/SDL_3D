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

/* Apply vertex skinning: transform a position or normal by the weighted
 * sum of up to 4 joint matrices. */
static sdl3d_vec3 sdl3d_skin_position(const sdl3d_mesh *mesh, unsigned int vi, const sdl3d_mat4 *joint_matrices,
                                      sdl3d_vec3 pos)
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
        sdl3d_vec4 tp = sdl3d_mat4_transform_vec4(joint_matrices[ji[i]], sdl3d_vec4_from_vec3(pos, 1.0f));
        rx += tp.x * jw[i];
        ry += tp.y * jw[i];
        rz += tp.z * jw[i];
    }
    return sdl3d_vec3_make(rx, ry, rz);
}

static sdl3d_vec3 sdl3d_skin_normal(const sdl3d_mesh *mesh, unsigned int vi, const sdl3d_mat4 *joint_matrices,
                                    sdl3d_vec3 n)
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
        const sdl3d_mat4 *m = &joint_matrices[ji[i]];
        rx += (m->m[0] * n.x + m->m[4] * n.y + m->m[8] * n.z) * jw[i];
        ry += (m->m[1] * n.x + m->m[5] * n.y + m->m[9] * n.z) * jw[i];
        rz += (m->m[2] * n.x + m->m[6] * n.y + m->m[10] * n.z) * jw[i];
    }
    return sdl3d_vec3_normalize(sdl3d_vec3_make(rx, ry, rz));
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

    /* Fog in linear space, then tonemap+gamma. */
    if (lp->tonemap_mode != SDL3D_TONEMAP_NONE)
    {
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
        sdl3d_tonemap(lp->tonemap_mode, &lit_r, &lit_g, &lit_b);
    }
    else
    {
        sdl3d_tonemap(SDL3D_TONEMAP_NONE, &lit_r, &lit_g, &lit_b);
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
    }

    c.r = sdl3d_color_channel_clamp(lit_r * 255.0f);
    c.g = sdl3d_color_channel_clamp(lit_g * 255.0f);
    c.b = sdl3d_color_channel_clamp(lit_b * 255.0f);
    c.a = sdl3d_color_channel_clamp(albedo_a * 255.0f);
    return c;
}

static sdl3d_vec4 sdl3d_shade_point_retro(const sdl3d_lighting_params *lp, sdl3d_vec4 base_color,
                                          sdl3d_vec3 world_normal, sdl3d_vec3 world_pos)
{
    sdl3d_vec3 n = sdl3d_vec3_normalize(world_normal);
    float lit_r = lp->ambient[0];
    float lit_g = lp->ambient[1];
    float lit_b = lp->ambient[2];
    sdl3d_vec4 out;

    for (int i = 0; i < lp->light_count; ++i)
    {
        const sdl3d_light *light = &lp->lights[i];
        sdl3d_vec3 l;
        float attenuation = 1.0f;

        if (light->type == SDL3D_LIGHT_DIRECTIONAL)
        {
            l = sdl3d_vec3_normalize(sdl3d_vec3_scale(light->direction, -1.0f));
        }
        else
        {
            sdl3d_vec3 to_light = sdl3d_vec3_sub(light->position, world_pos);
            float dist = sdl3d_vec3_length(to_light);
            if (dist <= 1e-6f)
            {
                continue;
            }
            l = sdl3d_vec3_scale(to_light, 1.0f / dist);

            if (light->range > 0.0f)
            {
                float ratio = dist / light->range;
                attenuation = SDL_max(1.0f - ratio * ratio, 0.0f);
                attenuation *= attenuation;
            }
            else
            {
                attenuation = 1.0f / (dist * dist + 1e-4f);
            }

            if (light->type == SDL3D_LIGHT_SPOT)
            {
                sdl3d_vec3 spot_dir = sdl3d_vec3_normalize(light->direction);
                float cos_angle = sdl3d_vec3_dot(sdl3d_vec3_scale(l, -1.0f), spot_dir);
                float epsilon = light->inner_cutoff - light->outer_cutoff;
                if (SDL_fabsf(epsilon) < 1e-6f)
                {
                    attenuation *= (cos_angle >= light->outer_cutoff) ? 1.0f : 0.0f;
                }
                else
                {
                    attenuation *= sdl3d_clamp01((cos_angle - light->outer_cutoff) / epsilon);
                }
            }
        }

        {
            float n_dot_l = SDL_max(sdl3d_vec3_dot(n, l), 0.0f);
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
    sdl3d_tonemap(SDL3D_TONEMAP_NONE, &out.x, &out.y, &out.z);

    if (lp->fog.mode != SDL3D_FOG_NONE)
    {
        float dx = world_pos.x - lp->camera_pos.x;
        float dy = world_pos.y - lp->camera_pos.y;
        float dz = world_pos.z - lp->camera_pos.z;
        float dist = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
        float fog_f = sdl3d_compute_fog_factor(&lp->fog, dist);
        out.x = out.x * (1.0f - fog_f) + lp->fog.color[0] * fog_f;
        out.y = out.y * (1.0f - fog_f) + lp->fog.color[1] * fog_f;
        out.z = out.z * (1.0f - fog_f) + lp->fog.color[2] * fog_f;
    }

    return out;
}

static bool sdl3d_draw_mesh_internal(sdl3d_render_context *context, const sdl3d_mesh *mesh,
                                     const sdl3d_texture2d *texture, sdl3d_vec4 base_modulate,
                                     const sdl3d_lighting_params *lighting, const sdl3d_mat4 *joint_matrices)
{
    const bool indexed = mesh->indices != NULL;
    const int triangle_count = indexed ? (mesh->index_count / 3) : (mesh->vertex_count / 3);
    const bool lit = lighting != NULL && (context->shading_mode == SDL3D_SHADING_FLAT || mesh->normals != NULL);
    const bool skinned = joint_matrices != NULL && mesh->joint_indices != NULL && mesh->joint_weights != NULL;
    sdl3d_framebuffer framebuffer;
    float *skinned_positions = NULL;
    float *skinned_normals = NULL;

/* Macro to read a position with optional skinning applied. */
#define SKIN_POS(idx)                                                                                                  \
    (skinned ? sdl3d_skin_position(mesh, (idx), joint_matrices, sdl3d_mesh_position(mesh, (idx)))                      \
             : sdl3d_mesh_position(mesh, (idx)))
#define SKIN_NORM(idx)                                                                                                 \
    (skinned ? sdl3d_skin_normal(mesh, (idx), joint_matrices, sdl3d_mesh_normal(mesh, (idx)))                          \
             : sdl3d_mesh_normal(mesh, (idx)))

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

    if (skinned)
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
            sdl3d_vec3 p =
                sdl3d_skin_position(mesh, (unsigned int)i, joint_matrices, sdl3d_mesh_position(mesh, (unsigned int)i));
            skinned_positions[i * 3 + 0] = p.x;
            skinned_positions[i * 3 + 1] = p.y;
            skinned_positions[i * 3 + 2] = p.z;

            if (skinned_normals != NULL)
            {
                sdl3d_vec3 n =
                    sdl3d_skin_normal(mesh, (unsigned int)i, joint_matrices, sdl3d_mesh_normal(mesh, (unsigned int)i));
                skinned_normals[i * 3 + 0] = n.x;
                skinned_normals[i * 3 + 1] = n.y;
                skinned_normals[i * 3 + 2] = n.z;
            }
        }
    }

    /* Backend dispatch: try the vtable first. Software returns false to
     * fall through to the per-triangle loop below. */
    if (lit)
    {
        sdl3d_draw_params_lit lp;
        SDL_zero(lp);
        lp.positions = (skinned_positions != NULL) ? skinned_positions : mesh->positions;
        lp.normals = (skinned_normals != NULL) ? skinned_normals : mesh->normals;
        lp.uvs = mesh->uvs;
        lp.colors = mesh->colors;
        lp.indices = mesh->indices;
        lp.vertex_count = mesh->vertex_count;
        lp.index_count = mesh->index_count;
        lp.texture = texture;
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
                const sdl3d_mat4 v = context->view;
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
        lp.vertex_snap = context->vertex_snap;
        lp.vertex_snap_precision = context->vertex_snap_precision;
        lp.texture_filter = (int)context->texture_filter;

        if (context->backend_iface.draw_mesh_lit(context, &lp))
        {
            SDL_free(skinned_positions);
            SDL_free(skinned_normals);
            return true;
        }
    }
    else
    {
        sdl3d_draw_params_unlit up;
        SDL_zero(up);
        up.positions = (skinned_positions != NULL) ? skinned_positions : mesh->positions;
        up.uvs = mesh->uvs;
        up.colors = mesh->colors;
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

        if (context->backend_iface.draw_mesh_unlit(context, &up))
        {
            SDL_free(skinned_positions);
            SDL_free(skinned_normals);
            return true;
        }
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
            /* FLAT: use lit rasterizer with face normal for all three vertices.
             * The lit rasterizer handles tonemapping, fog, and color quantization. */
            sdl3d_vec3 p0 = SKIN_POS(i0);
            sdl3d_vec3 p1 = SKIN_POS(i1);
            sdl3d_vec3 p2 = SKIN_POS(i2);
            sdl3d_vec3 edge1 = sdl3d_vec3_sub(p1, p0);
            sdl3d_vec3 edge2 = sdl3d_vec3_sub(p2, p0);
            sdl3d_vec3 fn = sdl3d_vec3_normalize(sdl3d_vec3_cross(edge1, edge2));
            sdl3d_vec3 wn = sdl3d_transform_normal(context->model, fn);
            sdl3d_vec3 wp0 = sdl3d_transform_position(context->model, p0);
            sdl3d_vec3 wp1 = sdl3d_transform_position(context->model, p1);
            sdl3d_vec3 wp2 = sdl3d_transform_position(context->model, p2);
            sdl3d_rasterize_triangle_lit(&framebuffer, context->model_view_projection, p0, p1, p2, uv0, uv1, uv2, wn,
                                         wn, wn, wp0, wp1, wp2, sdl3d_mesh_vertex_modulate(mesh, i0, base_modulate),
                                         sdl3d_mesh_vertex_modulate(mesh, i1, base_modulate),
                                         sdl3d_mesh_vertex_modulate(mesh, i2, base_modulate), texture, lighting,
                                         context->backface_culling_enabled, context->wireframe_enabled);
        }
        else if (lit && context->shading_mode == SDL3D_SHADING_GOURAUD)
        {
            /* GOURAUD: shade vertices once, then rasterize with the active
             * retro/profile rules (affine UVs, vertex snap, quantization). */
            sdl3d_vec3 p0 = SKIN_POS(i0);
            sdl3d_vec3 p1 = SKIN_POS(i1);
            sdl3d_vec3 p2 = SKIN_POS(i2);
            sdl3d_vec3 wn0, wn1, wn2;
            sdl3d_vec4 m0 = sdl3d_mesh_vertex_modulate(mesh, i0, base_modulate);
            sdl3d_vec4 m1 = sdl3d_mesh_vertex_modulate(mesh, i1, base_modulate);
            sdl3d_vec4 m2 = sdl3d_mesh_vertex_modulate(mesh, i2, base_modulate);
            if (mesh->normals != NULL)
            {
                wn0 = sdl3d_transform_normal(context->model, SKIN_NORM(i0));
                wn1 = sdl3d_transform_normal(context->model, SKIN_NORM(i1));
                wn2 = sdl3d_transform_normal(context->model, SKIN_NORM(i2));
            }
            else
            {
                sdl3d_vec3 fn = sdl3d_vec3_normalize(sdl3d_vec3_cross(sdl3d_vec3_sub(p1, p0), sdl3d_vec3_sub(p2, p0)));
                wn0 = wn1 = wn2 = sdl3d_transform_normal(context->model, fn);
            }
            sdl3d_vec3 wp0 = sdl3d_transform_position(context->model, p0);
            sdl3d_vec3 wp1 = sdl3d_transform_position(context->model, p1);
            sdl3d_vec3 wp2 = sdl3d_transform_position(context->model, p2);
            sdl3d_rasterize_triangle_textured_profiled(
                &framebuffer, context->model_view_projection, p0, p1, p2, uv0, uv1, uv2,
                sdl3d_shade_point_retro(lighting, m0, wn0, wp0), sdl3d_shade_point_retro(lighting, m1, wn1, wp1),
                sdl3d_shade_point_retro(lighting, m2, wn2, wp2), texture, lighting, context->backface_culling_enabled,
                context->wireframe_enabled);
        }
        else if (lit)
        {
            /* PHONG: per-fragment PBR with interpolated normals. */
            const sdl3d_mat4 m = context->model;
            sdl3d_vec3 rn0 = sdl3d_transform_normal(m, SKIN_NORM(i0));
            sdl3d_vec3 rn1 = sdl3d_transform_normal(m, SKIN_NORM(i1));
            sdl3d_vec3 rn2 = sdl3d_transform_normal(m, SKIN_NORM(i2));
            sdl3d_vec3 p0 = SKIN_POS(i0);
            sdl3d_vec3 p1 = SKIN_POS(i1);
            sdl3d_vec3 p2 = SKIN_POS(i2);
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
            sdl3d_rasterize_triangle_textured(&framebuffer, context->model_view_projection, SKIN_POS(i0), SKIN_POS(i1),
                                              SKIN_POS(i2), uv0, uv1, uv2,
                                              sdl3d_mesh_vertex_modulate(mesh, i0, base_modulate),
                                              sdl3d_mesh_vertex_modulate(mesh, i1, base_modulate),
                                              sdl3d_mesh_vertex_modulate(mesh, i2, base_modulate), texture,
                                              context->backface_culling_enabled, context->wireframe_enabled);
        }
    }

#undef SKIN_POS
#undef SKIN_NORM
    SDL_free(skinned_positions);
    SDL_free(skinned_normals);
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

    mode = context->shading_mode;

    /* Backend dispatch: match the software path's lit/unlit split. */
    {
        float positions[9] = {v0.x, v0.y, v0.z, v1.x, v1.y, v1.z, v2.x, v2.y, v2.z};

        if (mode != SDL3D_SHADING_UNLIT && context->light_count > 0)
        {
            sdl3d_vec3 edge1 = sdl3d_vec3_sub(v1, v0);
            sdl3d_vec3 edge2 = sdl3d_vec3_sub(v2, v0);
            sdl3d_vec3 face_normal = sdl3d_vec3_normalize(sdl3d_vec3_cross(edge1, edge2));
            float normals[9] = {face_normal.x, face_normal.y, face_normal.z, face_normal.x, face_normal.y,
                                face_normal.z, face_normal.x, face_normal.y, face_normal.z};
            sdl3d_lighting_params lighting;
            sdl3d_draw_params_lit lp;

            sdl3d_build_lighting_params(context, &lighting);
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
            sdl3d_draw_params_unlit up;

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

    framebuffer = sdl3d_framebuffer_from_context(context);

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
            sdl3d_vec2 uv0 = {0.0f, 0.0f};
            sdl3d_rasterize_triangle_textured_profiled(&framebuffer, context->model_view_projection, v0, v1, v2, uv0,
                                                       uv0, uv0, sdl3d_shade_point_retro(&lp, modulate, wn, wp0),
                                                       sdl3d_shade_point_retro(&lp, modulate, wn, wp1),
                                                       sdl3d_shade_point_retro(&lp, modulate, wn, wp2), NULL, &lp,
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
    if (context != NULL && context->shading_mode != SDL3D_SHADING_UNLIT && context->light_count > 0 && mesh != NULL &&
        mesh->normals != NULL)
    {
        sdl3d_lighting_params lp;
        sdl3d_build_lighting_params(context, &lp);
        lp.roughness = 1.0f;
        return sdl3d_draw_mesh_internal(context, mesh, texture, sdl3d_color_to_modulate(tint), &lp, NULL);
    }
    return sdl3d_draw_mesh_internal(context, mesh, texture, sdl3d_color_to_modulate(tint), NULL, NULL);
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
                if (material->albedo_map[0] == '#' && model->embedded_textures != NULL)
                {
                    /* Embedded texture reference: "#N" where N is the image index. */
                    int tex_idx = SDL_atoi(&material->albedo_map[1]);
                    if (tex_idx >= 0 && tex_idx < model->embedded_texture_count &&
                        model->embedded_textures[tex_idx].pixels != NULL)
                    {
                        /* Create texture from embedded image via cache (keyed by "#N"). */
                        sdl3d_texture_cache_entry *entry = NULL;
                        for (entry = context->texture_cache; entry != NULL; entry = entry->next)
                        {
                            if (SDL_strcmp(entry->path, material->albedo_map) == 0)
                            {
                                texture = &entry->texture;
                                break;
                            }
                        }
                        if (texture == NULL)
                        {
                            entry = (sdl3d_texture_cache_entry *)SDL_calloc(1, sizeof(*entry));
                            if (entry != NULL)
                            {
                                entry->path = SDL_strdup(material->albedo_map);
                                if (sdl3d_create_texture_from_image(&model->embedded_textures[tex_idx],
                                                                    &entry->texture))
                                {
                                    entry->next = context->texture_cache;
                                    context->texture_cache = entry;
                                    texture = &entry->texture;
                                }
                                else
                                {
                                    SDL_free(entry->path);
                                    SDL_free(entry);
                                }
                            }
                        }
                    }
                }
                else
                {
                    ok = sdl3d_texture_cache_get_or_load(&context->texture_cache, model->source_path,
                                                         material->albedo_map, &texture);
                    if (!ok)
                    {
                        break;
                    }
                }
            }
        }

        {
            sdl3d_lighting_params lp_storage;
            const sdl3d_lighting_params *lp_ptr = NULL;

            if (context->shading_mode != SDL3D_SHADING_UNLIT && context->light_count > 0)
            {
                sdl3d_build_lighting_params(context, &lp_storage);

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
                    lp_storage.roughness = 1.0f;
                }

                lp_ptr = &lp_storage;
            }

            ok = sdl3d_draw_mesh_internal(context, mesh, texture, mesh_modulate, lp_ptr, NULL);
        }
    }

    if (!sdl3d_pop_matrix(context))
    {
        return false;
    }

    return ok;
}

bool sdl3d_draw_model_skinned(sdl3d_render_context *context, const sdl3d_model *model, sdl3d_vec3 position,
                              sdl3d_vec3 rotation_axis, float rotation_angle_radians, sdl3d_vec3 scale,
                              sdl3d_color tint, const sdl3d_mat4 *joint_matrices)
{
    /* Re-use draw_model_ex logic but pass joint_matrices through.
     * For now, call draw_model_ex for the non-skinned parts and
     * handle skinning by replacing the NULL. */
    /* This is a simplified version — we duplicate the draw_model_ex
     * body but pass joint_matrices to draw_mesh_internal. */
    const sdl3d_vec4 tint_modulate = sdl3d_color_to_modulate(tint);
    bool ok = false;

    if (!sdl3d_require_mode_3d(context, "sdl3d_draw_model_skinned"))
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
                if (material->albedo_map[0] == '#' && model->embedded_textures != NULL)
                {
                    int tex_idx = SDL_atoi(&material->albedo_map[1]);
                    if (tex_idx >= 0 && tex_idx < model->embedded_texture_count &&
                        model->embedded_textures[tex_idx].pixels != NULL)
                    {
                        sdl3d_texture_cache_entry *entry = NULL;
                        for (entry = context->texture_cache; entry != NULL; entry = entry->next)
                        {
                            if (SDL_strcmp(entry->path, material->albedo_map) == 0)
                            {
                                texture = &entry->texture;
                                break;
                            }
                        }
                        if (texture == NULL)
                        {
                            entry = (sdl3d_texture_cache_entry *)SDL_calloc(1, sizeof(*entry));
                            if (entry != NULL)
                            {
                                entry->path = SDL_strdup(material->albedo_map);
                                if (sdl3d_create_texture_from_image(&model->embedded_textures[tex_idx],
                                                                    &entry->texture))
                                {
                                    entry->next = context->texture_cache;
                                    context->texture_cache = entry;
                                    texture = &entry->texture;
                                }
                                else
                                {
                                    SDL_free(entry->path);
                                    SDL_free(entry);
                                }
                            }
                        }
                    }
                }
                else
                {
                    ok = sdl3d_texture_cache_get_or_load(&context->texture_cache, model->source_path,
                                                         material->albedo_map, &texture);
                    if (!ok)
                    {
                        break;
                    }
                }
            }
        }

        {
            sdl3d_lighting_params lp_storage;
            const sdl3d_lighting_params *lp_ptr = NULL;

            if (context->shading_mode != SDL3D_SHADING_UNLIT && context->light_count > 0)
            {
                sdl3d_build_lighting_params(context, &lp_storage);
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
                    lp_storage.roughness = 1.0f;
                }
                lp_ptr = &lp_storage;
            }

            ok = sdl3d_draw_mesh_internal(context, mesh, texture, mesh_modulate, lp_ptr, joint_matrices);
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
