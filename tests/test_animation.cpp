/*
 * Tests for M5: skeletal animation — evaluation, quaternion math,
 * actor playback, skeleton/clip data structures.
 */

#include <gtest/gtest.h>

#include <cmath>

extern "C"
{
#include "sdl3d/animation.h"
#include "sdl3d/math.h"
#include "sdl3d/scene.h"
}

/* ================================================================== */
/* Helpers                                                            */
/* ================================================================== */

static sdl3d_joint make_joint(const char *name, int parent, float tx, float ty, float tz)
{
    sdl3d_joint j{};
    j.name = nullptr; /* Tests don't own the name string. */
    j.parent_index = parent;
    j.inverse_bind_matrix = sdl3d_mat4_identity();
    j.local_translation[0] = tx;
    j.local_translation[1] = ty;
    j.local_translation[2] = tz;
    j.local_rotation[0] = 0.0f;
    j.local_rotation[1] = 0.0f;
    j.local_rotation[2] = 0.0f;
    j.local_rotation[3] = 1.0f; /* identity quaternion */
    j.local_scale[0] = 1.0f;
    j.local_scale[1] = 1.0f;
    j.local_scale[2] = 1.0f;
    (void)name;
    return j;
}

static sdl3d_keyframe kf(float time, float x, float y, float z, float w)
{
    sdl3d_keyframe k{};
    k.time = time;
    k.value[0] = x;
    k.value[1] = y;
    k.value[2] = z;
    k.value[3] = w;
    return k;
}

/* ================================================================== */
/* Bind pose evaluation                                               */
/* ================================================================== */

TEST(SDL3DAnimation, BindPoseSingleJoint)
{
    sdl3d_joint joints[1] = {make_joint("root", -1, 0, 0, 0)};
    sdl3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 1;

    sdl3d_mat4 matrices[1];
    ASSERT_TRUE(sdl3d_compute_bind_pose(&skel, matrices));

    /* With identity inverse_bind and identity local TRS, result is identity. */
    for (int i = 0; i < 16; ++i)
    {
        float expected = (i % 5 == 0) ? 1.0f : 0.0f;
        EXPECT_NEAR(matrices[0].m[i], expected, 1e-5f) << "m[" << i << "]";
    }
}

TEST(SDL3DAnimation, BindPoseParentChild)
{
    sdl3d_joint joints[2] = {
        make_joint("root", -1, 0, 1, 0),
        make_joint("child", 0, 0, 2, 0),
    };
    sdl3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 2;

    sdl3d_mat4 matrices[2];
    ASSERT_TRUE(sdl3d_compute_bind_pose(&skel, matrices));

    /* Root at y=1, child at y=1+2=3 (parent chain). */
    EXPECT_NEAR(matrices[0].m[13], 1.0f, 1e-5f);
    EXPECT_NEAR(matrices[1].m[13], 3.0f, 1e-5f);
}

TEST(SDL3DAnimation, BindPoseNullRejected)
{
    sdl3d_mat4 m;
    EXPECT_FALSE(sdl3d_compute_bind_pose(nullptr, &m));
    sdl3d_skeleton skel{};
    EXPECT_FALSE(sdl3d_compute_bind_pose(&skel, nullptr));
}

/* ================================================================== */
/* Animation evaluation                                               */
/* ================================================================== */

