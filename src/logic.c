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

typedef enum logic_entity_binding_kind
{
    LOGIC_ENTITY_BINDING_RELAY,
    LOGIC_ENTITY_BINDING_TOGGLE,
    LOGIC_ENTITY_BINDING_COUNTER,
    LOGIC_ENTITY_BINDING_BRANCH,
    LOGIC_ENTITY_BINDING_RANDOM,
    LOGIC_ENTITY_BINDING_SEQUENCE,
    LOGIC_ENTITY_BINDING_ONCE,
} logic_entity_binding_kind;

typedef struct logic_entity_binding
{
    struct sdl3d_logic_world *owner;
    int id;
    int signal_id;
    int connection_id;
    logic_entity_binding_kind kind;
    void *entity;
    bool enabled;
    struct logic_entity_binding *next;
} logic_entity_binding;

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
    logic_entity_binding *entity_bindings;
    sdl3d_logic_target_context target_context;
    sdl3d_logic_game_adapters game_adapters;
    sector_alias *sector_aliases;
    int binding_count;
    int entity_binding_count;
    int sector_alias_count;
    int sector_alias_capacity;
    int next_binding_id;
    bool has_target_context;
    bool has_game_adapters;
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

static logic_entity_binding *find_entity_binding(const sdl3d_logic_world *world, int binding_id)
{
    if (world == NULL || binding_id <= 0)
        return NULL;

    for (logic_entity_binding *binding = world->entity_bindings; binding != NULL; binding = binding->next)
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

static sdl3d_logic_sensor_result sensor_result(bool active, sdl3d_logic_sensor_event event)
{
    sdl3d_logic_sensor_result result;
    SDL_zero(result);
    result.active = active;
    result.event = event;
    result.emitted = event != SDL3D_LOGIC_SENSOR_EVENT_NONE;
    return result;
}

static sdl3d_logic_entity_result entity_result(bool emitted, int signal_id, int output_index)
{
    sdl3d_logic_entity_result result;
    SDL_zero(result);
    result.emitted = emitted;
    result.signal_id = emitted ? signal_id : 0;
    result.output_index = emitted ? output_index : -1;
    return result;
}

static sdl3d_logic_sensor_event sensor_event_for_state(sdl3d_trigger_edge edge, bool was_active, bool active)
{
    switch (edge)
    {
    case SDL3D_TRIGGER_EDGE_ENTER:
        return active && !was_active ? SDL3D_LOGIC_SENSOR_EVENT_ENTER : SDL3D_LOGIC_SENSOR_EVENT_NONE;
    case SDL3D_TRIGGER_EDGE_EXIT:
        return !active && was_active ? SDL3D_LOGIC_SENSOR_EVENT_EXIT : SDL3D_LOGIC_SENSOR_EVENT_NONE;
    case SDL3D_TRIGGER_EDGE_BOTH:
        if (active && !was_active)
            return SDL3D_LOGIC_SENSOR_EVENT_ENTER;
        if (!active && was_active)
            return SDL3D_LOGIC_SENSOR_EVENT_EXIT;
        return SDL3D_LOGIC_SENSOR_EVENT_NONE;
    case SDL3D_TRIGGER_LEVEL:
        return active ? SDL3D_LOGIC_SENSOR_EVENT_LEVEL : SDL3D_LOGIC_SENSOR_EVENT_NONE;
    }
    return SDL3D_LOGIC_SENSOR_EVENT_NONE;
}

static bool point_inside_bounds(sdl3d_bounding_box bounds, sdl3d_vec3 point)
{
    return point.x >= bounds.min.x && point.x <= bounds.max.x && point.y >= bounds.min.y && point.y <= bounds.max.y &&
           point.z >= bounds.min.z && point.z <= bounds.max.z;
}

static bool emit_sensor_payload(sdl3d_logic_world *world, int signal_id, int sensor_id, sdl3d_logic_sensor_event event,
                                bool active, sdl3d_vec3 sample_position, int sector_index,
                                const sdl3d_registered_actor *actor, float distance)
{
    if (world == NULL || world->bus == NULL || event == SDL3D_LOGIC_SENSOR_EVENT_NONE)
        return false;

    sdl3d_properties *payload = sdl3d_properties_create();
    if (payload == NULL)
        return false;

    sdl3d_properties_set_int(payload, "sensor_id", sensor_id);
    sdl3d_properties_set_int(payload, "event", (int)event);
    sdl3d_properties_set_bool(payload, "inside", active);
    sdl3d_properties_set_vec3(payload, "sample_position", sample_position);
    if (sector_index >= 0)
        sdl3d_properties_set_int(payload, "sector_index", sector_index);
    if (actor != NULL)
    {
        sdl3d_properties_set_int(payload, "actor_id", actor->id);
        if (actor->name != NULL)
            sdl3d_properties_set_string(payload, "actor_name", actor->name);
        sdl3d_properties_set_float(payload, "distance", distance);
    }

    sdl3d_signal_emit(world->bus, signal_id, payload);
    sdl3d_properties_destroy(payload);
    return true;
}

static sdl3d_properties *create_entity_payload(int entity_id, const char *entity_type)
{
    sdl3d_properties *payload = sdl3d_properties_create();
    if (payload == NULL)
        return NULL;

    sdl3d_properties_set_int(payload, "entity_id", entity_id);
    sdl3d_properties_set_string(payload, "entity_type", entity_type != NULL ? entity_type : "");
    return payload;
}

static bool value_equals(const sdl3d_value *left, const sdl3d_value *right)
{
    if (left == NULL || right == NULL || left->type != right->type)
        return false;

    switch (left->type)
    {
    case SDL3D_VALUE_INT:
        return left->as_int == right->as_int;
    case SDL3D_VALUE_FLOAT:
        return left->as_float == right->as_float;
    case SDL3D_VALUE_BOOL:
        return left->as_bool == right->as_bool;
    case SDL3D_VALUE_VEC3:
        return left->as_vec3.x == right->as_vec3.x && left->as_vec3.y == right->as_vec3.y &&
               left->as_vec3.z == right->as_vec3.z;
    case SDL3D_VALUE_STRING:
        return SDL_strcmp(left->as_string != NULL ? left->as_string : "",
                          right->as_string != NULL ? right->as_string : "") == 0;
    case SDL3D_VALUE_COLOR:
        return left->as_color.r == right->as_color.r && left->as_color.g == right->as_color.g &&
               left->as_color.b == right->as_color.b && left->as_color.a == right->as_color.a;
    }

    return false;
}

static unsigned int normalized_seed(unsigned int seed)
{
    return seed != 0 ? seed : 1u;
}

static unsigned int next_random_state(unsigned int state)
{
    return state * 1664525u + 1013904223u;
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
    logic_binding *binding = (logic_binding *)userdata;
    if (binding == NULL || !binding->enabled || binding->signal_id != signal_id)
        return;

    sdl3d_logic_world *world = binding->owner;
    sdl3d_logic_world_execute_action_with_payload(world, &binding->action, payload);
}

static void execute_entity_binding(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    logic_entity_binding *binding = (logic_entity_binding *)userdata;
    if (binding == NULL || !binding->enabled || binding->signal_id != signal_id || binding->entity == NULL)
        return;

    sdl3d_logic_world *world = binding->owner;
    switch (binding->kind)
    {
    case LOGIC_ENTITY_BINDING_RELAY:
        sdl3d_logic_relay_activate(world, (sdl3d_logic_relay *)binding->entity, payload);
        break;
    case LOGIC_ENTITY_BINDING_TOGGLE:
        sdl3d_logic_toggle_activate(world, (sdl3d_logic_toggle *)binding->entity);
        break;
    case LOGIC_ENTITY_BINDING_COUNTER:
        sdl3d_logic_counter_activate(world, (sdl3d_logic_counter *)binding->entity);
        break;
    case LOGIC_ENTITY_BINDING_BRANCH:
        sdl3d_logic_branch_activate_with_payload(world, (sdl3d_logic_branch *)binding->entity, payload);
        break;
    case LOGIC_ENTITY_BINDING_RANDOM:
        sdl3d_logic_random_activate(world, (sdl3d_logic_random *)binding->entity);
        break;
    case LOGIC_ENTITY_BINDING_SEQUENCE:
        sdl3d_logic_sequence_activate(world, (sdl3d_logic_sequence *)binding->entity);
        break;
    case LOGIC_ENTITY_BINDING_ONCE:
        sdl3d_logic_once_activate(world, (sdl3d_logic_once *)binding->entity);
        break;
    }
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

    logic_entity_binding *entity_binding = world->entity_bindings;
    while (entity_binding != NULL)
    {
        logic_entity_binding *next = entity_binding->next;
        if (world->bus != NULL && entity_binding->connection_id > 0)
            sdl3d_signal_disconnect(world->bus, entity_binding->connection_id);
        SDL_free(entity_binding);
        entity_binding = next;
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

void sdl3d_logic_world_set_game_adapters(sdl3d_logic_world *world, const sdl3d_logic_game_adapters *adapters)
{
    if (world == NULL)
        return;

    if (adapters == NULL)
    {
        SDL_zero(world->game_adapters);
        world->has_game_adapters = false;
        return;
    }

    world->game_adapters = *adapters;
    world->has_game_adapters = true;
}

const sdl3d_logic_game_adapters *sdl3d_logic_world_get_game_adapters(const sdl3d_logic_world *world)
{
    return world != NULL && world->has_game_adapters ? &world->game_adapters : NULL;
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

sdl3d_logic_action sdl3d_logic_action_make_teleport_player(sdl3d_teleport_destination destination)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_TELEPORT_PLAYER;
    action.teleport_player.destination = destination;
    action.teleport_player.use_signal_payload = false;
    return action;
}

sdl3d_logic_action sdl3d_logic_action_make_teleport_player_from_payload(void)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_TELEPORT_PLAYER;
    action.teleport_player.use_signal_payload = true;
    return action;
}

sdl3d_logic_action sdl3d_logic_action_make_set_active_camera(const char *camera_name, const sdl3d_camera3d *camera)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_SET_ACTIVE_CAMERA;
    action.camera.camera_name = camera_name;
    if (camera != NULL)
    {
        action.camera.camera = *camera;
    }
    return action;
}

sdl3d_logic_action sdl3d_logic_action_make_restore_camera(void)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_RESTORE_CAMERA;
    return action;
}

