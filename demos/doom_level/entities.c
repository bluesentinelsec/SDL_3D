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

static void robot_enter_idle(sdl3d_sprite_actor *actor, doom_robot_npc *npc)
{
    if (actor == NULL || npc == NULL)
        return;
    npc->state = DOOM_ROBOT_AI_IDLE;
    npc->state_timer = npc->idle_duration;
    sdl3d_sprite_actor_stop_animation(actor);
}

static void robot_enter_walk(entities *e, sdl3d_sprite_actor *actor, doom_robot_npc *npc)
{
    if (e == NULL || actor == NULL || npc == NULL)
        return;
    npc->state = DOOM_ROBOT_AI_WALK;
    npc->state_timer = npc->walk_duration;
    sdl3d_sprite_actor_play_animation(actor, e->enemy_walk_rotations, DOOM_ROBOT_WALK_FRAME_COUNT, 8.0f, true);
}

static bool robot_move_toward_target(sdl3d_sprite_actor *actor, doom_robot_npc *npc, const sdl3d_level *level, float dt)
{
    if (actor == NULL || npc == NULL || level == NULL || npc->target_patrol_point < 0 ||
        npc->target_patrol_point >= DOOM_ROBOT_PATROL_POINT_COUNT)
    {
        return false;
    }

    sdl3d_vec3 target = npc->patrol_points[npc->target_patrol_point];
    float dx = target.x - actor->position.x;
    float dz = target.z - actor->position.z;
    float distance_sq = dx * dx + dz * dz;
    float arrival_radius_sq = npc->arrival_radius * npc->arrival_radius;
    if (distance_sq <= arrival_radius_sq)
        return false;

    float distance = SDL_sqrtf(distance_sq);
    float step = npc->speed * dt;
    if (step > distance)
        step = distance;

    float move_x = dx / distance;
    float move_z = dz / distance;
    float next_x = actor->position.x + move_x * step;
    float next_z = actor->position.z + move_z * step;
    float floor_y = actor->position.y;
    if (!sdl3d_sprite_actor_can_stand_at(actor, level, g_sectors, next_x, next_z, ROBOT_GROUND_STEP_HEIGHT,
                                         ROBOT_COLLISION_HEIGHT, &floor_y))
    {
        return false;
    }

    actor->position = sdl3d_vec3_make(next_x, floor_y, next_z);
    sdl3d_sprite_actor_set_facing_direction(actor, move_x, move_z);
    return true;
}

static void robot_advance_patrol_target(doom_robot_npc *npc)
{
    if (npc == NULL)
        return;
    npc->target_patrol_point = (npc->target_patrol_point + 1) % DOOM_ROBOT_PATROL_POINT_COUNT;
}

static void robot_update_npc(entities *e, doom_robot_npc *npc, const sdl3d_level *level, float dt)
{
    if (e == NULL || npc == NULL || npc->sprite_index < 0 || npc->sprite_index >= e->sprites.count)
        return;

    sdl3d_sprite_actor *actor = &e->sprites.actors[npc->sprite_index];
    robot_set_floor_position(actor, level);

    if (npc->state == DOOM_ROBOT_AI_WALK)
    {
        if (!robot_move_toward_target(actor, npc, level, dt))
        {
            robot_advance_patrol_target(npc);
            robot_enter_idle(actor, npc);
            return;
        }
    }

    npc->state_timer -= dt;
    if (npc->state_timer > 0.0f)
        return;

    if (npc->state == DOOM_ROBOT_AI_IDLE)
    {
        robot_enter_walk(e, actor, npc);
    }
    else
    {
        robot_enter_idle(actor, npc);
    }
}

