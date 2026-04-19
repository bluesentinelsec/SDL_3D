/*
 * Comprehensive tests for M4-1/2/3: lighting API, PBR shading,
 * directional/point/spot lights.
 */

#include <gtest/gtest.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_main.h>

#include <cmath>

extern "C"
{
#include "lighting_internal.h"
#include "sdl3d/lighting.h"
#include "sdl3d/sdl3d.h"
}

/* ================================================================== */
/* Lighting API tests (unit, no SDL video)                            */
/* ================================================================== */

/* Fixture that creates a real render context for API testing. */
class SDL3DLightingFixture : public ::testing::Test
{
  protected:
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    sdl3d_render_context *ctx = nullptr;

    void SetUp() override
    {
        SDL_SetMainReady();
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("test", 64, 64, SDL_WINDOW_HIDDEN);
        ASSERT_NE(window, nullptr);
        renderer = SDL_CreateRenderer(window, NULL);
        ASSERT_NE(renderer, nullptr);
        sdl3d_render_context_config config;
        sdl3d_init_render_context_config(&config);
        ASSERT_TRUE(sdl3d_create_render_context(window, renderer, &config, &ctx));
    }

    void TearDown() override
    {
        sdl3d_destroy_render_context(ctx);
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

TEST_F(SDL3DLightingFixture, DefaultState)
{
    EXPECT_FALSE(sdl3d_is_lighting_enabled(ctx));
    EXPECT_EQ(sdl3d_get_light_count(ctx), 0);
}

TEST_F(SDL3DLightingFixture, EnableDisable)
{
    ASSERT_TRUE(sdl3d_set_lighting_enabled(ctx, true));
    EXPECT_TRUE(sdl3d_is_lighting_enabled(ctx));
    ASSERT_TRUE(sdl3d_set_lighting_enabled(ctx, false));
    EXPECT_FALSE(sdl3d_is_lighting_enabled(ctx));
}

TEST_F(SDL3DLightingFixture, AddAndClearLights)
{
    sdl3d_light light{};
    light.type = SDL3D_LIGHT_DIRECTIONAL;
    light.direction = {0.0f, -1.0f, 0.0f};
    light.color[0] = light.color[1] = light.color[2] = 1.0f;
    light.intensity = 1.0f;

    ASSERT_TRUE(sdl3d_add_light(ctx, &light));
    EXPECT_EQ(sdl3d_get_light_count(ctx), 1);

    ASSERT_TRUE(sdl3d_add_light(ctx, &light));
    EXPECT_EQ(sdl3d_get_light_count(ctx), 2);

    ASSERT_TRUE(sdl3d_clear_lights(ctx));
    EXPECT_EQ(sdl3d_get_light_count(ctx), 0);
}

TEST_F(SDL3DLightingFixture, MaxLightsEnforced)
{
    sdl3d_light light{};
    light.type = SDL3D_LIGHT_POINT;
    light.intensity = 1.0f;

    for (int i = 0; i < SDL3D_MAX_LIGHTS; ++i)
    {
        ASSERT_TRUE(sdl3d_add_light(ctx, &light)) << "light " << i;
    }
    EXPECT_EQ(sdl3d_get_light_count(ctx), SDL3D_MAX_LIGHTS);
    EXPECT_FALSE(sdl3d_add_light(ctx, &light));
    EXPECT_EQ(sdl3d_get_light_count(ctx), SDL3D_MAX_LIGHTS);
}

TEST_F(SDL3DLightingFixture, SetAmbientLight)
{
    ASSERT_TRUE(sdl3d_set_ambient_light(ctx, 0.1f, 0.2f, 0.3f));
}

/* ================================================================== */
/* Null context rejection                                             */
/* ================================================================== */

struct NullCtxCase
{
    const char *label;
};

TEST(SDL3DLightingNullCtx, AllFunctionsRejectNull)
{
    sdl3d_light light{};
    EXPECT_FALSE(sdl3d_add_light(nullptr, &light));
    EXPECT_FALSE(sdl3d_add_light(nullptr, nullptr));
    EXPECT_FALSE(sdl3d_clear_lights(nullptr));
    EXPECT_FALSE(sdl3d_set_lighting_enabled(nullptr, true));
    EXPECT_FALSE(sdl3d_is_lighting_enabled(nullptr));
    EXPECT_EQ(sdl3d_get_light_count(nullptr), 0);
    EXPECT_FALSE(sdl3d_set_ambient_light(nullptr, 0, 0, 0));
}

/* ================================================================== */
/* PBR shading unit tests (sdl3d_shade_fragment_pbr)                  */
/* ================================================================== */

static sdl3d_lighting_params make_params(float metallic, float roughness)
{
    sdl3d_lighting_params p{};
    p.lights = nullptr;
    p.light_count = 0;
    p.ambient[0] = p.ambient[1] = p.ambient[2] = 0.0f;
    p.camera_pos = {0.0f, 0.0f, 5.0f};
    p.metallic = metallic;
    p.roughness = roughness;
    p.emissive[0] = p.emissive[1] = p.emissive[2] = 0.0f;
    return p;
}

static sdl3d_light make_directional(float dx, float dy, float dz, float intensity)
{
    sdl3d_light l{};
    l.type = SDL3D_LIGHT_DIRECTIONAL;
    l.direction = {dx, dy, dz};
    l.color[0] = l.color[1] = l.color[2] = 1.0f;
    l.intensity = intensity;
    return l;
}

static sdl3d_light make_point(float px, float py, float pz, float intensity, float range)
{
    sdl3d_light l{};
    l.type = SDL3D_LIGHT_POINT;
    l.position = {px, py, pz};
    l.color[0] = l.color[1] = l.color[2] = 1.0f;
    l.intensity = intensity;
    l.range = range;
    return l;
}

static sdl3d_light make_spot(float px, float py, float pz, float dx, float dy, float dz, float intensity, float range,
                             float inner_cos, float outer_cos)
{
    sdl3d_light l{};
    l.type = SDL3D_LIGHT_SPOT;
    l.position = {px, py, pz};
    l.direction = {dx, dy, dz};
    l.color[0] = l.color[1] = l.color[2] = 1.0f;
    l.intensity = intensity;
    l.range = range;
    l.inner_cutoff = inner_cos;
    l.outer_cutoff = outer_cos;
    return l;
}

TEST(SDL3DPBRShading, NoLightsNoAmbientProducesBlack)
{
    sdl3d_lighting_params p = make_params(0.0f, 1.0f);
    float r, g, b;
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.0f, 0.001f);
    EXPECT_NEAR(g, 0.0f, 0.001f);
    EXPECT_NEAR(b, 0.0f, 0.001f);
}

TEST(SDL3DPBRShading, AmbientOnlyProducesAmbientTimesAlbedo)
{
    sdl3d_lighting_params p = make_params(0.0f, 1.0f);
    p.ambient[0] = 0.1f;
    p.ambient[1] = 0.2f;
    p.ambient[2] = 0.3f;
    float r, g, b;
    sdl3d_shade_fragment_pbr(&p, 0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.05f, 0.001f);
    EXPECT_NEAR(g, 0.10f, 0.001f);
    EXPECT_NEAR(b, 0.15f, 0.001f);
}

TEST(SDL3DPBRShading, EmissiveAddsToOutput)
{
    sdl3d_lighting_params p = make_params(0.0f, 1.0f);
    p.emissive[0] = 0.5f;
    p.emissive[1] = 0.0f;
    p.emissive[2] = 0.0f;
    float r, g, b;
    sdl3d_shade_fragment_pbr(&p, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.5f, 0.001f);
    EXPECT_NEAR(g, 0.0f, 0.001f);
    EXPECT_NEAR(b, 0.0f, 0.001f);
}

TEST(SDL3DPBRShading, DirectionalLightFacingNormalProducesLight)
{
    sdl3d_light dl = make_directional(0.0f, -1.0f, 0.0f, 1.0f);
    sdl3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &dl;
    p.light_count = 1;

    float r, g, b;
    /* Normal pointing up, light pointing down → N·L = 1. */
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_GT(r, 0.1f);
    EXPECT_GT(g, 0.1f);
    EXPECT_GT(b, 0.1f);
}

TEST(SDL3DPBRShading, DirectionalLightBackfaceProducesNoLight)
{
    sdl3d_light dl = make_directional(0.0f, 1.0f, 0.0f, 1.0f);
    sdl3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &dl;
    p.light_count = 1;

    float r, g, b;
    /* Normal pointing up, light pointing up → N·L = -1 → clamped to 0. */
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.0f, 0.001f);
}

