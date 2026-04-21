/*
 * OpenGL 3.3 / ES 3.0 rendering backend for SDL3D.
 *
 * When SDL3D_BACKEND_SDLGPU (now repurposed as the GL backend) is
 * selected, this module handles all GPU rendering using embedded GLSL
 * shaders compiled at runtime.
 */

#include "gl_backend.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "gl_funcs.h"
#include "gl_shaders.h"

#include "sdl3d/lighting.h"
#include "sdl3d/texture.h"

static const char *const SDL3D_GL_DEBUG_ENV = "SDL3D_GL_DEBUG";
static const char *const SDL3D_GL_DISABLE_POSTPROCESS_ENV = "SDL3D_GL_DISABLE_POSTPROCESS";
static const Uint64 SDL3D_GL_DEBUG_FRAME_LIMIT = 5;

typedef struct sdl3d_gl_texture_entry
{
    const sdl3d_texture2d *texture;
    GLuint gl_texture;
    struct sdl3d_gl_texture_entry *next;
} sdl3d_gl_texture_entry;

typedef struct sdl3d_gl_stream_buffers
{
    GLuint vao;
    GLuint position_vbo;
    GLuint normal_vbo;
    GLuint uv_vbo;
    GLuint color_vbo;
    GLuint index_ebo;
} sdl3d_gl_stream_buffers;

typedef struct sdl3d_gl_light_uniforms
{
    GLint type_loc;
    GLint position_loc;
    GLint direction_loc;
    GLint color_loc;
    GLint intensity_loc;
    GLint range_loc;
    GLint inner_cutoff_loc;
    GLint outer_cutoff_loc;
} sdl3d_gl_light_uniforms;

typedef struct sdl3d_gl_lit_uniform_cache
{
    GLint mvp_loc;
    GLint model_loc;
    GLint normal_matrix_loc;
    GLint tint_loc;
    GLint texture_loc;
    GLint has_texture_loc;
    GLint camera_pos_loc;
    GLint ambient_loc;
    GLint metallic_loc;
    GLint roughness_loc;
    GLint emissive_loc;
    GLint light_count_loc;
    GLint tonemap_mode_loc;
    GLint fog_mode_loc;
    GLint fog_color_loc;
    GLint fog_start_loc;
    GLint fog_end_loc;
    GLint fog_density_loc;
    GLint snap_precision_loc;
    sdl3d_gl_light_uniforms lights[8];
    Uint64 scene_uniform_frame;
} sdl3d_gl_lit_uniform_cache;

struct sdl3d_gl_context
{
    SDL_Window *window;
    SDL_GLContext gl_context;
    sdl3d_gl_funcs gl;
    bool is_es; /* true if running OpenGL ES */
    bool doublebuffered;
    bool debug_enabled;
    bool disable_postprocess;
    Uint64 frame_index;
    int unlit_draw_calls;
    int lit_draw_calls;

    /* Shader programs. */
    GLuint unlit_program;
    GLuint lit_program;
    GLuint ps1_program;
    GLuint n64_program;
    GLuint dos_program;
    GLuint snes_program;

    /* Fullscreen copy shader used for FBO copies and window present. */
    GLuint copy_program;

    /* Post-process shader. */
    GLuint postprocess_program;

    /* Unlit shader uniforms. */
    GLint unlit_mvp_loc;
    GLint unlit_tint_loc;
    GLint unlit_texture_loc;
    GLint unlit_has_texture_loc;

    /* Lit shader uniforms. */
    GLint lit_mvp_loc;
    GLint lit_model_loc;
    GLint lit_normal_matrix_loc;
    GLint lit_tint_loc;
    GLint lit_texture_loc;
    GLint lit_has_texture_loc;
    GLint lit_camera_pos_loc;
    GLint lit_ambient_loc;
    GLint lit_metallic_loc;
    GLint lit_roughness_loc;
    GLint lit_emissive_loc;
    GLint lit_light_count_loc;
    GLint lit_tonemap_mode_loc;
    GLint lit_fog_mode_loc;
    GLint lit_fog_color_loc;
    GLint lit_fog_start_loc;
    GLint lit_fog_end_loc;
    GLint lit_fog_density_loc;
    sdl3d_gl_lit_uniform_cache lit_uniforms;
    sdl3d_gl_lit_uniform_cache ps1_uniforms;
    sdl3d_gl_lit_uniform_cache n64_uniforms;
    sdl3d_gl_lit_uniform_cache dos_uniforms;
    sdl3d_gl_lit_uniform_cache snes_uniforms;

    /* Default white texture for untextured meshes. */
    GLuint white_texture;
    sdl3d_gl_texture_entry *texture_cache;

    /* Reused GL objects to avoid per-draw object churn. */
    GLuint fullscreen_vao;
    sdl3d_gl_stream_buffers unlit_buffers;
    sdl3d_gl_stream_buffers lit_buffers;
    float *white_color_buffer;
    int white_color_capacity;

    GLint copy_texture_loc;
    GLint postprocess_scene_loc;
    GLint postprocess_texel_size_loc;
    GLint postprocess_effects_loc;
    GLint postprocess_bloom_threshold_loc;
    GLint postprocess_bloom_intensity_loc;
    GLint postprocess_vignette_intensity_loc;
    GLint postprocess_contrast_loc;
    GLint postprocess_brightness_loc;
    GLint postprocess_saturation_loc;

    /* Offscreen FBO for logical-resolution rendering. */
    GLuint fbo;
    GLuint fbo_color_texture;
    GLuint fbo_depth_rbo;
    int logical_width;
    int logical_height;

    /* Shadow mapping. */
    GLuint shadow_texture;
    GLint lit_shadow_map_loc;
    GLint lit_shadow_vp_loc;
    GLint lit_shadow_enabled_loc;
    GLint lit_shadow_bias_loc;

    int width;
    int height;
};

/* ------------------------------------------------------------------ */
/* Shader compilation                                                  */
/* ------------------------------------------------------------------ */

