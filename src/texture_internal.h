#ifndef SLAYER3D_TEXTURE_INTERNAL_H
#define SLAYER3D_TEXTURE_INTERNAL_H

#include "slayer3d/asset.h"
#include "slayer3d/texture.h"

typedef struct slayer3d_texture_cache_entry
{
    char *path;
    slayer3d_texture2d texture;
    struct slayer3d_texture_cache_entry *next;
} slayer3d_texture_cache_entry;

/* Reserve the next process-unique texture generation value. All texture
 * content/parameter changes must stamp generations from this counter so
 * renderer caches keyed by texture address plus generation never alias
 * recycled texture storage. */
Uint32 slayer3d_texture_next_generation(void);

void slayer3d_texture_sample_rgba(const slayer3d_texture2d *texture, float u, float v, float lod, float *out_r,
                                  float *out_g, float *out_b, float *out_a);
void slayer3d_texture_cache_destroy(slayer3d_texture_cache_entry *cache);
bool slayer3d_texture_cache_get_or_load(slayer3d_texture_cache_entry **cache, const char *source_path,
                                        const char *texture_path, const slayer3d_texture2d **out_texture);
bool slayer3d_texture_cache_get_or_load_asset(slayer3d_texture_cache_entry **cache,
                                              const slayer3d_asset_resolver *assets, const char *source_path,
                                              const char *texture_path, const slayer3d_texture2d **out_texture);
bool slayer3d_texture_cache_prepare_asset(const slayer3d_asset_resolver *assets, const char *source_path,
                                          const char *texture_path, char **out_resolved_path,
                                          slayer3d_texture2d *out_texture);
bool slayer3d_texture_cache_insert_prepared(slayer3d_texture_cache_entry **cache, char *resolved_path,
                                            slayer3d_texture2d *texture, const slayer3d_texture2d **out_texture);

#endif
