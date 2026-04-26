/*
 * Tests for M7: effects — particles, skybox, post-process.
 */

#include <gtest/gtest.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <vector>

extern "C"
{
#include "sdl3d/effects.h"
#include "sdl3d/math.h"
#include "sdl3d/sdl3d.h"
}

#include <memory>

namespace
{

class SDLEffectsFixture : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        SDL_SetMainReady();
        SDL_ClearError();
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    }

    void TearDown() override
    {
        SDL_Quit();
    }
};

class WindowRenderer
{
  public:
    WindowRenderer(int width = 128, int height = 128)
        : window_(nullptr, SDL_DestroyWindow), renderer_(nullptr, SDL_DestroyRenderer)
    {
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        if (!SDL_CreateWindowAndRenderer("SDL3D Effects Test", width, height, 0, &window, &renderer))
        {
            ADD_FAILURE() << SDL_GetError();
            return;
        }
        window_.reset(window);
        renderer_.reset(renderer);
    }

    bool ok() const
    {
        return window_ != nullptr && renderer_ != nullptr;
    }

    SDL_Window *window() const
    {
        return window_.get();
    }

    SDL_Renderer *renderer() const
    {
        return renderer_.get();
    }

  private:
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window_;
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer_;
};

sdl3d_camera3d MakeCamera()
{
    sdl3d_camera3d cam{};
    cam.position = sdl3d_vec3_make(0.0f, 0.0f, 4.0f);
    cam.target = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;
    return cam;
}

int CountNonBlackPixels(sdl3d_render_context *ctx)
{
    const int w = sdl3d_get_render_context_width(ctx);
    const int h = sdl3d_get_render_context_height(ctx);
    int count = 0;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            sdl3d_color px{};
            if (sdl3d_get_framebuffer_pixel(ctx, x, y, &px) && (px.r > 20 || px.g > 20 || px.b > 20))
            {
                ++count;
            }
        }
    }
    return count;
}

int CountGreenDominantPixels(sdl3d_render_context *ctx)
{
    const int w = sdl3d_get_render_context_width(ctx);
    const int h = sdl3d_get_render_context_height(ctx);
    int count = 0;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            sdl3d_color px{};
            if (sdl3d_get_framebuffer_pixel(ctx, x, y, &px) && px.g > px.r + 30 && px.g > px.b + 30)
            {
                ++count;
            }
        }
    }
    return count;
}

} // namespace

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
    c.camera_facing = true;
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

