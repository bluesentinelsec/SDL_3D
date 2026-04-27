/* Doom-level door setup, interaction, collision, and drawing. */
#include "doom_doors.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/math.h"
#include "sdl3d/properties.h"
#include "sdl3d/shapes.h"

#include "player.h"

#define DOOM_DOOR_TEXTURE SDL3D_MEDIA_DIR "/textures/door-hatch.png"
#define DOOM_DOOR_INTERACT_RANGE 2.25f
#define DOOM_DOOR_INTERACT_MIN_DOT 0.15f

enum
{
    DOOM_DOOR_NUKAGE_NORTH = 1,
    DOOM_DOOR_NUKAGE_EAST = 2,
    DOOM_DOOR_NUKAGE_WEST = 3,
};

static sdl3d_bounding_box bounds_make(float min_x, float min_y, float min_z, float max_x, float max_y, float max_z)
{
    sdl3d_bounding_box bounds;
    bounds.min = sdl3d_vec3_make(min_x, min_y, min_z);
    bounds.max = sdl3d_vec3_make(max_x, max_y, max_z);
    return bounds;
}

static sdl3d_vec3 bounds_center(sdl3d_bounding_box bounds)
{
    return sdl3d_vec3_make((bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.y + bounds.max.y) * 0.5f,
                           (bounds.min.z + bounds.max.z) * 0.5f);
}

static sdl3d_vec3 bounds_size(sdl3d_bounding_box bounds)
{
    return sdl3d_vec3_make(bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z);
}

static sdl3d_bounding_box door_closed_bounds(const sdl3d_door *door)
{
    sdl3d_bounding_box bounds = door->panels[0].closed_bounds;
    for (int i = 1; i < door->panel_count; ++i)
    {
        const sdl3d_bounding_box panel = door->panels[i].closed_bounds;
        if (panel.min.x < bounds.min.x)
            bounds.min.x = panel.min.x;
        if (panel.min.y < bounds.min.y)
            bounds.min.y = panel.min.y;
        if (panel.min.z < bounds.min.z)
            bounds.min.z = panel.min.z;
        if (panel.max.x > bounds.max.x)
            bounds.max.x = panel.max.x;
        if (panel.max.y > bounds.max.y)
            bounds.max.y = panel.max.y;
        if (panel.max.z > bounds.max.z)
            bounds.max.z = panel.max.z;
    }
    return bounds;
}

static float door_distance_sq_xz(const sdl3d_door *door, sdl3d_vec3 point)
{
    const sdl3d_vec3 center = bounds_center(door_closed_bounds(door));
    const float dx = center.x - point.x;
    const float dz = center.z - point.z;
    return dx * dx + dz * dz;
}

static bool door_is_in_front(const sdl3d_door *door, sdl3d_vec3 eye_position, float yaw)
{
    const sdl3d_vec3 center = bounds_center(door_closed_bounds(door));
    sdl3d_vec3 to_door = sdl3d_vec3_make(center.x - eye_position.x, 0.0f, center.z - eye_position.z);
    if (sdl3d_vec3_length_squared(to_door) <= 0.0001f)
        return true;

    to_door = sdl3d_vec3_normalize(to_door);
    const sdl3d_vec3 forward = sdl3d_vec3_make(SDL_sinf(yaw), 0.0f, -SDL_cosf(yaw));
    return sdl3d_vec3_dot(forward, to_door) >= DOOM_DOOR_INTERACT_MIN_DOT;
}

static sdl3d_door *find_door(doom_doors *doors, const char *door_name, int door_id)
{
    if (doors == NULL)
        return NULL;

    for (int i = 0; i < DOOM_DOOR_COUNT; ++i)
    {
        sdl3d_door *door = &doors->doors[i];
        if (door_id >= 0 && door->door_id == door_id)
            return door;
        if (door_name != NULL && door->name != NULL && SDL_strcmp(door->name, door_name) == 0)
            return door;
    }
    return NULL;
}

static void init_nukage_north_door(sdl3d_door *door)
{
    sdl3d_door_desc desc;
    SDL_zero(desc);
    desc.door_id = DOOM_DOOR_NUKAGE_NORTH;
    desc.name = "nukage_north";
    desc.panel_count = 2;
    desc.panels[0].closed_bounds = bounds_make(3.0f, -0.5f, 15.85f, 5.0f, 3.5f, 16.15f);
    desc.panels[0].open_offset = sdl3d_vec3_make(-2.15f, 0.0f, 0.0f);
    desc.panels[1].closed_bounds = bounds_make(5.0f, -0.5f, 15.85f, 7.0f, 3.5f, 16.15f);
    desc.panels[1].open_offset = sdl3d_vec3_make(2.15f, 0.0f, 0.0f);
    desc.open_seconds = 0.55f;
    desc.close_seconds = 0.55f;
    sdl3d_door_init(door, &desc);
}

static void init_nukage_east_door(sdl3d_door *door)
{
    sdl3d_door_desc desc;
    SDL_zero(desc);
    desc.door_id = DOOM_DOOR_NUKAGE_EAST;
    desc.name = "nukage_east";
    desc.panel_count = 1;
    desc.panels[0].closed_bounds = bounds_make(9.85f, -0.5f, 18.0f, 10.15f, 3.5f, 22.0f);
    desc.panels[0].open_offset = sdl3d_vec3_make(0.0f, 4.2f, 0.0f);
    desc.open_seconds = 0.7f;
    desc.close_seconds = 0.7f;
    sdl3d_door_init(door, &desc);
}