TEST(SDL3DAnimation, TranslationKeyframes)
{
    sdl3d_joint joints[1] = {make_joint("root", -1, 0, 0, 0)};
    sdl3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 1;

    sdl3d_keyframe keyframes[2] = {kf(0.0f, 0, 0, 0, 0), kf(1.0f, 10, 0, 0, 0)};
    sdl3d_anim_channel ch{};
    ch.joint_index = 0;
    ch.path = SDL3D_ANIM_TRANSLATION;
    ch.keyframes = keyframes;
    ch.keyframe_count = 2;

    sdl3d_animation_clip clip{};
    clip.duration = 1.0f;
    clip.channels = &ch;
    clip.channel_count = 1;

    sdl3d_mat4 matrices[1];

    /* At t=0: translation = (0,0,0). */
    ASSERT_TRUE(sdl3d_evaluate_animation(&skel, &clip, 0.0f, matrices));
    EXPECT_NEAR(matrices[0].m[12], 0.0f, 1e-5f);

    /* At t=0.5: translation = (5,0,0). */
    ASSERT_TRUE(sdl3d_evaluate_animation(&skel, &clip, 0.5f, matrices));
    EXPECT_NEAR(matrices[0].m[12], 5.0f, 1e-5f);

    /* At t=1.0: translation = (10,0,0). */
    ASSERT_TRUE(sdl3d_evaluate_animation(&skel, &clip, 1.0f, matrices));
    EXPECT_NEAR(matrices[0].m[12], 10.0f, 1e-5f);
}

TEST(SDL3DAnimation, RotationKeyframesSlerp)
{
    sdl3d_joint joints[1] = {make_joint("root", -1, 0, 0, 0)};
    sdl3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 1;

    /* Rotate 90 degrees around Y: quat (0, sin(45°), 0, cos(45°)). */
    float s45 = sinf(3.14159265f / 4.0f);
    float c45 = cosf(3.14159265f / 4.0f);
    sdl3d_keyframe keyframes[2] = {kf(0.0f, 0, 0, 0, 1), kf(1.0f, 0, s45, 0, c45)};
    sdl3d_anim_channel ch{};
    ch.joint_index = 0;
    ch.path = SDL3D_ANIM_ROTATION;
    ch.keyframes = keyframes;
    ch.keyframe_count = 2;

    sdl3d_animation_clip clip{};
    clip.duration = 1.0f;
    clip.channels = &ch;
    clip.channel_count = 1;

    sdl3d_mat4 matrices[1];

    /* At t=0: identity rotation. */
    ASSERT_TRUE(sdl3d_evaluate_animation(&skel, &clip, 0.0f, matrices));
    EXPECT_NEAR(matrices[0].m[0], 1.0f, 1e-4f);

    /* At t=1: 90° around Y. m[0] should be ~0 (cos90), m[8] should be ~1 (sin90). */
    ASSERT_TRUE(sdl3d_evaluate_animation(&skel, &clip, 1.0f, matrices));
    EXPECT_NEAR(matrices[0].m[0], 0.0f, 0.01f);
}

TEST(SDL3DAnimation, ScaleKeyframes)
{
    sdl3d_joint joints[1] = {make_joint("root", -1, 0, 0, 0)};
    sdl3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 1;

    sdl3d_keyframe keyframes[2] = {kf(0.0f, 1, 1, 1, 0), kf(1.0f, 2, 2, 2, 0)};
    sdl3d_anim_channel ch{};
    ch.joint_index = 0;
    ch.path = SDL3D_ANIM_SCALE;
    ch.keyframes = keyframes;
    ch.keyframe_count = 2;

    sdl3d_animation_clip clip{};
    clip.duration = 1.0f;
    clip.channels = &ch;
    clip.channel_count = 1;

    sdl3d_mat4 matrices[1];

    ASSERT_TRUE(sdl3d_evaluate_animation(&skel, &clip, 0.5f, matrices));
    /* Scale 1.5 at midpoint. m[0] = sx, m[5] = sy, m[10] = sz. */
    EXPECT_NEAR(matrices[0].m[0], 1.5f, 1e-4f);
    EXPECT_NEAR(matrices[0].m[5], 1.5f, 1e-4f);
    EXPECT_NEAR(matrices[0].m[10], 1.5f, 1e-4f);
}

