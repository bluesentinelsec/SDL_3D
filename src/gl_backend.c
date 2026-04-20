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

struct sdl3d_gl_context
{
    SDL_GLContext gl_context;
    sdl3d_gl_funcs gl;

    /* Shader programs. */
    GLuint unlit_program;
    GLuint lit_program;

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
