/**
 * @file actor_registry.c
 * @brief Actor registry implementation — flat dynamic array of registered actors.
 */

#include "sdl3d/actor_registry.h"

#include <SDL3/SDL_stdinc.h>

#include "sdl3d/math.h"

struct sdl3d_actor_registry
{
    sdl3d_registered_actor *actors;
    int count;
    int capacity;
    int next_id;
};

/* ================================================================== */
/* Internal helpers                                                   */
/* ================================================================== */

static bool ensure_capacity(sdl3d_actor_registry *reg)
{
    if (reg->count < reg->capacity)
        return true;
    int new_cap = reg->capacity < 8 ? 8 : reg->capacity * 2;
    sdl3d_registered_actor *buf =
        (sdl3d_registered_actor *)SDL_realloc(reg->actors, (size_t)new_cap * sizeof(sdl3d_registered_actor));
    if (buf == NULL)
        return false;
    reg->actors = buf;
    reg->capacity = new_cap;
    return true;
}

static void free_actor(sdl3d_registered_actor *a)
{
    sdl3d_properties_destroy(a->props);
    a->props = NULL;
    SDL_free((void *)a->name);
    a->name = NULL;
}

/* ================================================================== */
/* Lifecycle                                                          */
/* ================================================================== */

sdl3d_actor_registry *sdl3d_actor_registry_create(void)
{
    sdl3d_actor_registry *reg = (sdl3d_actor_registry *)SDL_calloc(1, sizeof(sdl3d_actor_registry));
    if (reg != NULL)
        reg->next_id = 1;
    return reg;
}

void sdl3d_actor_registry_destroy(sdl3d_actor_registry *reg)
{
    if (reg == NULL)
        return;
    for (int i = 0; i < reg->count; ++i)
        free_actor(&reg->actors[i]);
    SDL_free(reg->actors);
    SDL_free(reg);
}

/* ================================================================== */
/* Actor management                                                   */
/* ================================================================== */

sdl3d_registered_actor *sdl3d_actor_registry_add(sdl3d_actor_registry *reg, const char *name)
{
    if (reg == NULL || name == NULL)
        return NULL;
    if (!ensure_capacity(reg))
        return NULL;

    sdl3d_properties *props = sdl3d_properties_create();
    if (props == NULL)
        return NULL;

    char *name_copy = SDL_strdup(name);
    if (name_copy == NULL)
    {
        sdl3d_properties_destroy(props);
        return NULL;
    }

    sdl3d_registered_actor *a = &reg->actors[reg->count];
    SDL_zerop(a);
    a->id = reg->next_id++;
    a->name = name_copy;
    a->props = props;
    a->position = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    a->sector_id = -1;
    a->active = true;
    reg->count++;
    return a;
}

void sdl3d_actor_registry_remove(sdl3d_actor_registry *reg, int actor_id)
{
    if (reg == NULL || actor_id <= 0)
        return;
    for (int i = 0; i < reg->count; ++i)
    {
        if (reg->actors[i].id == actor_id)
        {
            free_actor(&reg->actors[i]);
            reg->count--;
            if (i < reg->count)
                reg->actors[i] = reg->actors[reg->count];
            return;
        }
    }
}

sdl3d_registered_actor *sdl3d_actor_registry_find(const sdl3d_actor_registry *reg, const char *name)
{
    if (reg == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < reg->count; ++i)
    {
        if (reg->actors[i].name != NULL && SDL_strcmp(reg->actors[i].name, name) == 0)
            return &reg->actors[i];
    }
    return NULL;
}

sdl3d_registered_actor *sdl3d_actor_registry_get(const sdl3d_actor_registry *reg, int actor_id)
{
    if (reg == NULL || actor_id <= 0)
        return NULL;
    for (int i = 0; i < reg->count; ++i)
    {
        if (reg->actors[i].id == actor_id)
            return &reg->actors[i];
    }
    return NULL;
}

int sdl3d_actor_registry_count(const sdl3d_actor_registry *reg)
{
    if (reg == NULL)
        return 0;
    return reg->count;
}

/* ================================================================== */
/* Per-frame update                                                   */
/* ================================================================== */

void sdl3d_actor_registry_update(sdl3d_actor_registry *reg, sdl3d_signal_bus *bus, sdl3d_vec3 test_point)
{
    if (reg == NULL || bus == NULL)
        return;

    for (int i = 0; i < reg->count; ++i)
    {
        sdl3d_registered_actor *a = &reg->actors[i];
        if (!a->active)
            continue;

        for (int t = 0; t < a->trigger_count; ++t)
        {
            sdl3d_trigger *tr = &a->triggers[t];
            switch (tr->type)
            {
            case SDL3D_TRIGGER_SPATIAL:
                sdl3d_trigger_test_spatial(tr, test_point);
                break;
            case SDL3D_TRIGGER_PROPERTY:
                sdl3d_trigger_test_property(tr);
                break;
            case SDL3D_TRIGGER_SIGNAL:
                /* Signal triggers are activated externally via
                 * sdl3d_trigger_activate_signal. Nothing to test here. */
                break;
            }
            sdl3d_trigger_evaluate(tr, bus);
        }
    }
}