static GLuint sdl3d_gl_compile_shader(sdl3d_gl_funcs *gl, GLenum type, const char *source, bool is_es)
{
    GLuint shader = gl->CreateShader(type);
    GLint status;
    const char *sources[2];
    sources[0] = is_es ? SDL3D_GLSL_VERSION_ES300 : SDL3D_GLSL_VERSION_330;
    sources[1] = source;
    gl->ShaderSource(shader, 2, sources, NULL);
    gl->CompileShader(shader);
    gl->GetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status)
    {
        char log[512];
        gl->GetShaderInfoLog(shader, 512, NULL, log);
        SDL_SetError("Shader compile error: %s", log);
        gl->DeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint sdl3d_gl_link_program(sdl3d_gl_funcs *gl, const char *vert_src, const char *frag_src, bool is_es)
{
    GLuint vert = sdl3d_gl_compile_shader(gl, GL_VERTEX_SHADER, vert_src, is_es);
    GLuint frag = sdl3d_gl_compile_shader(gl, GL_FRAGMENT_SHADER, frag_src, is_es);
    GLuint program;
    GLint status;

    if (vert == 0 || frag == 0)
    {
        if (vert)
        {
            gl->DeleteShader(vert);
        }
        if (frag)
        {
            gl->DeleteShader(frag);
        }
        return 0;
    }

    program = gl->CreateProgram();
    gl->AttachShader(program, vert);
    gl->AttachShader(program, frag);
    gl->LinkProgram(program);
    gl->GetProgramiv(program, GL_LINK_STATUS, &status);
    gl->DeleteShader(vert);
    gl->DeleteShader(frag);

    if (!status)
    {
        char log[512];
        gl->GetProgramInfoLog(program, 512, NULL, log);
        SDL_SetError("Shader link error: %s", log);
        gl->DeleteProgram(program);
        return 0;
    }

    return program;
}

static bool sdl3d_gl_env_flag_enabled(const char *name)
{
    const char *value = SDL_getenv(name);

    if (value == NULL || *value == '\0')
    {
        return false;
    }

    return SDL_strcasecmp(value, "0") != 0 && SDL_strcasecmp(value, "false") != 0 && SDL_strcasecmp(value, "no") != 0 &&
           SDL_strcasecmp(value, "off") != 0;
}

static bool sdl3d_gl_debug_should_log(const sdl3d_gl_context *ctx)
{
    return ctx != NULL && ctx->debug_enabled && ctx->frame_index > 0 && ctx->frame_index <= SDL3D_GL_DEBUG_FRAME_LIMIT;
}

static void sdl3d_gl_debug_log_pixel(sdl3d_gl_context *ctx, GLenum framebuffer_target, GLuint framebuffer, int width,
                                     int height, const char *stage)
{
    Uint8 pixel[4] = {0, 0, 0, 0};
    const int sample_x = (width > 0) ? (width / 2) : 0;
    const int sample_y = (height > 0) ? (height / 2) : 0;
    GLenum err;

    if (!sdl3d_gl_debug_should_log(ctx) || width <= 0 || height <= 0)
    {
        return;
    }

    ctx->gl.BindFramebuffer(framebuffer_target, framebuffer);
    ctx->gl.ReadPixels(sample_x, sample_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    err = ctx->gl.GetError();

    SDL_Log("SDL3D GL frame %llu %s sample(%d,%d) rgba=(%u,%u,%u,%u) draws=(unlit:%d lit:%d) err=0x%x fb=%u",
            (unsigned long long)ctx->frame_index, stage, sample_x, sample_y, (unsigned)pixel[0], (unsigned)pixel[1],
            (unsigned)pixel[2], (unsigned)pixel[3], ctx->unlit_draw_calls, ctx->lit_draw_calls, (unsigned)err,
            (unsigned)framebuffer);
}

static void sdl3d_gl_delete_stream_buffers(const sdl3d_gl_funcs *gl, sdl3d_gl_stream_buffers *buffers)
{
    if (gl == NULL || buffers == NULL)
    {
        return;
    }
    if (buffers->position_vbo)
    {
        gl->DeleteBuffers(1, &buffers->position_vbo);
    }
    if (buffers->normal_vbo)
    {
        gl->DeleteBuffers(1, &buffers->normal_vbo);
    }
    if (buffers->uv_vbo)
    {
        gl->DeleteBuffers(1, &buffers->uv_vbo);
    }
    if (buffers->color_vbo)
    {
        gl->DeleteBuffers(1, &buffers->color_vbo);
    }
    if (buffers->index_ebo)
    {
        gl->DeleteBuffers(1, &buffers->index_ebo);
    }
    if (buffers->vao)
    {
        gl->DeleteVertexArrays(1, &buffers->vao);
    }
    SDL_zero(*buffers);
}

static bool sdl3d_gl_init_stream_buffers(const sdl3d_gl_funcs *gl, sdl3d_gl_stream_buffers *buffers, bool lit)
{
    if (gl == NULL || buffers == NULL)
    {
        return false;
    }

    gl->GenVertexArrays(1, &buffers->vao);
    gl->BindVertexArray(buffers->vao);

    gl->GenBuffers(1, &buffers->position_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, buffers->position_vbo);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    if (lit)
    {
        gl->GenBuffers(1, &buffers->normal_vbo);
        gl->BindBuffer(GL_ARRAY_BUFFER, buffers->normal_vbo);
        gl->EnableVertexAttribArray(1);
        gl->VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    }

    gl->GenBuffers(1, &buffers->uv_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, buffers->uv_vbo);
    gl->EnableVertexAttribArray(lit ? 2u : 1u);
    gl->VertexAttribPointer(lit ? 2u : 1u, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &buffers->color_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, buffers->color_vbo);
    gl->EnableVertexAttribArray(lit ? 3u : 2u);
    gl->VertexAttribPointer(lit ? 3u : 2u, 4, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &buffers->index_ebo);
    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers->index_ebo);

    gl->BindVertexArray(0);
    gl->BindBuffer(GL_ARRAY_BUFFER, 0);
    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    return true;
}

static const float *sdl3d_gl_get_white_colors(sdl3d_gl_context *ctx, int vertex_count)
{
    int required_floats;
    int old_capacity;

    if (ctx == NULL || vertex_count <= 0)
    {
        return NULL;
    }

    if (ctx->white_color_capacity >= vertex_count)
    {
        return ctx->white_color_buffer;
    }

    required_floats = vertex_count * 4;
    old_capacity = ctx->white_color_capacity;
    {
        float *new_buffer = (float *)SDL_realloc(ctx->white_color_buffer, (size_t)required_floats * sizeof(float));
        if (new_buffer == NULL)
        {
            return NULL;
        }
        ctx->white_color_buffer = new_buffer;
    }
    if (ctx->white_color_buffer == NULL)
    {
        return NULL;
    }

    for (int i = old_capacity * 4; i < required_floats; ++i)
    {
        ctx->white_color_buffer[i] = 1.0f;
    }
    ctx->white_color_capacity = vertex_count;
    return ctx->white_color_buffer;
}

static GLint sdl3d_gl_texture_min_filter(const sdl3d_texture2d *texture, int filter_override)
{
    if (filter_override == 0)
    {
        return GL_NEAREST;
    }
    if (texture != NULL && texture->filter == SDL3D_TEXTURE_FILTER_TRILINEAR && texture->mip_count > 1)
    {
        return GL_LINEAR_MIPMAP_LINEAR;
    }
    return GL_LINEAR;
}

static GLint sdl3d_gl_texture_mag_filter(int filter_override)
{
    return (filter_override == 0) ? GL_NEAREST : GL_LINEAR;
}

static GLint sdl3d_gl_texture_wrap_mode(sdl3d_texture_wrap wrap)
{
    return (wrap == SDL3D_TEXTURE_WRAP_REPEAT) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

static void sdl3d_gl_destroy_texture_cache(sdl3d_gl_context *ctx)
{
    sdl3d_gl_texture_entry *entry;
    sdl3d_gl_texture_entry *next;

    if (ctx == NULL)
    {
        return;
    }

    for (entry = ctx->texture_cache; entry != NULL; entry = next)
    {
        next = entry->next;
        if (entry->gl_texture)
        {
            ctx->gl.DeleteTextures(1, &entry->gl_texture);
        }
        SDL_free(entry);
    }
    ctx->texture_cache = NULL;
}

static GLuint sdl3d_gl_get_or_upload_texture(sdl3d_gl_context *ctx, const sdl3d_texture2d *texture)
{
    sdl3d_gl_texture_entry *entry;
    GLuint gl_texture = 0;

    if (ctx == NULL || texture == NULL || texture->pixels == NULL || texture->width <= 0 || texture->height <= 0)
    {
        return 0;
    }

    for (entry = ctx->texture_cache; entry != NULL; entry = entry->next)
    {
        if (entry->texture == texture)
        {
            return entry->gl_texture;
        }
    }

    entry = (sdl3d_gl_texture_entry *)SDL_calloc(1, sizeof(*entry));
    if (entry == NULL)
    {
        SDL_OutOfMemory();
        return 0;
    }

    ctx->gl.GenTextures(1, &gl_texture);
    ctx->gl.BindTexture(GL_TEXTURE_2D, gl_texture);

    if (texture->filter == SDL3D_TEXTURE_FILTER_TRILINEAR && texture->mip_levels != NULL && texture->mip_count > 1)
    {
        for (int level = 0; level < texture->mip_count; ++level)
        {
            const sdl3d_texture_mip_level *mip = &texture->mip_levels[level];
            if (mip->pixels == NULL || mip->width <= 0 || mip->height <= 0)
            {
                continue;
            }
            ctx->gl.TexImage2D(GL_TEXTURE_2D, level, GL_RGBA, mip->width, mip->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                               mip->pixels);
        }
    }
    else
    {
        ctx->gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                           texture->pixels);
    }

    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sdl3d_gl_texture_wrap_mode(texture->wrap_u));
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sdl3d_gl_texture_wrap_mode(texture->wrap_v));

    entry->texture = texture;
    entry->gl_texture = gl_texture;
    entry->next = ctx->texture_cache;
    ctx->texture_cache = entry;

    return gl_texture;
}

