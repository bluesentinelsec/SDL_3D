/* Entity management for native model actors and textures not yet data-authored. */
#include "entities.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/animation.h"
#include "sdl3d/sdl3d.h"

bool entities_init(entities *e, const sdl3d_level *level, sdl3d_actor_registry *registry, sdl3d_signal_bus *bus)
{
    (void)level;

    if (e == NULL)
        return false;

    SDL_zerop(e);
    e->registry = registry;
    e->bus = bus;

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
    sdl3d_destroy_scene(e->scene);
    if (e->has_dragon)
        sdl3d_free_model(&e->dragon_model);
    for (int i = 0; i < 6; ++i)
        sdl3d_free_texture(&e->sky[i]);
    e->registry = NULL;
    e->bus = NULL;
}

void entities_update(entities *e, const sdl3d_level *level, float dt, sdl3d_vec3 player_position)
{
    (void)level;

    if (e == NULL)
        return;

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