TEST(SDL3DPBRShading, PointLightAttenuatesWithDistance)
{
    sdl3d_light pl = make_point(0.0f, 2.0f, 0.0f, 1.0f, 10.0f);
    sdl3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &pl;
    p.light_count = 1;

    float r_near, g_near, b_near;
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r_near, &g_near, &b_near);

    /* Move fragment far away. */
    float r_far, g_far, b_far;
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, -8.0f, 0.0f, &r_far, &g_far, &b_far);

    EXPECT_GT(r_near, r_far);
}

TEST(SDL3DPBRShading, PointLightOutOfRangeProducesNoLight)
{
    sdl3d_light pl = make_point(0.0f, 2.0f, 0.0f, 1.0f, 1.0f);
    sdl3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &pl;
    p.light_count = 1;

    float r, g, b;
    /* Fragment at origin, light at y=2, range=1 → distance > range. */
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.0f, 0.01f);
}

TEST(SDL3DPBRShading, SpotLightInsideConeProducesLight)
{
    /* Spot at y=2 pointing down, fragment at origin with normal up. */
    sdl3d_light sl = make_spot(0.0f, 2.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 10.0f, cosf(0.3f), cosf(0.5f));
    sdl3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &sl;
    p.light_count = 1;

    float r, g, b;
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_GT(r, 0.05f);
}