sdl3d_logic_action sdl3d_logic_action_make_set_ambient(int ambient_id, float fade_seconds)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_SET_AMBIENT;
    action.ambient.ambient_id = ambient_id;
    action.ambient.fade_seconds = fade_seconds;
    action.ambient.use_signal_payload = false;
    return action;
}

sdl3d_logic_action sdl3d_logic_action_make_set_ambient_from_payload(const char *payload_key, float fade_seconds)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_SET_AMBIENT;
    action.ambient.payload_key = payload_key;
    action.ambient.fade_seconds = fade_seconds;
    action.ambient.use_signal_payload = true;
    return action;
}

sdl3d_logic_action sdl3d_logic_action_make_trigger_feedback(const char *feedback_name, float duration_seconds)
{
    sdl3d_logic_action action;
    SDL_zero(action);
    action.type = SDL3D_LOGIC_ACTION_TRIGGER_FEEDBACK;
    action.feedback.feedback_name = feedback_name;
    action.feedback.duration_seconds = duration_seconds;
    return action;
}

bool sdl3d_logic_world_execute_action(sdl3d_logic_world *world, const sdl3d_logic_action *action)
{
    return sdl3d_logic_world_execute_action_with_payload(world, action, NULL);
}

bool sdl3d_logic_world_execute_action_with_payload(sdl3d_logic_world *world, const sdl3d_logic_action *action,
                                                   const sdl3d_properties *payload)
{
    if (world == NULL || action == NULL)
        return false;

    sdl3d_logic_resolved_target target;
    const sdl3d_logic_game_adapters *adapters = sdl3d_logic_world_get_game_adapters(world);
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

    case SDL3D_LOGIC_ACTION_TELEPORT_PLAYER: {
        if (adapters == NULL || adapters->teleport_player == NULL)
        {
            return false;
        }

        sdl3d_teleport_destination destination = action->teleport_player.destination;
        if (action->teleport_player.use_signal_payload &&
            !sdl3d_teleport_destination_from_payload(payload, &destination))
        {
            return false;
        }

        return adapters->teleport_player(adapters->userdata, &destination, payload);
    }

    case SDL3D_LOGIC_ACTION_SET_ACTIVE_CAMERA:
        if (adapters == NULL || adapters->set_active_camera == NULL)
        {
            return false;
        }
        return adapters->set_active_camera(adapters->userdata, action->camera.camera_name, &action->camera.camera,
                                           payload);

    case SDL3D_LOGIC_ACTION_RESTORE_CAMERA:
        if (adapters == NULL || adapters->restore_camera == NULL)
        {
            return false;
        }
        return adapters->restore_camera(adapters->userdata, payload);

    case SDL3D_LOGIC_ACTION_SET_AMBIENT: {
        if (adapters == NULL || adapters->set_ambient == NULL)
        {
            return false;
        }

        int ambient_id = action->ambient.ambient_id;
        if (action->ambient.use_signal_payload)
        {
            if (payload == NULL || action->ambient.payload_key == NULL)
            {
                return false;
            }

            const sdl3d_value *value = sdl3d_properties_get_value(payload, action->ambient.payload_key);
            if (value == NULL || value->type != SDL3D_VALUE_INT)
            {
                return false;
            }
            ambient_id = value->as_int;
        }

        return adapters->set_ambient(adapters->userdata, ambient_id,
                                     clamp_non_negative_float(action->ambient.fade_seconds), payload);
    }

    case SDL3D_LOGIC_ACTION_TRIGGER_FEEDBACK:
        if (adapters == NULL || adapters->trigger_feedback == NULL || action->feedback.feedback_name == NULL)
        {
            return false;
        }
        return adapters->trigger_feedback(adapters->userdata, action->feedback.feedback_name,
                                          clamp_non_negative_float(action->feedback.duration_seconds), payload);

    case SDL3D_LOGIC_ACTION_NONE:
        break;
    }

    return false;
}

