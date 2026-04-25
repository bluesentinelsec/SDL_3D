#include "sdl3d/scene.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/animation.h"
#include "sdl3d/collision.h"
#include "sdl3d/drawing3d.h"
#include "sdl3d/math.h"

#include "render_context_internal.h"

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

    /* Cached local-space bounding sphere derived from the model's mesh
     * AABBs at attach time. World bounds are produced per-frame by
     * applying the actor's position and scale. */
    sdl3d_vec3 local_bounds_center;
    float local_bounds_radius;
    bool local_bounds_valid;

    /* Optional sector for portal-based visibility culling; -1 = none. */
    int sector_id;
};

static void sdl3d_actor_compute_local_bounds(sdl3d_actor *actor)
{
    const sdl3d_model *model;
    sdl3d_bounding_box combined;
    bool have_box = false;
    sdl3d_vec3 center;
    float max_r_sq = 0.0f;

    actor->local_bounds_valid = false;
    actor->local_bounds_center = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    actor->local_bounds_radius = 0.0f;

    model = actor->model;
    if (model == NULL || model->meshes == NULL || model->mesh_count <= 0)
    {
        return;
    }

    SDL_zero(combined);
    for (int i = 0; i < model->mesh_count; ++i)
    {
        const sdl3d_mesh *mesh = &model->meshes[i];
        if (!mesh->has_local_bounds)
        {
            continue;
        }
        if (!have_box)
        {
            combined = mesh->local_bounds;
            have_box = true;
        }
        else
        {
            if (mesh->local_bounds.min.x < combined.min.x)
                combined.min.x = mesh->local_bounds.min.x;
            if (mesh->local_bounds.min.y < combined.min.y)
                combined.min.y = mesh->local_bounds.min.y;
            if (mesh->local_bounds.min.z < combined.min.z)
                combined.min.z = mesh->local_bounds.min.z;
            if (mesh->local_bounds.max.x > combined.max.x)
                combined.max.x = mesh->local_bounds.max.x;
            if (mesh->local_bounds.max.y > combined.max.y)
                combined.max.y = mesh->local_bounds.max.y;
            if (mesh->local_bounds.max.z > combined.max.z)
                combined.max.z = mesh->local_bounds.max.z;
        }
    }

    if (!have_box)
    {
        return;
    }

    center = sdl3d_vec3_make((combined.min.x + combined.max.x) * 0.5f, (combined.min.y + combined.max.y) * 0.5f,
                             (combined.min.z + combined.max.z) * 0.5f);

    /* Radius = max distance from center to any AABB corner. */
    for (int i = 0; i < 8; ++i)
    {
        float cx = (i & 1) ? combined.max.x : combined.min.x;
        float cy = (i & 2) ? combined.max.y : combined.min.y;
        float cz = (i & 4) ? combined.max.z : combined.min.z;
        float dx = cx - center.x;
        float dy = cy - center.y;
        float dz = cz - center.z;
        float r_sq = dx * dx + dy * dy + dz * dz;
        if (r_sq > max_r_sq)
        {
            max_r_sq = r_sq;
        }
    }

    actor->local_bounds_center = center;
    actor->local_bounds_radius = SDL_sqrtf(max_r_sq);
    actor->local_bounds_valid = true;
}

static bool sdl3d_actor_world_bounds(const sdl3d_actor *actor, sdl3d_sphere *out)
{
    float sx, sy, sz, max_scale;

    if (!actor->local_bounds_valid)
    {
        return false;
    }

    sx = actor->scale.x < 0.0f ? -actor->scale.x : actor->scale.x;
    sy = actor->scale.y < 0.0f ? -actor->scale.y : actor->scale.y;
    sz = actor->scale.z < 0.0f ? -actor->scale.z : actor->scale.z;
    max_scale = sx;
    if (sy > max_scale)
        max_scale = sy;
    if (sz > max_scale)
        max_scale = sz;

    /* The local center can be anywhere relative to the actor's pivot;
     * rotation about the pivot would sweep it around a sphere of radius
     * |local_center| * max_scale. Place the world sphere at the actor's
     * position and inflate the radius by that swept distance to stay
     * correct under arbitrary rotation. */
    {
        float lcx = actor->local_bounds_center.x;
        float lcy = actor->local_bounds_center.y;
        float lcz = actor->local_bounds_center.z;
        float center_dist = SDL_sqrtf(lcx * lcx + lcy * lcy + lcz * lcz);
        out->center = actor->position;
        out->radius = (center_dist + actor->local_bounds_radius) * max_scale;
    }
    return true;
}

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
    actor->sector_id = -1;

    sdl3d_actor_compute_local_bounds(actor);

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

void sdl3d_actor_set_sector(sdl3d_actor *actor, int sector_id)
{
    if (actor != NULL)
    {
        actor->sector_id = sector_id;
    }
}

int sdl3d_actor_get_sector(const sdl3d_actor *actor)
{
    if (actor == NULL)
    {
        return -1;
    }
    return actor->sector_id;
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

static bool sdl3d_draw_scene_impl(sdl3d_render_context *context, const sdl3d_scene *scene,
                                  const sdl3d_visibility_result *vis)
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

        /* Portal cull: actors with a known sector are skipped when that
         * sector is not in the visibility set. Actors with sector_id < 0
         * (the default) bypass this check and rely on frustum culling. */
        if (vis != NULL && vis->sector_visible != NULL && actor->sector_id >= 0)
        {
            if (!vis->sector_visible[actor->sector_id])
            {
                continue;
            }
        }

        /* Per-actor frustum cull: reject the whole actor before any
         * matrix push or per-mesh iteration. */
        if (context->frustum_planes_valid && actor->local_bounds_valid)
        {
            sdl3d_sphere world_bounds;
            if (sdl3d_actor_world_bounds(actor, &world_bounds) &&
                !sdl3d_sphere_intersects_frustum(world_bounds, context->frustum_planes))
            {
                continue;
            }
        }

        /* Animated actors: evaluate skeleton and draw skinned. */
        if (actor->anim_playing && actor->model->skeleton != NULL && actor->model->animation_count > 0)
        {
            int clip_idx = actor->anim_clip;
            if (clip_idx < 0 || clip_idx >= actor->model->animation_count)
                clip_idx = 0;
            const sdl3d_animation_clip *clip = &actor->model->animations[clip_idx];
            int jc = actor->model->skeleton->joint_count;
            sdl3d_mat4 *jm = (sdl3d_mat4 *)SDL_calloc((size_t)jc, sizeof(sdl3d_mat4));
            if (jm != NULL)
            {
                sdl3d_evaluate_animation(actor->model->skeleton, clip, actor->anim_time, jm);
                sdl3d_draw_model_skinned(context, actor->model, actor->position, actor->rotation_axis,
                                         actor->rotation_angle, actor->scale, actor->tint, jm);
                SDL_free(jm);
            }
        }
        else
        {
            if (!sdl3d_draw_model_ex(context, actor->model, actor->position, actor->rotation_axis,
                                     actor->rotation_angle, actor->scale, actor->tint))
            {
                return false;
            }
        }
    }

    return true;
}

bool sdl3d_draw_scene(sdl3d_render_context *context, const sdl3d_scene *scene)
{
    return sdl3d_draw_scene_impl(context, scene, NULL);
}

bool sdl3d_draw_scene_with_visibility(sdl3d_render_context *context, const sdl3d_scene *scene,
                                      const sdl3d_visibility_result *vis)
{
    return sdl3d_draw_scene_impl(context, scene, vis);
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