TEST(SDL3DPBRShading, SpotLightOutsideConeProducesNoLight)
{
    /* Spot at y=2 pointing down, fragment far to the side. */
    sdl3d_light sl = make_spot(0.0f, 2.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 100.0f, cosf(0.1f), cosf(0.2f));
    sdl3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &sl;
    p.light_count = 1;

    float r, g, b;
    /* Fragment at x=100, well outside the narrow cone. */
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 100.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.0f, 0.01f);
}

TEST(SDL3DPBRShading, MetallicSurfaceHasNoLambertianDiffuse)
{
    sdl3d_light dl = make_directional(0.0f, -1.0f, 0.0f, 1.0f);
    sdl3d_lighting_params p_metal = make_params(1.0f, 0.5f);
    p_metal.lights = &dl;
    p_metal.light_count = 1;

    sdl3d_lighting_params p_dielectric = make_params(0.0f, 0.5f);
    p_dielectric.lights = &dl;
    p_dielectric.light_count = 1;

    float rm, gm, bm;
    float rd, gd, bd;
    sdl3d_shade_fragment_pbr(&p_metal, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &rm, &gm, &bm);
    sdl3d_shade_fragment_pbr(&p_dielectric, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &rd, &gd, &bd);

    /* Metal should have less red (no diffuse) but more specular. The green
     * channel should be near zero for both since albedo green is 0. */
    EXPECT_NEAR(gm, 0.0f, 0.05f);
    EXPECT_NEAR(gd, 0.0f, 0.05f);
}

TEST(SDL3DPBRShading, MultipleLightsAccumulate)
{
    sdl3d_light lights[2];
    lights[0] = make_directional(0.0f, -1.0f, 0.0f, 1.0f);
    lights[1] = make_directional(0.0f, -1.0f, 0.0f, 1.0f);

    sdl3d_lighting_params p1 = make_params(0.0f, 1.0f);
    p1.lights = lights;
    p1.light_count = 1;

    sdl3d_lighting_params p2 = make_params(0.0f, 1.0f);
    p2.lights = lights;
    p2.light_count = 2;

    float r1, g1, b1, r2, g2, b2;
    sdl3d_shade_fragment_pbr(&p1, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r1, &g1, &b1);
    sdl3d_shade_fragment_pbr(&p2, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r2, &g2, &b2);

    /* Two identical lights should produce roughly double the output. */
    EXPECT_NEAR(r2, r1 * 2.0f, 0.01f);
}

TEST(SDL3DPBRShading, ColoredLightTintsOutput)
{
    sdl3d_light dl{};
    dl.type = SDL3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = 1.0f;
    dl.color[1] = 0.0f;
    dl.color[2] = 0.0f;
    dl.intensity = 1.0f;

    sdl3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &dl;
    p.light_count = 1;

    float r, g, b;
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_GT(r, 0.1f);
    EXPECT_NEAR(g, 0.0f, 0.001f);
    EXPECT_NEAR(b, 0.0f, 0.001f);
}

TEST(SDL3DPBRShading, RoughnessAffectsSpecular)
{
    sdl3d_light dl = make_directional(0.0f, -1.0f, 0.0f, 1.0f);

    sdl3d_lighting_params p_smooth = make_params(0.0f, 0.1f);
    p_smooth.lights = &dl;
    p_smooth.light_count = 1;

    sdl3d_lighting_params p_rough = make_params(0.0f, 1.0f);
    p_rough.lights = &dl;
    p_rough.light_count = 1;

    float rs, gs, bs, rr, gr, br;
    /* View from directly above → specular highlight should be stronger for smooth. */
    p_smooth.camera_pos = (sdl3d_vec3){0.0f, 5.0f, 0.0f};
    p_rough.camera_pos = (sdl3d_vec3){0.0f, 5.0f, 0.0f};
    sdl3d_shade_fragment_pbr(&p_smooth, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &rs, &gs, &bs);
    sdl3d_shade_fragment_pbr(&p_rough, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &rr, &gr, &br);

    /* Smooth surface should have higher total output due to specular peak. */
    EXPECT_GT(rs, rr);
}

/* ================================================================== */
/* Fog computation tests                                              */
/* ================================================================== */

struct FogFactorCase
{
    const char *label;
    sdl3d_fog_mode mode;
    float start;
    float end;
    float density;
    float distance;
    float expected_min;
    float expected_max;
};

class SDL3DFogFactor : public ::testing::TestWithParam<FogFactorCase>
{
};

TEST_P(SDL3DFogFactor, ComputesExpectedRange)
{
    const auto &c = GetParam();
    sdl3d_fog fog{};
    fog.mode = c.mode;
    fog.start = c.start;
    fog.end = c.end;
    fog.density = c.density;
    float f = sdl3d_compute_fog_factor(&fog, c.distance);
    EXPECT_GE(f, c.expected_min) << c.label;
    EXPECT_LE(f, c.expected_max) << c.label;
}

