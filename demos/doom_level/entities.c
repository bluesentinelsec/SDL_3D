/* Entity management: sprite actors, 3D model actors, textures. */
#include "entities.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/animation.h"
#include "sdl3d/sdl3d.h"

static void configure_sprite_texture(sdl3d_texture2d *texture)
{
    sdl3d_set_texture_filter(texture, SDL3D_TEXTURE_FILTER_NEAREST);
    sdl3d_set_texture_wrap(texture, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);
}

bool entities_init(entities *e)
{
    SDL_zerop(e);

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

    /* Sprite scene */
    sdl3d_sprite_scene_init(&e->sprites);
    struct
    {
        const sdl3d_texture2d *tex;
        const sdl3d_sprite_rotation_set *rot;
        float x, y, z, w, h, amp, spd;
    } defs[] = {
        {NULL, &e->enemy_rotations, 5.8f, -1.05f, 6.8f, 3.4f, 5.2f, 0.10f, 7.0f},
        {&e->health_tex, NULL, 5.0f, 0.25f, 10.8f, 1.0f, 1.0f, 0.12f, 1.8f},
        {NULL, &e->enemy_rotations, 4.2f, -1.4f, 21.5f, 3.2f, 4.8f, 0.08f, 6.0f},
        {&e->health_tex, NULL, 24.0f, 0.25f, 4.5f, 1.0f, 1.0f, 0.15f, 1.5f},
        {NULL, &e->enemy_rotations, 24.0f, -1.05f, 19.0f, 3.6f, 5.6f, 0.09f, 6.5f},
        {&e->health_tex, NULL, 24.0f, 0.25f, 28.5f, 1.0f, 1.0f, 0.12f, 2.1f},
        {NULL, &e->enemy_rotations, 24.0f, -1.05f, 37.5f, 3.4f, 5.2f, 0.09f, 5.8f},
        {&e->health_tex, NULL, 35.5f, 0.25f, 27.0f, 1.0f, 1.0f, 0.1f, 1.7f},
        {NULL, &e->enemy_rotations, 24.0f, -1.05f, 72.0f, 3.6f, 5.6f, 0.11f, 5.5f},
        {&e->health_tex, NULL, 10.0f, 0.25f, 84.0f, 1.0f, 1.0f, 0.14f, 1.6f},
    };
    for (int i = 0; i < (int)SDL_arraysize(defs); ++i)
    {
        sdl3d_sprite_actor *a = sdl3d_sprite_scene_add(&e->sprites);
        a->position = sdl3d_vec3_make(defs[i].x, defs[i].y, defs[i].z);
        a->size = (sdl3d_vec2){defs[i].w, defs[i].h};
        a->texture = defs[i].tex ? defs[i].tex : e->enemy_rotations.frames[0];
        a->rotations = defs[i].rot;
        a->bob_amplitude = defs[i].amp;
        a->bob_speed = defs[i].spd;
        a->bob_phase = (float)i;
    }

    /* 3D models */
    e->has_robot = sdl3d_load_model_from_file(SDL3D_MEDIA_DIR "/simple_robot/simple_robot.glb", &e->robot_model);
    if (!e->has_robot)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Robot model load failed: %s", SDL_GetError());

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
    if (e->has_robot && e->scene)
    {
        sdl3d_actor *r1 = sdl3d_scene_add_actor(e->scene, &e->robot_model);
        sdl3d_actor_set_position(r1, sdl3d_vec3_make(5.0f, 0.0f, 12.0f));
        sdl3d_actor_set_scale(r1, sdl3d_vec3_make(0.8f, 0.8f, 0.8f));

        sdl3d_actor *r2 = sdl3d_scene_add_actor(e->scene, &e->robot_model);
        sdl3d_actor_set_position(r2, sdl3d_vec3_make(24.0f, 0.0f, 20.0f));
        sdl3d_actor_set_scale(r2, sdl3d_vec3_make(0.8f, 0.8f, 0.8f));
        sdl3d_actor_set_tint(r2, (sdl3d_color){255, 180, 180, 255});
    }
    if (e->has_dragon && e->scene)
    {
        sdl3d_actor *dragon = sdl3d_scene_add_actor(e->scene, &e->dragon_model);
        sdl3d_actor_set_position(dragon, sdl3d_vec3_make(24.0f, 0.0f, 74.0f));
        sdl3d_actor_set_scale(dragon, sdl3d_vec3_make(2.0f, 2.0f, 2.0f));
        if (e->dragon_model.animation_count > 0)
            sdl3d_actor_play_animation(dragon, 0, true);
    }

    return true;
}

void entities_free(entities *e)
{
    sdl3d_sprite_scene_free(&e->sprites);
    sdl3d_destroy_scene(e->scene);
    if (e->has_robot)
        sdl3d_free_model(&e->robot_model);
    if (e->has_dragon)
        sdl3d_free_model(&e->dragon_model);
    for (int i = 0; i < SDL3D_SPRITE_ROTATION_COUNT; ++i)
        sdl3d_free_texture(&e->enemy_rot_tex[i]);
    sdl3d_free_texture(&e->health_tex);
    sdl3d_free_texture(&e->crate_tex);
    for (int i = 0; i < 6; ++i)
        sdl3d_free_texture(&e->sky[i]);
}

void entities_update(entities *e, float dt)
{
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
}
