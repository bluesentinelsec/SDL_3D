#ifndef SLAYER3D_GAME_DATA_ANIMATION_TYPES_H
#define SLAYER3D_GAME_DATA_ANIMATION_TYPES_H

#include "slayer3d/game_data.h"
#include "slayer3d/properties.h"

typedef enum game_data_tween_target
{
    GAME_DATA_TWEEN_UI,
    GAME_DATA_TWEEN_PROPERTY,
} game_data_tween_target;

typedef enum game_data_tween_value_type
{
    GAME_DATA_TWEEN_FLOAT,
    GAME_DATA_TWEEN_VEC3,
    GAME_DATA_TWEEN_COLOR,
} game_data_tween_value_type;

typedef enum game_data_tween_easing
{
    GAME_DATA_TWEEN_LINEAR,
    GAME_DATA_TWEEN_IN_QUAD,
    GAME_DATA_TWEEN_OUT_QUAD,
    GAME_DATA_TWEEN_IN_OUT_QUAD,
} game_data_tween_easing;

typedef enum game_data_tween_repeat
{
    GAME_DATA_TWEEN_REPEAT_NONE,
    GAME_DATA_TWEEN_REPEAT_LOOP,
    GAME_DATA_TWEEN_REPEAT_PING_PONG,
} game_data_tween_repeat;

typedef struct game_data_tween_value
{
    game_data_tween_value_type type;
    union {
        float as_float;
        slayer3d_vec3 as_vec3;
        slayer3d_color as_color;
    };
} game_data_tween_value;

typedef struct game_data_animation
{
    game_data_tween_target target_type;
    const char *target;
    const char *property;
    const char *key;
    const char *scene;
    float duration;
    float elapsed;
    game_data_tween_easing easing;
    game_data_tween_repeat repeat;
    game_data_tween_value from;
    game_data_tween_value to;
    slayer3d_value_type property_type;
    int done_signal_id;
} game_data_animation;

#endif
