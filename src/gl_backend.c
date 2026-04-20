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

struct sdl3d_gl_context
{
    SDL_GLContext gl_context;
    sdl3d_gl_funcs gl;
    bool is_es; /* true if running OpenGL ES */

    /* Shader programs. */
    GLuint unlit_program;
    GLuint lit_program;
    GLuint ps1_program;
    GLuint n64_program;
    GLuint dos_program;
    GLuint snes_program;

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

    /* Default white texture for untextured meshes. */
    GLuint white_texture;

    /* Offscreen FBO for logical-resolution rendering. */
    GLuint fbo;
    GLuint fbo_color_texture;
    GLuint fbo_depth_rbo;
    int logical_width;
    int logical_height;

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

    ctx = (sdl3d_gl_context *)SDL_calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

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

    if (ctx->unlit_program == 0 || ctx->lit_program == 0)
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

    /* Create default white texture. */
    ctx->gl.GenTextures(1, &ctx->white_texture);
    ctx->gl.BindTexture(GL_TEXTURE_2D, ctx->white_texture);
    ctx->gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_pixel);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ctx->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    /* Default GL state. */
    ctx->gl.Enable(GL_DEPTH_TEST);
    ctx->gl.DepthFunc(GL_LEQUAL);
    ctx->gl.Enable(GL_CULL_FACE);
    ctx->gl.CullFace(GL_BACK);

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
    if (ctx->white_texture)
    {
        ctx->gl.DeleteTextures(1, &ctx->white_texture);
    }
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
    /* Ensure we're rendering to the offscreen FBO. */
    ctx->gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    ctx->gl.ClearColor(r, g, b, a);
    ctx->gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void sdl3d_gl_present(sdl3d_gl_context *ctx, SDL_Window *window)
{
    int window_w = 0;
    int window_h = 0;

    if (ctx == NULL || window == NULL)
    {
        return;
    }

    SDL_GetWindowSizeInPixels(window, &window_w, &window_h);

    /* Blit the logical-resolution FBO to the default framebuffer. */
    ctx->gl.BindFramebuffer(GL_READ_FRAMEBUFFER, ctx->fbo);
    ctx->gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    ctx->gl.BlitFramebuffer(0, 0, ctx->logical_width, ctx->logical_height, 0, 0, window_w, window_h,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);

    /* Re-bind the offscreen FBO for the next frame. */
    ctx->gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
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
    /* Map shading mode to shader program:
     * FLAT → SNES shader (flat, no per-vertex lighting)
     * GOURAUD → N64 shader (per-vertex lighting)
     * PHONG → lit shader (per-fragment PBR)
     * UNLIT → unlit shader */
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
    GLuint vao, vbo_pos, vbo_uv, vbo_col, ebo;
    const sdl3d_gl_funcs *gl;
    static int draw_count = 0;
    const float *positions = params->positions;
    const float *uvs = params->uvs;
    const float *colors = params->colors;
    const unsigned int *indices = params->indices;
    int vertex_count = params->vertex_count;
    int index_count = params->index_count;
    const float *mvp = params->mvp;
    const float *tint = params->tint;
    GLuint texture = 0; /* texture upload handled by caller */

    if (ctx == NULL || positions == NULL || vertex_count <= 0)
    {
        return;
    }

    gl = &ctx->gl;

    /* Log first few draw calls for debugging. */
    if (draw_count < 3)
    {
        SDL_Log("GL draw_mesh_unlit: verts=%d, indices=%d, program=%u, tint=(%.2f,%.2f,%.2f,%.2f)", vertex_count,
                index_count, ctx->unlit_program, tint[0], tint[1], tint[2], tint[3]);
        SDL_Log("  MVP[0..3]: %.3f %.3f %.3f %.3f", mvp[0], mvp[1], mvp[2], mvp[3]);
        SDL_Log("  pos[0..2]: %.3f %.3f %.3f", positions[0], positions[1], positions[2]);
    }

    gl->UseProgram(ctx->unlit_program);
    if (draw_count < 1)
    {
        GLenum e = gl->GetError();
        if (e)
            SDL_Log("  err after UseProgram: 0x%04X", (unsigned)e);
    }
    gl->UniformMatrix4fv(ctx->unlit_mvp_loc, 1, GL_FALSE, mvp);
    if (draw_count < 1)
    {
        GLenum e = gl->GetError();
        if (e)
            SDL_Log("  err after UniformMatrix4fv: 0x%04X", (unsigned)e);
    }
    gl->Uniform4f(ctx->unlit_tint_loc, tint[0], tint[1], tint[2], tint[3]);

    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, texture ? texture : ctx->white_texture);
    gl->Uniform1i(ctx->unlit_texture_loc, 0);
    gl->Uniform1i(ctx->unlit_has_texture_loc, texture ? 1 : 0);
    if (draw_count < 1)
    {
        GLenum e = gl->GetError();
        if (e)
            SDL_Log("  err after texture setup: 0x%04X", (unsigned)e);
    }

    gl->GenVertexArrays(1, &vao);
    gl->BindVertexArray(vao);
    if (draw_count < 1)
    {
        GLenum e = gl->GetError();
        if (e)
            SDL_Log("  err after VAO: 0x%04X", (unsigned)e);
    }

    /* Positions. */
    gl->GenBuffers(1, &vbo_pos);
    gl->BindBuffer(GL_ARRAY_BUFFER, vbo_pos);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 3 * sizeof(float)), positions, GL_DYNAMIC_DRAW);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    if (draw_count < 1)
    {
        GLenum e = gl->GetError();
        if (e)
            SDL_Log("  err after pos VBO: 0x%04X", (unsigned)e);
    }

    /* UVs. */
    gl->GenBuffers(1, &vbo_uv);
    gl->BindBuffer(GL_ARRAY_BUFFER, vbo_uv);
    if (uvs != NULL)
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 2 * sizeof(float)), uvs, GL_DYNAMIC_DRAW);
    }
    else
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 2 * sizeof(float)), NULL, GL_DYNAMIC_DRAW);
    }
    gl->EnableVertexAttribArray(1);
    gl->VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    /* Colors. */
    gl->GenBuffers(1, &vbo_col);
    gl->BindBuffer(GL_ARRAY_BUFFER, vbo_col);
    if (colors != NULL)
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 4 * sizeof(float)), colors,
                       GL_DYNAMIC_DRAW);
    }
    else
    {
        /* Default white. */
        float *white = (float *)SDL_calloc((size_t)vertex_count * 4, sizeof(float));
        if (white != NULL)
        {
            for (int i = 0; i < vertex_count; ++i)
            {
                white[i * 4 + 0] = 1.0f;
                white[i * 4 + 1] = 1.0f;
                white[i * 4 + 2] = 1.0f;
                white[i * 4 + 3] = 1.0f;
            }
            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 4 * sizeof(float)), white,
                           GL_DYNAMIC_DRAW);
            SDL_free(white);
        }
    }
    gl->EnableVertexAttribArray(2);
    gl->VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, NULL);

    /* Indices + draw. */
    if (indices != NULL && index_count > 0)
    {
        gl->GenBuffers(1, &ebo);
        gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)index_count * sizeof(unsigned int)), indices,
                       GL_DYNAMIC_DRAW);
        gl->DrawElements(GL_TRIANGLES, (GLsizei)index_count, GL_UNSIGNED_INT, NULL);
        gl->DeleteBuffers(1, &ebo);
    }
    else
    {
        gl->DrawArrays(GL_TRIANGLES, 0, (GLsizei)vertex_count);
    }

    if (draw_count < 3)
    {
        GLenum err = gl->GetError();
        SDL_Log("  GL draw result: err=0x%04X", (unsigned)err);
        draw_count++;
    }
    gl->BindVertexArray(0);
    gl->DeleteBuffers(1, &vbo_pos);
    gl->DeleteBuffers(1, &vbo_uv);
    gl->DeleteBuffers(1, &vbo_col);
    gl->DeleteVertexArrays(1, &vao);
}

