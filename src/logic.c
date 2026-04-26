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
    sdl3d_logic_action action;
    bool enabled;
    struct logic_binding *next;
} logic_binding;

typedef struct sector_alias
{
    int sector_index;
    char *name;
} sector_alias;

struct sdl3d_logic_world
{
    sdl3d_signal_bus *bus;
    sdl3d_timer_pool *timers;
    logic_binding *bindings;
    sdl3d_logic_target_context target_context;
    sector_alias *sector_aliases;
    int binding_count;
    int sector_alias_count;
    int sector_alias_capacity;
    int next_binding_id;
    bool has_target_context;
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

static void clear_resolved_target(sdl3d_logic_resolved_target *target)
{
    if (target != NULL)
        SDL_zerop(target);
}

static void clear_sector_aliases(sdl3d_logic_world *world)
{
    if (world == NULL)
        return;

    for (int i = 0; i < world->sector_alias_count; ++i)
        SDL_free(world->sector_aliases[i].name);
    SDL_free(world->sector_aliases);
    world->sector_aliases = NULL;
    world->sector_alias_count = 0;
    world->sector_alias_capacity = 0;
}

static bool ensure_sector_alias_capacity(sdl3d_logic_world *world)
{
    if (world->sector_alias_count < world->sector_alias_capacity)
        return true;

    const int new_capacity = world->sector_alias_capacity < 8 ? 8 : world->sector_alias_capacity * 2;
    sector_alias *aliases = (sector_alias *)SDL_realloc(world->sector_aliases, (size_t)new_capacity * sizeof(*aliases));
    if (aliases == NULL)
        return false;

    world->sector_aliases = aliases;
    world->sector_alias_capacity = new_capacity;
    return true;
}

static void remove_sector_alias_at(sdl3d_logic_world *world, int index)
{
    if (world == NULL || index < 0 || index >= world->sector_alias_count)
        return;

    SDL_free(world->sector_aliases[index].name);
    world->sector_alias_count--;
    if (index < world->sector_alias_count)
        world->sector_aliases[index] = world->sector_aliases[world->sector_alias_count];
}

static bool sector_index_is_valid(const sdl3d_logic_target_context *context, int sector_index)
{
    return context != NULL && context->level != NULL && context->sectors != NULL && sector_index >= 0 &&
           sector_index < context->sector_count && sector_index < context->level->sector_count;
}

static bool resolved_target_is_actor(const sdl3d_logic_resolved_target *target)
{
    return target != NULL &&
           (target->kind == SDL3D_LOGIC_TARGET_ACTOR_ID || target->kind == SDL3D_LOGIC_TARGET_ACTOR_NAME) &&
           target->actor != NULL;
}

static bool resolved_target_is_sector(const sdl3d_logic_resolved_target *target)
{
    return target != NULL &&
           (target->kind == SDL3D_LOGIC_TARGET_SECTOR_INDEX || target->kind == SDL3D_LOGIC_TARGET_SECTOR_NAME) &&
           target->sector.sector != NULL;
}

static float clamp_non_negative_float(float value)
{
    return value > 0.0f ? value : 0.0f;
}

static int clamp_non_negative_int(int value)
{
    return value > 0 ? value : 0;
}

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

static void execute_binding(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    (void)payload;

    logic_binding *binding = (logic_binding *)userdata;
    if (binding == NULL || !binding->enabled || binding->signal_id != signal_id)
        return;

    sdl3d_logic_world *world = binding->owner;
    sdl3d_logic_world_execute_action(world, &binding->action);
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
    clear_sector_aliases(world);
    SDL_free(world);
}

sdl3d_logic_target_ref sdl3d_logic_target_actor_id(int actor_id)
{
    sdl3d_logic_target_ref ref;
    SDL_zero(ref);
    ref.kind = SDL3D_LOGIC_TARGET_ACTOR_ID;
    ref.actor_id = actor_id;
    return ref;
}

sdl3d_logic_target_ref sdl3d_logic_target_actor_name(const char *actor_name)
{
    sdl3d_logic_target_ref ref;
    SDL_zero(ref);
    ref.kind = SDL3D_LOGIC_TARGET_ACTOR_NAME;
    ref.actor_name = actor_name;
    return ref;
}

sdl3d_logic_target_ref sdl3d_logic_target_sector_index(int sector_index)
{
    sdl3d_logic_target_ref ref;
    SDL_zero(ref);
    ref.kind = SDL3D_LOGIC_TARGET_SECTOR_INDEX;
    ref.sector_index = sector_index;
    return ref;
}

sdl3d_logic_target_ref sdl3d_logic_target_sector_name(const char *sector_name)
{
    sdl3d_logic_target_ref ref;
    SDL_zero(ref);
    ref.kind = SDL3D_LOGIC_TARGET_SECTOR_NAME;
    ref.sector_name = sector_name;
    return ref;
}

void sdl3d_logic_world_set_target_context(sdl3d_logic_world *world, const sdl3d_logic_target_context *context)
{
    if (world == NULL)
        return;

    if (context == NULL)
    {
        SDL_zero(world->target_context);
        world->has_target_context = false;
        return;
    }

    world->target_context = *context;
    world->has_target_context = true;
}

const sdl3d_logic_target_context *sdl3d_logic_world_get_target_context(const sdl3d_logic_world *world)
{
    return world != NULL && world->has_target_context ? &world->target_context : NULL;
}

bool sdl3d_logic_world_set_sector_name(sdl3d_logic_world *world, int sector_index, const char *name)
{
    if (world == NULL || sector_index < 0 || name == NULL || name[0] == '\0')
        return false;

    char *name_copy = SDL_strdup(name);
    if (name_copy == NULL)
        return false;

    for (int i = world->sector_alias_count - 1; i >= 0; --i)
    {
        if (world->sector_aliases[i].sector_index == sector_index ||
            SDL_strcmp(world->sector_aliases[i].name, name) == 0)
        {
            remove_sector_alias_at(world, i);
        }
    }

    if (!ensure_sector_alias_capacity(world))
    {
        SDL_free(name_copy);
        return false;
    }

    sector_alias *alias = &world->sector_aliases[world->sector_alias_count++];
    alias->sector_index = sector_index;
    alias->name = name_copy;
    return true;
}

void sdl3d_logic_world_clear_sector_names(sdl3d_logic_world *world)
{
    clear_sector_aliases(world);
}

int sdl3d_logic_world_find_sector_index(const sdl3d_logic_world *world, const char *name)
{
    if (world == NULL || name == NULL || name[0] == '\0')
        return -1;

    for (int i = 0; i < world->sector_alias_count; ++i)
    {
        if (SDL_strcmp(world->sector_aliases[i].name, name) == 0)
            return world->sector_aliases[i].sector_index;
    }
    return -1;
}

bool sdl3d_logic_world_resolve_target(const sdl3d_logic_world *world, const sdl3d_logic_target_ref *ref,
                                      sdl3d_logic_resolved_target *out_target)
{
    clear_resolved_target(out_target);

    const sdl3d_logic_target_context *context = sdl3d_logic_world_get_target_context(world);
    if (context == NULL || ref == NULL || out_target == NULL)
        return false;

    switch (ref->kind)
    {
    case SDL3D_LOGIC_TARGET_ACTOR_ID:
        if (context->registry == NULL || ref->actor_id <= 0)
            return false;
        out_target->actor = sdl3d_actor_registry_get(context->registry, ref->actor_id);
        if (out_target->actor == NULL)
        {
            clear_resolved_target(out_target);
            return false;
        }
        out_target->kind = SDL3D_LOGIC_TARGET_ACTOR_ID;
        return true;

    case SDL3D_LOGIC_TARGET_ACTOR_NAME:
        if (context->registry == NULL || ref->actor_name == NULL || ref->actor_name[0] == '\0')
            return false;
        out_target->actor = sdl3d_actor_registry_find(context->registry, ref->actor_name);
        if (out_target->actor == NULL)
        {
            clear_resolved_target(out_target);
            return false;
        }
        out_target->kind = SDL3D_LOGIC_TARGET_ACTOR_NAME;
        return true;

    case SDL3D_LOGIC_TARGET_SECTOR_INDEX:
        if (!sector_index_is_valid(context, ref->sector_index))
            return false;
        out_target->kind = SDL3D_LOGIC_TARGET_SECTOR_INDEX;
        out_target->sector.level = context->level;
        out_target->sector.sector = &context->sectors[ref->sector_index];
        out_target->sector.sector_index = ref->sector_index;
        return true;

    case SDL3D_LOGIC_TARGET_SECTOR_NAME: {
        const int sector_index = sdl3d_logic_world_find_sector_index(world, ref->sector_name);
        if (!sector_index_is_valid(context, sector_index))
            return false;
        out_target->kind = SDL3D_LOGIC_TARGET_SECTOR_NAME;
        out_target->sector.level = context->level;
        out_target->sector.sector = &context->sectors[sector_index];
        out_target->sector.sector_index = sector_index;
        return true;
    }

    case SDL3D_LOGIC_TARGET_NONE:
        break;
    }

    return false;
}

sdl3d_logic_action sdl3d_logic_action_make_core(sdl3d_action action)
{
    sdl3d_logic_action logic_action;
    SDL_zero(logic_action);
    logic_action.type = SDL3D_LOGIC_ACTION_CORE;
    logic_action.core = action;
    return logic_action;
}

sdl3d_logic_action sdl3d_logic_action_make_set_actor_active(sdl3d_logic_target_ref target, bool active)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_SET_ACTOR_ACTIVE;
    action.actor_active.target = target;
    action.actor_active.active = active;
    return action;
}

