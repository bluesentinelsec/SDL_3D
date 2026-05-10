/*
 * Tests for M5: skeletal animation — evaluation, quaternion math,
 * actor playback, skeleton/clip data structures.
 */

#include <gtest/gtest.h>

#include <cmath>

extern "C"
{
#include "slayer3d/animation.h"
#include "slayer3d/math.h"
#include "slayer3d/scene.h"
}

/* ================================================================== */
/* Helpers                                                            */
/* ================================================================== */

static slayer3d_joint make_joint(const char *name, int parent, float tx, float ty, float tz)
{
    slayer3d_joint j{};
    j.name = nullptr; /* Tests don't own the name string. */
    j.parent_index = parent;
    j.inverse_bind_matrix = slayer3d_mat4_identity();
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

static slayer3d_keyframe kf(float time, float x, float y, float z, float w)
{
    slayer3d_keyframe k{};
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

TEST(SLAYER3DAnimation, BindPoseSingleJoint)
{
    slayer3d_joint joints[1] = {make_joint("root", -1, 0, 0, 0)};
    slayer3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 1;

    slayer3d_mat4 matrices[1];
    ASSERT_TRUE(slayer3d_compute_bind_pose(&skel, matrices));

    /* With identity inverse_bind and identity local TRS, result is identity. */
    for (int i = 0; i < 16; ++i)
    {
        float expected = (i % 5 == 0) ? 1.0f : 0.0f;
        EXPECT_NEAR(matrices[0].m[i], expected, 1e-5f) << "m[" << i << "]";
    }
}

TEST(SLAYER3DAnimation, BindPoseParentChild)
{
    slayer3d_joint joints[2] = {
        make_joint("root", -1, 0, 1, 0),
        make_joint("child", 0, 0, 2, 0),
    };
    slayer3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 2;

    slayer3d_mat4 matrices[2];
    ASSERT_TRUE(slayer3d_compute_bind_pose(&skel, matrices));

    /* Root at y=1, child at y=1+2=3 (parent chain). */
    EXPECT_NEAR(matrices[0].m[13], 1.0f, 1e-5f);
    EXPECT_NEAR(matrices[1].m[13], 3.0f, 1e-5f);
}

TEST(SLAYER3DAnimation, BindPoseNullRejected)
{
    slayer3d_mat4 m;
    EXPECT_FALSE(slayer3d_compute_bind_pose(nullptr, &m));
    slayer3d_skeleton skel{};
    EXPECT_FALSE(slayer3d_compute_bind_pose(&skel, nullptr));
}

/* ================================================================== */
/* Animation evaluation                                               */
/* ================================================================== */

TEST(SLAYER3DAnimation, TranslationKeyframes)
{
    slayer3d_joint joints[1] = {make_joint("root", -1, 0, 0, 0)};
    slayer3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 1;

    slayer3d_keyframe keyframes[2] = {kf(0.0f, 0, 0, 0, 0), kf(1.0f, 10, 0, 0, 0)};
    slayer3d_anim_channel ch{};
    ch.joint_index = 0;
    ch.path = SLAYER3D_ANIM_TRANSLATION;
    ch.keyframes = keyframes;
    ch.keyframe_count = 2;

    slayer3d_animation_clip clip{};
    clip.duration = 1.0f;
    clip.channels = &ch;
    clip.channel_count = 1;

    slayer3d_mat4 matrices[1];

    /* At t=0: translation = (0,0,0). */
    ASSERT_TRUE(slayer3d_evaluate_animation(&skel, &clip, 0.0f, matrices));
    EXPECT_NEAR(matrices[0].m[12], 0.0f, 1e-5f);

    /* At t=0.5: translation = (5,0,0). */
    ASSERT_TRUE(slayer3d_evaluate_animation(&skel, &clip, 0.5f, matrices));
    EXPECT_NEAR(matrices[0].m[12], 5.0f, 1e-5f);

    /* At t=1.0: translation = (10,0,0). */
    ASSERT_TRUE(slayer3d_evaluate_animation(&skel, &clip, 1.0f, matrices));
    EXPECT_NEAR(matrices[0].m[12], 10.0f, 1e-5f);
}

TEST(SLAYER3DAnimation, RotationKeyframesSlerp)
{
    slayer3d_joint joints[1] = {make_joint("root", -1, 0, 0, 0)};
    slayer3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 1;

    /* Rotate 90 degrees around Y: quat (0, sin(45°), 0, cos(45°)). */
    float s45 = sinf(3.14159265f / 4.0f);
    float c45 = cosf(3.14159265f / 4.0f);
    slayer3d_keyframe keyframes[2] = {kf(0.0f, 0, 0, 0, 1), kf(1.0f, 0, s45, 0, c45)};
    slayer3d_anim_channel ch{};
    ch.joint_index = 0;
    ch.path = SLAYER3D_ANIM_ROTATION;
    ch.keyframes = keyframes;
    ch.keyframe_count = 2;

    slayer3d_animation_clip clip{};
    clip.duration = 1.0f;
    clip.channels = &ch;
    clip.channel_count = 1;

    slayer3d_mat4 matrices[1];

    /* At t=0: identity rotation. */
    ASSERT_TRUE(slayer3d_evaluate_animation(&skel, &clip, 0.0f, matrices));
    EXPECT_NEAR(matrices[0].m[0], 1.0f, 1e-4f);

    /* At t=1: 90° around Y. m[0] should be ~0 (cos90), m[8] should be ~1 (sin90). */
    ASSERT_TRUE(slayer3d_evaluate_animation(&skel, &clip, 1.0f, matrices));
    EXPECT_NEAR(matrices[0].m[0], 0.0f, 0.01f);
}

TEST(SLAYER3DAnimation, ScaleKeyframes)
{
    slayer3d_joint joints[1] = {make_joint("root", -1, 0, 0, 0)};
    slayer3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 1;

    slayer3d_keyframe keyframes[2] = {kf(0.0f, 1, 1, 1, 0), kf(1.0f, 2, 2, 2, 0)};
    slayer3d_anim_channel ch{};
    ch.joint_index = 0;
    ch.path = SLAYER3D_ANIM_SCALE;
    ch.keyframes = keyframes;
    ch.keyframe_count = 2;

    slayer3d_animation_clip clip{};
    clip.duration = 1.0f;
    clip.channels = &ch;
    clip.channel_count = 1;

    slayer3d_mat4 matrices[1];

    ASSERT_TRUE(slayer3d_evaluate_animation(&skel, &clip, 0.5f, matrices));
    /* Scale 1.5 at midpoint. m[0] = sx, m[5] = sy, m[10] = sz. */
    EXPECT_NEAR(matrices[0].m[0], 1.5f, 1e-4f);
    EXPECT_NEAR(matrices[0].m[5], 1.5f, 1e-4f);
    EXPECT_NEAR(matrices[0].m[10], 1.5f, 1e-4f);
}

TEST(SLAYER3DAnimation, ClampsBeyondDuration)
{
    slayer3d_joint joints[1] = {make_joint("root", -1, 0, 0, 0)};
    slayer3d_skeleton skel{};
    skel.joints = joints;
    skel.joint_count = 1;

    slayer3d_keyframe keyframes[2] = {kf(0.0f, 0, 0, 0, 0), kf(1.0f, 10, 0, 0, 0)};
    slayer3d_anim_channel ch{};
    ch.joint_index = 0;
    ch.path = SLAYER3D_ANIM_TRANSLATION;
    ch.keyframes = keyframes;
    ch.keyframe_count = 2;

    slayer3d_animation_clip clip{};
    clip.duration = 1.0f;
    clip.channels = &ch;
    clip.channel_count = 1;

    slayer3d_mat4 matrices[1];

    /* Beyond duration: clamps to last keyframe. */
    ASSERT_TRUE(slayer3d_evaluate_animation(&skel, &clip, 5.0f, matrices));
    EXPECT_NEAR(matrices[0].m[12], 10.0f, 1e-5f);

    /* Before start: clamps to first keyframe. */
    ASSERT_TRUE(slayer3d_evaluate_animation(&skel, &clip, -1.0f, matrices));
    EXPECT_NEAR(matrices[0].m[12], 0.0f, 1e-5f);
}

/* ================================================================== */
/* Actor animation playback                                           */
/* ================================================================== */

TEST(SLAYER3DActorAnimation, PlayAndStop)
{
    slayer3d_model model{};
    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *actor = slayer3d_scene_add_actor(scene, &model);

    EXPECT_FALSE(slayer3d_actor_is_animation_playing(actor));
    EXPECT_EQ(slayer3d_actor_get_animation_clip(actor), -1);

    ASSERT_TRUE(slayer3d_actor_play_animation(actor, 0, true));
    EXPECT_TRUE(slayer3d_actor_is_animation_playing(actor));
    EXPECT_EQ(slayer3d_actor_get_animation_clip(actor), 0);
    EXPECT_FLOAT_EQ(slayer3d_actor_get_animation_time(actor), 0.0f);

    ASSERT_TRUE(slayer3d_actor_stop_animation(actor));
    EXPECT_FALSE(slayer3d_actor_is_animation_playing(actor));

    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DActorAnimation, AdvanceTime)
{
    slayer3d_animation_clip clip{};
    clip.duration = 2.0f;

    slayer3d_model model{};
    model.animations = &clip;
    model.animation_count = 1;

    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *actor = slayer3d_scene_add_actor(scene, &model);

    slayer3d_actor_play_animation(actor, 0, false);
    slayer3d_actor_advance_animation(actor, 0.5f);
    EXPECT_NEAR(slayer3d_actor_get_animation_time(actor), 0.5f, 1e-5f);
    EXPECT_TRUE(slayer3d_actor_is_animation_playing(actor));

    /* Advance past duration — should stop (non-looping). */
    slayer3d_actor_advance_animation(actor, 2.0f);
    EXPECT_FALSE(slayer3d_actor_is_animation_playing(actor));
    EXPECT_NEAR(slayer3d_actor_get_animation_time(actor), 2.0f, 1e-5f);

    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DActorAnimation, LoopWraps)
{
    slayer3d_animation_clip clip{};
    clip.duration = 1.0f;

    slayer3d_model model{};
    model.animations = &clip;
    model.animation_count = 1;

    slayer3d_scene *scene = slayer3d_create_scene();
    slayer3d_actor *actor = slayer3d_scene_add_actor(scene, &model);

    slayer3d_actor_play_animation(actor, 0, true);
    slayer3d_actor_advance_animation(actor, 1.5f);
    EXPECT_TRUE(slayer3d_actor_is_animation_playing(actor));
    EXPECT_NEAR(slayer3d_actor_get_animation_time(actor), 0.5f, 1e-5f);

    slayer3d_destroy_scene(scene);
}

TEST(SLAYER3DActorAnimation, NullActorSafe)
{
    EXPECT_FALSE(slayer3d_actor_play_animation(nullptr, 0, false));
    EXPECT_FALSE(slayer3d_actor_stop_animation(nullptr));
    EXPECT_FALSE(slayer3d_actor_advance_animation(nullptr, 1.0f));
    EXPECT_FALSE(slayer3d_actor_is_animation_playing(nullptr));
    EXPECT_EQ(slayer3d_actor_get_animation_clip(nullptr), -1);
    EXPECT_FLOAT_EQ(slayer3d_actor_get_animation_time(nullptr), 0.0f);
}
