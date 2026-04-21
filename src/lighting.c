#include "sdl3d/lighting.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/model.h"

#include "gl_renderer.h"
#include "render_context_internal.h"

bool sdl3d_add_light(sdl3d_render_context *context, const sdl3d_light *light)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (light == NULL)
    {
        return SDL_InvalidParamError("light");
    }
    if (context->light_count >= SDL3D_MAX_LIGHTS)
    {
        return SDL_SetError("Light list is full (max %d).", SDL3D_MAX_LIGHTS);
    }

    context->lights[context->light_count] = *light;
    context->light_count += 1;
    return true;
}

bool sdl3d_clear_lights(sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->light_count = 0;
    return true;
}

bool sdl3d_set_lighting_enabled(sdl3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->shading_mode = enabled ? SDL3D_SHADING_PHONG : SDL3D_SHADING_UNLIT;
    return true;
}

bool sdl3d_is_lighting_enabled(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return false;
    }
    return context->shading_mode != SDL3D_SHADING_UNLIT;
}

bool sdl3d_set_shading_mode(sdl3d_render_context *context, sdl3d_shading_mode mode)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    context->shading_mode = mode;
    return true;
}

sdl3d_shading_mode sdl3d_get_shading_mode(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL3D_SHADING_UNLIT;
    }
    return context->shading_mode;
}

bool sdl3d_set_ambient_light(sdl3d_render_context *context, float r, float g, float b)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    context->ambient[0] = r;
    context->ambient[1] = g;
    context->ambient[2] = b;
    return true;
}

bool sdl3d_set_emissive(sdl3d_render_context *context, float r, float g, float b)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    context->emissive[0] = r;
    context->emissive[1] = g;
    context->emissive[2] = b;
    return true;
}

bool sdl3d_set_bloom_enabled(sdl3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    context->bloom_enabled = enabled;
    return true;
}

bool sdl3d_set_ssao_enabled(sdl3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    context->ssao_enabled = enabled;
    return true;
}

int sdl3d_get_light_count(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return 0;
    }
    return context->light_count;
}

/* ------------------------------------------------------------------ */
/* Fog                                                                 */
/* ------------------------------------------------------------------ */

bool sdl3d_set_fog(sdl3d_render_context *context, const sdl3d_fog *fog)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (fog == NULL)
    {
        return SDL_InvalidParamError("fog");
    }
    context->fog = *fog;
    return true;
}

bool sdl3d_clear_fog(sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    context->fog.mode = SDL3D_FOG_NONE;
    return true;
}

/* ------------------------------------------------------------------ */
/* Tonemapping                                                         */
/* ------------------------------------------------------------------ */

bool sdl3d_set_tonemap_mode(sdl3d_render_context *context, sdl3d_tonemap_mode mode)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    context->tonemap_mode = mode;
    return true;
}

sdl3d_tonemap_mode sdl3d_get_tonemap_mode(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL3D_TONEMAP_NONE;
    }
    return context->tonemap_mode;
}

/* ------------------------------------------------------------------ */
/* Shadow mapping                                                      */
/* ------------------------------------------------------------------ */

bool sdl3d_enable_shadow(sdl3d_render_context *context, int light_index, sdl3d_vec3 scene_center, float scene_radius)
{
    sdl3d_mat4 light_view;
    sdl3d_mat4 light_proj;
    sdl3d_vec3 light_pos;
    sdl3d_vec3 light_dir;
    float len;
    size_t map_bytes;

    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (light_index < 0 || light_index >= context->light_count)
    {
        return SDL_SetError("Light index %d out of range (count=%d).", light_index, context->light_count);
    }
    if (context->lights[light_index].type != SDL3D_LIGHT_DIRECTIONAL)
    {
        return SDL_SetError("Shadow mapping is only supported for directional lights.");
    }
    if (scene_radius <= 0.0f)
    {
        return SDL_SetError("Scene radius must be positive.");
    }

    /* Allocate shadow depth buffer if not already allocated. */
    if (context->shadow_depth[light_index] == NULL)
    {
        map_bytes = (size_t)SDL3D_SHADOW_MAP_SIZE * SDL3D_SHADOW_MAP_SIZE * sizeof(float);
        context->shadow_depth[light_index] = (float *)SDL_malloc(map_bytes);
        if (context->shadow_depth[light_index] == NULL)
        {
            return SDL_OutOfMemory();
        }
        /* Initialize to 1.0 (far plane = no shadow) so the map is safe
         * to sample before sdl3d_render_shadow_map is first called. */
        for (size_t p = 0; p < (size_t)SDL3D_SHADOW_MAP_SIZE * SDL3D_SHADOW_MAP_SIZE; ++p)
        {
            context->shadow_depth[light_index][p] = 1.0f;
        }
    }

    /* Compute light-space view-projection matrix. */
    light_dir = context->lights[light_index].direction;
    len = SDL_sqrtf(light_dir.x * light_dir.x + light_dir.y * light_dir.y + light_dir.z * light_dir.z);
    if (len < 1e-7f)
    {
        return SDL_SetError("Directional light has zero-length direction.");
    }
    light_dir.x /= len;
    light_dir.y /= len;
    light_dir.z /= len;

    /* Place the light camera behind the scene center, looking along the light direction. */
    light_pos.x = scene_center.x - light_dir.x * scene_radius * 2.0f;
    light_pos.y = scene_center.y - light_dir.y * scene_radius * 2.0f;
    light_pos.z = scene_center.z - light_dir.z * scene_radius * 2.0f;

    if (!sdl3d_mat4_look_at(light_pos, scene_center, sdl3d_vec3_make(0.0f, 1.0f, 0.0f), &light_view))
    {
        /* If up is parallel to direction, try a different up vector. */
        if (!sdl3d_mat4_look_at(light_pos, scene_center, sdl3d_vec3_make(1.0f, 0.0f, 0.0f), &light_view))
        {
            return SDL_SetError("Cannot compute light view matrix.");
        }
    }

    if (!sdl3d_mat4_orthographic(-scene_radius, scene_radius, -scene_radius, scene_radius, 0.01f, scene_radius * 4.0f,
                                 &light_proj))
    {
        return SDL_SetError("Cannot compute light projection matrix.");
    }

    context->shadow_vp[light_index] = sdl3d_mat4_multiply(light_proj, light_view);
    context->shadow_enabled[light_index] = true;
    return true;
}

