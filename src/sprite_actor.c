#include "slayer3d/sprite_actor.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/drawing3d.h"
#include "slayer3d/math.h"

static const float SPRITE_PI = 3.14159265358979323846f;
static const float SPRITE_TWO_PI = 6.28318530717958647692f;

/* Internal draw entry for depth sorting. */
typedef struct sprite_draw_entry
{
    const slayer3d_sprite_actor *actor;
    const slayer3d_texture2d *texture;
    slayer3d_vec3 position;
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

void slayer3d_sprite_scene_init(slayer3d_sprite_scene *scene)
{
    if (scene == NULL)
        return;
    scene->actors = NULL;
    scene->count = 0;
    scene->capacity = 0;
}

void slayer3d_sprite_scene_free(slayer3d_sprite_scene *scene)
{
    if (scene == NULL)
        return;
    SDL_free(scene->actors);
    scene->actors = NULL;
    scene->count = 0;
    scene->capacity = 0;
}

slayer3d_sprite_actor *slayer3d_sprite_scene_add(slayer3d_sprite_scene *scene)
{
    slayer3d_sprite_actor *actor;
    if (scene == NULL)
        return NULL;

    if (scene->count >= scene->capacity)
    {
        int new_cap = scene->capacity < 8 ? 8 : scene->capacity * 2;
        slayer3d_sprite_actor *new_buf =
            (slayer3d_sprite_actor *)SDL_realloc(scene->actors, (size_t)new_cap * sizeof(slayer3d_sprite_actor));
        if (new_buf == NULL)
            return NULL;
        scene->actors = new_buf;
        scene->capacity = new_cap;
    }

    actor = &scene->actors[scene->count];
    SDL_zerop(actor);
    actor->visible = true;
    actor->sector_id = -1;
    actor->tint = (slayer3d_color){255, 255, 255, 255};
    scene->count++;
    return actor;
}

void slayer3d_sprite_scene_remove(slayer3d_sprite_scene *scene, int index)
{
    if (scene == NULL || index < 0 || index >= scene->count)
        return;
    scene->count--;
    if (index < scene->count)
        scene->actors[index] = scene->actors[scene->count];
}

void slayer3d_sprite_scene_update(slayer3d_sprite_scene *scene, float dt)
{
    if (scene == NULL || dt <= 0.0f)
        return;
    for (int i = 0; i < scene->count; ++i)
    {
        slayer3d_sprite_actor *actor = &scene->actors[i];
        actor->bob_phase += dt;
        if (actor->animation_frames != NULL && actor->animation_frame_count > 0 && actor->animation_fps > 0.0f)
        {
            actor->animation_time += dt;
            if (actor->animation_loop)
            {
                float duration = (float)actor->animation_frame_count / actor->animation_fps;
                if (duration > 0.0f && actor->animation_time >= duration)
                    actor->animation_time = SDL_fmodf(actor->animation_time, duration);
            }
            else
            {
                float last_frame_time = (float)(actor->animation_frame_count - 1) / actor->animation_fps;
                if (actor->animation_time > last_frame_time)
                    actor->animation_time = last_frame_time;
            }
        }
    }
}

void slayer3d_sprite_actor_play_animation(slayer3d_sprite_actor *actor, const slayer3d_sprite_rotation_set *frames,
                                          int frame_count, float fps, bool loop)
{
    if (actor == NULL)
        return;

    if (frames == NULL || frame_count <= 0 || fps <= 0.0f)
    {
        slayer3d_sprite_actor_stop_animation(actor);
        return;
    }

    actor->animation_frames = frames;
    actor->animation_frame_count = frame_count;
    actor->animation_fps = fps;
    actor->animation_loop = loop;
    actor->animation_time = 0.0f;
}

void slayer3d_sprite_actor_stop_animation(slayer3d_sprite_actor *actor)
{
    if (actor == NULL)
        return;
    actor->animation_frames = NULL;
    actor->animation_frame_count = 0;
    actor->animation_fps = 0.0f;
    actor->animation_loop = false;
    actor->animation_time = 0.0f;
}

int slayer3d_sprite_actor_current_animation_frame(const slayer3d_sprite_actor *actor)
{
    if (actor == NULL || actor->animation_frames == NULL || actor->animation_frame_count <= 0 ||
        actor->animation_fps <= 0.0f)
    {
        return 0;
    }

    int frame = (int)SDL_floorf(actor->animation_time * actor->animation_fps);
    if (actor->animation_loop)
    {
        frame %= actor->animation_frame_count;
        if (frame < 0)
            frame += actor->animation_frame_count;
        return frame;
    }

    if (frame < 0)
        return 0;
    if (frame >= actor->animation_frame_count)
        return actor->animation_frame_count - 1;
    return frame;
}

static const slayer3d_sprite_rotation_set *slayer3d_sprite_active_rotations(const slayer3d_sprite_actor *actor)
{
    if (actor == NULL)
        return NULL;
    if (actor->animation_frames != NULL && actor->animation_frame_count > 0 && actor->animation_fps > 0.0f)
        return &actor->animation_frames[slayer3d_sprite_actor_current_animation_frame(actor)];
    return actor->rotations;
}

static float slayer3d_sprite_wrap_angle(float radians)
{
    radians = SDL_fmodf(radians, SPRITE_TWO_PI);
    if (radians <= -SPRITE_PI)
        radians += SPRITE_TWO_PI;
    else if (radians > SPRITE_PI)
        radians -= SPRITE_TWO_PI;
    return radians;
}

slayer3d_vec3 slayer3d_sprite_actor_draw_position(const slayer3d_sprite_actor *actor)
{
    if (actor == NULL)
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);

    slayer3d_vec3 pos = actor->position;
    pos.y -= actor->visual_ground_offset;
    if (actor->bob_amplitude > 0.0f)
        pos.y += SDL_sinf(actor->bob_phase * actor->bob_speed) * actor->bob_amplitude;
    return pos;
}

