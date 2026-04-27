/* Entity management: sprite actors, 3D model actors, textures. */
#include "entities.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/animation.h"
#include "sdl3d/sdl3d.h"

#define ROBOT_SPRITE_FOOT_PADDING_RATIO (10.0f / 92.0f)
#define ROBOT_GROUND_STEP_HEIGHT 1.1f
#define ROBOT_COLLISION_HEIGHT 2.0f
#define ROBOT_DEFAULT_ARRIVAL_RADIUS 0.12f
#define DOOM_SIGNAL_ROBOT_WAYPOINT_REACHED 6201
#define DOOM_SIGNAL_ROBOT_LOOP_COMPLETED 6202
#define DOOM_SIGNAL_ROBOT_IDLE_STARTED 6203
#define DOOM_SIGNAL_ROBOT_WALK_STARTED 6204

static void configure_sprite_texture(sdl3d_texture2d *texture)
{
    sdl3d_set_texture_filter(texture, SDL3D_TEXTURE_FILTER_NEAREST);
    sdl3d_set_texture_wrap(texture, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);
}

static void robot_set_floor_position(sdl3d_sprite_actor *actor, const sdl3d_level *level)
{
    if (actor == NULL)
        return;
    sdl3d_sprite_actor_snap_to_ground(actor, level, g_sectors, ROBOT_GROUND_STEP_HEIGHT, ROBOT_COLLISION_HEIGHT);
}

static sdl3d_vec2 normalized_walk_direction(float x, float z)
{
    float length_sq = x * x + z * z;
    if (length_sq <= 0.0001f)
        return (sdl3d_vec2){1.0f, 0.0f};

    float inv_length = 1.0f / SDL_sqrtf(length_sq);
    return (sdl3d_vec2){x * inv_length, z * inv_length};
}

static doom_robot_npc *find_robot_by_actor_id(entities *e, int actor_id)
{
    if (e == NULL || actor_id <= 0)
        return NULL;
    for (int i = 0; i < e->robot_count; ++i)
    {
        if (e->robots[i].actor_id == actor_id)
            return &e->robots[i];
    }
    return NULL;
}

typedef struct robot_patrol_move_context
{
    entities *entities;
    const sdl3d_level *level;
} robot_patrol_move_context;

static bool robot_patrol_move(void *userdata, const sdl3d_actor_patrol_controller *controller,
                              sdl3d_registered_actor *registered_actor, sdl3d_vec3 desired_position,
                              sdl3d_vec3 *out_position)
{
    robot_patrol_move_context *context = (robot_patrol_move_context *)userdata;
    if (context == NULL || context->entities == NULL || controller == NULL || registered_actor == NULL ||
        out_position == NULL)
        return false;

    doom_robot_npc *npc = find_robot_by_actor_id(context->entities, controller->actor_id);
    if (npc == NULL || npc->sprite_index < 0 || npc->sprite_index >= context->entities->sprites.count)
        return false;

    sdl3d_sprite_actor *sprite = &context->entities->sprites.actors[npc->sprite_index];
    sprite->position = registered_actor->position;

    float floor_y = registered_actor->position.y;
    if (!sdl3d_sprite_actor_can_stand_at(sprite, context->level, g_sectors, desired_position.x, desired_position.z,
                                         ROBOT_GROUND_STEP_HEIGHT, ROBOT_COLLISION_HEIGHT, &floor_y))
    {
        return false;
    }

    out_position->x = desired_position.x;
    out_position->y = floor_y;
    out_position->z = desired_position.z;

    sdl3d_sprite_actor_set_facing_direction(sprite, desired_position.x - registered_actor->position.x,
                                            desired_position.z - registered_actor->position.z);
    return true;
}

static sdl3d_registered_actor *register_robot(entities *e, int robot_number, const sdl3d_sprite_actor *actor)
{
    if (e == NULL || e->registry == NULL || actor == NULL)
        return NULL;

    char name[32];
    SDL_snprintf(name, sizeof(name), "robot_%d", robot_number);
    sdl3d_registered_actor *ra = sdl3d_actor_registry_add(e->registry, name);
    if (ra == NULL)
        return NULL;

    ra->position = actor->position;
    sdl3d_properties_set_string(ra->props, "classname", "npc_robot");
    sdl3d_properties_set_int(ra->props, "health", 100);
    return ra;
}

static void robot_update_visual_state(entities *e, doom_robot_npc *npc, const sdl3d_registered_actor *registered_actor)
{
    if (e == NULL || npc == NULL || registered_actor == NULL || npc->sprite_index < 0 ||
        npc->sprite_index >= e->sprites.count)
    {
        return;
    }

    sdl3d_sprite_actor *sprite = &e->sprites.actors[npc->sprite_index];
    sprite->position = registered_actor->position;

    if (npc->last_visual_state == npc->patrol.state)
        return;

    npc->last_visual_state = npc->patrol.state;
    if (npc->patrol.state == SDL3D_ACTOR_PATROL_WALK)
        sdl3d_sprite_actor_play_animation(sprite, e->enemy_walk_rotations, DOOM_ROBOT_WALK_FRAME_COUNT, 8.0f, true);
    else
        sdl3d_sprite_actor_stop_animation(sprite);
}

