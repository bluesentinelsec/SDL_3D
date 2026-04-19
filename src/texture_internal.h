#ifndef SDL3D_TEXTURE_INTERNAL_H
#define SDL3D_TEXTURE_INTERNAL_H

#include "sdl3d/texture.h"

typedef struct sdl3d_texture_cache_entry
{
    char *path;
    sdl3d_texture2d texture;
    struct sdl3d_texture_cache_entry *next;
} sdl3d_texture_cache_entry;

void sdl3d_texture_sample_rgba(const sdl3d_texture2d *texture, float u, float v, float lod, float *out_r, float *out_g,
                               float *out_b, float *out_a);
void sdl3d_texture_cache_destroy(sdl3d_texture_cache_entry *cache);
bool sdl3d_texture_cache_get_or_load(sdl3d_texture_cache_entry **cache, const char *source_path,
                                     const char *texture_path, const sdl3d_texture2d **out_texture);

#endif