INSTANTIATE_TEST_SUITE_P(Fog, SDL3DFogFactor,
                         ::testing::Values(
                             /* No fog → always 0. */
                             FogFactorCase{"none d=0", SDL3D_FOG_NONE, 0, 0, 0, 0.0f, 0.0f, 0.0f},
                             FogFactorCase{"none d=100", SDL3D_FOG_NONE, 0, 0, 0, 100.0f, 0.0f, 0.0f},

                             /* Linear fog. */
                             FogFactorCase{"linear before start", SDL3D_FOG_LINEAR, 10, 50, 0, 5.0f, 0.0f, 0.0f},
                             FogFactorCase{"linear at start", SDL3D_FOG_LINEAR, 10, 50, 0, 10.0f, 0.0f, 0.001f},
                             FogFactorCase{"linear midpoint", SDL3D_FOG_LINEAR, 10, 50, 0, 30.0f, 0.49f, 0.51f},
                             FogFactorCase{"linear at end", SDL3D_FOG_LINEAR, 10, 50, 0, 50.0f, 0.99f, 1.0f},
                             FogFactorCase{"linear past end", SDL3D_FOG_LINEAR, 10, 50, 0, 100.0f, 1.0f, 1.0f},
                             FogFactorCase{"linear start==end", SDL3D_FOG_LINEAR, 10, 10, 0, 10.0f, 0.0f, 0.0f},

                             /* Exponential fog. */
                             FogFactorCase{"exp d=0", SDL3D_FOG_EXP, 0, 0, 0.1f, 0.0f, 0.0f, 0.001f},
                             FogFactorCase{"exp d=10", SDL3D_FOG_EXP, 0, 0, 0.1f, 10.0f, 0.5f, 0.7f},
                             FogFactorCase{"exp d=100", SDL3D_FOG_EXP, 0, 0, 0.1f, 100.0f, 0.99f, 1.0f},

                             /* Exponential squared fog. */
                             FogFactorCase{"exp2 d=0", SDL3D_FOG_EXP2, 0, 0, 0.1f, 0.0f, 0.0f, 0.001f},
                             FogFactorCase{"exp2 d=5", SDL3D_FOG_EXP2, 0, 0, 0.1f, 5.0f, 0.15f, 0.30f},
                             FogFactorCase{"exp2 d=50", SDL3D_FOG_EXP2, 0, 0, 0.1f, 50.0f, 0.99f, 1.0f}));

TEST_F(SDL3DLightingFixture, SetAndClearFog)
{
    sdl3d_fog fog{};
    fog.mode = SDL3D_FOG_LINEAR;
    fog.color[0] = 0.5f;
    fog.color[1] = 0.5f;
    fog.color[2] = 0.5f;
    fog.start = 10.0f;
    fog.end = 100.0f;
    ASSERT_TRUE(sdl3d_set_fog(ctx, &fog));
    ASSERT_TRUE(sdl3d_clear_fog(ctx));
}

TEST(SDL3DFogAPI, NullContextRejected)
{
    sdl3d_fog fog{};
    EXPECT_FALSE(sdl3d_set_fog(nullptr, &fog));
    EXPECT_FALSE(sdl3d_clear_fog(nullptr));
}

TEST(SDL3DFogAPI, NullFogRejected)
{
    /* Can't test without a context, but we can verify the null fog param. */
    EXPECT_FALSE(sdl3d_set_fog(nullptr, nullptr));
}

/* ================================================================== */
/* Tonemapping tests                                                  */
/* ================================================================== */

struct TonemapCase
{
    const char *label;
    sdl3d_tonemap_mode mode;
    float in_r, in_g, in_b;
    float expect_min_r, expect_max_r;
};

class SDL3DTonemapTable : public ::testing::TestWithParam<TonemapCase>
{
};

TEST_P(SDL3DTonemapTable, OutputInExpectedRange)
{
    const auto &c = GetParam();
    float r = c.in_r, g = c.in_g, b = c.in_b;
    sdl3d_tonemap(c.mode, &r, &g, &b);
    EXPECT_GE(r, c.expect_min_r) << c.label;
    EXPECT_LE(r, c.expect_max_r) << c.label;
    EXPECT_GE(g, 0.0f) << c.label;
    /* Tonemapped modes clamp to [0,1]; NONE is passthrough. */
    if (c.mode != SDL3D_TONEMAP_NONE)
    {
        EXPECT_LE(b, 1.01f) << c.label;
    }
}

INSTANTIATE_TEST_SUITE_P(Tonemap, SDL3DTonemapTable,
                         ::testing::Values(
                             /* None: passthrough. */
                             TonemapCase{"none 0", SDL3D_TONEMAP_NONE, 0.0f, 0.0f, 0.0f, 0.0f, 0.001f},
                             TonemapCase{"none 1", SDL3D_TONEMAP_NONE, 1.0f, 1.0f, 1.0f, 1.0f, 1.001f},
                             TonemapCase{"none HDR", SDL3D_TONEMAP_NONE, 5.0f, 5.0f, 5.0f, 5.0f, 5.001f},

                             /* Reinhard: x/(1+x), then gamma. */
                             TonemapCase{"reinhard 0", SDL3D_TONEMAP_REINHARD, 0.0f, 0.0f, 0.0f, 0.0f, 0.001f},
                             TonemapCase{"reinhard 1", SDL3D_TONEMAP_REINHARD, 1.0f, 1.0f, 1.0f, 0.5f, 0.8f},
                             TonemapCase{"reinhard HDR", SDL3D_TONEMAP_REINHARD, 10.0f, 10.0f, 10.0f, 0.9f, 1.0f},

                             /* ACES: should compress HDR to [0,1]. */
                             TonemapCase{"aces 0", SDL3D_TONEMAP_ACES, 0.0f, 0.0f, 0.0f, 0.0f, 0.01f},
                             TonemapCase{"aces 1", SDL3D_TONEMAP_ACES, 1.0f, 1.0f, 1.0f, 0.5f, 1.0f},
                             TonemapCase{"aces HDR", SDL3D_TONEMAP_ACES, 10.0f, 10.0f, 10.0f, 0.9f, 1.0f}));

