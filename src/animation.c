/*
 * Runtime skeletal animation: joint pose evaluation, quaternion math,
 * and actor animation playback.
 */

#include "sdl3d/animation.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

/* ------------------------------------------------------------------ */
/* Quaternion helpers                                                   */
/* ------------------------------------------------------------------ */

static void sdl3d_quat_normalize(float *q)
{
    float len = SDL_sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 1e-7f)
    {
        float inv = 1.0f / len;
        q[0] *= inv;
        q[1] *= inv;
        q[2] *= inv;
        q[3] *= inv;
    }
}

static void sdl3d_quat_slerp(const float *a, const float *b, float t, float *out)
{
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float nb[4];
    float theta, sin_theta, wa, wb;

    /* Ensure shortest path. */
    if (dot < 0.0f)
    {
        nb[0] = -b[0];
        nb[1] = -b[1];
        nb[2] = -b[2];
        nb[3] = -b[3];
        dot = -dot;
    }
    else
    {
        nb[0] = b[0];
        nb[1] = b[1];
        nb[2] = b[2];
        nb[3] = b[3];
    }

    if (dot > 0.9995f)
    {
        /* Linear interpolation for nearly identical quaternions. */
        out[0] = a[0] + (nb[0] - a[0]) * t;
        out[1] = a[1] + (nb[1] - a[1]) * t;
        out[2] = a[2] + (nb[2] - a[2]) * t;
        out[3] = a[3] + (nb[3] - a[3]) * t;
        sdl3d_quat_normalize(out);
        return;
    }

    theta = SDL_acosf(dot);
    sin_theta = SDL_sinf(theta);
    wa = SDL_sinf((1.0f - t) * theta) / sin_theta;
    wb = SDL_sinf(t * theta) / sin_theta;
    out[0] = wa * a[0] + wb * nb[0];
    out[1] = wa * a[1] + wb * nb[1];
    out[2] = wa * a[2] + wb * nb[2];
    out[3] = wa * a[3] + wb * nb[3];
}

