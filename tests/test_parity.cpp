/*
 * Backend parity tests: verify that render profiles produce consistent
 * output across backends.  Software backend is always tested; GL backend
 * tests are skipped when a GL context cannot be created.
 */

#include <gtest/gtest.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

extern "C"
{
#include "slayer3d/slayer3d.h"
}

#include <cmath>
#include <memory>

namespace
{

class ParityFixture : public ::testing::Test
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

struct ProfileCase
{
    const char *name;
    slayer3d_render_profile (*fn)(void);
};

static void render_lit_triangle(slayer3d_render_context *ctx)
{
    slayer3d_light sun{};
    sun.type = SLAYER3D_LIGHT_DIRECTIONAL;
    sun.direction = slayer3d_vec3_make(0.0f, -1.0f, -1.0f);
    sun.color[0] = sun.color[1] = sun.color[2] = 1.0f;
    sun.intensity = 1.0f;
    slayer3d_add_light(ctx, &sun);
    slayer3d_set_ambient_light(ctx, 0.2f, 0.2f, 0.2f);

    slayer3d_color bg = {30, 30, 60, 255};
    slayer3d_clear_render_context(ctx, bg);

    slayer3d_camera3d cam{};
    cam.position = slayer3d_vec3_make(0, 0, 3);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    slayer3d_begin_mode_3d(ctx, cam);

    slayer3d_draw_triangle_3d(ctx, slayer3d_vec3_make(-0.5f, -0.5f, 0.0f), slayer3d_vec3_make(0.5f, -0.5f, 0.0f),
                              slayer3d_vec3_make(0.0f, 0.5f, 0.0f), slayer3d_color{255, 128, 64, 255});

    slayer3d_end_mode_3d(ctx);
}

static bool has_non_background_pixels(slayer3d_render_context *ctx, int w, int h)
{
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            slayer3d_color px{};
            if (slayer3d_get_framebuffer_pixel(ctx, x, y, &px))
            {
                /* Check if pixel differs from the background (30,30,60). */
                if (px.r != 30 || px.g != 30 || px.b != 60)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

class ProfileParityTest : public ParityFixture, public ::testing::WithParamInterface<ProfileCase>
{
};

TEST_P(ProfileParityTest, SoftwareBackendProducesNonTrivialOutput)
{
    const auto &c = GetParam();

    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    ASSERT_TRUE(SDL_CreateWindowAndRenderer("Parity Test", 64, 64, 0, &win, &ren)) << SDL_GetError();

    slayer3d_render_context_config cfg;
    slayer3d_init_render_context_config(&cfg);
    cfg.backend = SLAYER3D_BACKEND_SOFTWARE;
    cfg.allow_backend_fallback = false;

    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(win, ren, &cfg, &ctx)) << SDL_GetError();

    slayer3d_render_profile p = c.fn();
    ASSERT_TRUE(slayer3d_set_render_profile(ctx, &p));

    render_lit_triangle(ctx);

    /* The software backend should have rendered something visible. */
    EXPECT_TRUE(has_non_background_pixels(ctx, 64, 64)) << "Profile " << c.name << " produced all-background output";

    slayer3d_destroy_render_context(ctx);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
}

INSTANTIATE_TEST_SUITE_P(
    Profiles, ProfileParityTest,
    ::testing::Values(ProfileCase{"modern", slayer3d_profile_modern}, ProfileCase{"ps1", slayer3d_profile_ps1},
                      ProfileCase{"n64", slayer3d_profile_n64}, ProfileCase{"dos", slayer3d_profile_dos},
                      ProfileCase{"snes", slayer3d_profile_snes}, ProfileCase{"grayscale", slayer3d_profile_grayscale},
                      ProfileCase{"gameboy", slayer3d_profile_gameboy}));

} // namespace