static GLuint sdl3d_gl_bind_texture_for_draw(sdl3d_gl_context *ctx, const sdl3d_texture2d *texture, int filter_override)
{
    GLuint gl_texture = sdl3d_gl_get_or_upload_texture(ctx, texture);

    if (gl_texture == 0)
    {
        gl_texture = ctx->white_texture;
    }

    ctx->gl.ActiveTexture(GL_TEXTURE0);
    ctx->gl.BindTexture(GL_TEXTURE_2D, gl_texture);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sdl3d_gl_texture_min_filter(texture, filter_override));
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sdl3d_gl_texture_mag_filter(filter_override));

    if (texture != NULL)
    {
        ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sdl3d_gl_texture_wrap_mode(texture->wrap_u));
        ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sdl3d_gl_texture_wrap_mode(texture->wrap_v));
    }
    else
    {
        ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    return gl_texture;
}

static void sdl3d_gl_init_lit_uniform_cache(sdl3d_gl_context *ctx, GLuint program, sdl3d_gl_lit_uniform_cache *cache)
{
    char name[64];

    if (ctx == NULL || program == 0 || cache == NULL)
    {
        return;
    }

    SDL_zero(*cache);
    cache->mvp_loc = ctx->gl.GetUniformLocation(program, "uMVP");
    cache->model_loc = ctx->gl.GetUniformLocation(program, "uModel");
    cache->normal_matrix_loc = ctx->gl.GetUniformLocation(program, "uNormalMatrix");
    cache->tint_loc = ctx->gl.GetUniformLocation(program, "uTint");
    cache->texture_loc = ctx->gl.GetUniformLocation(program, "uTexture");
    cache->has_texture_loc = ctx->gl.GetUniformLocation(program, "uHasTexture");
    cache->camera_pos_loc = ctx->gl.GetUniformLocation(program, "uCameraPos");
    cache->ambient_loc = ctx->gl.GetUniformLocation(program, "uAmbient");
    cache->metallic_loc = ctx->gl.GetUniformLocation(program, "uMetallic");
    cache->roughness_loc = ctx->gl.GetUniformLocation(program, "uRoughness");
    cache->emissive_loc = ctx->gl.GetUniformLocation(program, "uEmissive");
    cache->light_count_loc = ctx->gl.GetUniformLocation(program, "uLightCount");
    cache->tonemap_mode_loc = ctx->gl.GetUniformLocation(program, "uTonemapMode");
    cache->fog_mode_loc = ctx->gl.GetUniformLocation(program, "uFogMode");
    cache->fog_color_loc = ctx->gl.GetUniformLocation(program, "uFogColor");
    cache->fog_start_loc = ctx->gl.GetUniformLocation(program, "uFogStart");
    cache->fog_end_loc = ctx->gl.GetUniformLocation(program, "uFogEnd");
    cache->fog_density_loc = ctx->gl.GetUniformLocation(program, "uFogDensity");
    cache->snap_precision_loc = ctx->gl.GetUniformLocation(program, "uSnapPrecision");

    for (int i = 0; i < 8; ++i)
    {
        SDL_snprintf(name, sizeof(name), "uLights[%d].type", i);
        cache->lights[i].type_loc = ctx->gl.GetUniformLocation(program, name);
        SDL_snprintf(name, sizeof(name), "uLights[%d].position", i);
        cache->lights[i].position_loc = ctx->gl.GetUniformLocation(program, name);
        SDL_snprintf(name, sizeof(name), "uLights[%d].direction", i);
        cache->lights[i].direction_loc = ctx->gl.GetUniformLocation(program, name);
        SDL_snprintf(name, sizeof(name), "uLights[%d].color", i);
        cache->lights[i].color_loc = ctx->gl.GetUniformLocation(program, name);
        SDL_snprintf(name, sizeof(name), "uLights[%d].intensity", i);
        cache->lights[i].intensity_loc = ctx->gl.GetUniformLocation(program, name);
        SDL_snprintf(name, sizeof(name), "uLights[%d].range", i);
        cache->lights[i].range_loc = ctx->gl.GetUniformLocation(program, name);
        SDL_snprintf(name, sizeof(name), "uLights[%d].innerCutoff", i);
        cache->lights[i].inner_cutoff_loc = ctx->gl.GetUniformLocation(program, name);
        SDL_snprintf(name, sizeof(name), "uLights[%d].outerCutoff", i);
        cache->lights[i].outer_cutoff_loc = ctx->gl.GetUniformLocation(program, name);
    }
}

static void sdl3d_gl_upload_scene_uniforms(sdl3d_gl_context *ctx, sdl3d_gl_lit_uniform_cache *cache,
                                           const sdl3d_draw_params_lit *params)
{
    const sdl3d_light *lt;

    if (ctx == NULL || cache == NULL || params == NULL || ctx->frame_index == cache->scene_uniform_frame)
    {
        return;
    }

    ctx->gl.Uniform3f(cache->camera_pos_loc, params->camera_pos[0], params->camera_pos[1], params->camera_pos[2]);
    ctx->gl.Uniform3f(cache->ambient_loc, params->ambient[0], params->ambient[1], params->ambient[2]);
    ctx->gl.Uniform1i(cache->light_count_loc, params->light_count);
    ctx->gl.Uniform1i(cache->tonemap_mode_loc, params->tonemap_mode);
    ctx->gl.Uniform1i(cache->fog_mode_loc, params->fog_mode);
    ctx->gl.Uniform3f(cache->fog_color_loc, params->fog_color[0], params->fog_color[1], params->fog_color[2]);
    ctx->gl.Uniform1f(cache->fog_start_loc, params->fog_start);
    ctx->gl.Uniform1f(cache->fog_end_loc, params->fog_end);
    ctx->gl.Uniform1f(cache->fog_density_loc, params->fog_density);

    lt = (const sdl3d_light *)params->lights;
    for (int i = 0; i < params->light_count && i < 8; ++i)
    {
        const sdl3d_gl_light_uniforms *light = &cache->lights[i];
        ctx->gl.Uniform1i(light->type_loc, (int)lt[i].type);
        ctx->gl.Uniform3f(light->position_loc, lt[i].position.x, lt[i].position.y, lt[i].position.z);
        ctx->gl.Uniform3f(light->direction_loc, lt[i].direction.x, lt[i].direction.y, lt[i].direction.z);
        ctx->gl.Uniform3f(light->color_loc, lt[i].color[0], lt[i].color[1], lt[i].color[2]);
        ctx->gl.Uniform1f(light->intensity_loc, lt[i].intensity);
        ctx->gl.Uniform1f(light->range_loc, lt[i].range);
        ctx->gl.Uniform1f(light->inner_cutoff_loc, lt[i].inner_cutoff);
        ctx->gl.Uniform1f(light->outer_cutoff_loc, lt[i].outer_cutoff);
    }

    cache->scene_uniform_frame = ctx->frame_index;
}