void sdl3d_logic_contact_sensor_init(sdl3d_logic_contact_sensor *sensor, int sensor_id, sdl3d_bounding_box bounds,
                                     int signal_id, sdl3d_trigger_edge edge)
{
    if (sensor == NULL)
        return;

    SDL_zerop(sensor);
    sensor->sensor_id = sensor_id;
    sensor->bounds = bounds;
    sensor->signal_id = signal_id;
    sensor->edge = edge;
    sensor->enabled = true;
}

sdl3d_logic_sensor_result sdl3d_logic_contact_sensor_update(sdl3d_logic_contact_sensor *sensor,
                                                            sdl3d_logic_world *world, sdl3d_vec3 sample_position)
{
    if (sensor == NULL || !sensor->enabled)
        return sensor_result(false, SDL3D_LOGIC_SENSOR_EVENT_NONE);

    const bool active = point_inside_bounds(sensor->bounds, sample_position);
    const sdl3d_logic_sensor_event event = sensor_event_for_state(sensor->edge, sensor->was_inside, active);
    sensor->was_inside = active;

    if (emit_sensor_payload(world, sensor->signal_id, sensor->sensor_id, event, active, sample_position, -1, NULL,
                            0.0f))
    {
        return sensor_result(active, event);
    }
    return sensor_result(active, SDL3D_LOGIC_SENSOR_EVENT_NONE);
}

