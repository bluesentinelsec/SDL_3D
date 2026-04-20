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

    int width;
    int height;
};

/* ------------------------------------------------------------------ */
/* Shader compilation                                                  */
/* ------------------------------------------------------------------ */

static GLuint sdl3d_gl_compile_shader(sdl3d_gl_funcs *gl, GLenum type, const char *source)
{
    GLuint shader = gl->CreateShader(type);
    GLint status;
    gl->ShaderSource(shader, 1, &source, NULL);
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

static GLuint sdl3d_gl_link_program(sdl3d_gl_funcs *gl, const char *vert_src, const char *frag_src)
{
    GLuint vert = sdl3d_gl_compile_shader(gl, GL_VERTEX_SHADER, vert_src);
    GLuint frag = sdl3d_gl_compile_shader(gl, GL_FRAGMENT_SHADER, frag_src);
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
        SDL_free(ctx);
        return NULL;
    }

    SDL_GL_MakeCurrent(window, ctx->gl_context);

    if (!sdl3d_gl_load_funcs(&ctx->gl))
    {
        SDL_SetError("Failed to load OpenGL functions.");
        SDL_GL_DestroyContext(ctx->gl_context);
        SDL_free(ctx);
        return NULL;
    }

    /* Compile shaders. */
    ctx->unlit_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_unlit_vert, sdl3d_shader_unlit_frag);
    ctx->lit_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_lit_vert, sdl3d_shader_lit_frag);
    ctx->ps1_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_ps1_vert, sdl3d_shader_ps1_frag);
    ctx->n64_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_n64_vert, sdl3d_shader_n64_frag);
    ctx->dos_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_dos_vert, sdl3d_shader_dos_frag);
    ctx->snes_program = sdl3d_gl_link_program(&ctx->gl, sdl3d_shader_snes_vert, sdl3d_shader_snes_frag);

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
    SDL_GL_DestroyContext(ctx->gl_context);
    SDL_free(ctx);
}

void sdl3d_gl_clear(sdl3d_gl_context *ctx, float r, float g, float b, float a)
{
    if (ctx == NULL)
    {
        return;
    }
    ctx->gl.ClearColor(r, g, b, a);
    ctx->gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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

void sdl3d_gl_draw_mesh_unlit(sdl3d_gl_context *ctx, const float *positions, const float *uvs, const float *colors,
                              const unsigned int *indices, int vertex_count, int index_count, GLuint texture,
                              const float *mvp, const float *tint)
{
    GLuint vao, vbo_pos, vbo_uv, vbo_col, ebo;
    const sdl3d_gl_funcs *gl;

    if (ctx == NULL || positions == NULL || vertex_count <= 0)
    {
        return;
    }

    gl = &ctx->gl;
    gl->UseProgram(ctx->unlit_program);
    gl->UniformMatrix4fv(ctx->unlit_mvp_loc, 1, GL_FALSE, mvp);
    gl->Uniform4f(ctx->unlit_tint_loc, tint[0], tint[1], tint[2], tint[3]);

    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, texture ? texture : ctx->white_texture);
    gl->Uniform1i(ctx->unlit_texture_loc, 0);
    gl->Uniform1i(ctx->unlit_has_texture_loc, texture ? 1 : 0);

    gl->GenVertexArrays(1, &vao);
    gl->BindVertexArray(vao);

    /* Positions. */
    gl->GenBuffers(1, &vbo_pos);
    gl->BindBuffer(GL_ARRAY_BUFFER, vbo_pos);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 3 * sizeof(float)), positions, GL_DYNAMIC_DRAW);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    /* UVs. */
    gl->GenBuffers(1, &vbo_uv);
    gl->BindBuffer(GL_ARRAY_BUFFER, vbo_uv);
    if (uvs != NULL)
    {
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vertex_count * 2 * sizeof(float)), uvs, GL_DYNAMIC_DRAW);
    }
    else
    {
        float zero[2] = {0, 0};
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(2 * sizeof(float)), zero, GL_DYNAMIC_DRAW);
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

    gl->BindVertexArray(0);
    gl->DeleteBuffers(1, &vbo_pos);
    gl->DeleteBuffers(1, &vbo_uv);
    gl->DeleteBuffers(1, &vbo_col);
    gl->DeleteVertexArrays(1, &vao);
}

