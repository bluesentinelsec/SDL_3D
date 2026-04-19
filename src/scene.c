#include "sdl3d/scene.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/drawing3d.h"

struct sdl3d_actor
{
    const sdl3d_model *model;
    sdl3d_vec3 position;
    sdl3d_vec3 rotation_axis;
    float rotation_angle;
    sdl3d_vec3 scale;
    sdl3d_color tint;
    bool visible;
    sdl3d_actor *next;
    sdl3d_actor *prev;

    /* Animation playback state. */
    int anim_clip;
    float anim_time;
    bool anim_playing;
    bool anim_looping;
};

struct sdl3d_scene
{
    sdl3d_actor *first;
    int actor_count;
};

sdl3d_scene *sdl3d_create_scene(void)
{
    sdl3d_scene *scene = (sdl3d_scene *)SDL_calloc(1, sizeof(sdl3d_scene));
    if (scene == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }
    return scene;
}

void sdl3d_destroy_scene(sdl3d_scene *scene)
{
    sdl3d_actor *actor;
    if (scene == NULL)
    {
        return;
    }
    actor = scene->first;
    while (actor != NULL)
    {
        sdl3d_actor *next = actor->next;
        SDL_free(actor);
        actor = next;
    }
    SDL_free(scene);
}

sdl3d_actor *sdl3d_scene_add_actor(sdl3d_scene *scene, const sdl3d_model *model)
{
    sdl3d_actor *actor;
    if (scene == NULL)
    {
        SDL_InvalidParamError("scene");
        return NULL;
    }
    if (model == NULL)
    {
        SDL_InvalidParamError("model");
        return NULL;
    }

    actor = (sdl3d_actor *)SDL_calloc(1, sizeof(sdl3d_actor));
    if (actor == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

    actor->model = model;
    actor->position = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    actor->rotation_axis = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    actor->rotation_angle = 0.0f;
    actor->scale = sdl3d_vec3_make(1.0f, 1.0f, 1.0f);
    actor->tint.r = 255;
    actor->tint.g = 255;
    actor->tint.b = 255;
    actor->tint.a = 255;
    actor->visible = true;
    actor->anim_clip = -1;
    actor->anim_time = 0.0f;
    actor->anim_playing = false;
    actor->anim_looping = false;

    /* Prepend to linked list. */
    actor->next = scene->first;
    actor->prev = NULL;
    if (scene->first != NULL)
    {
        scene->first->prev = actor;
    }
    scene->first = actor;
    scene->actor_count += 1;

    return actor;
}

void sdl3d_scene_remove_actor(sdl3d_scene *scene, sdl3d_actor *actor)
{
    if (scene == NULL || actor == NULL)
    {
        return;
    }

    if (actor->prev != NULL)
    {
        actor->prev->next = actor->next;
    }
    else
    {
        scene->first = actor->next;
    }
    if (actor->next != NULL)
    {
        actor->next->prev = actor->prev;
    }

    scene->actor_count -= 1;
    SDL_free(actor);
}

int sdl3d_scene_get_actor_count(const sdl3d_scene *scene)
{
    if (scene == NULL)
    {
        return 0;
    }
    return scene->actor_count;
}

sdl3d_actor *sdl3d_scene_get_actor_at(const sdl3d_scene *scene, int index)
{
    sdl3d_actor *actor;
    int i;
    if (scene == NULL || index < 0 || index >= scene->actor_count)
    {
        return NULL;
    }
    actor = scene->first;
    for (i = 0; i < index && actor != NULL; ++i)
    {
        actor = actor->next;
    }
    return actor;
}

void sdl3d_actor_set_position(sdl3d_actor *actor, sdl3d_vec3 position)
{
    if (actor != NULL)
    {
        actor->position = position;
    }
}

sdl3d_vec3 sdl3d_actor_get_position(const sdl3d_actor *actor)
{
    if (actor == NULL)
    {
        return sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    }
    return actor->position;
}

void sdl3d_actor_set_rotation(sdl3d_actor *actor, sdl3d_vec3 axis, float angle_radians)
{
    if (actor != NULL)
    {
        actor->rotation_axis = axis;
        actor->rotation_angle = angle_radians;
    }
}

void sdl3d_actor_set_scale(sdl3d_actor *actor, sdl3d_vec3 scale)
{
    if (actor != NULL)
    {
        actor->scale = scale;
    }
}

sdl3d_vec3 sdl3d_actor_get_scale(const sdl3d_actor *actor)
{
    if (actor == NULL)
    {
        return sdl3d_vec3_make(1.0f, 1.0f, 1.0f);
    }
    return actor->scale;
}

void sdl3d_actor_set_visible(sdl3d_actor *actor, bool visible)
{
    if (actor != NULL)
    {
        actor->visible = visible;
    }
}

bool sdl3d_actor_is_visible(const sdl3d_actor *actor)
{
    if (actor == NULL)
    {
        return false;
    }
    return actor->visible;
}

void sdl3d_actor_set_tint(sdl3d_actor *actor, sdl3d_color tint)
{
    if (actor != NULL)
    {
        actor->tint = tint;
    }
}

const sdl3d_model *sdl3d_actor_get_model(const sdl3d_actor *actor)
{
    if (actor == NULL)
    {
        return NULL;
    }
    return actor->model;
}

bool sdl3d_draw_scene(sdl3d_render_context *context, const sdl3d_scene *scene)
{
    const sdl3d_actor *actor;

    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (scene == NULL)
    {
        return SDL_InvalidParamError("scene");
    }

    for (actor = scene->first; actor != NULL; actor = actor->next)
    {
        if (!actor->visible || actor->model == NULL)
        {
            continue;
        }

        if (!sdl3d_draw_model_ex(context, actor->model, actor->position, actor->rotation_axis, actor->rotation_angle,
                                 actor->scale, actor->tint))
        {
            return false;
        }
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Animation state accessors (called from animation.c)                 */
/* ------------------------------------------------------------------ */

void sdl3d_actor_set_anim_state(sdl3d_actor *actor, int clip, float time, bool playing, bool looping)
{
    if (actor == NULL)
    {
        return;
    }
    actor->anim_clip = clip;
    actor->anim_time = time;
    actor->anim_playing = playing;
    actor->anim_looping = looping;
}

void sdl3d_actor_get_anim_state(const sdl3d_actor *actor, int *clip, float *time, bool *playing, bool *looping)
{
    if (actor == NULL)
    {
        if (clip)
        {
            *clip = -1;
        }
        if (time)
        {
            *time = 0.0f;
        }
        if (playing)
        {
            *playing = false;
        }
        if (looping)
        {
            *looping = false;
        }
        return;
    }
    if (clip)
    {
        *clip = actor->anim_clip;
    }
    if (time)
    {
        *time = actor->anim_time;
    }
    if (playing)
    {
        *playing = actor->anim_playing;
    }
    if (looping)
    {
        *looping = actor->anim_looping;
    }
}