TEST_F(SDL3DLightingFixture, SetAndGetTonemapMode)
{
    EXPECT_EQ(sdl3d_get_tonemap_mode(ctx), SDL3D_TONEMAP_NONE);
    ASSERT_TRUE(sdl3d_set_tonemap_mode(ctx, SDL3D_TONEMAP_REINHARD));
    EXPECT_EQ(sdl3d_get_tonemap_mode(ctx), SDL3D_TONEMAP_REINHARD);
    ASSERT_TRUE(sdl3d_set_tonemap_mode(ctx, SDL3D_TONEMAP_ACES));
    EXPECT_EQ(sdl3d_get_tonemap_mode(ctx), SDL3D_TONEMAP_ACES);
    ASSERT_TRUE(sdl3d_set_tonemap_mode(ctx, SDL3D_TONEMAP_NONE));
    EXPECT_EQ(sdl3d_get_tonemap_mode(ctx), SDL3D_TONEMAP_NONE);
}

TEST(SDL3DTonemapAPI, NullContextRejected)
{
    EXPECT_FALSE(sdl3d_set_tonemap_mode(nullptr, SDL3D_TONEMAP_REINHARD));
    EXPECT_EQ(sdl3d_get_tonemap_mode(nullptr), SDL3D_TONEMAP_NONE);
}

/* ================================================================== */
/* Shadow mapping tests                                               */
/* ================================================================== */

TEST_F(SDL3DLightingFixture, EnableShadowOnDirectionalLight)
{
    sdl3d_light dl{};
    dl.type = SDL3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = dl.color[1] = dl.color[2] = 1.0f;
    dl.intensity = 1.0f;
    ASSERT_TRUE(sdl3d_add_light(ctx, &dl));

    ASSERT_TRUE(sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), 10.0f));
    ASSERT_TRUE(sdl3d_disable_shadow(ctx, 0));
}

TEST_F(SDL3DLightingFixture, ShadowRejectsNonDirectionalLight)
{
    sdl3d_light pl{};
    pl.type = SDL3D_LIGHT_POINT;
    pl.intensity = 1.0f;
    ASSERT_TRUE(sdl3d_add_light(ctx, &pl));

    EXPECT_FALSE(sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), 10.0f));
}

TEST_F(SDL3DLightingFixture, ShadowRejectsInvalidLightIndex)
{
    EXPECT_FALSE(sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), 10.0f));
    EXPECT_FALSE(sdl3d_enable_shadow(ctx, -1, sdl3d_vec3_make(0, 0, 0), 10.0f));
    EXPECT_FALSE(sdl3d_enable_shadow(ctx, 99, sdl3d_vec3_make(0, 0, 0), 10.0f));
}

TEST_F(SDL3DLightingFixture, ShadowRejectsZeroRadius)
{
    sdl3d_light dl{};
    dl.type = SDL3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.intensity = 1.0f;
    ASSERT_TRUE(sdl3d_add_light(ctx, &dl));

    EXPECT_FALSE(sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), 0.0f));
    EXPECT_FALSE(sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), -1.0f));
}

TEST(SDL3DShadowAPI, NullContextRejected)
{
    EXPECT_FALSE(sdl3d_enable_shadow(nullptr, 0, sdl3d_vec3_make(0, 0, 0), 10.0f));
    EXPECT_FALSE(sdl3d_disable_shadow(nullptr, 0));
    EXPECT_FALSE(sdl3d_render_shadow_map(nullptr, nullptr, 0, nullptr));
}

TEST_F(SDL3DLightingFixture, RenderShadowMapWithNoMeshes)
{
    sdl3d_light dl{};
    dl.type = SDL3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = dl.color[1] = dl.color[2] = 1.0f;
    dl.intensity = 1.0f;
    ASSERT_TRUE(sdl3d_add_light(ctx, &dl));
    ASSERT_TRUE(sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), 10.0f));

    /* Rendering with zero meshes should succeed (no-op). */
    ASSERT_TRUE(sdl3d_render_shadow_map(ctx, nullptr, 0, nullptr));

    sdl3d_disable_shadow(ctx, 0);
}

