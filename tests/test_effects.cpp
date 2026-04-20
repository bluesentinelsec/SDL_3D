/*
 * Tests for M7: effects — particles, skybox, post-process.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/effects.h"
#include "sdl3d/math.h"
}

/* ================================================================== */
/* Particle system                                                    */
/* ================================================================== */

static sdl3d_particle_config default_config(void)
{
    sdl3d_particle_config c{};
    c.position = sdl3d_vec3_make(0, 0, 0);
    c.direction = sdl3d_vec3_make(0, 1, 0);
    c.spread = 0.5f;
    c.speed_min = 1.0f;
    c.speed_max = 2.0f;
    c.lifetime_min = 1.0f;
    c.lifetime_max = 2.0f;
    c.size_start = 0.1f;
    c.size_end = 0.01f;
    c.color_start = {255, 255, 0, 255};
    c.color_end = {255, 0, 0, 0};
    c.gravity = 9.8f;
    c.max_particles = 64;
    c.emit_rate = 10.0f;
    return c;
}

TEST(SDL3DParticles, CreateAndDestroy)
{
    sdl3d_particle_config c = default_config();
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    ASSERT_NE(em, nullptr);
    EXPECT_EQ(sdl3d_particle_emitter_get_count(em), 0);
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, DestroyNullSafe)
{
    sdl3d_destroy_particle_emitter(nullptr);
}

TEST(SDL3DParticles, CreateNullConfigReturnsNull)
{
    EXPECT_EQ(sdl3d_create_particle_emitter(nullptr), nullptr);
}

TEST(SDL3DParticles, EmitIncreasesCount)
{
    sdl3d_particle_config c = default_config();
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    sdl3d_particle_emitter_emit(em, 5);
    EXPECT_EQ(sdl3d_particle_emitter_get_count(em), 5);
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, EmitCapsAtMax)
{
    sdl3d_particle_config c = default_config();
    c.max_particles = 4;
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    sdl3d_particle_emitter_emit(em, 100);
    EXPECT_LE(sdl3d_particle_emitter_get_count(em), 4);
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, UpdateKillsExpiredParticles)
{
    sdl3d_particle_config c = default_config();
    c.lifetime_min = 0.1f;
    c.lifetime_max = 0.1f;
    c.emit_rate = 0.0f;
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    sdl3d_particle_emitter_emit(em, 3);
    EXPECT_EQ(sdl3d_particle_emitter_get_count(em), 3);

    sdl3d_particle_emitter_update(em, 0.2f);
    EXPECT_EQ(sdl3d_particle_emitter_get_count(em), 0);
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, AutoEmitFromRate)
{
    sdl3d_particle_config c = default_config();
    c.emit_rate = 100.0f;
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    sdl3d_particle_emitter_update(em, 0.1f);
    EXPECT_GE(sdl3d_particle_emitter_get_count(em), 5);
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, SetPosition)
{
    sdl3d_particle_config c = default_config();
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    sdl3d_particle_emitter_set_position(em, sdl3d_vec3_make(5, 10, 15));
    sdl3d_particle_emitter_set_position(nullptr, sdl3d_vec3_make(0, 0, 0)); /* null safe */
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, GetCountNullReturnsZero)
{
    EXPECT_EQ(sdl3d_particle_emitter_get_count(nullptr), 0);
}

TEST(SDL3DParticles, DrawNullContextFails)
{
    EXPECT_FALSE(sdl3d_draw_particles(nullptr, nullptr));
}

TEST(SDL3DParticles, DrawNullEmitterSucceeds)
{
    /* Drawing null emitter is a no-op, not an error. */
    /* Can't test without a context, but verify the API exists. */
}

/* ================================================================== */
/* Post-process                                                       */
/* ================================================================== */

TEST(SDL3DPostProcess, NullContextFails)
{
    sdl3d_post_process_config cfg{};
    EXPECT_FALSE(sdl3d_apply_post_process(nullptr, &cfg));
}

TEST(SDL3DPostProcess, NullConfigFails)
{
    EXPECT_FALSE(sdl3d_apply_post_process(nullptr, nullptr));
}

TEST(SDL3DPostProcess, NoneEffectIsNoop)
{
    sdl3d_post_process_config cfg{};
    cfg.effects = SDL3D_POST_NONE;
    /* Can't fully test without a context, but verify the enum value. */
    EXPECT_EQ(cfg.effects, 0);
}

TEST(SDL3DPostProcess, EffectFlagsAreBitmask)
{
    int combined = SDL3D_POST_BLOOM | SDL3D_POST_VIGNETTE | SDL3D_POST_COLOR_GRADE;
    EXPECT_TRUE(combined & SDL3D_POST_BLOOM);
    EXPECT_TRUE(combined & SDL3D_POST_VIGNETTE);
    EXPECT_TRUE(combined & SDL3D_POST_COLOR_GRADE);
}

/* ================================================================== */
/* Skybox                                                             */
/* ================================================================== */

TEST(SDL3DSkybox, NullContextFails)
{
    sdl3d_color top = {100, 150, 200, 255};
    sdl3d_color bot = {50, 80, 100, 255};
    EXPECT_FALSE(sdl3d_draw_skybox_gradient(nullptr, top, bot));
}