static sdl3d_gl_lit_uniform_cache *sdl3d_gl_get_uniform_cache_for_program(sdl3d_gl_context *ctx, GLuint program)
{
    if (ctx == NULL || program == 0)
    {
        return NULL;
    }
    if (program == ctx->lit_program)
    {
        return &ctx->lit_uniforms;
    }
    if (program == ctx->ps1_program)
    {
        return &ctx->ps1_uniforms;
    }
    if (program == ctx->n64_program)
    {
        return &ctx->n64_uniforms;
    }
    if (program == ctx->dos_program)
    {
        return &ctx->dos_uniforms;
    }
    if (program == ctx->snes_program)
    {
        return &ctx->snes_uniforms;
    }
    return NULL;
}

static void sdl3d_gl_draw_fullscreen_triangle(const sdl3d_gl_funcs *gl)
{
    if (gl == NULL)
    {
        return;
    }

    gl->DrawArrays(GL_TRIANGLES, 0, 3);
}

static void sdl3d_gl_copy_texture_to_bound_framebuffer(sdl3d_gl_context *ctx, GLuint texture, int viewport_width,
                                                       int viewport_height)
{
    const sdl3d_gl_funcs *gl;

    if (ctx == NULL || texture == 0 || viewport_width <= 0 || viewport_height <= 0)
    {
        return;
    }

    gl = &ctx->gl;
    gl->Viewport(0, 0, viewport_width, viewport_height);
    gl->UseProgram(ctx->copy_program);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, texture);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->Uniform1i(ctx->copy_texture_loc, 0);
    gl->BindVertexArray(ctx->fullscreen_vao);
    sdl3d_gl_draw_fullscreen_triangle(gl);
    gl->BindVertexArray(0);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

sdl3d_gl_context *sdl3d_gl_create(SDL_Window *window, int width, int height)
{
    sdl3d_gl_context *ctx;
    Uint8 white_pixel[] = {255, 255, 255, 255};

    if (window == NULL)
    {
        SDL_InvalidParamError("window");
        return NULL;
    }

    /* Request OpenGL 3.3 core (or ES 3.0 on mobile). */
#ifdef SDL3D_OPENGL_ES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    ctx = (sdl3d_gl_context *)SDL_calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

    ctx->debug_enabled = sdl3d_gl_env_flag_enabled(SDL3D_GL_DEBUG_ENV);
    ctx->disable_postprocess = sdl3d_gl_env_flag_enabled(SDL3D_GL_DISABLE_POSTPROCESS_ENV);

    ctx->gl_context = SDL_GL_CreateContext(window);
    if (ctx->gl_context == NULL)
    {
#ifndef SDL3D_OPENGL_ES
        /* Desktop GL failed — try OpenGL ES 3.0 as fallback (e.g. Raspberry Pi). */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        ctx->gl_context = SDL_GL_CreateContext(window);
#endif
    }
    if (ctx->gl_context == NULL)
    {
        SDL_free(ctx);
        return NULL;
    }

    SDL_GL_MakeCurrent(window, ctx->gl_context);
    ctx->window = window;

    /* Presentation on macOS is sensitive to the drawable/swap setup. */
    SDL_GL_SetSwapInterval(1);

    /* Detect if we're running ES (either compiled for ES or fell back to ES). */
#ifdef SDL3D_OPENGL_ES
    ctx->is_es = true;
#else
    {
        int profile = 0;
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile);
        ctx->is_es = (profile == SDL_GL_CONTEXT_PROFILE_ES);
    }
