#ifndef SLAYER3D_GAME_DATA_BRUSH_INTERNAL_H
#define SLAYER3D_GAME_DATA_BRUSH_INTERNAL_H

#include <stdbool.h>

#include "slayer3d/game_data.h"

typedef bool (*slayer3d_game_data_brush_trace_fn)(void *userdata, const slayer3d_game_data_brush_trace_desc *desc,
                                                  slayer3d_game_data_brush_trace_result *out_result);

slayer3d_game_data_brush_trace_result slayer3d_game_data_brush_trace_default_result(
    const slayer3d_game_data_brush_trace_desc *desc);

bool slayer3d_game_data_brush_trace_shape_valid(const slayer3d_game_data_brush_trace_desc *desc);

bool slayer3d_game_data_brush_world_trace_local(const slayer3d_game_data_brush_world *world,
                                                const slayer3d_game_data_brush_trace_desc *desc,
                                                slayer3d_game_data_brush_trace_result *out_result);

bool slayer3d_game_data_brush_slide_with_trace(slayer3d_game_data_brush_trace_fn trace_fn, void *trace_userdata,
                                               const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                               slayer3d_game_data_brush_trace_result *out_result);

#endif