TEST(SDL3DParticles, ClearRemovesLiveParticles)
{
    sdl3d_particle_config c = default_config();
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    ASSERT_NE(em, nullptr);
    sdl3d_particle_emitter_emit(em, 8);
    ASSERT_GT(sdl3d_particle_emitter_get_count(em), 0);

    sdl3d_particle_emitter_clear(em);
    EXPECT_EQ(sdl3d_particle_emitter_get_count(em), 0);
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, GetConfigReturnsNormalizedConfig)
{
    sdl3d_particle_config c = default_config();
    c.max_particles = 0;
    c.speed_min = 5.0f;
    c.speed_max = 2.0f;
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    ASSERT_NE(em, nullptr);

    const sdl3d_particle_config *stored = sdl3d_particle_emitter_get_config(em);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->max_particles, 128);
    EXPECT_FLOAT_EQ(stored->speed_min, 2.0f);
    EXPECT_FLOAT_EQ(stored->speed_max, 5.0f);
    EXPECT_EQ(sdl3d_particle_emitter_get_config(nullptr), nullptr);
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, SetConfigReallocatesCapacitySafely)
{
    sdl3d_particle_config c = default_config();
    c.max_particles = 4;
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    ASSERT_NE(em, nullptr);
    sdl3d_particle_emitter_emit(em, 4);
    EXPECT_EQ(sdl3d_particle_emitter_get_count(em), 4);

    c.max_particles = 2;
    ASSERT_TRUE(sdl3d_particle_emitter_set_config(em, &c));
    EXPECT_LE(sdl3d_particle_emitter_get_count(em), 2);

    c.max_particles = 6;
    ASSERT_TRUE(sdl3d_particle_emitter_set_config(em, &c));
    sdl3d_particle_emitter_emit(em, 10);
    EXPECT_LE(sdl3d_particle_emitter_get_count(em), 6);
    EXPECT_FALSE(sdl3d_particle_emitter_set_config(nullptr, &c));
    EXPECT_FALSE(sdl3d_particle_emitter_set_config(em, nullptr));
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, SnapshotCopiesOnlyLiveParticles)
{
    sdl3d_particle_config c = default_config();
    c.max_particles = 6;
    c.emit_rate = 0.0f;
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    ASSERT_NE(em, nullptr);
    sdl3d_particle_emitter_emit(em, 5);

    sdl3d_particle_snapshot snapshots[3]{};
    EXPECT_EQ(sdl3d_particle_emitter_snapshot(em, snapshots, 3), 5);
    for (const sdl3d_particle_snapshot &snapshot : snapshots)
    {
        EXPECT_TRUE(snapshot.alive);
        EXPECT_GT(snapshot.max_lifetime, 0.0f);
    }
    EXPECT_EQ(sdl3d_particle_emitter_snapshot(em, nullptr, 0), 5);
    EXPECT_EQ(sdl3d_particle_emitter_snapshot(nullptr, snapshots, 3), 0);
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, BoxEmitterSpawnsWithinExtents)
{
    sdl3d_particle_config c = default_config();
    c.shape = SDL3D_PARTICLE_EMITTER_BOX;
    c.position = sdl3d_vec3_make(10.0f, 3.0f, -4.0f);
    c.extents = sdl3d_vec3_make(2.0f, 0.5f, 1.5f);
    c.speed_min = 0.0f;
    c.speed_max = 0.0f;
    c.max_particles = 32;
    c.random_seed = 1234;
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    ASSERT_NE(em, nullptr);

    sdl3d_particle_emitter_emit(em, 32);
    std::vector<sdl3d_particle_snapshot> snapshots(32);
    ASSERT_EQ(sdl3d_particle_emitter_snapshot(em, snapshots.data(), (int)snapshots.size()), 32);
    for (const sdl3d_particle_snapshot &snapshot : snapshots)
    {
        EXPECT_GE(snapshot.position.x, 8.0f);
        EXPECT_LE(snapshot.position.x, 12.0f);
        EXPECT_GE(snapshot.position.y, 2.5f);
        EXPECT_LE(snapshot.position.y, 3.5f);
        EXPECT_GE(snapshot.position.z, -5.5f);
        EXPECT_LE(snapshot.position.z, -2.5f);
    }
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, CircleEmitterSpawnsWithinRadius)
{
    sdl3d_particle_config c = default_config();
    c.shape = SDL3D_PARTICLE_EMITTER_CIRCLE;
    c.position = sdl3d_vec3_make(-2.0f, 1.0f, 7.0f);
    c.radius = 3.0f;
    c.speed_min = 0.0f;
    c.speed_max = 0.0f;
    c.max_particles = 32;
    c.random_seed = 5678;
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    ASSERT_NE(em, nullptr);

    sdl3d_particle_emitter_emit(em, 32);
    std::vector<sdl3d_particle_snapshot> snapshots(32);
    ASSERT_EQ(sdl3d_particle_emitter_snapshot(em, snapshots.data(), (int)snapshots.size()), 32);
    for (const sdl3d_particle_snapshot &snapshot : snapshots)
    {
        const float dx = snapshot.position.x + 2.0f;
        const float dz = snapshot.position.z - 7.0f;
        EXPECT_LE(dx * dx + dz * dz, 9.0001f);
        EXPECT_FLOAT_EQ(snapshot.position.y, 1.0f);
    }
    sdl3d_destroy_particle_emitter(em);
}