static void robot_update_npc(entities *e, doom_robot_npc *npc, const sdl3d_level *level, float dt)
{
    if (e == NULL || npc == NULL || npc->sprite_index < 0 || npc->sprite_index >= e->sprites.count ||
        e->registry == NULL)
    {
        return;
    }

    sdl3d_registered_actor *registered_actor = sdl3d_actor_registry_get(e->registry, npc->actor_id);
    if (registered_actor == NULL)
        return;

    sdl3d_sprite_actor *sprite = &e->sprites.actors[npc->sprite_index];
    sprite->position = registered_actor->position;
    robot_set_floor_position(sprite, level);
    registered_actor->position = sprite->position;

    robot_patrol_move_context move_context = {e, level};
    sdl3d_actor_patrol_controller_update(&npc->patrol, e->registry, e->bus, dt, robot_patrol_move, &move_context);
    robot_update_visual_state(e, npc, registered_actor);
}

bool entities_init(entities *e, const sdl3d_level *level, sdl3d_actor_registry *registry, sdl3d_signal_bus *bus)
{
    if (e == NULL)
        return false;

    SDL_zerop(e);
    e->registry = registry;
    e->bus = bus;

    /* Load enemy rotation sprites. */
    const char *rot_paths[SDL3D_SPRITE_ROTATION_COUNT] = {
        SDL3D_MEDIA_DIR "/sprites/skeletal_robot/south.png", SDL3D_MEDIA_DIR "/sprites/skeletal_robot/south-east.png",
        SDL3D_MEDIA_DIR "/sprites/skeletal_robot/east.png",  SDL3D_MEDIA_DIR "/sprites/skeletal_robot/north-east.png",
        SDL3D_MEDIA_DIR "/sprites/skeletal_robot/north.png", SDL3D_MEDIA_DIR "/sprites/skeletal_robot/north-west.png",
        SDL3D_MEDIA_DIR "/sprites/skeletal_robot/west.png",  SDL3D_MEDIA_DIR "/sprites/skeletal_robot/south-west.png",
    };
    for (int i = 0; i < SDL3D_SPRITE_ROTATION_COUNT; ++i)
    {
        if (!sdl3d_load_texture_from_file(rot_paths[i], &e->enemy_rot_tex[i]))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Sprite load failed: %s", SDL_GetError());
            return false;
        }
        configure_sprite_texture(&e->enemy_rot_tex[i]);
    }

    const char *rot_names[SDL3D_SPRITE_ROTATION_COUNT] = {
        "south", "south-east", "east", "north-east", "north", "north-west", "west", "south-west",
    };
    for (int frame = 0; frame < DOOM_ROBOT_WALK_FRAME_COUNT; ++frame)
    {
        for (int dir = 0; dir < SDL3D_SPRITE_ROTATION_COUNT; ++dir)
        {
            char path[512];
            SDL_snprintf(path, sizeof(path), SDL3D_MEDIA_DIR "/sprites/skeletal_robot/walking_frames/%s_%02d.png",
                         rot_names[dir], frame);
            if (!sdl3d_load_texture_from_file(path, &e->enemy_walk_tex[frame][dir]))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Robot walk sprite load failed: %s", SDL_GetError());
                return false;
            }
            configure_sprite_texture(&e->enemy_walk_tex[frame][dir]);
        }
    }

    if (!sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/sprites/health-pack.png", &e->health_tex))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Health sprite load failed: %s", SDL_GetError());
        return false;
    }
    configure_sprite_texture(&e->health_tex);

    if (!sdl3d_load_texture_from_file(SDL3D_MEDIA_DIR "/textures/radioactive-crate.png", &e->crate_tex))
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Crate texture load failed: %s", SDL_GetError());

    /* Skybox */
    const char *sky_paths[6] = {
        SDL3D_MEDIA_DIR "/skyboxes/sky_17/px.png", SDL3D_MEDIA_DIR "/skyboxes/sky_17/nx.png",
        SDL3D_MEDIA_DIR "/skyboxes/sky_17/py.png", SDL3D_MEDIA_DIR "/skyboxes/sky_17/ny.png",
        SDL3D_MEDIA_DIR "/skyboxes/sky_17/pz.png", SDL3D_MEDIA_DIR "/skyboxes/sky_17/nz.png",
    };
    for (int i = 0; i < 6; ++i)
    {
        if (!sdl3d_load_texture_from_file(sky_paths[i], &e->sky[i]))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Skybox load failed: %s", SDL_GetError());
            return false;
        }
        sdl3d_set_texture_wrap(&e->sky[i], SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);
    }

    /* Rotation set */
    for (int i = 0; i < SDL3D_SPRITE_ROTATION_COUNT; ++i)
        e->enemy_rotations.frames[i] = &e->enemy_rot_tex[i];
    for (int frame = 0; frame < DOOM_ROBOT_WALK_FRAME_COUNT; ++frame)
    {
        for (int dir = 0; dir < SDL3D_SPRITE_ROTATION_COUNT; ++dir)
            e->enemy_walk_rotations[frame].frames[dir] = &e->enemy_walk_tex[frame][dir];
    }

    /* Sprite scene */
    sdl3d_sprite_scene_init(&e->sprites);
    struct
    {
        const sdl3d_texture2d *tex;
        const sdl3d_sprite_rotation_set *rot;
        float x, y, z, w, h, amp, spd;
        bool robot;
        float walk_x, walk_z, patrol_distance, speed, idle_duration;
    } defs[] = {
        {NULL, &e->enemy_rotations, 5.8f, 0.0f, 6.8f, 3.4f, 5.2f, 0.0f, 0.0f, true, 1.0f, 0.0f, 2.4f, 0.75f, 1.4f},
        {&e->health_tex, NULL, 5.0f, 0.25f, 10.8f, 1.0f, 1.0f, 0.12f, 1.8f, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {NULL, &e->enemy_rotations, 4.2f, 0.0f, 21.5f, 3.2f, 4.8f, 0.0f, 0.0f, true, 1.0f, 0.0f, 3.0f, 0.65f, 1.8f},
        {&e->health_tex, NULL, 24.0f, 0.25f, 4.5f, 1.0f, 1.0f, 0.15f, 1.5f, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {NULL, &e->enemy_rotations, 24.0f, 0.0f, 19.0f, 3.6f, 5.6f, 0.0f, 0.0f, true, 0.0f, 1.0f, 4.0f, 0.8f, 1.2f},
        {&e->health_tex, NULL, 24.0f, 0.25f, 28.5f, 1.0f, 1.0f, 0.12f, 2.1f, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {NULL, &e->enemy_rotations, 24.0f, 0.0f, 37.5f, 3.4f, 5.2f, 0.0f, 0.0f, true, 0.0f, 1.0f, 4.0f, 0.7f, 1.6f},
        {&e->health_tex, NULL, 35.5f, 0.25f, 27.0f, 1.0f, 1.0f, 0.1f, 1.7f, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {NULL, &e->enemy_rotations, 24.0f, 0.0f, 72.0f, 3.6f, 5.6f, 0.0f, 0.0f, true, 1.0f, 0.0f, 8.0f, 1.0f, 1.5f},
        {&e->health_tex, NULL, 10.0f, 0.25f, 84.0f, 1.0f, 1.0f, 0.14f, 1.6f, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    };
    for (int i = 0; i < (int)SDL_arraysize(defs); ++i)
    {
        sdl3d_sprite_actor *a = sdl3d_sprite_scene_add(&e->sprites);
        if (a == NULL)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Sprite actor allocation failed");
            return false;
        }
        a->position = sdl3d_vec3_make(defs[i].x, defs[i].y, defs[i].z);
        a->size = (sdl3d_vec2){defs[i].w, defs[i].h};
        a->texture = defs[i].tex ? defs[i].tex : e->enemy_rotations.frames[0];
        a->rotations = defs[i].rot;
        a->bob_amplitude = defs[i].amp;
        a->bob_speed = defs[i].spd;
        a->bob_phase = (float)i;
        if (defs[i].robot)
        {
            a->visual_ground_offset = defs[i].h * ROBOT_SPRITE_FOOT_PADDING_RATIO;
            sdl3d_sprite_actor_set_facing_direction(a, defs[i].walk_x, defs[i].walk_z);
            robot_set_floor_position(a, level);
            if (e->robot_count < DOOM_ROBOT_NPC_COUNT)
            {
                doom_robot_npc *npc = &e->robots[e->robot_count];
                const int robot_number = e->robot_count + 1;
                sdl3d_registered_actor *registered_actor = register_robot(e, robot_number, a);
                npc->sprite_index = i;
                npc->actor_id = registered_actor != NULL ? registered_actor->id : 0;
                npc->last_visual_state = SDL3D_ACTOR_PATROL_IDLE;

                sdl3d_vec2 walk_direction = normalized_walk_direction(defs[i].walk_x, defs[i].walk_z);
                sdl3d_actor_patrol_config config = sdl3d_actor_patrol_default_config();
                config.speed = defs[i].speed;
                config.wait_time = defs[i].idle_duration;
                config.arrival_radius = ROBOT_DEFAULT_ARRIVAL_RADIUS;
                config.mode = SDL3D_ACTOR_PATROL_LOOP;
                config.start_idle = true;
                config.signals.waypoint_reached = DOOM_SIGNAL_ROBOT_WAYPOINT_REACHED;
                config.signals.loop_completed = DOOM_SIGNAL_ROBOT_LOOP_COMPLETED;
                config.signals.idle_started = DOOM_SIGNAL_ROBOT_IDLE_STARTED;
                config.signals.walk_started = DOOM_SIGNAL_ROBOT_WALK_STARTED;
                sdl3d_actor_patrol_controller_init(&npc->patrol, robot_number, npc->actor_id, &config);
                sdl3d_actor_patrol_controller_add_waypoint(&npc->patrol, a->position);
                sdl3d_actor_patrol_controller_add_waypoint(
                    &npc->patrol,
                    sdl3d_vec3_make(a->position.x + walk_direction.x * defs[i].patrol_distance, a->position.y,
                                    a->position.z + walk_direction.y * defs[i].patrol_distance));
                if (registered_actor != NULL)
                    sdl3d_actor_patrol_controller_sync_properties(&npc->patrol, registered_actor);
                e->robot_count++;
            }
        }
    }

    /* 3D models */
    e->has_dragon = sdl3d_load_model_from_file(SDL3D_MEDIA_DIR "/black_dragon/scene.gltf", &e->dragon_model);
    if (!e->has_dragon)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Dragon model load failed: %s", SDL_GetError());
    else
    {
        for (int i = 0; i < e->dragon_model.material_count; ++i)
        {
            for (int c = 0; c < 3; ++c)
                e->dragon_model.materials[i].albedo[c] = SDL_min(e->dragon_model.materials[i].albedo[c] * 3.0f, 1.0f);
            e->dragon_model.materials[i].albedo[3] = 1.0f;
        }
    }

    /* Scene with 3D actors */
    e->scene = sdl3d_create_scene();
    if (e->has_dragon && e->scene)
    {
        sdl3d_actor *dragon = sdl3d_scene_add_actor(e->scene, &e->dragon_model);
        sdl3d_actor_set_position(dragon, sdl3d_vec3_make(24.0f, 0.0f, 74.0f));
        sdl3d_actor_set_scale(dragon, sdl3d_vec3_make(2.0f, 2.0f, 2.0f));
        if (e->dragon_model.animation_count > 0)
            sdl3d_actor_play_animation(dragon, 0, true);
    }

    /* Register game objects in the managed-loop actor registry. */
    if (e->registry)
    {
        sdl3d_registered_actor *ra;

        ra = sdl3d_actor_registry_add(e->registry, "dragon");
        if (ra)
        {
            ra->position = sdl3d_vec3_make(24.0f, 0.0f, 74.0f);
            sdl3d_properties_set_string(ra->props, "classname", "npc_dragon");
            sdl3d_properties_set_int(ra->props, "health", 500);
        }
    }

    return true;
}

void entities_free(entities *e)
{
    if (e == NULL)
        return;
    sdl3d_sprite_scene_free(&e->sprites);
    sdl3d_destroy_scene(e->scene);
    if (e->has_dragon)
        sdl3d_free_model(&e->dragon_model);
    for (int i = 0; i < SDL3D_SPRITE_ROTATION_COUNT; ++i)
        sdl3d_free_texture(&e->enemy_rot_tex[i]);
    for (int frame = 0; frame < DOOM_ROBOT_WALK_FRAME_COUNT; ++frame)
    {
        for (int dir = 0; dir < SDL3D_SPRITE_ROTATION_COUNT; ++dir)
            sdl3d_free_texture(&e->enemy_walk_tex[frame][dir]);
    }
    sdl3d_free_texture(&e->health_tex);
    sdl3d_free_texture(&e->crate_tex);
    for (int i = 0; i < 6; ++i)
        sdl3d_free_texture(&e->sky[i]);
    e->registry = NULL;
    e->bus = NULL;
}

void entities_update(entities *e, const sdl3d_level *level, float dt, sdl3d_vec3 player_position)
{
    if (e == NULL)
        return;

    for (int i = 0; i < e->robot_count; ++i)
        robot_update_npc(e, &e->robots[i], level, dt);

    sdl3d_sprite_scene_update(&e->sprites, dt);
    if (e->scene)
    {
        int ac = sdl3d_scene_get_actor_count(e->scene);
        for (int i = 0; i < ac; i++)
        {
            sdl3d_actor *a = sdl3d_scene_get_actor_at(e->scene, i);
            if (a)
                sdl3d_actor_advance_animation(a, dt);
        }
    }
    sdl3d_actor_registry_update(e->registry, e->bus, player_position);
}