TEST(SDL3DPBRShading, ShadowOccludesLight)
{
    /* Create a shadow map that is all zeros (everything at depth 0 = near).
     * Any fragment behind depth 0 should be in shadow. */
    const size_t map_size = (size_t)SDL3D_SHADOW_MAP_SIZE * SDL3D_SHADOW_MAP_SIZE;
    std::vector<float> shadow_depth(map_size, 0.0f);

    sdl3d_light dl{};
    dl.type = SDL3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = dl.color[1] = dl.color[2] = 1.0f;
    dl.intensity = 1.0f;

    sdl3d_lighting_params p{};
    p.lights = &dl;
    p.light_count = 1;
    p.camera_pos = {0.0f, 0.0f, 5.0f};
    p.roughness = 1.0f;
    p.shadow_depth[0] = shadow_depth.data();
    p.shadow_enabled[0] = true;
    p.shadow_bias = 0.005f;
    /* Identity VP → fragment at origin maps to NDC (0,0,0) → depth 0.5. */
    p.shadow_vp[0] = sdl3d_mat4_identity();

    float r, g, b;
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);

    /* Fragment should be in shadow → no direct light contribution. */
    EXPECT_NEAR(r, 0.0f, 0.01f);
    EXPECT_NEAR(g, 0.0f, 0.01f);
    EXPECT_NEAR(b, 0.0f, 0.01f);
}

TEST(SDL3DPBRShading, NoShadowWhenDisabled)
{
    sdl3d_light dl{};
    dl.type = SDL3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = dl.color[1] = dl.color[2] = 1.0f;
    dl.intensity = 1.0f;

    sdl3d_lighting_params p{};
    p.lights = &dl;
    p.light_count = 1;
    p.camera_pos = {0.0f, 0.0f, 5.0f};
    p.roughness = 1.0f;
    /* shadow_enabled[0] defaults to false. */

    float r, g, b;
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);

    /* Should be lit (no shadow). */
    EXPECT_GT(r, 0.1f);
}

/* ================================================================== */
/* Shading mode API tests                                             */
/* ================================================================== */

TEST_F(SDL3DLightingFixture, DefaultShadingModeIsUnlit)
{
    EXPECT_EQ(sdl3d_get_shading_mode(ctx), SDL3D_SHADING_UNLIT);
}

struct ShadingModeCase
{
    const char *label;
    sdl3d_shading_mode mode;
};

class SDL3DShadingModeSet : public SDL3DLightingFixture, public ::testing::WithParamInterface<ShadingModeCase>
{
};

TEST_P(SDL3DShadingModeSet, SetAndGet)
{
    const auto &c = GetParam();
    ASSERT_TRUE(sdl3d_set_shading_mode(ctx, c.mode)) << c.label;
    EXPECT_EQ(sdl3d_get_shading_mode(ctx), c.mode) << c.label;
}

INSTANTIATE_TEST_SUITE_P(Shading, SDL3DShadingModeSet,
                         ::testing::Values(ShadingModeCase{"unlit", SDL3D_SHADING_UNLIT},
                                           ShadingModeCase{"flat", SDL3D_SHADING_FLAT},
                                           ShadingModeCase{"gouraud", SDL3D_SHADING_GOURAUD},
                                           ShadingModeCase{"phong", SDL3D_SHADING_PHONG}));

TEST_F(SDL3DLightingFixture, SetLightingEnabledMapsToShadingMode)
{
    ASSERT_TRUE(sdl3d_set_lighting_enabled(ctx, true));
    EXPECT_EQ(sdl3d_get_shading_mode(ctx), SDL3D_SHADING_PHONG);
    EXPECT_TRUE(sdl3d_is_lighting_enabled(ctx));

    ASSERT_TRUE(sdl3d_set_lighting_enabled(ctx, false));
    EXPECT_EQ(sdl3d_get_shading_mode(ctx), SDL3D_SHADING_UNLIT);
    EXPECT_FALSE(sdl3d_is_lighting_enabled(ctx));
}

TEST_F(SDL3DLightingFixture, IsLightingEnabledReflectsShadingMode)
{
    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_FLAT);
    EXPECT_TRUE(sdl3d_is_lighting_enabled(ctx));

    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_GOURAUD);
    EXPECT_TRUE(sdl3d_is_lighting_enabled(ctx));

    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG);
    EXPECT_TRUE(sdl3d_is_lighting_enabled(ctx));

    sdl3d_set_shading_mode(ctx, SDL3D_SHADING_UNLIT);
    EXPECT_FALSE(sdl3d_is_lighting_enabled(ctx));
}

TEST(SDL3DShadingModeAPI, NullContextRejected)
{
    EXPECT_FALSE(sdl3d_set_shading_mode(nullptr, SDL3D_SHADING_PHONG));
    EXPECT_EQ(sdl3d_get_shading_mode(nullptr), SDL3D_SHADING_UNLIT);
}

TEST_F(SDL3DLightingFixture, RuntimeShadingModeSwitch)
{
    /* Verify we can switch modes rapidly without issues. */
    sdl3d_shading_mode modes[] = {SDL3D_SHADING_UNLIT, SDL3D_SHADING_FLAT, SDL3D_SHADING_GOURAUD,
                                  SDL3D_SHADING_PHONG, SDL3D_SHADING_FLAT, SDL3D_SHADING_UNLIT};
    for (int i = 0; i < 6; ++i)
    {
        ASSERT_TRUE(sdl3d_set_shading_mode(ctx, modes[i])) << "iteration " << i;
        EXPECT_EQ(sdl3d_get_shading_mode(ctx), modes[i]) << "iteration " << i;
    }
}