bool sdl3d_disable_shadow(sdl3d_render_context *context, int light_index)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (light_index < 0 || light_index >= SDL3D_MAX_LIGHTS)
    {
        return SDL_SetError("Light index %d out of range.", light_index);
    }
    context->shadow_enabled[light_index] = false;
    SDL_free(context->shadow_depth[light_index]);
    context->shadow_depth[light_index] = NULL;
    return true;
}

bool sdl3d_render_shadow_map(sdl3d_render_context *context, const sdl3d_mesh *meshes, int mesh_count,
                             const sdl3d_mat4 *model_matrices)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (meshes == NULL && mesh_count > 0)
    {
        return SDL_InvalidParamError("meshes");
    }

    for (int li = 0; li < context->light_count; ++li)
    {
        float *depth_buf;
        size_t map_pixels;
        sdl3d_mat4 light_mvp;

        if (!context->shadow_enabled[li] || context->shadow_depth[li] == NULL)
        {
            continue;
        }

        depth_buf = context->shadow_depth[li];
        map_pixels = (size_t)SDL3D_SHADOW_MAP_SIZE * SDL3D_SHADOW_MAP_SIZE;

        /* Clear shadow depth buffer to 1.0 (far). */
        for (size_t p = 0; p < map_pixels; ++p)
        {
            depth_buf[p] = 1.0f;
        }

        /* Render each mesh into the shadow map. */
        for (int mi = 0; mi < mesh_count; ++mi)
        {
            const sdl3d_mesh *mesh = &meshes[mi];
            sdl3d_mat4 model_mat = (model_matrices != NULL) ? model_matrices[mi] : sdl3d_mat4_identity();
            light_mvp = sdl3d_mat4_multiply(context->shadow_vp[li], model_mat);

            if (mesh->positions == NULL || mesh->vertex_count <= 0)
            {
                continue;
            }

            {
                const bool indexed = mesh->indices != NULL;
                const int tri_count = indexed ? (mesh->index_count / 3) : (mesh->vertex_count / 3);

                for (int t = 0; t < tri_count; ++t)
                {
                    const unsigned int i0 = indexed ? mesh->indices[t * 3 + 0] : (unsigned int)(t * 3 + 0);
                    const unsigned int i1 = indexed ? mesh->indices[t * 3 + 1] : (unsigned int)(t * 3 + 1);
                    const unsigned int i2 = indexed ? mesh->indices[t * 3 + 2] : (unsigned int)(t * 3 + 2);

                    if ((int)i0 >= mesh->vertex_count || (int)i1 >= mesh->vertex_count || (int)i2 >= mesh->vertex_count)
                    {
                        continue;
                    }

                    {
                        const float *p0 = &mesh->positions[i0 * 3];
                        const float *p1 = &mesh->positions[i1 * 3];
                        const float *p2 = &mesh->positions[i2 * 3];

                        sdl3d_vec4 c0 =
                            sdl3d_mat4_transform_vec4(light_mvp, sdl3d_vec4_make(p0[0], p0[1], p0[2], 1.0f));
                        sdl3d_vec4 c1 =
                            sdl3d_mat4_transform_vec4(light_mvp, sdl3d_vec4_make(p1[0], p1[1], p1[2], 1.0f));
                        sdl3d_vec4 c2 =
                            sdl3d_mat4_transform_vec4(light_mvp, sdl3d_vec4_make(p2[0], p2[1], p2[2], 1.0f));

                        /* Perspective divide → NDC. */
                        if (c0.w <= 0.0f || c1.w <= 0.0f || c2.w <= 0.0f)
                        {
                            continue;
                        }

                        {
                            float ndc[3][3];
                            int sx[3], sy[3];
                            float sz[3];
                            int min_x, max_x, min_y, max_y;

                            ndc[0][0] = c0.x / c0.w;
                            ndc[0][1] = c0.y / c0.w;
                            ndc[0][2] = c0.z / c0.w;
                            ndc[1][0] = c1.x / c1.w;
                            ndc[1][1] = c1.y / c1.w;
                            ndc[1][2] = c1.z / c1.w;
                            ndc[2][0] = c2.x / c2.w;
                            ndc[2][1] = c2.y / c2.w;
                            ndc[2][2] = c2.z / c2.w;

                            for (int v = 0; v < 3; ++v)
                            {
                                sx[v] = (int)((ndc[v][0] * 0.5f + 0.5f) * (float)SDL3D_SHADOW_MAP_SIZE);
                                sy[v] = (int)((ndc[v][1] * 0.5f + 0.5f) * (float)SDL3D_SHADOW_MAP_SIZE);
                                sz[v] = ndc[v][2] * 0.5f + 0.5f;
                            }

                            /* Simple bounding-box rasterization for the shadow map. */
                            min_x = sx[0] < sx[1] ? sx[0] : sx[1];
                            min_x = min_x < sx[2] ? min_x : sx[2];
                            max_x = sx[0] > sx[1] ? sx[0] : sx[1];
                            max_x = max_x > sx[2] ? max_x : sx[2];
                            min_y = sy[0] < sy[1] ? sy[0] : sy[1];
                            min_y = min_y < sy[2] ? min_y : sy[2];
                            max_y = sy[0] > sy[1] ? sy[0] : sy[1];
                            max_y = max_y > sy[2] ? max_y : sy[2];

                            if (min_x < 0)
                            {
                                min_x = 0;
                            }
                            if (min_y < 0)
                            {
                                min_y = 0;
                            }
                            if (max_x >= SDL3D_SHADOW_MAP_SIZE)
                            {
                                max_x = SDL3D_SHADOW_MAP_SIZE - 1;
                            }
                            if (max_y >= SDL3D_SHADOW_MAP_SIZE)
                            {
                                max_y = SDL3D_SHADOW_MAP_SIZE - 1;
                            }

                            for (int py = min_y; py <= max_y; ++py)
                            {
                                for (int px = min_x; px <= max_x; ++px)
                                {
                                    /* Barycentric test. */
                                    float w0, w1, w2, area, depth;
                                    w0 = (float)(sx[1] - sx[2]) * (float)(py - sy[2]) -
                                         (float)(sy[1] - sy[2]) * (float)(px - sx[2]);
                                    w1 = (float)(sx[2] - sx[0]) * (float)(py - sy[0]) -
                                         (float)(sy[2] - sy[0]) * (float)(px - sx[0]);
                                    w2 = (float)(sx[0] - sx[1]) * (float)(py - sy[1]) -
                                         (float)(sy[0] - sy[1]) * (float)(px - sx[1]);
                                    area = w0 + w1 + w2;
                                    if (area <= 0.0f)
                                    {
                                        continue;
                                    }
                                    w0 /= area;
                                    w1 /= area;
                                    w2 /= area;
                                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                                    {
                                        continue;
                                    }
                                    depth = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
                                    {
                                        int idx = py * SDL3D_SHADOW_MAP_SIZE + px;
                                        if (depth < depth_buf[idx])
                                        {
                                            depth_buf[idx] = depth;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return true;
}

bool sdl3d_begin_shadow_pass(sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (context->in_mode_3d)
    {
        return SDL_SetError("Cannot begin shadow pass inside begin_mode_3d.");
    }
    if (!context->shadow_enabled[0])
    {
        return SDL_SetError("No shadow-enabled light.");
    }

    context->in_shadow_pass = true;

    /* Set up model stack for shadow pass draws. */
    if (!context->model_stack)
    {
        context->model_stack = (sdl3d_mat4 *)SDL_malloc(8 * sizeof(sdl3d_mat4));
        if (!context->model_stack)
        {
            return SDL_OutOfMemory();
        }
        context->model_stack_capacity = 8;
    }
    context->view_projection = context->shadow_vp[0];
    context->model_stack_depth = 1;
    context->model_stack[0] = sdl3d_mat4_identity();
    context->model = sdl3d_mat4_identity();
    context->model_view_projection = context->shadow_vp[0];
    context->in_mode_3d = true;

    if (context->gl)
    {
        sdl3d_gl_begin_shadow_pass(context->gl, context->shadow_vp[0].m,
                                   context->shadow_bias > 0 ? context->shadow_bias : 0.005f);
    }
    return true;
}

bool sdl3d_end_shadow_pass(sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    context->in_shadow_pass = false;
    context->in_mode_3d = false;
    if (context->gl)
    {
        sdl3d_gl_end_shadow_pass(context->gl);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Render profiles                                                     */
/* ------------------------------------------------------------------ */

bool sdl3d_set_render_profile(sdl3d_render_context *context, const sdl3d_render_profile *profile)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (profile == NULL)
    {
        return SDL_InvalidParamError("profile");
    }
    context->shading_mode = profile->shading;
    context->tonemap_mode = profile->tonemap;
    context->uv_mode = profile->uv_mode;
    context->fog_eval = profile->fog_eval;
    context->texture_filter = profile->texture_filter;
    context->vertex_snap = profile->vertex_snap;
    context->vertex_snap_precision = profile->vertex_snap_precision > 0 ? profile->vertex_snap_precision : 1;
    context->color_quantize = profile->color_quantize;
    context->color_depth = profile->color_depth;
    return true;
}

bool sdl3d_get_render_profile(const sdl3d_render_context *context, sdl3d_render_profile *out)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (out == NULL)
    {
        return SDL_InvalidParamError("out");
    }
    out->shading = context->shading_mode;
    out->tonemap = context->tonemap_mode;
    out->uv_mode = context->uv_mode;
    out->fog_eval = context->fog_eval;
    out->vertex_snap = context->vertex_snap;
    out->vertex_snap_precision = context->vertex_snap_precision;
    out->color_quantize = context->color_quantize;
    out->color_depth = context->color_depth;
    out->texture_filter = context->texture_filter;
    return true;
}

sdl3d_render_profile sdl3d_profile_modern(void)
{
    sdl3d_render_profile p;
    SDL_zerop(&p);
    p.shading = SDL3D_SHADING_PHONG;
    p.texture_filter = SDL3D_TEXTURE_FILTER_BILINEAR;
    p.uv_mode = SDL3D_UV_PERSPECTIVE;
    p.fog_eval = SDL3D_FOG_EVAL_FRAGMENT;
    p.tonemap = SDL3D_TONEMAP_ACES;
    return p;
}

sdl3d_render_profile sdl3d_profile_ps1(void)
{
    sdl3d_render_profile p;
    SDL_zerop(&p);
    p.shading = SDL3D_SHADING_GOURAUD;
    p.texture_filter = SDL3D_TEXTURE_FILTER_NEAREST;
    p.uv_mode = SDL3D_UV_AFFINE;
    p.fog_eval = SDL3D_FOG_EVAL_VERTEX;
    p.tonemap = SDL3D_TONEMAP_NONE;
    p.vertex_snap = true;
    p.vertex_snap_precision = 1;
    p.color_quantize = true;
    p.color_depth = 32;
    return p;
}

sdl3d_render_profile sdl3d_profile_n64(void)
{
    sdl3d_render_profile p;
    SDL_zerop(&p);
    p.shading = SDL3D_SHADING_GOURAUD;
    p.texture_filter = SDL3D_TEXTURE_FILTER_BILINEAR;
    p.uv_mode = SDL3D_UV_PERSPECTIVE;
    p.fog_eval = SDL3D_FOG_EVAL_VERTEX;
    p.tonemap = SDL3D_TONEMAP_NONE;
    return p;
}

sdl3d_render_profile sdl3d_profile_dos(void)
{
    sdl3d_render_profile p;
    SDL_zerop(&p);
    p.shading = SDL3D_SHADING_GOURAUD;
    p.texture_filter = SDL3D_TEXTURE_FILTER_NEAREST;
    p.uv_mode = SDL3D_UV_AFFINE;
    p.fog_eval = SDL3D_FOG_EVAL_VERTEX;
    p.tonemap = SDL3D_TONEMAP_NONE;
    p.color_quantize = true;
    p.color_depth = 6;
    return p;
}

sdl3d_render_profile sdl3d_profile_snes(void)
{
    sdl3d_render_profile p;
    SDL_zerop(&p);
    p.shading = SDL3D_SHADING_FLAT;
    p.texture_filter = SDL3D_TEXTURE_FILTER_NEAREST;
    p.uv_mode = SDL3D_UV_AFFINE;
    p.fog_eval = SDL3D_FOG_EVAL_VERTEX;
    p.tonemap = SDL3D_TONEMAP_NONE;
    p.color_quantize = true;
    p.color_depth = 32;
    return p;
}