void sdl3d_logic_contact_sensor_reset(sdl3d_logic_contact_sensor *sensor)
{
    if (sensor != NULL)
        sensor->was_inside = false;
}

void sdl3d_logic_sector_sensor_init(sdl3d_logic_sector_sensor *sensor, int sensor_id, sdl3d_logic_target_ref sector,
                                    int signal_id, sdl3d_trigger_edge edge)
{
    if (sensor == NULL)
        return;

    SDL_zerop(sensor);
    sensor->sensor_id = sensor_id;
    sensor->sector = sector;
    sensor->signal_id = signal_id;
    sensor->edge = edge;
    sensor->enabled = true;
}

sdl3d_logic_sensor_result sdl3d_logic_sector_sensor_update(sdl3d_logic_sector_sensor *sensor, sdl3d_logic_world *world,
                                                           sdl3d_vec3 sample_position)
{
    if (sensor == NULL || world == NULL || !sensor->enabled)
        return sensor_result(false, SDL3D_LOGIC_SENSOR_EVENT_NONE);

    sdl3d_logic_resolved_target target;
    if (!sdl3d_logic_world_resolve_target(world, &sensor->sector, &target) || !resolved_target_is_sector(&target))
        return sensor_result(false, SDL3D_LOGIC_SENSOR_EVENT_NONE);

    const sdl3d_logic_target_context *context = sdl3d_logic_world_get_target_context(world);
    const int current_sector = sdl3d_level_find_sector_at(context->level, context->sectors, sample_position.x,
                                                          sample_position.z, sample_position.y);
    const bool active = current_sector == target.sector.sector_index;
    const sdl3d_logic_sensor_event event = sensor_event_for_state(sensor->edge, sensor->was_inside, active);
    sensor->was_inside = active;

    if (emit_sensor_payload(world, sensor->signal_id, sensor->sensor_id, event, active, sample_position,
                            target.sector.sector_index, NULL, 0.0f))
    {
        return sensor_result(active, event);
    }
    return sensor_result(active, SDL3D_LOGIC_SENSOR_EVENT_NONE);
}

void sdl3d_logic_sector_sensor_reset(sdl3d_logic_sector_sensor *sensor)
{
    if (sensor != NULL)
        sensor->was_inside = false;
}

void sdl3d_logic_proximity_sensor_init(sdl3d_logic_proximity_sensor *sensor, int sensor_id,
                                       sdl3d_logic_target_ref actor, float radius, int signal_id,
                                       sdl3d_trigger_edge edge)
{
    if (sensor == NULL)
        return;

    SDL_zerop(sensor);
    sensor->sensor_id = sensor_id;
    sensor->actor = actor;
    sensor->radius = clamp_non_negative_float(radius);
    sensor->signal_id = signal_id;
    sensor->edge = edge;
    sensor->enabled = true;
}