/* ================================================================== */
/* Shading mode PBR output tests (unit, no SDL video)                 */
/* ================================================================== */

TEST(SDL3DPBRShading, FlatShadingProducesUniformColorPerTriangle)
{
    /* FLAT mode evaluates PBR once at the centroid. All three vertices
     * of the resulting flat-color triangle get the same color. We verify
     * this by calling sdl3d_shade_point with the centroid and checking
     * the result is non-zero (lit) and deterministic. */
    sdl3d_light dl{};
    dl.type = SDL3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = dl.color[1] = dl.color[2] = 1.0f;
    dl.intensity = 1.0f;

    sdl3d_lighting_params p{};
    p.lights = &dl;
    p.light_count = 1;
    p.camera_pos = {0.0f, 0.0f, 5.0f};
    p.roughness = 1.0f;

    /* Shade the same point twice — must be deterministic. */
    float r1, g1, b1, r2, g2, b2;
    sdl3d_shade_fragment_pbr(&p, 0.8f, 0.6f, 0.4f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r1, &g1, &b1);
    sdl3d_shade_fragment_pbr(&p, 0.8f, 0.6f, 0.4f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r2, &g2, &b2);
    EXPECT_FLOAT_EQ(r1, r2);
    EXPECT_FLOAT_EQ(g1, g2);
    EXPECT_FLOAT_EQ(b1, b2);
    EXPECT_GT(r1, 0.0f); /* Must be lit. */
}

TEST(SDL3DPBRShading, GouraudShadingProducesDifferentColorsAtDifferentPositions)
{
    /* GOURAUD evaluates PBR at each vertex. Vertices at different
     * distances from a point light should get different intensities. */
    sdl3d_light pl{};
    pl.type = SDL3D_LIGHT_POINT;
    pl.position = {0.0f, 2.0f, 0.0f};
    pl.color[0] = pl.color[1] = pl.color[2] = 1.0f;
    pl.intensity = 1.0f;
    pl.range = 10.0f;

    sdl3d_lighting_params p{};
    p.lights = &pl;
    p.light_count = 1;
    p.camera_pos = {0.0f, 0.0f, 5.0f};
    p.roughness = 1.0f;

    float r_near, g_near, b_near;
    float r_far, g_far, b_far;
    /* Vertex near the light. */
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r_near, &g_near, &b_near);
    /* Vertex far from the light. */
    sdl3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, -5.0f, 0.0f, &r_far, &g_far, &b_far);

    EXPECT_GT(r_near, r_far);
}

/* ================================================================== */
/* Render profile API tests                                           */
/* ================================================================== */

TEST_F(SDL3DLightingFixture, SetAndGetRenderProfile)
{
    sdl3d_render_profile p = sdl3d_profile_ps1();
    ASSERT_TRUE(sdl3d_set_render_profile(ctx, &p));

    sdl3d_render_profile out{};
    ASSERT_TRUE(sdl3d_get_render_profile(ctx, &out));
    EXPECT_EQ(out.shading, SDL3D_SHADING_GOURAUD);
    EXPECT_EQ(out.uv_mode, SDL3D_UV_AFFINE);
    EXPECT_EQ(out.fog_eval, SDL3D_FOG_EVAL_VERTEX);
    EXPECT_EQ(out.tonemap, SDL3D_TONEMAP_NONE);
    EXPECT_TRUE(out.vertex_snap);
    EXPECT_EQ(out.vertex_snap_precision, 1);
    EXPECT_FALSE(out.color_quantize);
}

TEST_F(SDL3DLightingFixture, RuntimeProfileSwitch)
{
    sdl3d_render_profile profiles[] = {
        sdl3d_profile_modern(), sdl3d_profile_ps1(), sdl3d_profile_n64(), sdl3d_profile_dos(), sdl3d_profile_snes(),
    };
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(sdl3d_set_render_profile(ctx, &profiles[i])) << "profile " << i;
        sdl3d_render_profile out{};
        ASSERT_TRUE(sdl3d_get_render_profile(ctx, &out));
        EXPECT_EQ(out.shading, profiles[i].shading) << "profile " << i;
        EXPECT_EQ(out.uv_mode, profiles[i].uv_mode) << "profile " << i;
    }
}

TEST(SDL3DRenderProfileAPI, NullContextRejected)
{
    sdl3d_render_profile p = sdl3d_profile_modern();
    sdl3d_render_profile out{};
    EXPECT_FALSE(sdl3d_set_render_profile(nullptr, &p));
    EXPECT_FALSE(sdl3d_set_render_profile(nullptr, nullptr));
    EXPECT_FALSE(sdl3d_get_render_profile(nullptr, &out));
}

TEST(SDL3DRenderProfileAPI, NullProfileRejected)
{
    EXPECT_FALSE(sdl3d_set_render_profile(nullptr, nullptr));
}

/* ================================================================== */
/* Named preset validation                                            */
/* ================================================================== */