sdl3d_logic_action sdl3d_logic_action_make_toggle_actor_active(sdl3d_logic_target_ref target)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_TOGGLE_ACTOR_ACTIVE;
    action.actor_active.target = target;
    return action;
}

sdl3d_logic_action sdl3d_logic_action_make_set_actor_property(sdl3d_logic_target_ref target, const char *key,
                                                              sdl3d_value value)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_SET_ACTOR_PROPERTY;
    action.actor_property.target = target;
    action.actor_property.key = key;
    action.actor_property.value = value;
    return action;
}

sdl3d_logic_action sdl3d_logic_action_make_set_sector_push(sdl3d_logic_target_ref target, sdl3d_vec3 velocity)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_SET_SECTOR_PUSH;
    action.sector_push.target = target;
    action.sector_push.velocity = velocity;
    return action;
}

sdl3d_logic_action sdl3d_logic_action_make_set_sector_damage(sdl3d_logic_target_ref target, float damage_per_second)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_SET_SECTOR_DAMAGE;
    action.sector_damage.target = target;
    action.sector_damage.damage_per_second = damage_per_second;
    return action;
}

sdl3d_logic_action sdl3d_logic_action_make_set_sector_ambient(sdl3d_logic_target_ref target, int ambient_sound_id)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_SET_SECTOR_AMBIENT;
    action.sector_ambient.target = target;
    action.sector_ambient.ambient_sound_id = ambient_sound_id;
    return action;
}

