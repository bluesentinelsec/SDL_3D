/*
 * Internal OpenGL backend interface. Not part of the public SDL3D API.
 */

#ifndef SDL3D_GL_BACKEND_H
#define SDL3D_GL_BACKEND_H

#include <SDL3/SDL.h>

/* GL types needed for the public interface. */
typedef unsigned int GLuint;

typedef struct sdl3d_gl_context sdl3d_gl_context;

sdl3d_gl_context *sdl3d_gl_create(SDL_Window *window, int width, int height);
void sdl3d_gl_destroy(sdl3d_gl_context *ctx);
void sdl3d_gl_clear(sdl3d_gl_context *ctx, float r, float g, float b, float a);

GLuint sdl3d_gl_get_unlit_program(const sdl3d_gl_context *ctx);
GLuint sdl3d_gl_get_lit_program(const sdl3d_gl_context *ctx);
GLuint sdl3d_gl_get_white_texture(const sdl3d_gl_context *ctx);

/* Get the shader program for a given shading mode. */
GLuint sdl3d_gl_get_program_for_profile(const sdl3d_gl_context *ctx, int shading_mode, bool has_lights);

/*
 * Draw a mesh using the unlit shader. Positions, UVs, and colors are
 * uploaded as a dynamic VBO each call (immediate-mode style, matching
 * the software renderer's per-frame draw pattern).
 */
void sdl3d_gl_draw_mesh_unlit(sdl3d_gl_context *ctx, const float *positions, const float *uvs, const float *colors,
                              const unsigned int *indices, int vertex_count, int index_count, GLuint texture,
                              const float *mvp, const float *tint);

void sdl3d_gl_draw_mesh_lit(sdl3d_gl_context *ctx, const float *positions, const float *normals, const float *uvs,
                            const float *colors, const unsigned int *indices, int vertex_count, int index_count,
                            GLuint texture, const float *mvp, const float *model_matrix, const float *normal_matrix,
                            const float *tint, const float *camera_pos, const float *ambient, float metallic,
                            float roughness, const float *emissive, const void *lights, int light_count,
                            int tonemap_mode, int fog_mode, const float *fog_color, float fog_start, float fog_end,
                            float fog_density);

#endif