static sdl3d_mat4 sdl3d_mat4_from_trs(const float *t, const float *r, const float *s)
{
    /* Build a matrix from translation, rotation (quaternion), scale. */
    float x = r[0], y = r[1], z = r[2], w = r[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;
    sdl3d_mat4 m;

    m.m[0] = (1.0f - (yy + zz)) * s[0];
    m.m[1] = (xy + wz) * s[0];
    m.m[2] = (xz - wy) * s[0];
    m.m[3] = 0.0f;
    m.m[4] = (xy - wz) * s[1];
    m.m[5] = (1.0f - (xx + zz)) * s[1];
    m.m[6] = (yz + wx) * s[1];
    m.m[7] = 0.0f;
    m.m[8] = (xz + wy) * s[2];
    m.m[9] = (yz - wx) * s[2];
    m.m[10] = (1.0f - (xx + yy)) * s[2];
    m.m[11] = 0.0f;
    m.m[12] = t[0];
    m.m[13] = t[1];
    m.m[14] = t[2];
    m.m[15] = 1.0f;

    return m;
}

/* ------------------------------------------------------------------ */
/* Keyframe sampling                                                   */
/* ------------------------------------------------------------------ */

static void sdl3d_sample_channel(const sdl3d_anim_channel *ch, float time, float *out, int components)
{
    int i;
    float t;

    if (ch->keyframe_count <= 0)
    {
        return;
    }
    if (ch->keyframe_count == 1 || time <= ch->keyframes[0].time)
    {
        for (i = 0; i < components; ++i)
        {
            out[i] = ch->keyframes[0].value[i];
        }
        return;
    }
    if (time >= ch->keyframes[ch->keyframe_count - 1].time)
    {
        for (i = 0; i < components; ++i)
        {
            out[i] = ch->keyframes[ch->keyframe_count - 1].value[i];
        }
        return;
    }

    /* Find the two keyframes bracketing `time`. */
    for (i = 0; i < ch->keyframe_count - 1; ++i)
    {
        if (time < ch->keyframes[i + 1].time)
        {
            break;
        }
    }

    {
        float t0 = ch->keyframes[i].time;
        float t1 = ch->keyframes[i + 1].time;
        t = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
    }

    if (ch->path == SDL3D_ANIM_ROTATION)
    {
        sdl3d_quat_slerp(ch->keyframes[i].value, ch->keyframes[i + 1].value, t, out);
    }
    else
    {
        for (int c = 0; c < components; ++c)
        {
            out[c] = ch->keyframes[i].value[c] + (ch->keyframes[i + 1].value[c] - ch->keyframes[i].value[c]) * t;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Animation evaluation                                                */
/* ------------------------------------------------------------------ */

bool sdl3d_evaluate_animation(const sdl3d_skeleton *skeleton, const sdl3d_animation_clip *clip, float time,
                              sdl3d_mat4 *out_joint_matrices)
{
    float *local_t = NULL;
    float *local_r = NULL;
    float *local_s = NULL;
    sdl3d_mat4 *world = NULL;
    int jc;

    if (skeleton == NULL || out_joint_matrices == NULL)
    {
        return SDL_InvalidParamError("skeleton or out_joint_matrices");
    }

    jc = skeleton->joint_count;
    if (jc <= 0)
    {
        return true;
    }

    /* Allocate temp arrays for local TRS per joint. */
    local_t = (float *)SDL_malloc((size_t)jc * 3 * sizeof(float));
    local_r = (float *)SDL_malloc((size_t)jc * 4 * sizeof(float));
    local_s = (float *)SDL_malloc((size_t)jc * 3 * sizeof(float));
    world = (sdl3d_mat4 *)SDL_malloc((size_t)jc * sizeof(sdl3d_mat4));
    if (!local_t || !local_r || !local_s || !world)
    {
        SDL_free(local_t);
        SDL_free(local_r);
        SDL_free(local_s);
        SDL_free(world);
        return SDL_OutOfMemory();
    }

    /* Initialize from bind pose. */
    for (int j = 0; j < jc; ++j)
    {
        SDL_memcpy(&local_t[j * 3], skeleton->joints[j].local_translation, 3 * sizeof(float));
        SDL_memcpy(&local_r[j * 4], skeleton->joints[j].local_rotation, 4 * sizeof(float));
        SDL_memcpy(&local_s[j * 3], skeleton->joints[j].local_scale, 3 * sizeof(float));
    }

    /* Apply animation channels. */
    if (clip != NULL)
    {
        for (int c = 0; c < clip->channel_count; ++c)
        {
            const sdl3d_anim_channel *ch = &clip->channels[c];
            if (ch->joint_index < 0 || ch->joint_index >= jc)
            {
                continue;
            }
            switch (ch->path)
            {
            case SDL3D_ANIM_TRANSLATION:
                sdl3d_sample_channel(ch, time, &local_t[ch->joint_index * 3], 3);
                break;
            case SDL3D_ANIM_ROTATION:
                sdl3d_sample_channel(ch, time, &local_r[ch->joint_index * 4], 4);
                break;
            case SDL3D_ANIM_SCALE:
                sdl3d_sample_channel(ch, time, &local_s[ch->joint_index * 3], 3);
                break;
            }
        }
    }

    /* Compute world transforms (parent-first order). */
    for (int j = 0; j < jc; ++j)
    {
        sdl3d_mat4 local = sdl3d_mat4_from_trs(&local_t[j * 3], &local_r[j * 4], &local_s[j * 3]);
        int parent = skeleton->joints[j].parent_index;
        if (parent >= 0 && parent < jc)
        {
            world[j] = sdl3d_mat4_multiply(world[parent], local);
        }
        else
        {
            world[j] = local;
        }
    }

    /* Final skinning matrices: world * inverse_bind. */
    for (int j = 0; j < jc; ++j)
    {
        out_joint_matrices[j] = sdl3d_mat4_multiply(world[j], skeleton->joints[j].inverse_bind_matrix);
    }

    SDL_free(local_t);
    SDL_free(local_r);
    SDL_free(local_s);
    SDL_free(world);
    return true;
}

bool sdl3d_compute_bind_pose(const sdl3d_skeleton *skeleton, sdl3d_mat4 *out_joint_matrices)
{
    return sdl3d_evaluate_animation(skeleton, NULL, 0.0f, out_joint_matrices);
}

/* ------------------------------------------------------------------ */
/* Actor animation playback                                            */
/* ------------------------------------------------------------------ */

/* These functions access actor fields defined in scene.c. We use the
 * public getter/setter API plus new animation-specific fields that
 * we add to the actor struct via scene_internal.h. For now, we store
 * animation state in a simple struct accessed via opaque helpers
 * declared in scene.c. */

/* Forward declarations — implemented in scene.c */
extern void sdl3d_actor_set_anim_state(sdl3d_actor *actor, int clip, float time, bool playing, bool looping);
extern void sdl3d_actor_get_anim_state(const sdl3d_actor *actor, int *clip, float *time, bool *playing, bool *looping);

bool sdl3d_actor_play_animation(sdl3d_actor *actor, int clip_index, bool loop)
{
    if (actor == NULL)
    {
        return SDL_InvalidParamError("actor");
    }
    sdl3d_actor_set_anim_state(actor, clip_index, 0.0f, true, loop);
    return true;
}

bool sdl3d_actor_stop_animation(sdl3d_actor *actor)
{
    if (actor == NULL)
    {
        return SDL_InvalidParamError("actor");
    }
    sdl3d_actor_set_anim_state(actor, -1, 0.0f, false, false);
    return true;
}

bool sdl3d_actor_advance_animation(sdl3d_actor *actor, float delta_time)
{
    int clip;
    float time;
    bool playing, looping;
    const sdl3d_model *model;

    if (actor == NULL)
    {
        return SDL_InvalidParamError("actor");
    }

    sdl3d_actor_get_anim_state(actor, &clip, &time, &playing, &looping);
    if (!playing || clip < 0)
    {
        return true;
    }

    model = sdl3d_actor_get_model(actor);
    if (model == NULL || clip >= model->animation_count)
    {
        return true;
    }

    time += delta_time;
    if (time > model->animations[clip].duration)
    {
        if (looping)
        {
            float dur = model->animations[clip].duration;
            if (dur > 0.0f)
            {
                time = SDL_fmodf(time, dur);
            }
            else
            {
                time = 0.0f;
            }
        }
        else
        {
            time = model->animations[clip].duration;
            playing = false;
        }
    }

    sdl3d_actor_set_anim_state(actor, clip, time, playing, looping);
    return true;
}

bool sdl3d_actor_is_animation_playing(const sdl3d_actor *actor)
{
    int clip;
    float time;
    bool playing, looping;
    if (actor == NULL)
    {
        return false;
    }
    sdl3d_actor_get_anim_state(actor, &clip, &time, &playing, &looping);
    return playing;
}

int sdl3d_actor_get_animation_clip(const sdl3d_actor *actor)
{
    int clip;
    float time;
    bool playing, looping;
    if (actor == NULL)
    {
        return -1;
    }
    sdl3d_actor_get_anim_state(actor, &clip, &time, &playing, &looping);
    return clip;
}

float sdl3d_actor_get_animation_time(const sdl3d_actor *actor)
{
    int clip;
    float time;
    bool playing, looping;
    if (actor == NULL)
    {
        return 0.0f;
    }
    sdl3d_actor_get_anim_state(actor, &clip, &time, &playing, &looping);
    return time;
}
