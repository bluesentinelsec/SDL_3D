#ifndef SLAYER3D_GAME_DATA_BRUSH_SOURCE_MODEL_INTERNAL_H
#define SLAYER3D_GAME_DATA_BRUSH_SOURCE_MODEL_INTERNAL_H

#include "game_data_internal.h"

extern const char *const editor_brush_source_box_face_keys[6];

int editor_brush_source_units_from_meters(const brush_world_runtime *world_runtime, float value);
float editor_brush_source_meters_from_units(const brush_world_runtime *world_runtime, int value);
bool editor_brush_source_contents_are_structural(unsigned int contents);

#endif