#endif

    if (!sdl3d_gl_load_funcs(&ctx->gl))
    {
        SDL_SetError("Failed to load OpenGL functions.");
        SDL_GL_DestroyContext(ctx->gl_context);
        SDL_free(ctx);
        return NULL;
    }

    /* Compile shaders. */
    ctx->unlit_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_unlit_vert, sdl3d_shader_unlit_frag, ctx->is_es);
    ctx->lit_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_lit_vert, sdl3d_shader_lit_frag, ctx->is_es);
    ctx->ps1_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_ps1_vert, sdl3d_shader_ps1_frag, ctx->is_es);
    ctx->n64_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_n64_vert, sdl3d_shader_n64_frag, ctx->is_es);
    ctx->dos_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_dos_vert, sdl3d_shader_dos_frag, ctx->is_es);
    ctx->snes_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_snes_vert, sdl3d_shader_snes_frag, ctx->is_es);
    ctx->copy_program =
        sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_fullscreen_vert, sdl3d_shader_copy_frag, ctx->is_es);
    ctx->postprocess_program =
        sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_fullscreen_vert, sdl3d_shader_postprocess_frag, ctx->is_es);

    if (ctx->unlit_program == 0 || ctx->lit_program == 0 || ctx->copy_program == 0)
    {
        SDL_GL_DestroyContext(ctx->gl_context);
        SDL_free(ctx);
        return NULL;
    }

    /* Cache uniform locations. */
    ctx->unlit_mvp_loc = ctx->gl.GetUniformLocation(ctx->unlit_program, "uMVP");
    ctx->unlit_tint_loc = ctx->gl.GetUniformLocation(ctx->unlit_program, "uTint");
    ctx->unlit_texture_loc = ctx->gl.GetUniformLocation(ctx->unlit_program, "uTexture");
    ctx->unlit_has_texture_loc = ctx->gl.GetUniformLocation(ctx->unlit_program, "uHasTexture");

    ctx->lit_mvp_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uMVP");
    ctx->lit_model_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uModel");
    ctx->lit_normal_matrix_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uNormalMatrix");
    ctx->lit_tint_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uTint");
    ctx->lit_texture_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uTexture");
    ctx->lit_has_texture_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uHasTexture");
    ctx->lit_camera_pos_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uCameraPos");
    ctx->lit_ambient_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uAmbient");
    ctx->lit_metallic_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uMetallic");
    ctx->lit_roughness_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uRoughness");
    ctx->lit_emissive_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uEmissive");
    ctx->lit_light_count_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uLightCount");
    ctx->lit_tonemap_mode_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uTonemapMode");
    ctx->lit_fog_mode_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uFogMode");
    ctx->lit_fog_color_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uFogColor");
    ctx->lit_fog_start_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uFogStart");
    ctx->lit_fog_end_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uFogEnd");
    ctx->lit_fog_density_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uFogDensity");
    sdl3d_gl_init_lit_uniform_cache(ctx, ctx->lit_program, &ctx->lit_uniforms);
    sdl3d_gl_init_lit_uniform_cache(ctx, ctx->ps1_program, &ctx->ps1_uniforms);
    sdl3d_gl_init_lit_uniform_cache(ctx, ctx->n64_program, &ctx->n64_uniforms);
    sdl3d_gl_init_lit_uniform_cache(ctx, ctx->dos_program, &ctx->dos_uniforms);
    sdl3d_gl_init_lit_uniform_cache(ctx, ctx->snes_program, &ctx->snes_uniforms);

    /* Shadow map uniform locations and texture. */
    ctx->lit_shadow_map_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uShadowMap");
    ctx->lit_shadow_vp_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uShadowVP");
    ctx->lit_shadow_enabled_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uShadowEnabled");
    ctx->lit_shadow_bias_loc = ctx->gl.GetUniformLocation(ctx->lit_program, "uShadowBias");

    ctx->gl.GenTextures(1, &ctx->shadow_texture);
    ctx->gl.BindTexture(GL_TEXTURE_2D, ctx->shadow_texture);
    ctx->gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 512, 512, 0, GL_RED, GL_FLOAT, NULL);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ctx->copy_texture_loc = ctx->gl.GetUniformLocation(ctx->copy_program, "uScene");
    ctx->postprocess_scene_loc = ctx->gl.GetUniformLocation(ctx->postprocess_program, "uScene");
    ctx->postprocess_texel_size_loc = ctx->gl.GetUniformLocation(ctx->postprocess_program, "uTexelSize");
    ctx->postprocess_effects_loc = ctx->gl.GetUniformLocation(ctx->postprocess_program, "uEffects");
    ctx->postprocess_bloom_threshold_loc = ctx->gl.GetUniformLocation(ctx->postprocess_program, "uBloomThreshold");
    ctx->postprocess_bloom_intensity_loc = ctx->gl.GetUniformLocation(ctx->postprocess_program, "uBloomIntensity");
    ctx->postprocess_vignette_intensity_loc =
        ctx->gl.GetUniformLocation(ctx->postprocess_program, "uVignetteIntensity");
    ctx->postprocess_contrast_loc = ctx->gl.GetUniformLocation(ctx->postprocess_program, "uContrast");
    ctx->postprocess_brightness_loc = ctx->gl.GetUniformLocation(ctx->postprocess_program, "uBrightness");
    ctx->postprocess_saturation_loc = ctx->gl.GetUniformLocation(ctx->postprocess_program, "uSaturation");

    /* Create default white texture. */
    ctx->gl.GenTextures(1, &ctx->white_texture);
    ctx->gl.BindTexture(GL_TEXTURE_2D, ctx->white_texture);
    ctx->gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_pixel);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    ctx->gl.GenVertexArrays(1, &ctx->fullscreen_vao);
    if (ctx->fullscreen_vao == 0 || !sdl3d_gl_init_stream_buffers(&ctx->gl, &ctx->unlit_buffers, false) ||
        !sdl3d_gl_init_stream_buffers(&ctx->gl, &ctx->lit_buffers, true))
    {
        SDL_SetError("Failed to create OpenGL streaming buffers.");
        sdl3d_gl_destroy(ctx);
        return NULL;
    }

    /* Default GL state. */
    ctx->gl.Enable(GL_DEPTH_TEST);
    ctx->gl.DepthFunc(GL_LEQUAL);
    ctx->gl.Enable(GL_CULL_FACE);
    ctx->gl.CullFace(GL_BACK);
    ctx->gl.FrontFace(GL_CCW);

    ctx->width = width;
    ctx->height = height;

    /* Create offscreen FBO at logical resolution. */
    ctx->logical_width = width;
    ctx->logical_height = height;
    ctx->gl.GenFramebuffers(1, &ctx->fbo);
    ctx->gl.GenTextures(1, &ctx->fbo_color_texture);
    ctx->gl.GenRenderbuffers(1, &ctx->fbo_depth_rbo);

    ctx->gl.BindTexture(GL_TEXTURE_2D, ctx->fbo_color_texture);
    ctx->gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    ctx->gl.BindRenderbuffer(GL_RENDERBUFFER, ctx->fbo_depth_rbo);
    ctx->gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

    ctx->gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    ctx->gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->fbo_color_texture, 0);
    ctx->gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ctx->fbo_depth_rbo);

    if (ctx->gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_SetError("Failed to create OpenGL offscreen framebuffer.");
        ctx->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        sdl3d_gl_destroy(ctx);
        return NULL;
    }

    /* Leave the FBO bound — all rendering targets the offscreen buffer. */
    ctx->gl.Viewport(0, 0, width, height);

    if (ctx->debug_enabled)
    {
        int major = 0;
        int minor = 0;
        int profile = 0;
        int doublebuffer = 0;
        int swap_interval = -1;
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile);
        SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &doublebuffer);
        ctx->doublebuffered = (doublebuffer != 0);
        (void)SDL_GL_GetSwapInterval(&swap_interval);
        SDL_Log("SDL3D GL create: ctx=%p window=%p profile=%d version=%d.%d logical=%dx%d fbo=%u color=%u depth=%u "
                "doublebuffer=%d swap=%d disable_post=%d",
                (void *)ctx->gl_context, (void *)window, profile, major, minor, width, height, (unsigned)ctx->fbo,
                (unsigned)ctx->fbo_color_texture, (unsigned)ctx->fbo_depth_rbo, doublebuffer, swap_interval,
                (int)ctx->disable_postprocess);
    }
    else
    {
        int doublebuffer = 0;
        SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &doublebuffer);
        ctx->doublebuffered = (doublebuffer != 0);
    }

    /* Start at 1 so the zero-initialized uniform caches don't falsely
     * match on the first frame (scene_uniform_frame defaults to 0). */
    ctx->frame_index = 1;

    return ctx;
}

void sdl3d_gl_destroy(sdl3d_gl_context *ctx)
{
    if (ctx == NULL)
    {
        return;
    }
    if (ctx->unlit_program)
    {
        ctx->gl.DeleteProgram(ctx->unlit_program);
    }
    if (ctx->lit_program)
    {
        ctx->gl.DeleteProgram(ctx->lit_program);
    }
    if (ctx->ps1_program)
    {
        ctx->gl.DeleteProgram(ctx->ps1_program);
    }
    if (ctx->n64_program)
    {
        ctx->gl.DeleteProgram(ctx->n64_program);
    }
    if (ctx->dos_program)
    {
        ctx->gl.DeleteProgram(ctx->dos_program);
    }
    if (ctx->snes_program)
    {
        ctx->gl.DeleteProgram(ctx->snes_program);
    }
    if (ctx->copy_program)
    {
        ctx->gl.DeleteProgram(ctx->copy_program);
    }
    if (ctx->postprocess_program)
    {
        ctx->gl.DeleteProgram(ctx->postprocess_program);
    }
    if (ctx->white_texture)
    {
        ctx->gl.DeleteTextures(1, &ctx->white_texture);
    }
    if (ctx->shadow_texture)
    {
        ctx->gl.DeleteTextures(1, &ctx->shadow_texture);
    }
    sdl3d_gl_destroy_texture_cache(ctx);
    sdl3d_gl_delete_stream_buffers(&ctx->gl, &ctx->unlit_buffers);
    sdl3d_gl_delete_stream_buffers(&ctx->gl, &ctx->lit_buffers);
    if (ctx->fullscreen_vao)
    {
        ctx->gl.DeleteVertexArrays(1, &ctx->fullscreen_vao);
    }
    SDL_free(ctx->white_color_buffer);
    if (ctx->fbo)
    {
        ctx->gl.DeleteFramebuffers(1, &ctx->fbo);
    }
    if (ctx->fbo_color_texture)
    {
        ctx->gl.DeleteTextures(1, &ctx->fbo_color_texture);
    }
    if (ctx->fbo_depth_rbo)
    {
        ctx->gl.DeleteRenderbuffers(1, &ctx->fbo_depth_rbo);
    }
    SDL_GL_DestroyContext(ctx->gl_context);
    SDL_free(ctx);
}

