#include "slayer3d/actor_controller.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/math.h"
#include "slayer3d/properties.h"

#define SLAYER3D_PATROL_DEFAULT_SPEED 1.0f
#define SLAYER3D_PATROL_DEFAULT_WAIT_TIME 0.0f
#define SLAYER3D_PATROL_DEFAULT_ARRIVAL_RADIUS 0.1f

static slayer3d_actor_patrol_result patrol_result(const slayer3d_actor_patrol_controller *controller,
                                                  const slayer3d_registered_actor *actor)
{
    slayer3d_actor_patrol_result result;
    SDL_zero(result);
    result.reached_waypoint = -1;
    result.state = controller != NULL ? controller->state : SLAYER3D_ACTOR_PATROL_IDLE;
    if (actor != NULL)
    {
        result.previous_position = actor->position;
        result.position = actor->position;
    }
    return result;
}

static float clamp_positive(float value, float fallback)
{
    return value > 0.0f ? value : fallback;
}

static float distance_xz_sq(slayer3d_vec3 a, slayer3d_vec3 b)
{
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    return dx * dx + dz * dz;
}

static void emit_patrol_signal(slayer3d_signal_bus *bus, int signal_id,
                               const slayer3d_actor_patrol_controller *controller,
                               const slayer3d_registered_actor *actor, int waypoint_index)
{
    if (bus == NULL || signal_id <= 0 || controller == NULL || actor == NULL)
        return;

    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return;

    slayer3d_properties_set_int(payload, "controller_id", controller->controller_id);
    slayer3d_properties_set_int(payload, "actor_id", actor->id);
    slayer3d_properties_set_string(payload, "actor_name", actor->name != NULL ? actor->name : "");
    slayer3d_properties_set_string(payload, "state", slayer3d_actor_patrol_state_name(controller->state));
    slayer3d_properties_set_int(payload, "target_waypoint", controller->target_waypoint);
    if (waypoint_index >= 0)
        slayer3d_properties_set_int(payload, "waypoint_index", waypoint_index);

    slayer3d_signal_emit(bus, signal_id, payload);
    slayer3d_properties_destroy(payload);
}

static void enter_state(slayer3d_actor_patrol_controller *controller, slayer3d_registered_actor *actor,
                        slayer3d_signal_bus *bus, slayer3d_actor_patrol_state state,
                        slayer3d_actor_patrol_result *result)
{
    if (controller == NULL || actor == NULL || controller->state == state)
        return;

    controller->state = state;
    if (state == SLAYER3D_ACTOR_PATROL_IDLE)
        controller->wait_remaining = controller->wait_time;
    result->state_changed = true;
    result->state = state;
    slayer3d_actor_patrol_controller_sync_properties(controller, actor);

    const int signal_id =
        state == SLAYER3D_ACTOR_PATROL_IDLE ? controller->signals.idle_started : controller->signals.walk_started;
    emit_patrol_signal(bus, signal_id, controller, actor, -1);
}

static bool compute_next_target(slayer3d_actor_patrol_controller *controller, int reached_waypoint)
{
    if (controller == NULL || controller->waypoint_count <= 1)
        return false;

    if (reached_waypoint != 0)
        controller->has_left_start = true;

    bool completed_cycle = reached_waypoint == 0 && controller->has_left_start;
    if (completed_cycle)
        controller->has_left_start = false;

    if (controller->mode == SLAYER3D_ACTOR_PATROL_PING_PONG)
    {
        if (reached_waypoint >= controller->waypoint_count - 1)
            controller->direction = -1;
        else if (reached_waypoint <= 0)
            controller->direction = 1;

        controller->target_waypoint = reached_waypoint + controller->direction;
    }
    else
    {
        controller->direction = 1;
        controller->target_waypoint = (reached_waypoint + 1) % controller->waypoint_count;
    }

    if (controller->target_waypoint < 0)
        controller->target_waypoint = 0;
    if (controller->target_waypoint >= controller->waypoint_count)
        controller->target_waypoint = controller->waypoint_count - 1;

    return completed_cycle;
}

slayer3d_actor_patrol_config slayer3d_actor_patrol_default_config(void)
{
    slayer3d_actor_patrol_config config;
    SDL_zero(config);
    config.speed = SLAYER3D_PATROL_DEFAULT_SPEED;
    config.wait_time = SLAYER3D_PATROL_DEFAULT_WAIT_TIME;
    config.arrival_radius = SLAYER3D_PATROL_DEFAULT_ARRIVAL_RADIUS;
    config.mode = SLAYER3D_ACTOR_PATROL_LOOP;
    config.start_idle = true;
    return config;
}