void sdl3d_gl_draw_mesh_lit(sdl3d_gl_context *ctx, const sdl3d_draw_params_lit *params)
{
    GLuint vao, vbo_pos, vbo_norm, vbo_uv, vbo_col, ebo;
    const sdl3d_gl_funcs *gl;
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
    GLuint texture = 0; /* texture upload handled by caller */

    if (ctx == NULL || positions == NULL || vertex_count <= 0)
    {
        return;
    }

    gl = &ctx->gl;
    program = sdl3d_gl_get_program_for_profile(ctx, params->shading_mode, light_count > 0);
    gl->UseProgram(program);

    /* Set uniforms — query locations dynamically since each profile
     * shader has a different uniform set.  Unused uniforms return -1
     * and the gl->Uniform* calls silently ignore location -1. */
    gl->UniformMatrix4fv(gl->GetUniformLocation(program, "uMVP"), 1, GL_FALSE, mvp);
    gl->UniformMatrix4fv(gl->GetUniformLocation(program, "uModel"), 1, GL_FALSE, model_matrix);
    {
        float nm[9] = {normal_matrix[0], normal_matrix[1], normal_matrix[2], normal_matrix[3], normal_matrix[4],
                       normal_matrix[5], normal_matrix[6], normal_matrix[7], normal_matrix[8]};
        GLint loc = gl->GetUniformLocation(program, "uNormalMatrix");
        (void)nm;
        (void)loc;
    }
    gl->Uniform4f(gl->GetUniformLocation(program, "uTint"), tint[0], tint[1], tint[2], tint[3]);
    gl->Uniform3f(gl->GetUniformLocation(program, "uCameraPos"), camera_pos[0], camera_pos[1], camera_pos[2]);
    gl->Uniform3f(gl->GetUniformLocation(program, "uAmbient"), ambient[0], ambient[1], ambient[2]);
    gl->Uniform1f(gl->GetUniformLocation(program, "uMetallic"), metallic);
    gl->Uniform1f(gl->GetUniformLocation(program, "uRoughness"), roughness);
    gl->Uniform3f(gl->GetUniformLocation(program, "uEmissive"), emissive[0], emissive[1], emissive[2]);
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

    /* Upload light uniforms. */
    if (lights != NULL && light_count > 0)
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

    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, texture ? texture : ctx->white_texture);
    gl->Uniform1i(gl->GetUniformLocation(program, "uTexture"), 0);
    gl->Uniform1i(gl->GetUniformLocation(program, "uHasTexture"), texture ? 1 : 0);

    gl->GenVertexArrays(1, &vao);
    gl->BindVertexArray(vao);

    gl->GenBuffers(1, &vbo_pos);
    gl->BindBuffer(GL_ARRAY_BUFFER, vbo_pos);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 3 * sizeof(float)), positions, GL_DYNAMIC_DRAW);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &vbo_norm);
    gl->BindBuffer(GL_ARRAY_BUFFER, vbo_norm);
    if (normals != NULL)
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 3 * sizeof(float)), normals,
                       GL_DYNAMIC_DRAW);
    }
    else
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 3 * sizeof(float)), NULL, GL_DYNAMIC_DRAW);
    }
    gl->EnableVertexAttribArray(1);
    gl->VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &vbo_uv);
    gl->BindBuffer(GL_ARRAY_BUFFER, vbo_uv);
    if (uvs != NULL)
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 2 * sizeof(float)), uvs, GL_DYNAMIC_DRAW);
    }
    else
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 2 * sizeof(float)), NULL, GL_DYNAMIC_DRAW);
    }
    gl->EnableVertexAttribArray(2);
    gl->VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    gl->GenBuffers(1, &vbo_col);
    gl->BindBuffer(GL_ARRAY_BUFFER, vbo_col);
    if (colors != NULL)
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 4 * sizeof(float)), colors,
                       GL_DYNAMIC_DRAW);
    }
    else
    {
        float *white = (float *)SDL_calloc((size_t)vertex_count * 4, sizeof(float));
        if (white != NULL)
        {
            for (int i = 0; i < vertex_count; ++i)
            {
                white[i * 4] = white[i * 4 + 1] = white[i * 4 + 2] = white[i * 4 + 3] = 1.0f;
            }
            gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 4 * sizeof(float)), white,
                           GL_DYNAMIC_DRAW);
            SDL_free(white);
        }
    }
    gl->EnableVertexAttribArray(3);
    gl->VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, NULL);

    if (indices != NULL && index_count > 0)
    {
        gl->GenBuffers(1, &ebo);
        gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)index_count * sizeof(unsigned int)), indices,
                       GL_DYNAMIC_DRAW);
        gl->DrawElements(GL_TRIANGLES, (GLsizei)index_count, GL_UNSIGNED_INT, NULL);
        gl->DeleteBuffers(1, &ebo);
    }
    else
    {
        gl->DrawArrays(GL_TRIANGLES, 0, (GLsizei)vertex_count);
    }

    gl->BindVertexArray(0);
    gl->DeleteBuffers(1, &vbo_pos);
    gl->DeleteBuffers(1, &vbo_norm);
    gl->DeleteBuffers(1, &vbo_uv);
    gl->DeleteBuffers(1, &vbo_col);
    gl->DeleteVertexArrays(1, &vao);
}
