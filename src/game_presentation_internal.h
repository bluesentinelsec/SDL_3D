#ifndef SLAYER3D_GAME_PRESENTATION_INTERNAL_H
#define SLAYER3D_GAME_PRESENTATION_INTERNAL_H

#include "slayer3d/game_presentation.h"

bool slayer3d_game_data_ensure_mesh_primitive_cache_capacity(slayer3d_game_data_mesh_primitive_cache *cache,
                                                             int required);

slayer3d_font *slayer3d_game_data_find_or_load_font(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_font_cache *cache, const char *font_id);

slayer3d_game_data_image_cache_entry *slayer3d_game_data_find_or_load_image_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_image_cache *cache, const char *image_id);

slayer3d_game_data_sprite_cache_entry *slayer3d_game_data_find_or_load_sprite_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_sprite_cache *cache, const char *sprite_id);

slayer3d_game_data_model_cache_entry *slayer3d_game_data_find_or_load_model_entry(
    const slayer3d_game_data_runtime *runtime, slayer3d_game_data_model_cache *cache, const char *model_id);

bool slayer3d_game_data_draw_particles_filtered(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_render_context *renderer,
                                                slayer3d_game_data_particle_cache *cache, bool draw_world_space,
                                                bool draw_view_space);

#endif