struct PresetCase
{
    const char *label;
    sdl3d_render_profile (*fn)(void);
    sdl3d_shading_mode expected_shading;
    sdl3d_uv_mode expected_uv;
    sdl3d_tonemap_mode expected_tonemap;
    bool expected_snap;
    bool expected_quantize;
};

class SDL3DPresetTable : public ::testing::TestWithParam<PresetCase>
{
};

TEST_P(SDL3DPresetTable, FieldsMatchSpec)
{
    const auto &c = GetParam();
    sdl3d_render_profile p = c.fn();
    EXPECT_EQ(p.shading, c.expected_shading) << c.label;
    EXPECT_EQ(p.uv_mode, c.expected_uv) << c.label;
    EXPECT_EQ(p.tonemap, c.expected_tonemap) << c.label;
    EXPECT_EQ(p.vertex_snap, c.expected_snap) << c.label;
    EXPECT_EQ(p.color_quantize, c.expected_quantize) << c.label;
}

INSTANTIATE_TEST_SUITE_P(Presets, SDL3DPresetTable,
                         ::testing::Values(PresetCase{"modern", sdl3d_profile_modern, SDL3D_SHADING_PHONG,
                                                      SDL3D_UV_PERSPECTIVE, SDL3D_TONEMAP_ACES, false, false},
                                           PresetCase{"ps1", sdl3d_profile_ps1, SDL3D_SHADING_GOURAUD, SDL3D_UV_AFFINE,
                                                      SDL3D_TONEMAP_NONE, true, false},
                                           PresetCase{"n64", sdl3d_profile_n64, SDL3D_SHADING_GOURAUD,
                                                      SDL3D_UV_PERSPECTIVE, SDL3D_TONEMAP_NONE, false, false},
                                           PresetCase{"dos", sdl3d_profile_dos, SDL3D_SHADING_GOURAUD, SDL3D_UV_AFFINE,
                                                      SDL3D_TONEMAP_NONE, false, true},
                                           PresetCase{"snes", sdl3d_profile_snes, SDL3D_SHADING_FLAT, SDL3D_UV_AFFINE,
                                                      SDL3D_TONEMAP_NONE, false, false}));

/* ================================================================== */
/* Color quantization unit tests                                      */
/* ================================================================== */

TEST(SDL3DColorQuantize, DosProfileSets256Colors)
{
    sdl3d_render_profile p = sdl3d_profile_dos();
    EXPECT_TRUE(p.color_quantize);
    EXPECT_EQ(p.color_depth, 256);
}

/* ================================================================== */
/* Custom profile: mix and match                                      */
/* ================================================================== */

TEST_F(SDL3DLightingFixture, CustomProfileMixAndMatch)
{
    /* Start from PS1, override texture filter. */
    sdl3d_render_profile p = sdl3d_profile_ps1();
    p.texture_filter = SDL3D_TEXTURE_FILTER_BILINEAR;
    p.vertex_snap = false;
    ASSERT_TRUE(sdl3d_set_render_profile(ctx, &p));

    sdl3d_render_profile out{};
    ASSERT_TRUE(sdl3d_get_render_profile(ctx, &out));
    EXPECT_EQ(out.shading, SDL3D_SHADING_GOURAUD);
    EXPECT_EQ(out.uv_mode, SDL3D_UV_AFFINE);
    EXPECT_FALSE(out.vertex_snap);
}

/* ================================================================== */
/* Fog evaluation mode                                                */
/* ================================================================== */

TEST(SDL3DFogEval, PS1ProfileUsesVertexFog)
{
    sdl3d_render_profile p = sdl3d_profile_ps1();
    EXPECT_EQ(p.fog_eval, SDL3D_FOG_EVAL_VERTEX);
}

TEST(SDL3DFogEval, ModernProfileUsesFragmentFog)
{
    sdl3d_render_profile p = sdl3d_profile_modern();
    EXPECT_EQ(p.fog_eval, SDL3D_FOG_EVAL_FRAGMENT);
}

/* ================================================================== */
/* UV mode                                                            */
/* ================================================================== */

TEST(SDL3DUVMode, PS1ProfileUsesAffine)
{
    sdl3d_render_profile p = sdl3d_profile_ps1();
    EXPECT_EQ(p.uv_mode, SDL3D_UV_AFFINE);
}

TEST(SDL3DUVMode, ModernProfileUsesPerspective)
{
    sdl3d_render_profile p = sdl3d_profile_modern();
    EXPECT_EQ(p.uv_mode, SDL3D_UV_PERSPECTIVE);
}

/* ================================================================== */
/* Vertex snap                                                        */
/* ================================================================== */

TEST(SDL3DVertexSnap, PS1ProfileEnablesSnap)
{
    sdl3d_render_profile p = sdl3d_profile_ps1();
    EXPECT_TRUE(p.vertex_snap);
    EXPECT_EQ(p.vertex_snap_precision, 1);
}

TEST(SDL3DVertexSnap, ModernProfileDisablesSnap)
{
    sdl3d_render_profile p = sdl3d_profile_modern();
    EXPECT_FALSE(p.vertex_snap);
}