void slayer3d_actor_patrol_controller_init(slayer3d_actor_patrol_controller *controller, int controller_id,
                                           int actor_id, const slayer3d_actor_patrol_config *config)
{
    if (controller == NULL)
        return;

    slayer3d_actor_patrol_config defaults = slayer3d_actor_patrol_default_config();
    if (config == NULL)
        config = &defaults;

    SDL_zerop(controller);
    controller->controller_id = controller_id;
    controller->actor_id = actor_id;
    controller->enabled = true;
    controller->speed = clamp_positive(config->speed, SLAYER3D_PATROL_DEFAULT_SPEED);
    controller->wait_time = config->wait_time > 0.0f ? config->wait_time : 0.0f;
    controller->arrival_radius = clamp_positive(config->arrival_radius, SLAYER3D_PATROL_DEFAULT_ARRIVAL_RADIUS);
    controller->mode = config->mode;
    controller->signals = config->signals;
    slayer3d_actor_patrol_controller_reset(controller, config->start_idle);
}

void slayer3d_actor_patrol_controller_clear_waypoints(slayer3d_actor_patrol_controller *controller)
{
    if (controller == NULL)
        return;

    controller->waypoint_count = 0;
    controller->target_waypoint = 0;
    controller->direction = 1;
    controller->has_left_start = false;
}

bool slayer3d_actor_patrol_controller_add_waypoint(slayer3d_actor_patrol_controller *controller, slayer3d_vec3 waypoint)
{
    if (controller == NULL || controller->waypoint_count >= SLAYER3D_ACTOR_PATROL_MAX_WAYPOINTS)
        return false;

    controller->waypoints[controller->waypoint_count++] = waypoint;
    if (controller->waypoint_count == 2 && controller->target_waypoint == 0)
        controller->target_waypoint = 1;
    return true;
}

void slayer3d_actor_patrol_controller_reset(slayer3d_actor_patrol_controller *controller, bool start_idle)
{
    if (controller == NULL)
        return;

    controller->direction = 1;
    controller->target_waypoint = controller->waypoint_count > 1 ? 1 : 0;
    controller->state = start_idle ? SLAYER3D_ACTOR_PATROL_IDLE : SLAYER3D_ACTOR_PATROL_WALK;
    controller->wait_remaining = start_idle ? controller->wait_time : 0.0f;
    controller->has_left_start = false;
}

void slayer3d_actor_patrol_controller_set_enabled(slayer3d_actor_patrol_controller *controller, bool enabled)
{
    if (controller != NULL)
        controller->enabled = enabled;
}

const char *slayer3d_actor_patrol_state_name(slayer3d_actor_patrol_state state)
{
    switch (state)
    {
    case SLAYER3D_ACTOR_PATROL_IDLE:
        return "idle";
    case SLAYER3D_ACTOR_PATROL_WALK:
        return "walk";
    }
    return "unknown";
}

void slayer3d_actor_patrol_controller_sync_properties(const slayer3d_actor_patrol_controller *controller,
                                                      slayer3d_registered_actor *actor)
{
    if (controller == NULL || actor == NULL || actor->props == NULL)
        return;

    slayer3d_properties_set_string(actor->props, "state", slayer3d_actor_patrol_state_name(controller->state));
    slayer3d_properties_set_int(actor->props, "target_waypoint", controller->target_waypoint);
    slayer3d_properties_set_bool(actor->props, "patrol_enabled", controller->enabled);
    slayer3d_properties_set_bool(actor->props, "enabled", controller->enabled);
    if (!slayer3d_properties_has(actor->props, "alerted"))
        slayer3d_properties_set_bool(actor->props, "alerted", false);
    slayer3d_properties_set_string(actor->props, "patrol_mode",
                                   controller->mode == SLAYER3D_ACTOR_PATROL_PING_PONG ? "ping_pong" : "loop");
    slayer3d_properties_set_float(actor->props, "patrol_speed", controller->speed);
    slayer3d_properties_set_float(actor->props, "patrol_wait_time", controller->wait_time);
    slayer3d_properties_set_float(actor->props, "patrol_wait_remaining", controller->wait_remaining);
}