TEST(SDL3DAnimation, ClampsBeyondDuration)
{
    sdl3d_joint joints[1] = {make_joint("root", -1, 0, 0, 0)};
    sdl3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 1;

    sdl3d_keyframe keyframes[2] = {kf(0.0f, 0, 0, 0, 0), kf(1.0f, 10, 0, 0, 0)};
    sdl3d_anim_channel ch{};
    ch.joint_index = 0;
    ch.path = SDL3D_ANIM_TRANSLATION;
    ch.keyframes = keyframes;
    ch.keyframe_count = 2;

    sdl3d_animation_clip clip{};
    clip.duration = 1.0f;
    clip.channels = &ch;
    clip.channel_count = 1;

    sdl3d_mat4 matrices[1];

    /* Beyond duration: clamps to last keyframe. */
    ASSERT_TRUE(sdl3d_evaluate_animation(&skel, &clip, 5.0f, matrices));
    EXPECT_NEAR(matrices[0].m[12], 10.0f, 1e-5f);

    /* Before start: clamps to first keyframe. */
    ASSERT_TRUE(sdl3d_evaluate_animation(&skel, &clip, -1.0f, matrices));
    EXPECT_NEAR(matrices[0].m[12], 0.0f, 1e-5f);
}

/* ================================================================== */
/* Actor animation playback                                           */
/* ================================================================== */

TEST(SDL3DActorAnimation, PlayAndStop)
{
    sdl3d_model model{};
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);

    EXPECT_FALSE(sdl3d_actor_is_animation_playing(actor));
    EXPECT_EQ(sdl3d_actor_get_animation_clip(actor), -1);

    ASSERT_TRUE(sdl3d_actor_play_animation(actor, 0, true));
    EXPECT_TRUE(sdl3d_actor_is_animation_playing(actor));
    EXPECT_EQ(sdl3d_actor_get_animation_clip(actor), 0);
    EXPECT_FLOAT_EQ(sdl3d_actor_get_animation_time(actor), 0.0f);

    ASSERT_TRUE(sdl3d_actor_stop_animation(actor));
    EXPECT_FALSE(sdl3d_actor_is_animation_playing(actor));

    sdl3d_destroy_scene(scene);
}

TEST(SDL3DActorAnimation, AdvanceTime)
{
    sdl3d_animation_clip clip{};
    clip.duration = 2.0f;

    sdl3d_model model{};
    model.animations = &clip;
    model.animation_count = 1;

    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);

    sdl3d_actor_play_animation(actor, 0, false);
    sdl3d_actor_advance_animation(actor, 0.5f);
    EXPECT_NEAR(sdl3d_actor_get_animation_time(actor), 0.5f, 1e-5f);
    EXPECT_TRUE(sdl3d_actor_is_animation_playing(actor));

    /* Advance past duration — should stop (non-looping). */
    sdl3d_actor_advance_animation(actor, 2.0f);
    EXPECT_FALSE(sdl3d_actor_is_animation_playing(actor));
    EXPECT_NEAR(sdl3d_actor_get_animation_time(actor), 2.0f, 1e-5f);

    sdl3d_destroy_scene(scene);
}

TEST(SDL3DActorAnimation, LoopWraps)
{
    sdl3d_animation_clip clip{};
    clip.duration = 1.0f;

    sdl3d_model model{};
    model.animations = &clip;
    model.animation_count = 1;

    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);

    sdl3d_actor_play_animation(actor, 0, true);
    sdl3d_actor_advance_animation(actor, 1.5f);
    EXPECT_TRUE(sdl3d_actor_is_animation_playing(actor));
    EXPECT_NEAR(sdl3d_actor_get_animation_time(actor), 0.5f, 1e-5f);

    sdl3d_destroy_scene(scene);
}

TEST(SDL3DActorAnimation, NullActorSafe)
{
    EXPECT_FALSE(sdl3d_actor_play_animation(nullptr, 0, false));
    EXPECT_FALSE(sdl3d_actor_stop_animation(nullptr));
    EXPECT_FALSE(sdl3d_actor_advance_animation(nullptr, 1.0f));
    EXPECT_FALSE(sdl3d_actor_is_animation_playing(nullptr));
    EXPECT_EQ(sdl3d_actor_get_animation_clip(nullptr), -1);
    EXPECT_FLOAT_EQ(sdl3d_actor_get_animation_time(nullptr), 0.0f);
}