sdl3d_logic_sensor_result sdl3d_logic_proximity_sensor_update(sdl3d_logic_proximity_sensor *sensor,
                                                              sdl3d_logic_world *world, sdl3d_vec3 sample_position)
{
    if (sensor == NULL || world == NULL || !sensor->enabled)
        return sensor_result(false, SDL3D_LOGIC_SENSOR_EVENT_NONE);

    sdl3d_logic_resolved_target target;
    if (!sdl3d_logic_world_resolve_target(world, &sensor->actor, &target) || !resolved_target_is_actor(&target))
        return sensor_result(false, SDL3D_LOGIC_SENSOR_EVENT_NONE);

    const float dx = sample_position.x - target.actor->position.x;
    const float dy = sample_position.y - target.actor->position.y;
    const float dz = sample_position.z - target.actor->position.z;
    const float distance_sq = dx * dx + dy * dy + dz * dz;
    const float radius = clamp_non_negative_float(sensor->radius);
    const bool active = distance_sq <= radius * radius;
    const sdl3d_logic_sensor_event event = sensor_event_for_state(sensor->edge, sensor->was_inside, active);
    sensor->was_inside = active;

    if (emit_sensor_payload(world, sensor->signal_id, sensor->sensor_id, event, active, sample_position, -1,
                            target.actor, SDL_sqrtf(distance_sq)))
    {
        return sensor_result(active, event);
    }
    return sensor_result(active, SDL3D_LOGIC_SENSOR_EVENT_NONE);
}

void sdl3d_logic_proximity_sensor_reset(sdl3d_logic_proximity_sensor *sensor)
{
    if (sensor != NULL)
        sensor->was_inside = false;
}

bool sdl3d_logic_world_emit_signal(sdl3d_logic_world *world, int signal_id, const sdl3d_properties *payload)
{
    if (world == NULL || world->bus == NULL)
        return false;

    sdl3d_signal_emit(world->bus, signal_id, payload);
    return true;
}

void sdl3d_logic_relay_init(sdl3d_logic_relay *relay, int entity_id)
{
    if (relay == NULL)
        return;

    SDL_zerop(relay);
    relay->entity_id = entity_id;
    relay->enabled = true;
}

bool sdl3d_logic_relay_add_output(sdl3d_logic_relay *relay, int signal_id)
{
    if (relay == NULL || relay->output_count >= SDL3D_LOGIC_MAX_ENTITY_OUTPUTS)
        return false;

    relay->outputs[relay->output_count++] = signal_id;
    return true;
}

sdl3d_logic_entity_result sdl3d_logic_relay_activate(sdl3d_logic_world *world, sdl3d_logic_relay *relay,
                                                     const sdl3d_properties *payload)
{
    if (relay == NULL || !relay->enabled || relay->output_count <= 0)
        return entity_result(false, 0, -1);

    sdl3d_logic_entity_result result = entity_result(false, 0, -1);
    for (int i = 0; i < relay->output_count; ++i)
    {
        if (sdl3d_logic_world_emit_signal(world, relay->outputs[i], payload))
            result = entity_result(true, relay->outputs[i], i);
    }
    return result;
}

void sdl3d_logic_toggle_init(sdl3d_logic_toggle *toggle, int entity_id, bool initial_state, int on_signal,
                             int off_signal)
{
    if (toggle == NULL)
        return;

    SDL_zerop(toggle);
    toggle->entity_id = entity_id;
    toggle->state = initial_state;
    toggle->on_signal = on_signal;
    toggle->off_signal = off_signal;
    toggle->enabled = true;
}

sdl3d_logic_entity_result sdl3d_logic_toggle_activate(sdl3d_logic_world *world, sdl3d_logic_toggle *toggle)
{
    if (toggle == NULL || !toggle->enabled)
        return entity_result(false, 0, -1);

    toggle->state = !toggle->state;
    const int signal_id = toggle->state ? toggle->on_signal : toggle->off_signal;
    sdl3d_properties *payload = create_entity_payload(toggle->entity_id, "toggle");
    if (payload == NULL)
        return entity_result(false, 0, -1);

    sdl3d_properties_set_bool(payload, "state", toggle->state);
    const bool emitted = sdl3d_logic_world_emit_signal(world, signal_id, payload);
    sdl3d_properties_destroy(payload);
    return entity_result(emitted, signal_id, 0);
}

void sdl3d_logic_toggle_reset(sdl3d_logic_toggle *toggle, bool state)
{
    if (toggle != NULL)
        toggle->state = state;
}

void sdl3d_logic_counter_init(sdl3d_logic_counter *counter, int entity_id, int threshold, int output_signal,
                              bool reset_on_fire)
{
    if (counter == NULL)
        return;

    SDL_zerop(counter);
    counter->entity_id = entity_id;
    counter->threshold = threshold > 1 ? threshold : 1;
    counter->output_signal = output_signal;
    counter->reset_on_fire = reset_on_fire;
    counter->enabled = true;
}