void sdl3d_gl_clear(sdl3d_gl_context *ctx, float r, float g, float b, float a)
{
    if (ctx == NULL)
    {
        return;
    }
    ctx->frame_index += 1;
    ctx->unlit_draw_calls = 0;
    ctx->lit_draw_calls = 0;
    /* Ensure our GL context is current (may have been lost after window recreation). */
    SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);
    /* Ensure we're rendering to the offscreen FBO. */
    ctx->gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    ctx->gl.Viewport(0, 0, ctx->logical_width, ctx->logical_height);
    ctx->gl.ClearColor(r, g, b, a);
    ctx->gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    sdl3d_gl_debug_log_pixel(ctx, GL_FRAMEBUFFER, ctx->fbo, ctx->logical_width, ctx->logical_height, "after-clear");
}

void sdl3d_gl_present(sdl3d_gl_context *ctx, SDL_Window *window)
{
    int window_w = 0;
    int window_h = 0;
    int vp_x, vp_y, vp_w, vp_h;
    float scale_x, scale_y, scale;

    if (ctx == NULL || window == NULL)
    {
        return;
    }

    SDL_GetWindowSizeInPixels(window, &window_w, &window_h);
    SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);

    /* Compute letterbox viewport to maintain aspect ratio. */
    scale_x = (float)window_w / (float)ctx->logical_width;
    scale_y = (float)window_h / (float)ctx->logical_height;
    scale = (scale_x < scale_y) ? scale_x : scale_y;
    vp_w = (int)((float)ctx->logical_width * scale);
    vp_h = (int)((float)ctx->logical_height * scale);
    vp_x = (window_w - vp_w) / 2;
    vp_y = (window_h - vp_h) / 2;

    ctx->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    ctx->gl.Disable(GL_DEPTH_TEST);
    ctx->gl.Disable(GL_CULL_FACE);

    /* Clear to black for letterbox bars. */
    ctx->gl.Viewport(0, 0, window_w, window_h);
    ctx->gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    ctx->gl.Clear(GL_COLOR_BUFFER_BIT);

    /* Copy FBO into the letterboxed viewport. */
    sdl3d_gl_copy_texture_to_bound_framebuffer(ctx, ctx->fbo_color_texture, vp_w, vp_h);
    /* The copy function sets viewport internally; override with letterbox offset. */
    ctx->gl.Viewport(vp_x, vp_y, vp_w, vp_h);
    ctx->gl.BindVertexArray(ctx->fullscreen_vao);
    ctx->gl.UseProgram(ctx->copy_program);
    ctx->gl.ActiveTexture(GL_TEXTURE0);
    ctx->gl.BindTexture(GL_TEXTURE_2D, ctx->fbo_color_texture);
    ctx->gl.Uniform1i(ctx->copy_texture_loc, 0);
    sdl3d_gl_draw_fullscreen_triangle(&ctx->gl);
    ctx->gl.BindVertexArray(0);

    sdl3d_gl_debug_log_pixel(ctx, GL_FRAMEBUFFER, 0, window_w, window_h, "after-present-copy");
    ctx->gl.Enable(GL_CULL_FACE);
    ctx->gl.Enable(GL_DEPTH_TEST);
}

void sdl3d_gl_flush(sdl3d_gl_context *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);
    ctx->gl.Flush();
}

void sdl3d_gl_finish(sdl3d_gl_context *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);
    ctx->gl.Finish();
}

bool sdl3d_gl_is_doublebuffered(const sdl3d_gl_context *ctx)
{
    return ctx != NULL && ctx->doublebuffered;
}

GLuint sdl3d_gl_get_unlit_program(const sdl3d_gl_context *ctx)
{
    return ctx ? ctx->unlit_program : 0;
}

GLuint sdl3d_gl_get_lit_program(const sdl3d_gl_context *ctx)
{
    return ctx ? ctx->lit_program : 0;
}

const sdl3d_gl_funcs *sdl3d_gl_get_funcs(const sdl3d_gl_context *ctx)
{
    return ctx ? &ctx->gl : NULL;
}

GLuint sdl3d_gl_get_white_texture(const sdl3d_gl_context *ctx)
{
    return ctx ? ctx->white_texture : 0;
}

GLuint sdl3d_gl_get_program_for_profile(const sdl3d_gl_context *ctx, int shading_mode, bool has_lights)
{
    if (ctx == NULL)
    {
        return 0;
    }
    /* Map shading mode to shader program. For GOURAUD, further
     * disambiguate by checking which profile program to use via the
     * draw params (handled by caller selecting ps1/n64/dos). For now,
     * default GOURAUD to N64 (the generic Gouraud program). */
    switch (shading_mode)
    {
    case 1: /* SDL3D_SHADING_FLAT */
        return ctx->snes_program ? ctx->snes_program : ctx->unlit_program;
    case 2: /* SDL3D_SHADING_GOURAUD */
        return ctx->n64_program ? ctx->n64_program : ctx->unlit_program;
    case 3: /* SDL3D_SHADING_PHONG */
        return has_lights ? ctx->lit_program : ctx->unlit_program;
    default: /* SDL3D_SHADING_UNLIT */
        return ctx->unlit_program;
    }
}

/* ------------------------------------------------------------------ */
/* Mesh rendering                                                      */
/* ------------------------------------------------------------------ */

void sdl3d_gl_draw_mesh_unlit(sdl3d_gl_context *ctx, const sdl3d_draw_params_unlit *params)
{
    const sdl3d_gl_funcs *gl;
    const sdl3d_gl_stream_buffers *buffers;
    const float *positions = params->positions;
    const float *uvs = params->uvs;
    const float *colors = params->colors;
    const unsigned int *indices = params->indices;
    int vertex_count = params->vertex_count;
    int index_count = params->index_count;
    const float *mvp = params->mvp;
    const float *tint = params->tint;
    const float *color_data;
    GLuint texture;
    bool has_texture;

    if (ctx == NULL || positions == NULL || vertex_count <= 0)
    {
        return;
    }

    ctx->unlit_draw_calls += 1;

    gl = &ctx->gl;
    buffers = &ctx->unlit_buffers;
    color_data = (colors != NULL) ? colors : sdl3d_gl_get_white_colors(ctx, vertex_count);
    if (color_data == NULL)
    {
        SDL_OutOfMemory();
        return;
    }
    texture = sdl3d_gl_bind_texture_for_draw(ctx, params->texture, params->texture_filter);
    has_texture = (params->texture != NULL && texture != ctx->white_texture);

    gl->UseProgram(ctx->unlit_program);
    gl->UniformMatrix4fv(ctx->unlit_mvp_loc, 1, GL_FALSE, mvp);
    gl->Uniform4f(ctx->unlit_tint_loc, tint[0], tint[1], tint[2], tint[3]);
    gl->Uniform1i(ctx->unlit_texture_loc, 0);
    gl->Uniform1i(ctx->unlit_has_texture_loc, has_texture ? 1 : 0);

    gl->BindVertexArray(buffers->vao);

    /* Positions. */
    gl->BindBuffer(GL_ARRAY_BUFFER, buffers->position_vbo);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 3 * sizeof(float)), positions, GL_DYNAMIC_DRAW);

    /* UVs. */
    gl->BindBuffer(GL_ARRAY_BUFFER, buffers->uv_vbo);
    if (uvs != NULL)
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 2 * sizeof(float)), uvs, GL_DYNAMIC_DRAW);
    }
    else
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 2 * sizeof(float)), NULL, GL_DYNAMIC_DRAW);
    }

    /* Colors. */
    gl->BindBuffer(GL_ARRAY_BUFFER, buffers->color_vbo);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 4 * sizeof(float)), color_data,
                   GL_DYNAMIC_DRAW);

    /* Indices + draw. */
    if (indices != NULL && index_count > 0)
    {
        gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers->index_ebo);
        gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)index_count * sizeof(unsigned int)), indices,
                       GL_DYNAMIC_DRAW);
        gl->DrawElements(GL_TRIANGLES, (GLsizei)index_count, GL_UNSIGNED_INT, NULL);
    }
    else
    {
        gl->DrawArrays(GL_TRIANGLES, 0, (GLsizei)vertex_count);
    }

    gl->BindVertexArray(0);
}

