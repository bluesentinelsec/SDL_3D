#ifndef SLAYER3D_GL_RENDERER_PROGRAMS_H
#define SLAYER3D_GL_RENDERER_PROGRAMS_H

#include <stdbool.h>

#include "gl_funcs.h"

GLuint slayer3d_gl_compile_shader(slayer3d_gl_funcs *gl, GLenum type, const char *version, const char *body);
GLuint slayer3d_gl_compile_shader_multi(slayer3d_gl_funcs *gl, GLenum type, int count, const char **srcs);
GLuint slayer3d_gl_link_program(slayer3d_gl_funcs *gl, GLuint vert, GLuint frag);
GLuint slayer3d_gl_build_program(slayer3d_gl_funcs *gl, const char *version, const char *vert_body,
                                 const char *frag_body);
bool slayer3d_gl_shader_source_has_version_prefix(const char *source);
GLuint slayer3d_gl_compile_shader_source(slayer3d_gl_funcs *gl, GLenum type, const char *version, const char *source);
GLuint slayer3d_gl_build_program_from_sources(slayer3d_gl_funcs *gl, const char *version,
                                              const char *default_vert_source, const char *vertex_source,
                                              const char *fragment_source);

#endif /* SLAYER3D_GL_RENDERER_PROGRAMS_H */
