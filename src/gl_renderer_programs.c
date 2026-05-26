/**
 * @file gl_renderer_programs.c
 * @brief OpenGL shader and program compilation helpers.
 */

#include "gl_renderer_programs.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

GLuint slayer3d_gl_compile_shader(slayer3d_gl_funcs *gl, GLenum type, const char *version, const char *body)
{
    GLuint s = gl->CreateShader(type);
    const char *srcs[2] = {version, body};
    gl->ShaderSource(s, 2, srcs, NULL);
    gl->CompileShader(s);

    GLint ok = 0;
    gl->GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char buf[1024];
        gl->GetShaderInfoLog(s, sizeof(buf), NULL, buf);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL shader compile error: %s", buf);
        gl->DeleteShader(s);
        return 0;
    }
    return s;
}

GLuint slayer3d_gl_compile_shader_multi(slayer3d_gl_funcs *gl, GLenum type, int count, const char **srcs)
{
    GLuint s = gl->CreateShader(type);
    gl->ShaderSource(s, count, srcs, NULL);
    gl->CompileShader(s);

    GLint ok = 0;
    gl->GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char buf[1024];
        gl->GetShaderInfoLog(s, sizeof(buf), NULL, buf);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL shader compile error: %s", buf);
        gl->DeleteShader(s);
        return 0;
    }
    return s;
}

GLuint slayer3d_gl_link_program(slayer3d_gl_funcs *gl, GLuint vert, GLuint frag)
{
    GLuint p = gl->CreateProgram();
    gl->AttachShader(p, vert);
    gl->AttachShader(p, frag);
    gl->LinkProgram(p);

    GLint ok = 0;
    gl->GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char buf[1024];
        gl->GetProgramInfoLog(p, sizeof(buf), NULL, buf);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL program link error: %s", buf);
        gl->DeleteProgram(p);
        return 0;
    }
    return p;
}

GLuint slayer3d_gl_build_program(slayer3d_gl_funcs *gl, const char *version, const char *vert_body,
                                 const char *frag_body)
{
    GLuint vs = slayer3d_gl_compile_shader(gl, GL_VERTEX_SHADER, version, vert_body);
    if (!vs)
        return 0;
    GLuint fs = slayer3d_gl_compile_shader(gl, GL_FRAGMENT_SHADER, version, frag_body);
    if (!fs)
    {
        gl->DeleteShader(vs);
        return 0;
    }
    GLuint prog = slayer3d_gl_link_program(gl, vs, fs);
    gl->DeleteShader(vs);
    gl->DeleteShader(fs);
    return prog;
}

bool slayer3d_gl_shader_source_has_version_prefix(const char *source)
{
    if (source == NULL)
        return false;
    while (*source != '\0' && (*source == ' ' || *source == '\t' || *source == '\r' || *source == '\n'))
        ++source;
    return SDL_strncmp(source, "#version", 8) == 0;
}

GLuint slayer3d_gl_compile_shader_source(slayer3d_gl_funcs *gl, GLenum type, const char *version, const char *source)
{
    GLuint shader;
    const char *srcs[2];
    int count = 0;

    if (source == NULL || source[0] == '\0')
        return 0;

    shader = gl->CreateShader(type);
    if (!shader)
        return 0;

    if (slayer3d_gl_shader_source_has_version_prefix(source))
    {
        srcs[0] = source;
        count = 1;
    }
    else if (version != NULL)
    {
        srcs[0] = version;
        srcs[1] = source;
        count = 2;
    }
    else
    {
        srcs[0] = source;
        count = 1;
    }

    gl->ShaderSource(shader, count, srcs, NULL);
    gl->CompileShader(shader);

    GLint compiled = 0;
    gl->GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        char buf[1024];
        GLsizei len = 0;
        gl->GetShaderInfoLog(shader, (GLsizei)sizeof(buf), &len, buf);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL shader compile error: %s", buf);
        gl->DeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint slayer3d_gl_build_program_from_sources(slayer3d_gl_funcs *gl, const char *version,
                                              const char *default_vert_source, const char *vertex_source,
                                              const char *fragment_source)
{
    GLuint vs = slayer3d_gl_compile_shader_source(
        gl, GL_VERTEX_SHADER, version,
        (vertex_source != NULL && vertex_source[0] != '\0') ? vertex_source : default_vert_source);
    if (!vs)
        return 0;
    GLuint fs = slayer3d_gl_compile_shader_source(gl, GL_FRAGMENT_SHADER, version, fragment_source);
    if (!fs)
    {
        gl->DeleteShader(vs);
        return 0;
    }
    GLuint prog = slayer3d_gl_link_program(gl, vs, fs);
    gl->DeleteShader(vs);
    gl->DeleteShader(fs);
    return prog;
}