void sdl3d_gl_draw_mesh_lit(sdl3d_gl_context *ctx, const sdl3d_draw_params_lit *params)
{
    const sdl3d_gl_funcs *gl;
    const sdl3d_gl_stream_buffers *buffers;
    sdl3d_gl_lit_uniform_cache *cache = NULL;
    GLuint program;
    const float *positions = params->positions;
    const float *normals = params->normals;
    const float *uvs = params->uvs;
    const float *colors = params->colors;
    const unsigned int *indices = params->indices;
    int vertex_count = params->vertex_count;
    int index_count = params->index_count;
    const float *mvp = params->mvp;
    const float *model_matrix = params->model_matrix;
    const float *normal_matrix = params->normal_matrix;
    const float *tint = params->tint;
    const float *camera_pos = params->camera_pos;
    const float *ambient = params->ambient;
    float metallic = params->metallic;
    float roughness = params->roughness;
    const float *emissive = params->emissive;
    const struct sdl3d_light *lights = params->lights;
    int light_count = params->light_count;
    int tonemap_mode = params->tonemap_mode;
    int fog_mode = params->fog_mode;
    const float *fog_color = params->fog_color;
    float fog_start = params->fog_start;
    float fog_end = params->fog_end;
    float fog_density = params->fog_density;
    const float *color_data;
    GLuint texture;
    bool has_texture;

    if (ctx == NULL || positions == NULL || vertex_count <= 0)
    {
        return;
    }

    ctx->lit_draw_calls += 1;

    gl = &ctx->gl;
    buffers = &ctx->lit_buffers;
    color_data = (colors != NULL) ? colors : sdl3d_gl_get_white_colors(ctx, vertex_count);
    if (color_data == NULL)
    {
        SDL_OutOfMemory();
        return;
    }
    texture = sdl3d_gl_bind_texture_for_draw(ctx, params->texture, params->texture_filter);
    has_texture = (params->texture != NULL && texture != ctx->white_texture);

    /* Select shader program based on profile characteristics. */
    if (params->shading_mode == 2 /* GOURAUD */ && params->vertex_snap)
    {
        program = ctx->ps1_program ? ctx->ps1_program : ctx->n64_program;
    }
    else if (params->shading_mode == 2 /* GOURAUD */ && params->uv_mode == 1 /* AFFINE */)
    {
        program = ctx->dos_program ? ctx->dos_program : ctx->n64_program;
    }
    else
    {
        program = sdl3d_gl_get_program_for_profile(ctx, params->shading_mode, light_count > 0);
    }
    gl->UseProgram(program);
    cache = sdl3d_gl_get_uniform_cache_for_program(ctx, program);

    /* Set uniforms — query locations dynamically since each profile
     * shader has a different uniform set.  Unused uniforms return -1
     * and the gl->Uniform* calls silently ignore location -1. */
    gl->UniformMatrix4fv(cache ? cache->mvp_loc : gl->GetUniformLocation(program, "uMVP"), 1, GL_FALSE, mvp);
    gl->UniformMatrix4fv(cache ? cache->model_loc : gl->GetUniformLocation(program, "uModel"), 1, GL_FALSE,
                         model_matrix);
    gl->Uniform1i(cache ? cache->snap_precision_loc : gl->GetUniformLocation(program, "uSnapPrecision"),
                  params->vertex_snap_precision);
    {
        float nm[9] = {normal_matrix[0], normal_matrix[1], normal_matrix[2], normal_matrix[3], normal_matrix[4],
                       normal_matrix[5], normal_matrix[6], normal_matrix[7], normal_matrix[8]};
        GLint loc = cache ? cache->normal_matrix_loc : gl->GetUniformLocation(program, "uNormalMatrix");
        gl->UniformMatrix3fv(loc, 1, GL_FALSE, nm);
    }
    gl->Uniform4f(cache ? cache->tint_loc : gl->GetUniformLocation(program, "uTint"), tint[0], tint[1], tint[2],
                  tint[3]);
    gl->Uniform1f(cache ? cache->metallic_loc : gl->GetUniformLocation(program, "uMetallic"), metallic);
    gl->Uniform1f(cache ? cache->roughness_loc : gl->GetUniformLocation(program, "uRoughness"), roughness);
    gl->Uniform3f(cache ? cache->emissive_loc : gl->GetUniformLocation(program, "uEmissive"), emissive[0], emissive[1],
                  emissive[2]);
    if (cache != NULL)
    {
        sdl3d_gl_upload_scene_uniforms(ctx, cache, params);
    }
    else
    {
        gl->Uniform3f(gl->GetUniformLocation(program, "uCameraPos"), camera_pos[0], camera_pos[1], camera_pos[2]);
        gl->Uniform3f(gl->GetUniformLocation(program, "uAmbient"), ambient[0], ambient[1], ambient[2]);
        gl->Uniform1i(gl->GetUniformLocation(program, "uLightCount"), light_count);
        gl->Uniform1i(gl->GetUniformLocation(program, "uTonemapMode"), tonemap_mode);
        gl->Uniform1i(gl->GetUniformLocation(program, "uFogMode"), fog_mode);
        if (fog_color != NULL)
        {
            gl->Uniform3f(gl->GetUniformLocation(program, "uFogColor"), fog_color[0], fog_color[1], fog_color[2]);
        }
        gl->Uniform1f(gl->GetUniformLocation(program, "uFogStart"), fog_start);
        gl->Uniform1f(gl->GetUniformLocation(program, "uFogEnd"), fog_end);
        gl->Uniform1f(gl->GetUniformLocation(program, "uFogDensity"), fog_density);
    }

    /* Upload light uniforms. */
    if (cache == NULL && lights != NULL && light_count > 0)
    {
        const sdl3d_light *lt = (const sdl3d_light *)lights;
        char name[64];
        for (int i = 0; i < light_count && i < 8; ++i)
        {
            GLint loc;
            SDL_snprintf(name, sizeof(name), "uLights[%d].type", i);
            loc = gl->GetUniformLocation(program, name);
            gl->Uniform1i(loc, (int)lt[i].type);
            SDL_snprintf(name, sizeof(name), "uLights[%d].position", i);
            loc = gl->GetUniformLocation(program, name);
            gl->Uniform3f(loc, lt[i].position.x, lt[i].position.y, lt[i].position.z);
            SDL_snprintf(name, sizeof(name), "uLights[%d].direction", i);
            loc = gl->GetUniformLocation(program, name);
            gl->Uniform3f(loc, lt[i].direction.x, lt[i].direction.y, lt[i].direction.z);
            SDL_snprintf(name, sizeof(name), "uLights[%d].color", i);
            loc = gl->GetUniformLocation(program, name);
            gl->Uniform3f(loc, lt[i].color[0], lt[i].color[1], lt[i].color[2]);
            SDL_snprintf(name, sizeof(name), "uLights[%d].intensity", i);
            loc = gl->GetUniformLocation(program, name);
            gl->Uniform1f(loc, lt[i].intensity);
            SDL_snprintf(name, sizeof(name), "uLights[%d].range", i);
            loc = gl->GetUniformLocation(program, name);
            gl->Uniform1f(loc, lt[i].range);
            SDL_snprintf(name, sizeof(name), "uLights[%d].innerCutoff", i);
            loc = gl->GetUniformLocation(program, name);
            gl->Uniform1f(loc, lt[i].inner_cutoff);
            SDL_snprintf(name, sizeof(name), "uLights[%d].outerCutoff", i);
            loc = gl->GetUniformLocation(program, name);
            gl->Uniform1f(loc, lt[i].outer_cutoff);
        }
    }
    gl->Uniform1i(cache ? cache->texture_loc : gl->GetUniformLocation(program, "uTexture"), 0);
    gl->Uniform1i(cache ? cache->has_texture_loc : gl->GetUniformLocation(program, "uHasTexture"), has_texture ? 1 : 0);

    /* Shadow map uniforms. */
    if (program == ctx->lit_program && params->shadow_depth_data != NULL)
    {
        ctx->gl.ActiveTexture(GL_TEXTURE0 + 1);
        ctx->gl.BindTexture(GL_TEXTURE_2D, ctx->shadow_texture);
        ctx->gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 512, GL_RED, GL_FLOAT, params->shadow_depth_data);
        gl->Uniform1i(ctx->lit_shadow_map_loc, 1);
        gl->UniformMatrix4fv(ctx->lit_shadow_vp_loc, 1, GL_FALSE, params->shadow_vp);
        gl->Uniform1i(ctx->lit_shadow_enabled_loc, 1);
        gl->Uniform1f(ctx->lit_shadow_bias_loc, params->shadow_bias);
        ctx->gl.ActiveTexture(GL_TEXTURE0);
    }
    else if (program == ctx->lit_program)
    {
        gl->Uniform1i(ctx->lit_shadow_enabled_loc, 0);
    }

    gl->BindVertexArray(buffers->vao);

    gl->BindBuffer(GL_ARRAY_BUFFER, buffers->position_vbo);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 3 * sizeof(float)), positions, GL_DYNAMIC_DRAW);

    gl->BindBuffer(GL_ARRAY_BUFFER, buffers->normal_vbo);
    if (normals != NULL)
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 3 * sizeof(float)), normals,
                       GL_DYNAMIC_DRAW);
    }
    else
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 3 * sizeof(float)), NULL, GL_DYNAMIC_DRAW);
    }

    gl->BindBuffer(GL_ARRAY_BUFFER, buffers->uv_vbo);
    if (uvs != NULL)
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 2 * sizeof(float)), uvs, GL_DYNAMIC_DRAW);
    }
    else
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 2 * sizeof(float)), NULL, GL_DYNAMIC_DRAW);
    }

    gl->BindBuffer(GL_ARRAY_BUFFER, buffers->color_vbo);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 4 * sizeof(float)), color_data,
                   GL_DYNAMIC_DRAW);

    if (indices != NULL && index_count > 0)
    {
        gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers->index_ebo);
        gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)index_count * sizeof(unsigned int)), indices,
                       GL_DYNAMIC_DRAW);
        gl->DrawElements(GL_TRIANGLES, (GLsizei)index_count, GL_UNSIGNED_INT, NULL);
    }
    else
    {
        gl->DrawArrays(GL_TRIANGLES, 0, (GLsizei)vertex_count);
    }

    gl->BindVertexArray(0);
}

