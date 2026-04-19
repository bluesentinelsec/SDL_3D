#ifndef SDL3D_ANIMATION_H
#define SDL3D_ANIMATION_H

#include <stdbool.h>

#include "sdl3d/math.h"
#include "sdl3d/scene.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ============================================================== */
    /* Skeleton and animation data types (stored in sdl3d_model)       */
    /* ============================================================== */

    typedef struct sdl3d_joint
    {
        char *name;
        int parent_index; /* -1 for root joints */
        sdl3d_mat4 inverse_bind_matrix;
        float local_translation[3];
        float local_rotation[4]; /* quaternion (x, y, z, w) */
        float local_scale[3];
    } sdl3d_joint;

    typedef struct sdl3d_skeleton
    {
        sdl3d_joint *joints;
        int joint_count;
    } sdl3d_skeleton;

    typedef enum sdl3d_anim_path
    {
        SDL3D_ANIM_TRANSLATION = 0,
        SDL3D_ANIM_ROTATION = 1,
        SDL3D_ANIM_SCALE = 2
    } sdl3d_anim_path;

    typedef struct sdl3d_keyframe
    {
        float time;
        float value[4]; /* 3 for translation/scale, 4 for rotation quat */
    } sdl3d_keyframe;

    typedef struct sdl3d_anim_channel
    {
        int joint_index;
        sdl3d_anim_path path;
        sdl3d_keyframe *keyframes;
        int keyframe_count;
    } sdl3d_anim_channel;

    typedef struct sdl3d_animation_clip
    {
        char *name;
        float duration;
        sdl3d_anim_channel *channels;
        int channel_count;
    } sdl3d_animation_clip;

    /* ============================================================== */
    /* Runtime animation evaluation                                   */
    /* ============================================================== */

    /*
     * Evaluate an animation clip at the given time and compute world-
     * space joint matrices suitable for vertex skinning.
     *
     * `out_joint_matrices` must have room for skeleton->joint_count
     * mat4 entries. Each matrix transforms from bind space to the
     * posed world space (inverse_bind_matrix * joint_world_transform).
     */
    bool sdl3d_evaluate_animation(const sdl3d_skeleton *skeleton, const sdl3d_animation_clip *clip, float time,
                                  sdl3d_mat4 *out_joint_matrices);

    /*
     * Compute bind-pose joint matrices (identity pose).
     */
    bool sdl3d_compute_bind_pose(const sdl3d_skeleton *skeleton, sdl3d_mat4 *out_joint_matrices);

    /* ============================================================== */
    /* Actor animation playback                                       */
    /* ============================================================== */

    bool sdl3d_actor_play_animation(sdl3d_actor *actor, int clip_index, bool loop);
    bool sdl3d_actor_stop_animation(sdl3d_actor *actor);
    bool sdl3d_actor_advance_animation(sdl3d_actor *actor, float delta_time);
    bool sdl3d_actor_is_animation_playing(const sdl3d_actor *actor);
    int sdl3d_actor_get_animation_clip(const sdl3d_actor *actor);
    float sdl3d_actor_get_animation_time(const sdl3d_actor *actor);

#ifdef __cplusplus
}
#endif

#endif