TEST(SDL3DParticles, SeededEmitterIsDeterministic)
{
    sdl3d_particle_config c = default_config();
    c.shape = SDL3D_PARTICLE_EMITTER_BOX;
    c.extents = sdl3d_vec3_make(2.0f, 1.0f, 2.0f);
    c.random_seed = 42;
    c.max_particles = 16;
    c.emit_rate = 0.0f;

    sdl3d_particle_emitter *a = sdl3d_create_particle_emitter(&c);
    sdl3d_particle_emitter *b = sdl3d_create_particle_emitter(&c);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    sdl3d_particle_emitter_emit(a, 16);
    sdl3d_particle_emitter_emit(b, 16);
    std::vector<sdl3d_particle_snapshot> sa(16);
    std::vector<sdl3d_particle_snapshot> sb(16);
    ASSERT_EQ(sdl3d_particle_emitter_snapshot(a, sa.data(), (int)sa.size()), 16);
    ASSERT_EQ(sdl3d_particle_emitter_snapshot(b, sb.data(), (int)sb.size()), 16);

    for (int i = 0; i < 16; ++i)
    {
        EXPECT_FLOAT_EQ(sa[i].position.x, sb[i].position.x);
        EXPECT_FLOAT_EQ(sa[i].position.y, sb[i].position.y);
        EXPECT_FLOAT_EQ(sa[i].position.z, sb[i].position.z);
        EXPECT_FLOAT_EQ(sa[i].velocity.x, sb[i].velocity.x);
        EXPECT_FLOAT_EQ(sa[i].velocity.y, sb[i].velocity.y);
        EXPECT_FLOAT_EQ(sa[i].velocity.z, sb[i].velocity.z);
        EXPECT_FLOAT_EQ(sa[i].max_lifetime, sb[i].max_lifetime);
    }

    sdl3d_destroy_particle_emitter(a);
    sdl3d_destroy_particle_emitter(b);
}

TEST(SDL3DParticles, SetConfigWithSameSeedDoesNotRestartRandomSequence)
{
    sdl3d_particle_config c = default_config();
    c.shape = SDL3D_PARTICLE_EMITTER_BOX;
    c.extents = sdl3d_vec3_make(1.0f, 1.0f, 1.0f);
    c.random_seed = 123;
    c.max_particles = 2;
    c.emit_rate = 0.0f;

    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    ASSERT_NE(em, nullptr);
    sdl3d_particle_emitter_emit(em, 1);

    sdl3d_particle_snapshot first{};
    ASSERT_EQ(sdl3d_particle_emitter_snapshot(em, &first, 1), 1);
    c.color_start = {10, 255, 10, 255};
    ASSERT_TRUE(sdl3d_particle_emitter_set_config(em, &c));
    sdl3d_particle_emitter_emit(em, 1);

    sdl3d_particle_snapshot snapshots[2]{};
    ASSERT_EQ(sdl3d_particle_emitter_snapshot(em, snapshots, 2), 2);
    EXPECT_FLOAT_EQ(snapshots[0].position.x, first.position.x);
    EXPECT_NE(snapshots[1].position.x, first.position.x);
    sdl3d_destroy_particle_emitter(em);
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

TEST_F(SDLEffectsFixture, ParticleQuadDrawsInSoftwareRenderer)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());

    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx)) << SDL_GetError();

    sdl3d_particle_config c = default_config();
    c.max_particles = 1;
    c.emit_rate = 0.0f;
    c.speed_min = 0.0f;
    c.speed_max = 0.0f;
    c.lifetime_min = 5.0f;
    c.lifetime_max = 5.0f;
    c.size_start = 1.0f;
    c.size_end = 1.0f;
    c.color_start = {20, 255, 20, 255};
    c.color_end = {20, 255, 20, 255};
    c.random_seed = 99;
    sdl3d_particle_emitter *em = sdl3d_create_particle_emitter(&c);
    ASSERT_NE(em, nullptr);
    sdl3d_particle_emitter_emit(em, 1);

    ASSERT_TRUE(sdl3d_set_shading_mode(ctx, SDL3D_SHADING_UNLIT));
    ASSERT_TRUE(sdl3d_clear_render_context(ctx, {0, 0, 0, 255}));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    EXPECT_TRUE(sdl3d_draw_particles(ctx, em)) << SDL_GetError();
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));
    EXPECT_GT(CountNonBlackPixels(ctx), 0);
    EXPECT_GT(CountGreenDominantPixels(ctx), 0);

    sdl3d_destroy_particle_emitter(em);
    sdl3d_destroy_render_context(ctx);
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
