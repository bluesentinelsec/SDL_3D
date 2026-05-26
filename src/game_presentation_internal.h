#ifndef SLAYER3D_GAME_PRESENTATION_INTERNAL_H
#define SLAYER3D_GAME_PRESENTATION_INTERNAL_H

#include "slayer3d/game_presentation.h"

bool slayer3d_game_data_draw_particles_filtered(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_render_context *renderer,
                                                slayer3d_game_data_particle_cache *cache, bool draw_world_space,
                                                bool draw_view_space);

#endif
