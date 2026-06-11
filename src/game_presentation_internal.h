#ifndef SLAYER3D_GAME_PRESENTATION_INTERNAL_H
#define SLAYER3D_GAME_PRESENTATION_INTERNAL_H

#include "slayer3d/game_presentation.h"

typedef struct primitive_draw_context
{
    const slayer3d_game_data_runtime *runtime;
    slayer3d_render_context *renderer;
    slayer3d_game_data_image_cache *image_cache;
    slayer3d_game_data_sprite_cache *sprite_cache;
    slayer3d_game_data_model_cache *model_cache;
    slayer3d_game_data_mesh_primitive_cache *mesh_primitive_cache;
    const slayer3d_camera3d *camera;
    const slayer3d_game_data_render_eval *eval;
    slayer3d_game_data_render_settings render_settings;
    slayer3d_game_data_render_primitive sphere_batch;
    slayer3d_vec3 *sphere_batch_positions;
    int sphere_batch_count;
    int sphere_batch_capacity;
    bool sphere_batch_active;
    bool draw_world_space;
    bool draw_view_space;
} primitive_draw_context;

bool slayer3d_game_data_ensure_mesh_primitive_cache_capacity(slayer3d_game_data_mesh_primitive_cache *cache,
                                                             int required);

slayer3d_font *slayer3d_game_data_find_or_load_font(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_font_cache *cache, const char *font_id);

slayer3d_game_data_image_cache_entry *slayer3d_game_data_find_or_load_image_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_image_cache *cache, const char *image_id);

bool slayer3d_game_data_prepare_direct_image_texture(slayer3d_asset_resolver *assets,
                                                     const slayer3d_game_data_image_asset *asset,
                                                     slayer3d_texture2d *out_texture);

slayer3d_game_data_image_cache_entry *slayer3d_game_data_image_cache_insert_prepared_texture(
    slayer3d_game_data_image_cache *cache, const char *image_id, slayer3d_texture2d *texture);

slayer3d_game_data_sprite_cache_entry *slayer3d_game_data_find_or_load_sprite_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_sprite_cache *cache, const char *sprite_id);

slayer3d_game_data_model_cache_entry *slayer3d_game_data_find_or_load_model_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_model_cache *cache, const char *model_id);

bool slayer3d_game_data_draw_particles_filtered(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_render_context *renderer,
                                                slayer3d_game_data_particle_cache *cache, bool draw_world_space,
                                                bool draw_view_space);

bool slayer3d_game_data_draw_sphere_batch(slayer3d_render_context *renderer,
                                          const slayer3d_game_data_render_primitive *primitive);

bool slayer3d_game_data_primitive_sphere_can_batch(const slayer3d_game_data_render_primitive *primitive);

void slayer3d_game_data_apply_primitive_lod(primitive_draw_context *context,
                                            slayer3d_game_data_render_primitive *primitive);

bool slayer3d_game_data_flush_sphere_draw_batch(primitive_draw_context *context);

bool slayer3d_game_data_append_sphere_draw_batch(primitive_draw_context *context,
                                                 const slayer3d_game_data_render_primitive *primitive);

const slayer3d_texture2d *slayer3d_game_data_primitive_texture(primitive_draw_context *context,
                                                               const slayer3d_game_data_render_primitive *primitive);

bool slayer3d_game_data_draw_mesh_primitive(primitive_draw_context *context,
                                            const slayer3d_game_data_render_primitive *primitive);

bool slayer3d_game_data_model_lod_should_cull(primitive_draw_context *context, const slayer3d_model *model,
                                              const slayer3d_game_data_render_primitive *primitive);

#endif
