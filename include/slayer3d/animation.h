#ifndef SLAYER3D_ANIMATION_H
#define SLAYER3D_ANIMATION_H

#include <stdbool.h>

#include "slayer3d/math.h"
#include "slayer3d/scene.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ============================================================== */
    /* Skeleton and animation data types (stored in slayer3d_model)       */
    /* ============================================================== */

    typedef struct slayer3d_joint
    {
        char *name;
        int parent_index; /* -1 for root joints */
        slayer3d_mat4 inverse_bind_matrix;
        float local_translation[3];
        float local_rotation[4]; /* quaternion (x, y, z, w) */
        float local_scale[3];
    } slayer3d_joint;

    typedef struct slayer3d_skeleton
    {
        slayer3d_joint *joints;
        int joint_count;
    } slayer3d_skeleton;

    typedef enum slayer3d_anim_path
    {
        SLAYER3D_ANIM_TRANSLATION = 0,
        SLAYER3D_ANIM_ROTATION = 1,
        SLAYER3D_ANIM_SCALE = 2
    } slayer3d_anim_path;

    typedef struct slayer3d_keyframe
    {
        float time;
        float value[4]; /* 3 for translation/scale, 4 for rotation quat */
    } slayer3d_keyframe;

    typedef struct slayer3d_anim_channel
    {
        int joint_index;
        slayer3d_anim_path path;
        slayer3d_keyframe *keyframes;
        int keyframe_count;
    } slayer3d_anim_channel;

    typedef struct slayer3d_animation_clip
    {
        char *name;
        float duration;
        slayer3d_anim_channel *channels;
        int channel_count;
    } slayer3d_animation_clip;

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
    bool slayer3d_evaluate_animation(const slayer3d_skeleton *skeleton, const slayer3d_animation_clip *clip, float time,
                                     slayer3d_mat4 *out_joint_matrices);

    /*
     * Compute bind-pose joint matrices (identity pose).
     */
    bool slayer3d_compute_bind_pose(const slayer3d_skeleton *skeleton, slayer3d_mat4 *out_joint_matrices);

    /* ============================================================== */
    /* Actor animation playback                                       */
    /* ============================================================== */

    bool slayer3d_actor_play_animation(slayer3d_actor *actor, int clip_index, bool loop);
    bool slayer3d_actor_stop_animation(slayer3d_actor *actor);
    bool slayer3d_actor_advance_animation(slayer3d_actor *actor, float delta_time);
    bool slayer3d_actor_is_animation_playing(const slayer3d_actor *actor);
    int slayer3d_actor_get_animation_clip(const slayer3d_actor *actor);
    float slayer3d_actor_get_animation_time(const slayer3d_actor *actor);

#ifdef __cplusplus
}
#endif

#endif