static void register_robot(entities *e, int robot_number, const sdl3d_sprite_actor *actor)
{
    if (e == NULL || e->registry == NULL || actor == NULL)
        return;

    char name[32];
    SDL_snprintf(name, sizeof(name), "robot_%d", robot_number);
    sdl3d_registered_actor *ra = sdl3d_actor_registry_add(e->registry, name);
    if (ra == NULL)
        return;

    ra->position = actor->position;
    sdl3d_properties_set_string(ra->props, "classname", "npc_robot");
    sdl3d_properties_set_int(ra->props, "health", 100);
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
        float walk_x, walk_z, patrol_distance, speed, idle_duration, walk_duration;
    } defs[] = {
        {NULL, &e->enemy_rotations, 5.8f, 0.0f, 6.8f, 3.4f, 5.2f, 0.0f, 0.0f, true, 1.0f, 0.0f, 2.4f, 0.75f, 1.4f,
         2.4f},
        {&e->health_tex, NULL, 5.0f, 0.25f, 10.8f, 1.0f, 1.0f, 0.12f, 1.8f, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {NULL, &e->enemy_rotations, 4.2f, 0.0f, 21.5f, 3.2f, 4.8f, 0.0f, 0.0f, true, 1.0f, 0.0f, 3.0f, 0.65f, 1.8f,
         2.8f},
        {&e->health_tex, NULL, 24.0f, 0.25f, 4.5f, 1.0f, 1.0f, 0.15f, 1.5f, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {NULL, &e->enemy_rotations, 24.0f, 0.0f, 19.0f, 3.6f, 5.6f, 0.0f, 0.0f, true, 0.0f, 1.0f, 4.0f, 0.8f, 1.2f,
         2.0f},
        {&e->health_tex, NULL, 24.0f, 0.25f, 28.5f, 1.0f, 1.0f, 0.12f, 2.1f, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {NULL, &e->enemy_rotations, 24.0f, 0.0f, 37.5f, 3.4f, 5.2f, 0.0f, 0.0f, true, 0.0f, 1.0f, 4.0f, 0.7f, 1.6f,
         2.6f},
        {&e->health_tex, NULL, 35.5f, 0.25f, 27.0f, 1.0f, 1.0f, 0.1f, 1.7f, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {NULL, &e->enemy_rotations, 24.0f, 0.0f, 72.0f, 3.6f, 5.6f, 0.0f, 0.0f, true, 1.0f, 0.0f, 8.0f, 1.0f, 1.5f,
         3.0f},
        {&e->health_tex, NULL, 10.0f, 0.25f, 84.0f, 1.0f, 1.0f, 0.14f, 1.6f, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
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
                npc->sprite_index = i;
                npc->state = DOOM_ROBOT_AI_IDLE;
                npc->state_timer = defs[i].idle_duration;
                npc->idle_duration = defs[i].idle_duration;
                npc->walk_duration = defs[i].walk_duration;
                npc->speed = defs[i].speed;
                npc->walk_direction = normalized_walk_direction(defs[i].walk_x, defs[i].walk_z);
                npc->patrol_points[0] = a->position;
                npc->patrol_points[1] =
                    sdl3d_vec3_make(a->position.x + npc->walk_direction.x * defs[i].patrol_distance, a->position.y,
                                    a->position.z + npc->walk_direction.y * defs[i].patrol_distance);
                npc->target_patrol_point = 1;
                npc->arrival_radius = ROBOT_DEFAULT_ARRIVAL_RADIUS;
                e->robot_count++;
                register_robot(e, e->robot_count, a);
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
    if (e->registry)
    {
        for (int i = 0; i < e->robot_count; ++i)
        {
            const doom_robot_npc *npc = &e->robots[i];
            if (npc->sprite_index < 0 || npc->sprite_index >= e->sprites.count)
                continue;

            char name[32];
            SDL_snprintf(name, sizeof(name), "robot_%d", i + 1);
            sdl3d_registered_actor *ra = sdl3d_actor_registry_find(e->registry, name);
            if (ra)
                ra->position = e->sprites.actors[npc->sprite_index].position;
        }
    }
    sdl3d_actor_registry_update(e->registry, e->bus, player_position);
}