void slayer3d_actor_patrol_controller_apply_actor_properties(slayer3d_actor_patrol_controller *controller,
                                                             const slayer3d_registered_actor *actor)
{
    if (controller == NULL || actor == NULL || actor->props == NULL)
        return;

    if (slayer3d_properties_has(actor->props, "patrol_enabled"))
        controller->enabled = slayer3d_properties_get_bool(actor->props, "patrol_enabled", controller->enabled);

    if (slayer3d_properties_has(actor->props, "patrol_speed"))
    {
        const float speed = slayer3d_properties_get_float(actor->props, "patrol_speed", controller->speed);
        if (speed > 0.0f)
            controller->speed = speed;
    }

    if (slayer3d_properties_has(actor->props, "patrol_wait_time"))
    {
        const float wait_time = slayer3d_properties_get_float(actor->props, "patrol_wait_time", controller->wait_time);
        controller->wait_time = wait_time > 0.0f ? wait_time : 0.0f;
        if (controller->state == SLAYER3D_ACTOR_PATROL_IDLE && controller->wait_remaining > controller->wait_time)
            controller->wait_remaining = controller->wait_time;
    }
}

slayer3d_actor_patrol_result slayer3d_actor_patrol_controller_update(slayer3d_actor_patrol_controller *controller,
                                                                     slayer3d_actor_registry *registry,
                                                                     slayer3d_signal_bus *bus, float dt,
                                                                     slayer3d_actor_patrol_move_fn move_fn,
                                                                     void *move_userdata)
{
    slayer3d_registered_actor *actor =
        controller != NULL ? slayer3d_actor_registry_get(registry, controller->actor_id) : NULL;
    slayer3d_actor_patrol_result result = patrol_result(controller, actor);
    if (controller == NULL || actor == NULL || dt < 0.0f)
        return result;

    result.updated = true;
    slayer3d_actor_patrol_controller_apply_actor_properties(controller, actor);
    slayer3d_actor_patrol_controller_sync_properties(controller, actor);
    if (!controller->enabled || !actor->active || controller->waypoint_count < 2 || controller->speed <= 0.0f)
        return result;

    if (controller->target_waypoint < 0 || controller->target_waypoint >= controller->waypoint_count)
        controller->target_waypoint = controller->waypoint_count > 1 ? 1 : 0;

    if (controller->state == SLAYER3D_ACTOR_PATROL_IDLE)
    {
        controller->wait_remaining -= dt;
        if (controller->wait_remaining <= 0.0f)
        {
            controller->wait_remaining = 0.0f;
            enter_state(controller, actor, bus, SLAYER3D_ACTOR_PATROL_WALK, &result);
        }
        slayer3d_actor_patrol_controller_sync_properties(controller, actor);
        return result;
    }

    const int target_index = controller->target_waypoint;
    const slayer3d_vec3 target = controller->waypoints[target_index];
    const float arrival_radius_sq = controller->arrival_radius * controller->arrival_radius;
    float distance_sq = distance_xz_sq(actor->position, target);

    if (distance_sq > arrival_radius_sq)
    {
        const float distance = SDL_sqrtf(distance_sq);
        float step = controller->speed * dt;
        if (step > distance)
            step = distance;

        const float move_x = (target.x - actor->position.x) / distance;
        const float move_z = (target.z - actor->position.z) / distance;
        slayer3d_vec3 desired = actor->position;
        desired.x += move_x * step;
        desired.z += move_z * step;
        desired.y = target.y;

        slayer3d_vec3 accepted = desired;
        bool accepted_move = true;
        if (move_fn != NULL)
            accepted_move = move_fn(move_userdata, controller, actor, desired, &accepted);

        if (!accepted_move)
        {
            slayer3d_actor_patrol_controller_sync_properties(controller, actor);
            return result;
        }

        actor->position = accepted;
        result.moved = distance_xz_sq(result.previous_position, actor->position) > 0.000001f;
        result.position = actor->position;
        result.movement_delta = slayer3d_vec3_make(actor->position.x - result.previous_position.x,
                                                   actor->position.y - result.previous_position.y,
                                                   actor->position.z - result.previous_position.z);
        distance_sq = distance_xz_sq(actor->position, target);
    }

    if (distance_sq <= arrival_radius_sq)
    {
        result.waypoint_reached = true;
        result.reached_waypoint = target_index;
        emit_patrol_signal(bus, controller->signals.waypoint_reached, controller, actor, target_index);
        result.loop_completed = compute_next_target(controller, target_index);
        if (result.loop_completed)
            emit_patrol_signal(bus, controller->signals.loop_completed, controller, actor, target_index);
        enter_state(controller, actor, bus, SLAYER3D_ACTOR_PATROL_IDLE, &result);
    }

    slayer3d_actor_patrol_controller_sync_properties(controller, actor);
    result.state = controller->state;
    result.position = actor->position;
    return result;
}