void sdl3d_gl_draw_mesh_lit(sdl3d_gl_context *ctx, const float *positions, const float *normals, const float *uvs,
                            const float *colors, const unsigned int *indices, int vertex_count, int index_count,
                            GLuint texture, const float *mvp, const float *model_matrix, const float *normal_matrix,
                            const float *tint, const float *camera_pos, const float *ambient, float metallic,
                            float roughness, const float *emissive, const void *lights, int light_count,
                            int tonemap_mode, int fog_mode, const float *fog_color, float fog_start, float fog_end,
                            float fog_density)
{
    GLuint vao, vbo_pos, vbo_norm, vbo_uv, vbo_col, ebo;
    const sdl3d_gl_funcs *gl;

    if (ctx == NULL || positions == NULL || vertex_count <= 0)
    {
        return;
    }

    gl = &ctx->gl;
    gl->UseProgram(ctx->lit_program);
    gl->UniformMatrix4fv(ctx->lit_mvp_loc, 1, GL_FALSE, mvp);
    gl->UniformMatrix4fv(ctx->lit_model_loc, 1, GL_FALSE, model_matrix);
    /* Normal matrix is 3x3 — pass as 3 vec3 uniforms or use mat3 uniform. */
    /* For now, pass the upper-left 3x3 of the model matrix. */
    {
        float nm[9] = {normal_matrix[0], normal_matrix[1], normal_matrix[2], normal_matrix[3], normal_matrix[4],
                       normal_matrix[5], normal_matrix[6], normal_matrix[7], normal_matrix[8]};
        GLint loc = ctx->lit_normal_matrix_loc;
        /* glUniformMatrix3fv — we need to add this to our function table. For now skip. */
        (void)nm;
        (void)loc;
    }
    gl->Uniform4f(ctx->lit_tint_loc, tint[0], tint[1], tint[2], tint[3]);
    gl->Uniform3f(ctx->lit_camera_pos_loc, camera_pos[0], camera_pos[1], camera_pos[2]);
    gl->Uniform3f(ctx->lit_ambient_loc, ambient[0], ambient[1], ambient[2]);
    gl->Uniform1f(ctx->lit_metallic_loc, metallic);
    gl->Uniform1f(ctx->lit_roughness_loc, roughness);
    gl->Uniform3f(ctx->lit_emissive_loc, emissive[0], emissive[1], emissive[2]);
    gl->Uniform1i(ctx->lit_light_count_loc, light_count);
    gl->Uniform1i(ctx->lit_tonemap_mode_loc, tonemap_mode);
    gl->Uniform1i(ctx->lit_fog_mode_loc, fog_mode);
    if (fog_color != NULL)
    {
        gl->Uniform3f(ctx->lit_fog_color_loc, fog_color[0], fog_color[1], fog_color[2]);
    }
    gl->Uniform1f(ctx->lit_fog_start_loc, fog_start);
    gl->Uniform1f(ctx->lit_fog_end_loc, fog_end);
    gl->Uniform1f(ctx->lit_fog_density_loc, fog_density);

    /* Upload light uniforms. */
    if (lights != NULL && light_count > 0)
    {
        const sdl3d_light *lt = (const sdl3d_light *)lights;
        char name[64];
        for (int i = 0; i < light_count && i < 8; ++i)
        {
            GLint loc;
            SDL_snprintf(name, sizeof(name), "uLights[%d].type", i);
            loc = gl->GetUniformLocation(ctx->lit_program, name);
            gl->Uniform1i(loc, (int)lt[i].type);
            SDL_snprintf(name, sizeof(name), "uLights[%d].position", i);
            loc = gl->GetUniformLocation(ctx->lit_program, name);
            gl->Uniform3f(loc, lt[i].position.x, lt[i].position.y, lt[i].position.z);
            SDL_snprintf(name, sizeof(name), "uLights[%d].direction", i);
            loc = gl->GetUniformLocation(ctx->lit_program, name);
            gl->Uniform3f(loc, lt[i].direction.x, lt[i].direction.y, lt[i].direction.z);
            SDL_snprintf(name, sizeof(name), "uLights[%d].color", i);
            loc = gl->GetUniformLocation(ctx->lit_program, name);
            gl->Uniform3f(loc, lt[i].color[0], lt[i].color[1], lt[i].color[2]);
            SDL_snprintf(name, sizeof(name), "uLights[%d].intensity", i);
            loc = gl->GetUniformLocation(ctx->lit_program, name);
            gl->Uniform1f(loc, lt[i].intensity);
            SDL_snprintf(name, sizeof(name), "uLights[%d].range", i);
            loc = gl->GetUniformLocation(ctx->lit_program, name);
            gl->Uniform1f(loc, lt[i].range);
            SDL_snprintf(name, sizeof(name), "uLights[%d].innerCutoff", i);
            loc = gl->GetUniformLocation(ctx->lit_program, name);
            gl->Uniform1f(loc, lt[i].inner_cutoff);
            SDL_snprintf(name, sizeof(name), "uLights[%d].outerCutoff", i);
            loc = gl->GetUniformLocation(ctx->lit_program, name);
            gl->Uniform1f(loc, lt[i].outer_cutoff);
        }
    }

    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, texture ? texture : ctx->white_texture);
    gl->Uniform1i(ctx->lit_texture_loc, 0);
    gl->Uniform1i(ctx->lit_has_texture_loc, texture ? 1 : 0);

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
        float zero[3] = {0, 1, 0};
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(3 * sizeof(float)), zero, GL_DYNAMIC_DRAW);
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
        float zero[2] = {0, 0};
        gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(2 * sizeof(float)), zero, GL_DYNAMIC_DRAW);
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
