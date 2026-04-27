/* Doom-level door setup, interaction, collision, and drawing. */
#ifndef DOOM_DOORS_H
#define DOOM_DOORS_H

#include <stdbool.h>

#include "sdl3d/door.h"
#include "sdl3d/fps_mover.h"
#include "sdl3d/logic.h"
#include "sdl3d/render_context.h"
#include "sdl3d/texture.h"

#define DOOM_DOOR_COUNT 3

typedef struct doom_doors
{
    sdl3d_door doors[DOOM_DOOR_COUNT];
    sdl3d_texture2d texture;
    bool has_texture;
} doom_doors;

bool doom_doors_init(doom_doors *doors);
void doom_doors_free(doom_doors *doors);
void doom_doors_update(doom_doors *doors, float dt);
bool doom_doors_emit_interact(doom_doors *doors, sdl3d_logic_world *logic, int signal_id, sdl3d_vec3 eye_position,
                              float yaw);
bool doom_doors_apply_command(doom_doors *doors, const char *door_name, int door_id, sdl3d_logic_door_command command,
                              float auto_close_seconds);
bool doom_doors_resolve_player(const doom_doors *doors, sdl3d_fps_mover *mover);
void doom_doors_draw(const doom_doors *doors, sdl3d_render_context *ctx);

#endif
