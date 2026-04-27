/* Per-frame rendering: visibility, level, entities, HUD. */
#ifndef DOOM_RENDERER_H
#define DOOM_RENDERER_H

#include "sdl3d/font.h"
#include "sdl3d/render_context.h"
#include "sdl3d/ui.h"

#include "doom_doors.h"
#include "entities.h"
#include "hazard_effects.h"
#include "level_data.h"
#include "player.h"
#include "surveillance.h"

#include <stdbool.h>

typedef struct render_state
{
    bool portal_culling;
    bool show_debug;
    bool *sector_visible;
    int sector_visible_capacity;
    sdl3d_visibility_result vis;
} render_state;

void render_state_init(render_state *rs);
void render_state_free(render_state *rs);

/* Draw one complete frame. Presentation is owned by the managed game loop. */
void render_draw_frame(render_state *rs, sdl3d_render_context *ctx, const sdl3d_font *font, sdl3d_ui_context *ui,
                       level_data *ld, entities *ent, const doom_hazard_particles *hazards, const doom_doors *doors,
                       const doom_surveillance_camera *surveillance, const player_state *player, int backbuffer_w,
                       int backbuffer_h, float dt, const char *render_profile_name, bool ambient_feedback_active,
                       bool teleport_feedback_active, bool launcher_feedback_active);

#endif