bool sdl3d_logic_world_execute_action(sdl3d_logic_world *world, const sdl3d_logic_action *action)
{
    if (world == NULL || action == NULL)
        return false;

    sdl3d_logic_resolved_target target;
    switch (action->type)
    {
    case SDL3D_LOGIC_ACTION_CORE:
        sdl3d_action_execute(&action->core, world->bus, world->timers);
        return true;

    case SDL3D_LOGIC_ACTION_SET_ACTOR_ACTIVE:
        if (!sdl3d_logic_world_resolve_target(world, &action->actor_active.target, &target) ||
            !resolved_target_is_actor(&target))
        {
            return false;
        }
        target.actor->active = action->actor_active.active;
        return true;

    case SDL3D_LOGIC_ACTION_TOGGLE_ACTOR_ACTIVE:
        if (!sdl3d_logic_world_resolve_target(world, &action->actor_active.target, &target) ||
            !resolved_target_is_actor(&target))
        {
            return false;
        }
        target.actor->active = !target.actor->active;
        return true;

    case SDL3D_LOGIC_ACTION_SET_ACTOR_PROPERTY:
        if (action->actor_property.key == NULL ||
            !sdl3d_logic_world_resolve_target(world, &action->actor_property.target, &target) ||
            !resolved_target_is_actor(&target))
        {
            return false;
        }
        set_property_value(target.actor->props, action->actor_property.key, &action->actor_property.value);
        return true;

    case SDL3D_LOGIC_ACTION_SET_SECTOR_PUSH:
        if (!sdl3d_logic_world_resolve_target(world, &action->sector_push.target, &target) ||
            !resolved_target_is_sector(&target))
        {
            return false;
        }
        target.sector.sector->push_velocity[0] = action->sector_push.velocity.x;
        target.sector.sector->push_velocity[1] = action->sector_push.velocity.y;
        target.sector.sector->push_velocity[2] = action->sector_push.velocity.z;
        return true;

    case SDL3D_LOGIC_ACTION_SET_SECTOR_DAMAGE:
        if (!sdl3d_logic_world_resolve_target(world, &action->sector_damage.target, &target) ||
            !resolved_target_is_sector(&target))
        {
            return false;
        }
        target.sector.sector->damage_per_second = clamp_non_negative_float(action->sector_damage.damage_per_second);
        return true;

    case SDL3D_LOGIC_ACTION_SET_SECTOR_AMBIENT:
        if (!sdl3d_logic_world_resolve_target(world, &action->sector_ambient.target, &target) ||
            !resolved_target_is_sector(&target))
        {
            return false;
        }
        target.sector.sector->ambient_sound_id = clamp_non_negative_int(action->sector_ambient.ambient_sound_id);
        return true;

    case SDL3D_LOGIC_ACTION_NONE:
        break;
    }

    return false;
}

int sdl3d_logic_world_bind_action(sdl3d_logic_world *world, int signal_id, const sdl3d_action *action)
{
    if (action == NULL)
        return 0;

    sdl3d_logic_action logic_action = sdl3d_logic_action_make_core(*action);
    return sdl3d_logic_world_bind_logic_action(world, signal_id, &logic_action);
}

int sdl3d_logic_world_bind_logic_action(sdl3d_logic_world *world, int signal_id, const sdl3d_logic_action *action)
{
    if (world == NULL || world->bus == NULL || action == NULL || action->type == SDL3D_LOGIC_ACTION_NONE)
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