static void init_nukage_west_door(sdl3d_door *door)
{
    sdl3d_door_desc desc;
    SDL_zero(desc);
    desc.door_id = DOOM_DOOR_NUKAGE_WEST;
    desc.name = "nukage_west";
    desc.panel_count = 2;
    desc.panels[0].closed_bounds = bounds_make(-2.15f, -0.5f, 18.0f, -1.85f, 3.5f, 21.0f);
    desc.panels[0].open_offset = sdl3d_vec3_make(0.0f, 0.0f, -3.15f);
    desc.panels[1].closed_bounds = bounds_make(-2.15f, -0.5f, 21.0f, -1.85f, 3.5f, 24.0f);
    desc.panels[1].open_offset = sdl3d_vec3_make(0.0f, 0.0f, 3.15f);
    desc.open_seconds = 0.65f;
    desc.close_seconds = 0.65f;
    sdl3d_door_init(door, &desc);
}

bool doom_doors_init(doom_doors *doors)
{
    if (doors == NULL)
        return false;

    SDL_zerop(doors);
    init_nukage_north_door(&doors->doors[0]);
    init_nukage_east_door(&doors->doors[1]);
    init_nukage_west_door(&doors->doors[2]);

    if (!sdl3d_load_texture_from_file(DOOM_DOOR_TEXTURE, &doors->texture))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Door texture load failed: %s", SDL_GetError());
        return false;
    }
    doors->has_texture = true;
    return true;
}

void doom_doors_free(doom_doors *doors)
{
    if (doors == NULL)
        return;

    if (doors->has_texture)
        sdl3d_free_texture(&doors->texture);
    SDL_zerop(doors);
}

void doom_doors_update(doom_doors *doors, float dt)
{
    if (doors == NULL)
        return;

    for (int i = 0; i < DOOM_DOOR_COUNT; ++i)
        sdl3d_door_update(&doors->doors[i], dt);
}

bool doom_doors_emit_interact(doom_doors *doors, sdl3d_logic_world *logic, int signal_id, sdl3d_vec3 eye_position,
                              float yaw)
{
    if (doors == NULL || logic == NULL)
        return false;

    sdl3d_door *best = NULL;
    float best_distance_sq = 0.0f;
    for (int i = 0; i < DOOM_DOOR_COUNT; ++i)
    {
        sdl3d_door *door = &doors->doors[i];
        if (!sdl3d_door_point_in_interaction_range(door, eye_position, DOOM_DOOR_INTERACT_RANGE) ||
            !door_is_in_front(door, eye_position, yaw))
        {
            continue;
        }

        const float distance_sq = door_distance_sq_xz(door, eye_position);
        if (best == NULL || distance_sq < best_distance_sq)
        {
            best = door;
            best_distance_sq = distance_sq;
        }
    }

    if (best == NULL)
        return false;

    sdl3d_properties *payload = sdl3d_properties_create();
    if (payload == NULL)
        return false;

    sdl3d_properties_set_int(payload, "door_id", best->door_id);
    sdl3d_properties_set_string(payload, "door_name", best->name != NULL ? best->name : "");
    const bool emitted = sdl3d_logic_world_emit_signal(logic, signal_id, payload);
    sdl3d_properties_destroy(payload);
    return emitted;
}

bool doom_doors_apply_command(doom_doors *doors, const char *door_name, int door_id, sdl3d_logic_door_command command,
                              float auto_close_seconds)
{
    sdl3d_door *door = find_door(doors, door_name, door_id);
    if (door == NULL)
        return false;

    if (auto_close_seconds >= 0.0f)
        sdl3d_door_set_auto_close_delay(door, auto_close_seconds);

    bool applied = false;
    switch (command)
    {
    case SDL3D_LOGIC_DOOR_OPEN:
        applied = sdl3d_door_open(door);
        break;
    case SDL3D_LOGIC_DOOR_CLOSE:
        applied = sdl3d_door_close(door);
        break;
    case SDL3D_LOGIC_DOOR_TOGGLE:
        applied = sdl3d_door_toggle(door);
        break;
    }

    if (applied)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Door %d (%s) command %d", door->door_id,
                    door->name != NULL ? door->name : "unnamed", (int)command);
    }
    return applied;
}

bool doom_doors_resolve_player(const doom_doors *doors, sdl3d_fps_mover *mover)
{
    if (doors == NULL || mover == NULL)
        return false;

    bool resolved = false;
    for (int i = 0; i < DOOM_DOOR_COUNT; ++i)
    {
        if (sdl3d_door_resolve_cylinder(&doors->doors[i], &mover->position, mover->config.player_height,
                                        mover->config.player_radius))
        {
            mover->last_good_position = mover->position;
            resolved = true;
        }
    }
    return resolved;
}

void doom_doors_draw(const doom_doors *doors, sdl3d_render_context *ctx)
{
    if (doors == NULL || ctx == NULL)
        return;

    const sdl3d_texture2d *texture = doors->has_texture ? &doors->texture : NULL;
    const sdl3d_color tint = {255, 255, 255, 255};
    for (int i = 0; i < DOOM_DOOR_COUNT; ++i)
    {
        const sdl3d_door *door = &doors->doors[i];
        for (int panel_index = 0; panel_index < sdl3d_door_panel_count(door); ++panel_index)
        {
            sdl3d_bounding_box bounds;
            if (!sdl3d_door_get_panel_bounds(door, panel_index, &bounds))
                continue;

            const sdl3d_vec3 center = bounds_center(bounds);
            const sdl3d_vec3 size = bounds_size(bounds);
            sdl3d_draw_cube_textured(ctx, center, size, sdl3d_vec3_make(0.0f, 1.0f, 0.0f), 0.0f, texture, tint);
        }
    }
}
