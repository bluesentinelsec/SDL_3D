/**
 * @file logic.c
 * @brief Signal-to-action binding runtime implementation.
 */

#include "sdl3d/logic.h"

#include <SDL3/SDL_stdinc.h>

typedef struct logic_binding
{
    struct sdl3d_logic_world *owner;
    int id;
    int signal_id;
    int connection_id;
    sdl3d_action action;
    bool enabled;
    struct logic_binding *next;
} logic_binding;

struct sdl3d_logic_world
{
    sdl3d_signal_bus *bus;
    sdl3d_timer_pool *timers;
    logic_binding *bindings;
    int binding_count;
    int next_binding_id;
};

static logic_binding *find_binding(const sdl3d_logic_world *world, int binding_id)
{
    if (world == NULL || binding_id <= 0)
        return NULL;

    for (logic_binding *binding = world->bindings; binding != NULL; binding = binding->next)
    {
        if (binding->id == binding_id)
            return binding;
    }
    return NULL;
}

static void execute_binding(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    (void)payload;

    logic_binding *binding = (logic_binding *)userdata;
    if (binding == NULL || !binding->enabled || binding->signal_id != signal_id)
        return;

    /*
     * Current action types do not consume signal payloads. Payload-aware actions
     * can be added without changing the binding dispatch model.
     */
    sdl3d_logic_world *world = binding->owner;
    sdl3d_action_execute(&binding->action, world != NULL ? world->bus : NULL, world != NULL ? world->timers : NULL);
}

sdl3d_logic_world *sdl3d_logic_world_create(sdl3d_signal_bus *bus, sdl3d_timer_pool *timers)
{
    if (bus == NULL)
        return NULL;

    sdl3d_logic_world *world = (sdl3d_logic_world *)SDL_calloc(1, sizeof(sdl3d_logic_world));
    if (world == NULL)
        return NULL;

    world->bus = bus;
    world->timers = timers;
    world->next_binding_id = 1;
    return world;
}

void sdl3d_logic_world_destroy(sdl3d_logic_world *world)
{
    if (world == NULL)
        return;

    logic_binding *binding = world->bindings;
    while (binding != NULL)
    {
        logic_binding *next = binding->next;
        if (world->bus != NULL && binding->connection_id > 0)
            sdl3d_signal_disconnect(world->bus, binding->connection_id);
        SDL_free(binding);
        binding = next;
    }
    SDL_free(world);
}

int sdl3d_logic_world_bind_action(sdl3d_logic_world *world, int signal_id, const sdl3d_action *action)
{
    if (world == NULL || world->bus == NULL || action == NULL)
        return 0;

    logic_binding *binding = (logic_binding *)SDL_calloc(1, sizeof(logic_binding));
    if (binding == NULL)
        return 0;

    binding->id = world->next_binding_id++;
    binding->owner = world;
    binding->signal_id = signal_id;
    binding->action = *action;
    binding->enabled = true;

    binding->connection_id = sdl3d_signal_connect(world->bus, signal_id, execute_binding, binding);
    if (binding->connection_id == 0)
    {
        SDL_free(binding);
        return 0;
    }

    binding->next = world->bindings;
    world->bindings = binding;
    world->binding_count++;
    return binding->id;
}

void sdl3d_logic_world_unbind_action(sdl3d_logic_world *world, int binding_id)
{
    if (world == NULL || binding_id <= 0)
        return;

    logic_binding **cursor = &world->bindings;
    while (*cursor != NULL)
    {
        logic_binding *binding = *cursor;
        if (binding->id == binding_id)
        {
            *cursor = binding->next;
            if (world->bus != NULL && binding->connection_id > 0)
                sdl3d_signal_disconnect(world->bus, binding->connection_id);
            SDL_free(binding);
            world->binding_count--;
            return;
        }
        cursor = &binding->next;
    }
}

bool sdl3d_logic_world_set_binding_enabled(sdl3d_logic_world *world, int binding_id, bool enabled)
{
    logic_binding *binding = find_binding(world, binding_id);
    if (binding == NULL)
        return false;

    binding->enabled = enabled;
    return true;
}

bool sdl3d_logic_world_binding_enabled(const sdl3d_logic_world *world, int binding_id)
{
    const logic_binding *binding = find_binding(world, binding_id);
    return binding != NULL && binding->enabled;
}

int sdl3d_logic_world_binding_count(const sdl3d_logic_world *world)
{
    return world != NULL ? world->binding_count : 0;
}