sdl3d_logic_entity_result sdl3d_logic_counter_activate(sdl3d_logic_world *world, sdl3d_logic_counter *counter)
{
    if (counter == NULL || !counter->enabled)
        return entity_result(false, 0, -1);

    if (!counter->fired)
        counter->count++;
    if (counter->count < counter->threshold || counter->fired)
        return entity_result(false, 0, -1);

    sdl3d_properties *payload = create_entity_payload(counter->entity_id, "counter");
    if (payload == NULL)
        return entity_result(false, 0, -1);

    sdl3d_properties_set_int(payload, "count", counter->count);
    sdl3d_properties_set_int(payload, "threshold", counter->threshold);
    const bool emitted = sdl3d_logic_world_emit_signal(world, counter->output_signal, payload);
    sdl3d_properties_destroy(payload);

    if (emitted)
    {
        if (counter->reset_on_fire)
        {
            counter->count = 0;
            counter->fired = false;
        }
        else
        {
            counter->fired = true;
        }
    }

    return entity_result(emitted, counter->output_signal, 0);
}

void sdl3d_logic_counter_reset(sdl3d_logic_counter *counter)
{
    if (counter == NULL)
        return;

    counter->count = 0;
    counter->fired = false;
}

void sdl3d_logic_branch_init(sdl3d_logic_branch *branch, int entity_id, const sdl3d_properties *props, const char *key,
                             sdl3d_value expected, int true_signal, int false_signal)
{
    if (branch == NULL)
        return;

    SDL_zerop(branch);
    branch->entity_id = entity_id;
    branch->props = props;
    branch->key = key;
    branch->expected = expected;
    branch->true_signal = true_signal;
    branch->false_signal = false_signal;
    branch->enabled = true;
}

void sdl3d_logic_branch_init_payload(sdl3d_logic_branch *branch, int entity_id, const char *key, sdl3d_value expected,
                                     int true_signal, int false_signal)
{
    if (branch == NULL)
        return;

    sdl3d_logic_branch_init(branch, entity_id, NULL, key, expected, true_signal, false_signal);
    branch->use_activation_payload = true;
}

sdl3d_logic_entity_result sdl3d_logic_branch_activate(sdl3d_logic_world *world, sdl3d_logic_branch *branch)
{
    return sdl3d_logic_branch_activate_with_payload(world, branch, NULL);
}

sdl3d_logic_entity_result sdl3d_logic_branch_activate_with_payload(sdl3d_logic_world *world, sdl3d_logic_branch *branch,
                                                                   const sdl3d_properties *payload)
{
    if (branch == NULL || !branch->enabled)
        return entity_result(false, 0, -1);

    const sdl3d_properties *source = branch->use_activation_payload ? payload : branch->props;
    const sdl3d_value *actual = sdl3d_properties_get_value(source, branch->key);
    const bool matched = value_equals(actual, &branch->expected);
    const int signal_id = matched ? branch->true_signal : branch->false_signal;
    sdl3d_properties *branch_payload = create_entity_payload(branch->entity_id, "branch");
    if (branch_payload == NULL)
        return entity_result(false, 0, -1);

    sdl3d_properties_set_bool(branch_payload, "matched", matched);
    const bool emitted = sdl3d_logic_world_emit_signal(world, signal_id, branch_payload);
    sdl3d_properties_destroy(branch_payload);
    return entity_result(emitted, signal_id, matched ? 0 : 1);
}

void sdl3d_logic_random_init(sdl3d_logic_random *random, int entity_id, unsigned int seed)
{
    if (random == NULL)
        return;

    SDL_zerop(random);
    random->entity_id = entity_id;
    random->state = normalized_seed(seed);
    random->enabled = true;
}

bool sdl3d_logic_random_add_output(sdl3d_logic_random *random, int signal_id)
{
    if (random == NULL || random->output_count >= SDL3D_LOGIC_MAX_ENTITY_OUTPUTS)
        return false;

    random->outputs[random->output_count++] = signal_id;
    return true;
}

sdl3d_logic_entity_result sdl3d_logic_random_activate(sdl3d_logic_world *world, sdl3d_logic_random *random)
{
    if (random == NULL || !random->enabled || random->output_count <= 0)
        return entity_result(false, 0, -1);

    random->state = next_random_state(random->state);
    const int output_index = (int)(random->state % (unsigned int)random->output_count);
    const int signal_id = random->outputs[output_index];
    sdl3d_properties *payload = create_entity_payload(random->entity_id, "random");
    if (payload == NULL)
        return entity_result(false, 0, -1);

    sdl3d_properties_set_int(payload, "output_index", output_index);
    const bool emitted = sdl3d_logic_world_emit_signal(world, signal_id, payload);
    sdl3d_properties_destroy(payload);
    return entity_result(emitted, signal_id, output_index);
}

