/**
 * @file action.c
 * @brief Action execution — data-driven effects for gameplay mechanics.
 */

#include "sdl3d/action.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/timer_pool.h"

/* ================================================================== */
/* Property setters by value type                                     */
/* ================================================================== */

static void set_property_value(sdl3d_properties *target, const char *key, const sdl3d_value *value)
{
    if (target == NULL || key == NULL || value == NULL)
        return;

    switch (value->type)
    {
    case SDL3D_VALUE_INT:
        sdl3d_properties_set_int(target, key, value->as_int);
        break;
    case SDL3D_VALUE_FLOAT:
        sdl3d_properties_set_float(target, key, value->as_float);
        break;
    case SDL3D_VALUE_BOOL:
        sdl3d_properties_set_bool(target, key, value->as_bool);
        break;
    case SDL3D_VALUE_VEC3:
        sdl3d_properties_set_vec3(target, key, value->as_vec3);
        break;
    case SDL3D_VALUE_STRING:
        sdl3d_properties_set_string(target, key, value->as_string);
        break;
    case SDL3D_VALUE_COLOR:
        sdl3d_properties_set_color(target, key, value->as_color);
        break;
    }
}

/* ================================================================== */
/* Execute                                                            */
/* ================================================================== */

void sdl3d_action_execute(const sdl3d_action *action, sdl3d_signal_bus *bus, sdl3d_timer_pool *timer_pool)
{
    if (action == NULL)
        return;

    switch (action->type)
    {
    case SDL3D_ACTION_SET_PROPERTY:
        set_property_value(action->set_property.target, action->set_property.key, &action->set_property.value);
        break;

    case SDL3D_ACTION_EMIT_SIGNAL:
        if (bus != NULL)
            sdl3d_signal_emit(bus, action->emit_signal.signal_id, NULL);
        break;

    case SDL3D_ACTION_START_TIMER:
        if (timer_pool != NULL)
            sdl3d_timer_start(timer_pool, action->start_timer.delay, action->start_timer.signal_id,
                              action->start_timer.repeating, action->start_timer.interval);
        break;

    case SDL3D_ACTION_LOG:
        if (action->log.message != NULL)
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[action] %s", action->log.message);
        break;
    }
}

/* ================================================================== */
/* Convenience constructors                                          */
/* ================================================================== */

sdl3d_action sdl3d_action_make_set_bool(sdl3d_properties *target, const char *key, bool value)
{
    sdl3d_action a;
    SDL_zero(a);
    a.type = SDL3D_ACTION_SET_PROPERTY;
    a.set_property.target = target;
    a.set_property.key = key;
    a.set_property.value.type = SDL3D_VALUE_BOOL;
    a.set_property.value.as_bool = value;
    return a;
}

sdl3d_action sdl3d_action_make_set_float(sdl3d_properties *target, const char *key, float value)
{
    sdl3d_action a;
    SDL_zero(a);
    a.type = SDL3D_ACTION_SET_PROPERTY;
    a.set_property.target = target;
    a.set_property.key = key;
    a.set_property.value.type = SDL3D_VALUE_FLOAT;
    a.set_property.value.as_float = value;
    return a;
}

sdl3d_action sdl3d_action_make_set_int(sdl3d_properties *target, const char *key, int value)
{
    sdl3d_action a;
    SDL_zero(a);
    a.type = SDL3D_ACTION_SET_PROPERTY;
    a.set_property.target = target;
    a.set_property.key = key;
    a.set_property.value.type = SDL3D_VALUE_INT;
    a.set_property.value.as_int = value;
    return a;
}

sdl3d_action sdl3d_action_make_emit_signal(int signal_id)
{
    sdl3d_action a;
    SDL_zero(a);
    a.type = SDL3D_ACTION_EMIT_SIGNAL;
    a.emit_signal.signal_id = signal_id;
    return a;
}

sdl3d_action sdl3d_action_make_log(const char *message)
{
    sdl3d_action a;
    SDL_zero(a);
    a.type = SDL3D_ACTION_LOG;
    a.log.message = message;
    return a;
}
