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
    const slayer3d_game_data_asset_warmup_queue *asset_warmup;
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

/* Variant that bakes the atlas at text_scale × the cache display scale. */
slayer3d_font *slayer3d_game_data_find_or_load_font_scaled(const slayer3d_game_data_runtime *runtime,
                                                           slayer3d_game_data_font_cache *cache, const char *font_id,
                                                           float text_scale);

slayer3d_font *slayer3d_game_data_font_cache_insert_prepared(slayer3d_game_data_font_cache *cache, const char *font_id,
                                                             slayer3d_font *font);

slayer3d_game_data_image_cache_entry *slayer3d_game_data_find_or_load_image_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_image_cache *cache, const char *image_id);

bool slayer3d_game_data_asset_warmup_request_ui_image_source(slayer3d_game_data_asset_warmup_queue *queue,
                                                             const char *source_path, const char *image_id);

bool slayer3d_game_data_prepare_direct_image_texture(slayer3d_asset_resolver *assets,
                                                     const slayer3d_game_data_image_asset *asset,
                                                     slayer3d_texture2d *out_texture);

bool slayer3d_game_data_prepare_sprite_backed_image_texture(const slayer3d_game_data_runtime *runtime,
                                                            const slayer3d_game_data_image_asset *asset,
                                                            slayer3d_texture2d *out_texture, const char **out_effect,
                                                            float *out_effect_delay, float *out_effect_duration,
                                                            char **out_shader_vertex_source,
                                                            char **out_shader_fragment_source);

slayer3d_game_data_image_cache_entry *slayer3d_game_data_image_cache_insert_prepared(
    slayer3d_game_data_image_cache *cache, const char *image_id, slayer3d_texture2d *texture, const char *effect,
    float effect_delay, float effect_duration, char **shader_vertex_source, char **shader_fragment_source,
    const char *source_path);

slayer3d_game_data_image_cache_entry *slayer3d_game_data_image_cache_insert_prepared_texture(
    slayer3d_game_data_image_cache *cache, const char *image_id, slayer3d_texture2d *texture, const char *source_path);

slayer3d_game_data_sprite_cache_entry *slayer3d_game_data_find_or_load_sprite_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_sprite_cache *cache, const char *sprite_id);

slayer3d_game_data_sprite_cache_entry *slayer3d_game_data_sprite_cache_insert_prepared(
    slayer3d_game_data_sprite_cache *cache, const char *sprite_id, slayer3d_sprite_asset_runtime *sprite);

slayer3d_game_data_model_cache_entry *slayer3d_game_data_find_or_load_model_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_model_cache *cache, const char *model_id);

slayer3d_game_data_model_cache_entry *slayer3d_game_data_model_cache_insert_prepared(
    slayer3d_game_data_model_cache *cache, const char *model_id, slayer3d_model *model);

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

bool slayer3d_game_data_primitive_asset_ready(const primitive_draw_context *context,
                                              slayer3d_game_data_asset_warmup_kind kind, const char *id);

bool slayer3d_game_data_mesh_primitive_cacheable(const slayer3d_game_data_render_primitive *primitive);

bool slayer3d_game_data_mesh_primitive_warmup_key(const slayer3d_game_data_render_primitive *primitive, char *buffer,
                                                  int buffer_size);

const slayer3d_mesh *slayer3d_game_data_find_or_build_mesh_primitive(
    slayer3d_game_data_mesh_primitive_cache *cache, const slayer3d_game_data_render_primitive *primitive);

const slayer3d_texture2d *slayer3d_game_data_primitive_texture(primitive_draw_context *context,
                                                               const slayer3d_game_data_render_primitive *primitive);

bool slayer3d_game_data_draw_ui_layered(const slayer3d_game_data_runtime *runtime, slayer3d_render_context *renderer,
                                        slayer3d_game_data_font_cache *font_cache,
                                        slayer3d_game_data_image_cache *image_cache,
                                        const slayer3d_game_data_asset_warmup_queue *asset_warmup,
                                        const slayer3d_game_data_ui_metrics *metrics,
                                        const slayer3d_game_data_render_eval *render_eval, float pulse_phase);

bool slayer3d_game_data_draw_mesh_primitive(primitive_draw_context *context,
                                            const slayer3d_game_data_render_primitive *primitive);

bool slayer3d_game_data_model_lod_should_cull(primitive_draw_context *context, const slayer3d_model *model,
                                              const slayer3d_game_data_render_primitive *primitive);

#endif