void sdl3d_logic_random_reset(sdl3d_logic_random *random, unsigned int seed)
{
    if (random != NULL)
        random->state = normalized_seed(seed);
}

void sdl3d_logic_sequence_init(sdl3d_logic_sequence *sequence, int entity_id, bool loop)
{
    if (sequence == NULL)
        return;

    SDL_zerop(sequence);
    sequence->entity_id = entity_id;
    sequence->loop = loop;
    sequence->enabled = true;
}

bool sdl3d_logic_sequence_add_output(sdl3d_logic_sequence *sequence, int signal_id)
{
    if (sequence == NULL || sequence->output_count >= SDL3D_LOGIC_MAX_ENTITY_OUTPUTS)
        return false;

    sequence->outputs[sequence->output_count++] = signal_id;
    return true;
}

sdl3d_logic_entity_result sdl3d_logic_sequence_activate(sdl3d_logic_world *world, sdl3d_logic_sequence *sequence)
{
    if (sequence == NULL || !sequence->enabled || sequence->output_count <= 0 || sequence->next_index < 0 ||
        sequence->next_index >= sequence->output_count)
    {
        return entity_result(false, 0, -1);
    }

    const int output_index = sequence->next_index;
    const int signal_id = sequence->outputs[output_index];
    sdl3d_properties *payload = create_entity_payload(sequence->entity_id, "sequence");
    if (payload == NULL)
        return entity_result(false, 0, -1);

    sdl3d_properties_set_int(payload, "output_index", output_index);
    const bool emitted = sdl3d_logic_world_emit_signal(world, signal_id, payload);
    sdl3d_properties_destroy(payload);

    if (emitted)
    {
        sequence->next_index++;
        if (sequence->next_index >= sequence->output_count && sequence->loop)
            sequence->next_index = 0;
    }

    return entity_result(emitted, signal_id, output_index);
}

void sdl3d_logic_sequence_reset(sdl3d_logic_sequence *sequence)
{
    if (sequence != NULL)
        sequence->next_index = 0;
}

void sdl3d_logic_once_init(sdl3d_logic_once *once, int entity_id, int output_signal)
{
    if (once == NULL)
        return;

    SDL_zerop(once);
    once->entity_id = entity_id;
    once->output_signal = output_signal;
    once->enabled = true;
}

sdl3d_logic_entity_result sdl3d_logic_once_activate(sdl3d_logic_world *world, sdl3d_logic_once *once)
{
    if (once == NULL || !once->enabled || once->fired)
        return entity_result(false, 0, -1);

    sdl3d_properties *payload = create_entity_payload(once->entity_id, "once");
    if (payload == NULL)
        return entity_result(false, 0, -1);

    const bool emitted = sdl3d_logic_world_emit_signal(world, once->output_signal, payload);
    sdl3d_properties_destroy(payload);
    if (emitted)
        once->fired = true;
    return entity_result(emitted, once->output_signal, 0);
}

void sdl3d_logic_once_reset(sdl3d_logic_once *once)
{
    if (once != NULL)
        once->fired = false;
}

void sdl3d_logic_timer_init(sdl3d_logic_timer *timer, int entity_id, float delay, int output_signal, bool repeating,
                            float interval)
{
    if (timer == NULL)
        return;

    SDL_zerop(timer);
    timer->entity_id = entity_id;
    timer->delay = delay;
    timer->remaining = delay;
    timer->output_signal = output_signal;
    timer->repeating = repeating;
    timer->interval = interval;
    timer->enabled = true;
}

bool sdl3d_logic_timer_start(sdl3d_logic_timer *timer)
{
    if (timer == NULL || !timer->enabled || timer->delay <= 0.0f)
        return false;
    if (timer->repeating && timer->interval <= 0.0f)
        return false;

    timer->remaining = timer->delay;
    timer->active = true;
    return true;
}

sdl3d_logic_entity_result sdl3d_logic_timer_update(sdl3d_logic_world *world, sdl3d_logic_timer *timer, float dt)
{
    if (timer == NULL || !timer->enabled || !timer->active || dt < 0.0f)
        return entity_result(false, 0, -1);

    timer->remaining -= dt;
    if (timer->remaining > 0.0f)
        return entity_result(false, 0, -1);

    sdl3d_properties *payload = create_entity_payload(timer->entity_id, "timer");
    if (payload == NULL)
        return entity_result(false, 0, -1);

    sdl3d_properties_set_bool(payload, "repeating", timer->repeating);
    const bool emitted = sdl3d_logic_world_emit_signal(world, timer->output_signal, payload);
    sdl3d_properties_destroy(payload);

    if (emitted)
    {
        if (timer->repeating)
        {
            timer->remaining += timer->interval;
            if (timer->remaining <= 0.0f)
                timer->remaining = timer->interval;
        }
        else
        {
            timer->active = false;
            timer->remaining = 0.0f;
        }
    }

    return entity_result(emitted, timer->output_signal, 0);
}