void sdl3d_gl_post_process(sdl3d_gl_context *ctx, int effects, float bloom_threshold, float bloom_intensity,
                           float vignette_intensity, float contrast, float brightness, float saturation)
{
    const sdl3d_gl_funcs *gl;
    GLuint copy_fbo;
    GLuint copy_tex;
    GLuint program;

    if (ctx == NULL || ctx->postprocess_program == 0 || effects == 0)
    {
        return;
    }

    if (ctx->disable_postprocess)
    {
        if (sdl3d_gl_debug_should_log(ctx))
        {
            SDL_Log("SDL3D GL frame %llu post-process skipped by %s", (unsigned long long)ctx->frame_index,
                    SDL3D_GL_DISABLE_POSTPROCESS_ENV);
        }
        return;
    }

    gl = &ctx->gl;
    program = ctx->postprocess_program;
    SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);
    sdl3d_gl_debug_log_pixel(ctx, GL_FRAMEBUFFER, ctx->fbo, ctx->logical_width, ctx->logical_height,
                             "before-postprocess");

    gl->GenTextures(1, &copy_tex);
    gl->BindTexture(GL_TEXTURE_2D, copy_tex);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ctx->logical_width, ctx->logical_height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   NULL);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl->GenFramebuffers(1, &copy_fbo);
    gl->BindFramebuffer(GL_FRAMEBUFFER, copy_fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, copy_tex, 0);

    gl->Disable(GL_DEPTH_TEST);
    gl->Disable(GL_CULL_FACE);

    /* Copy the scene texture with a shader pass instead of glBlitFramebuffer. */
    sdl3d_gl_copy_texture_to_bound_framebuffer(ctx, ctx->fbo_color_texture, ctx->logical_width, ctx->logical_height);

    /* Apply post-process from the copied scene back into the main FBO. */
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    gl->Viewport(0, 0, ctx->logical_width, ctx->logical_height);
    gl->UseProgram(program);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, copy_tex);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->Uniform1i(ctx->postprocess_scene_loc, 0);
    gl->Uniform2f(ctx->postprocess_texel_size_loc, 1.0f / (float)ctx->logical_width, 1.0f / (float)ctx->logical_height);
    gl->Uniform1i(ctx->postprocess_effects_loc, effects);
    gl->Uniform1f(ctx->postprocess_bloom_threshold_loc, bloom_threshold);
    gl->Uniform1f(ctx->postprocess_bloom_intensity_loc, bloom_intensity);
    gl->Uniform1f(ctx->postprocess_vignette_intensity_loc, vignette_intensity);
    gl->Uniform1f(ctx->postprocess_contrast_loc, contrast);
    gl->Uniform1f(ctx->postprocess_brightness_loc, brightness);
    gl->Uniform1f(ctx->postprocess_saturation_loc, saturation);

    gl->BindVertexArray(ctx->fullscreen_vao);
    sdl3d_gl_draw_fullscreen_triangle(gl);
    gl->BindVertexArray(0);

    gl->DeleteFramebuffers(1, &copy_fbo);
    gl->DeleteTextures(1, &copy_tex);
    gl->Enable(GL_CULL_FACE);
    gl->Enable(GL_DEPTH_TEST);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    gl->Viewport(0, 0, ctx->logical_width, ctx->logical_height);
    sdl3d_gl_debug_log_pixel(ctx, GL_FRAMEBUFFER, ctx->fbo, ctx->logical_width, ctx->logical_height,
                             "after-postprocess");
}
