#include "sdl3d/sprite_actor.h"

#include <SDL3/SDL_stdinc.h>

#include "sdl3d/drawing3d.h"
#include "sdl3d/math.h"

static const float SPRITE_PI = 3.14159265358979323846f;

/* Internal draw entry for depth sorting. */
typedef struct sprite_draw_entry
{
    const sdl3d_sprite_actor *actor;
    const sdl3d_texture2d *texture;
    sdl3d_vec3 position;
    float distance_sq;
} sprite_draw_entry;

static int compare_sprite_draws(const void *lhs, const void *rhs)
{
    const sprite_draw_entry *a = (const sprite_draw_entry *)lhs;
    const sprite_draw_entry *b = (const sprite_draw_entry *)rhs;
    if (a->distance_sq < b->distance_sq)
        return 1;
    if (a->distance_sq > b->distance_sq)
        return -1;
    return 0;
}

void sdl3d_sprite_scene_init(sdl3d_sprite_scene *scene)
{
    if (scene == NULL)
        return;
    scene->actors = NULL;
    scene->count = 0;
    scene->capacity = 0;
}

void sdl3d_sprite_scene_free(sdl3d_sprite_scene *scene)
{
    if (scene == NULL)
        return;
    SDL_free(scene->actors);
    scene->actors = NULL;
    scene->count = 0;
    scene->capacity = 0;
}

sdl3d_sprite_actor *sdl3d_sprite_scene_add(sdl3d_sprite_scene *scene)
{
    sdl3d_sprite_actor *actor;
    if (scene == NULL)
        return NULL;

    if (scene->count >= scene->capacity)
    {
        int new_cap = scene->capacity < 8 ? 8 : scene->capacity * 2;
        sdl3d_sprite_actor *new_buf =
            (sdl3d_sprite_actor *)SDL_realloc(scene->actors, (size_t)new_cap * sizeof(sdl3d_sprite_actor));
        if (new_buf == NULL)
            return NULL;
        scene->actors = new_buf;
        scene->capacity = new_cap;
    }

    actor = &scene->actors[scene->count];
    SDL_zerop(actor);
    actor->visible = true;
    actor->sector_id = -1;
    actor->tint = (sdl3d_color){255, 255, 255, 255};
    scene->count++;
    return actor;
}

void sdl3d_sprite_scene_remove(sdl3d_sprite_scene *scene, int index)
{
    if (scene == NULL || index < 0 || index >= scene->count)
        return;
    scene->count--;
    if (index < scene->count)
        scene->actors[index] = scene->actors[scene->count];
}

void sdl3d_sprite_scene_update(sdl3d_sprite_scene *scene, float dt)
{
    if (scene == NULL || dt <= 0.0f)
        return;
    for (int i = 0; i < scene->count; ++i)
        scene->actors[i].bob_phase += dt;
}

const sdl3d_texture2d *sdl3d_sprite_select_texture(const sdl3d_sprite_actor *actor, float cam_x, float cam_z)
{
    if (actor == NULL)
        return NULL;
    if (actor->rotations == NULL)
        return actor->texture;

    float dx = cam_x - actor->position.x;
    float dz = cam_z - actor->position.z;
    float angle = SDL_atan2f(dx, dz);
    float octant = (SPRITE_PI - angle) / (SPRITE_PI * 0.25f);
    int index = (int)SDL_floorf(octant + 0.5f) % SDL3D_SPRITE_ROTATION_COUNT;
    if (index < 0)
        index += SDL3D_SPRITE_ROTATION_COUNT;

    const sdl3d_texture2d *tex = actor->rotations->frames[index];
    return tex != NULL ? tex : actor->texture;
}

void sdl3d_sprite_scene_draw(sdl3d_sprite_scene *scene, sdl3d_render_context *context, sdl3d_vec3 camera_position,
                             const sdl3d_visibility_result *vis)
{
    sprite_draw_entry *draws;
    int draw_count = 0;

    if (scene == NULL || context == NULL || scene->count == 0)
        return;

    draws = (sprite_draw_entry *)SDL_malloc((size_t)scene->count * sizeof(sprite_draw_entry));
    if (draws == NULL)
        return;

    for (int i = 0; i < scene->count; ++i)
    {
        const sdl3d_sprite_actor *actor = &scene->actors[i];
        if (!actor->visible)
            continue;

        sdl3d_vec3 pos = actor->position;
        if (actor->bob_amplitude > 0.0f)
            pos.y += SDL_sinf(actor->bob_phase * actor->bob_speed) * actor->bob_amplitude;

        /* Portal cull. */
        if (vis != NULL && vis->sector_visible != NULL && actor->sector_id >= 0)
        {
            if (!vis->sector_visible[actor->sector_id])
                continue;
        }

        float dx = pos.x - camera_position.x;
        float dy = pos.y - camera_position.y;
        float dz = pos.z - camera_position.z;

        draws[draw_count].actor = actor;
        draws[draw_count].texture = sdl3d_sprite_select_texture(actor, camera_position.x, camera_position.z);
        draws[draw_count].position = pos;
        draws[draw_count].distance_sq = dx * dx + dy * dy + dz * dz;
        draw_count++;
    }

    SDL_qsort(draws, (size_t)draw_count, sizeof(draws[0]), compare_sprite_draws);

    for (int i = 0; i < draw_count; ++i)
    {
        sdl3d_draw_billboard(context, draws[i].texture, draws[i].position, draws[i].actor->size, draws[i].actor->tint);
    }

    SDL_free(draws);
}