void slayer3d_sprite_actor_set_facing_yaw(slayer3d_sprite_actor *actor, float yaw_radians)
{
    if (actor == NULL)
        return;
    actor->facing_yaw = slayer3d_sprite_wrap_angle(yaw_radians);
}

void slayer3d_sprite_actor_set_facing_direction(slayer3d_sprite_actor *actor, float direction_x, float direction_z)
{
    if (actor == NULL)
        return;

    float length_sq = direction_x * direction_x + direction_z * direction_z;
    if (length_sq <= 0.000001f)
        return;

    slayer3d_sprite_actor_set_facing_yaw(actor, SDL_atan2f(direction_x, -direction_z));
}

bool slayer3d_sprite_actor_can_stand_at(const slayer3d_sprite_actor *actor, const slayer3d_level *level,
                                        const slayer3d_sector *sectors, float target_x, float target_z,
                                        float step_height, float actor_height, float *out_floor_y)
{
    if (actor == NULL || level == NULL || sectors == NULL || step_height < 0.0f || actor_height <= 0.0f)
        return false;

    int sector = slayer3d_level_find_walkable_sector(level, sectors, target_x, target_z, actor->position.y, step_height,
                                                     actor_height);
    if (sector < 0)
        return false;

    if (out_floor_y != NULL)
        *out_floor_y = slayer3d_sector_floor_at(&sectors[sector], target_x, target_z);
    return true;
}

bool slayer3d_sprite_actor_snap_to_ground(slayer3d_sprite_actor *actor, const slayer3d_level *level,
                                          const slayer3d_sector *sectors, float step_height, float actor_height)
{
    if (actor == NULL || level == NULL || sectors == NULL || step_height < 0.0f || actor_height <= 0.0f)
        return false;

    float probe_y = actor->position.y + step_height;
    int sector =
        slayer3d_level_find_support_sector(level, sectors, actor->position.x, actor->position.z, probe_y, actor_height);
    if (sector < 0)
        return false;

    actor->position.y = slayer3d_sector_floor_at(&sectors[sector], actor->position.x, actor->position.z);
    return true;
}

const slayer3d_texture2d *slayer3d_sprite_select_texture(const slayer3d_sprite_actor *actor, float cam_x, float cam_z)
{
    if (actor == NULL)
        return NULL;
    const slayer3d_sprite_rotation_set *rotations = slayer3d_sprite_active_rotations(actor);
    if (rotations == NULL)
        return actor->texture;

    float dx = cam_x - actor->position.x;
    float dz = cam_z - actor->position.z;
    float yaw_to_camera = SDL_atan2f(dx, -dz);
    float relative_yaw = slayer3d_sprite_wrap_angle(yaw_to_camera - actor->facing_yaw);
    float octant = relative_yaw / (SPRITE_PI * 0.25f);
    int index = (int)SDL_floorf(octant + 0.5f) % SLAYER3D_SPRITE_ROTATION_COUNT;
    if (index < 0)
        index += SLAYER3D_SPRITE_ROTATION_COUNT;

    const slayer3d_texture2d *tex = rotations->frames[index];
    return tex != NULL ? tex : actor->texture;
}

void slayer3d_sprite_scene_draw(slayer3d_sprite_scene *scene, slayer3d_render_context *context,
                                slayer3d_vec3 camera_position, const slayer3d_visibility_result *vis)
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
        const slayer3d_sprite_actor *actor = &scene->actors[i];
        if (!actor->visible)
            continue;

        slayer3d_vec3 pos = slayer3d_sprite_actor_draw_position(actor);

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
        draws[draw_count].texture = slayer3d_sprite_select_texture(actor, camera_position.x, camera_position.z);
        draws[draw_count].position = pos;
        draws[draw_count].distance_sq = dx * dx + dy * dy + dz * dz;
        draw_count++;
    }

    SDL_qsort(draws, (size_t)draw_count, sizeof(draws[0]), compare_sprite_draws);

    for (int i = 0; i < draw_count; ++i)
    {
        const slayer3d_sprite_actor *actor = draws[i].actor;
        if ((actor->shader_vertex_source != NULL && actor->shader_vertex_source[0] != '\0') ||
            (actor->shader_fragment_source != NULL && actor->shader_fragment_source[0] != '\0'))
        {
            slayer3d_draw_billboard_shader_ex(context, draws[i].texture, draws[i].position, draws[i].actor->size,
                                              (slayer3d_vec2){0.5f, 0.0f}, SLAYER3D_BILLBOARD_UPRIGHT,
                                              draws[i].actor->tint, actor->lighting, actor->shader_vertex_source,
                                              actor->shader_fragment_source);
        }
        else
        {
            slayer3d_draw_billboard(context, draws[i].texture, draws[i].position, draws[i].actor->size,
                                    draws[i].actor->tint);
        }
    }

    SDL_free(draws);
}
