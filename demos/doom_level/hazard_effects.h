/* Hazard particle effects for the doom_level demo. */
#ifndef DOOM_HAZARD_EFFECTS_H
#define DOOM_HAZARD_EFFECTS_H

#include "sdl3d/effects.h"
#include "sdl3d/level.h"
#include "sdl3d/render_context.h"
#include "sdl3d/texture.h"

#include <stdbool.h>

typedef struct doom_hazard_particles
{
    sdl3d_particle_emitter *nukage_vapor;
    sdl3d_particle_emitter *nukage_motes;
    sdl3d_texture2d soft_particle_tex;
    bool has_soft_particle_tex;
} doom_hazard_particles;

bool doom_hazard_particles_init(doom_hazard_particles *particles, const sdl3d_level *level,
                                const sdl3d_sector *sectors);
void doom_hazard_particles_free(doom_hazard_particles *particles);
void doom_hazard_particles_update(doom_hazard_particles *particles, float dt);
bool doom_hazard_particles_draw(const doom_hazard_particles *particles, sdl3d_render_context *context);

#endif