void sdl3d_logic_timer_stop(sdl3d_logic_timer *timer)
{
    if (timer == NULL)
        return;

    timer->active = false;
    timer->remaining = timer->delay;
}

bool sdl3d_logic_timer_active(const sdl3d_logic_timer *timer)
{
    return timer != NULL && timer->active;
}

static int bind_entity(sdl3d_logic_world *world, int signal_id, logic_entity_binding_kind kind, void *entity)
{
    if (world == NULL || world->bus == NULL || entity == NULL)
        return 0;

    logic_entity_binding *binding = (logic_entity_binding *)SDL_calloc(1, sizeof(logic_entity_binding));
    if (binding == NULL)
        return 0;

    binding->id = world->next_binding_id++;
    binding->owner = world;
    binding->signal_id = signal_id;
    binding->kind = kind;
    binding->entity = entity;
    binding->enabled = true;
    binding->connection_id = sdl3d_signal_connect(world->bus, signal_id, execute_entity_binding, binding);
    if (binding->connection_id == 0)
    {
        SDL_free(binding);
        return 0;
    }

    binding->next = world->entity_bindings;
    world->entity_bindings = binding;
    world->entity_binding_count++;
    return binding->id;
}

int sdl3d_logic_world_bind_relay(sdl3d_logic_world *world, int signal_id, sdl3d_logic_relay *relay)
{
    return bind_entity(world, signal_id, LOGIC_ENTITY_BINDING_RELAY, relay);
}

int sdl3d_logic_world_bind_toggle(sdl3d_logic_world *world, int signal_id, sdl3d_logic_toggle *toggle)
{
    return bind_entity(world, signal_id, LOGIC_ENTITY_BINDING_TOGGLE, toggle);
}

int sdl3d_logic_world_bind_counter(sdl3d_logic_world *world, int signal_id, sdl3d_logic_counter *counter)
{
    return bind_entity(world, signal_id, LOGIC_ENTITY_BINDING_COUNTER, counter);
}

int sdl3d_logic_world_bind_branch(sdl3d_logic_world *world, int signal_id, sdl3d_logic_branch *branch)
{
    return bind_entity(world, signal_id, LOGIC_ENTITY_BINDING_BRANCH, branch);
}

int sdl3d_logic_world_bind_random(sdl3d_logic_world *world, int signal_id, sdl3d_logic_random *random)
{
    return bind_entity(world, signal_id, LOGIC_ENTITY_BINDING_RANDOM, random);
}

int sdl3d_logic_world_bind_sequence(sdl3d_logic_world *world, int signal_id, sdl3d_logic_sequence *sequence)
{
    return bind_entity(world, signal_id, LOGIC_ENTITY_BINDING_SEQUENCE, sequence);
}

int sdl3d_logic_world_bind_once(sdl3d_logic_world *world, int signal_id, sdl3d_logic_once *once)
{
    return bind_entity(world, signal_id, LOGIC_ENTITY_BINDING_ONCE, once);
}

void sdl3d_logic_world_unbind_entity(sdl3d_logic_world *world, int binding_id)
{
    if (world == NULL || binding_id <= 0)
        return;

    logic_entity_binding **cursor = &world->entity_bindings;
    while (*cursor != NULL)
    {
        logic_entity_binding *binding = *cursor;
        if (binding->id == binding_id)
        {
            *cursor = binding->next;
            if (world->bus != NULL && binding->connection_id > 0)
                sdl3d_signal_disconnect(world->bus, binding->connection_id);
            SDL_free(binding);
            world->entity_binding_count--;
            return;
        }
        cursor = &binding->next;
    }
}

bool sdl3d_logic_world_set_entity_binding_enabled(sdl3d_logic_world *world, int binding_id, bool enabled)
{
    logic_entity_binding *binding = find_entity_binding(world, binding_id);
    if (binding == NULL)
        return false;

    binding->enabled = enabled;
    return true;
}

bool sdl3d_logic_world_entity_binding_enabled(const sdl3d_logic_world *world, int binding_id)
{
    const logic_entity_binding *binding = find_entity_binding(world, binding_id);
    return binding != NULL && binding->enabled;
}

int sdl3d_logic_world_entity_binding_count(const sdl3d_logic_world *world)
{
    return world != NULL ? world->entity_binding_count : 0;
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
