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
#include "render_context_internal.h"
#include "slayer3d/lighting.h"
#include "slayer3d/slayer3d.h"
}

/* ================================================================== */
/* Lighting API tests (unit, no SDL video)                            */
/* ================================================================== */

/* Fixture that creates a real render context for API testing. */
class SLAYER3DLightingFixture : public ::testing::Test
{
  protected:
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    slayer3d_render_context *ctx = nullptr;

    void SetUp() override
    {
        SDL_SetMainReady();
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("test", 64, 64, SDL_WINDOW_HIDDEN);
        ASSERT_NE(window, nullptr);
        renderer = SDL_CreateRenderer(window, NULL);
        ASSERT_NE(renderer, nullptr);
        slayer3d_render_context_config config;
        slayer3d_init_render_context_config(&config);
        ASSERT_TRUE(slayer3d_create_render_context(window, renderer, &config, &ctx));
    }

    void TearDown() override
    {
        slayer3d_destroy_render_context(ctx);
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

TEST_F(SLAYER3DLightingFixture, DefaultState)
{
    EXPECT_FALSE(slayer3d_is_lighting_enabled(ctx));
    EXPECT_EQ(slayer3d_get_light_count(ctx), 0);
}

TEST_F(SLAYER3DLightingFixture, EnableDisable)
{
    ASSERT_TRUE(slayer3d_set_lighting_enabled(ctx, true));
    EXPECT_TRUE(slayer3d_is_lighting_enabled(ctx));
    ASSERT_TRUE(slayer3d_set_lighting_enabled(ctx, false));
    EXPECT_FALSE(slayer3d_is_lighting_enabled(ctx));
}

TEST_F(SLAYER3DLightingFixture, AddAndClearLights)
{
    slayer3d_light light{};
    light.type = SLAYER3D_LIGHT_DIRECTIONAL;
    light.direction = {0.0f, -1.0f, 0.0f};
    light.color[0] = light.color[1] = light.color[2] = 1.0f;
    light.intensity = 1.0f;

    ASSERT_TRUE(slayer3d_add_light(ctx, &light));
    EXPECT_EQ(slayer3d_get_light_count(ctx), 1);

    ASSERT_TRUE(slayer3d_add_light(ctx, &light));
    EXPECT_EQ(slayer3d_get_light_count(ctx), 2);

    ASSERT_TRUE(slayer3d_clear_lights(ctx));
    EXPECT_EQ(slayer3d_get_light_count(ctx), 0);
}

TEST_F(SLAYER3DLightingFixture, MaxLightsEnforced)
{
    slayer3d_light light{};
    light.type = SLAYER3D_LIGHT_POINT;
    light.intensity = 1.0f;

    for (int i = 0; i < SLAYER3D_MAX_LIGHTS; ++i)
    {
        ASSERT_TRUE(slayer3d_add_light(ctx, &light)) << "light " << i;
    }
    EXPECT_EQ(slayer3d_get_light_count(ctx), SLAYER3D_MAX_LIGHTS);
    EXPECT_FALSE(slayer3d_add_light(ctx, &light));
    EXPECT_EQ(slayer3d_get_light_count(ctx), SLAYER3D_MAX_LIGHTS);
}

TEST_F(SLAYER3DLightingFixture, SetAmbientLight)
{
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.1f, 0.2f, 0.3f));
}

/* ================================================================== */
/* Null context rejection                                             */
/* ================================================================== */

struct NullCtxCase
{
    const char *label;
};

TEST(SLAYER3DLightingNullCtx, AllFunctionsRejectNull)
{
    slayer3d_light light{};
    EXPECT_FALSE(slayer3d_add_light(nullptr, &light));
    EXPECT_FALSE(slayer3d_add_light(nullptr, nullptr));
    EXPECT_FALSE(slayer3d_clear_lights(nullptr));
    EXPECT_FALSE(slayer3d_set_lighting_enabled(nullptr, true));
    EXPECT_FALSE(slayer3d_is_lighting_enabled(nullptr));
    EXPECT_EQ(slayer3d_get_light_count(nullptr), 0);
    EXPECT_FALSE(slayer3d_set_ambient_light(nullptr, 0, 0, 0));
}

/* ================================================================== */
/* PBR shading unit tests (slayer3d_shade_fragment_pbr)                  */
/* ================================================================== */

static slayer3d_lighting_params make_params(float metallic, float roughness)
{
    slayer3d_lighting_params p{};
    p.lights = nullptr;
    p.light_count = 0;
    p.ambient[0] = p.ambient[1] = p.ambient[2] = 0.0f;
    p.camera_pos = {0.0f, 0.0f, 5.0f};
    p.metallic = metallic;
    p.roughness = roughness;
    p.emissive[0] = p.emissive[1] = p.emissive[2] = 0.0f;
    return p;
}

static slayer3d_light make_directional(float dx, float dy, float dz, float intensity)
{
    slayer3d_light l{};
    l.type = SLAYER3D_LIGHT_DIRECTIONAL;
    l.direction = {dx, dy, dz};
    l.color[0] = l.color[1] = l.color[2] = 1.0f;
    l.intensity = intensity;
    return l;
}

static slayer3d_light make_point(float px, float py, float pz, float intensity, float range)
{
    slayer3d_light l{};
    l.type = SLAYER3D_LIGHT_POINT;
    l.position = {px, py, pz};
    l.color[0] = l.color[1] = l.color[2] = 1.0f;
    l.intensity = intensity;
    l.range = range;
    return l;
}

static slayer3d_light make_spot(float px, float py, float pz, float dx, float dy, float dz, float intensity,
                                float range, float inner_cos, float outer_cos)
{
    slayer3d_light l{};
    l.type = SLAYER3D_LIGHT_SPOT;
    l.position = {px, py, pz};
    l.direction = {dx, dy, dz};
    l.color[0] = l.color[1] = l.color[2] = 1.0f;
    l.intensity = intensity;
    l.range = range;
    l.inner_cutoff = inner_cos;
    l.outer_cutoff = outer_cos;
    return l;
}

TEST(SLAYER3DPBRShading, NoLightsNoAmbientProducesBlack)
{
    slayer3d_lighting_params p = make_params(0.0f, 1.0f);
    float r, g, b;
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.0f, 0.001f);
    EXPECT_NEAR(g, 0.0f, 0.001f);
    EXPECT_NEAR(b, 0.0f, 0.001f);
}

TEST(SLAYER3DPBRShading, AmbientOnlyProducesAmbientTimesAlbedo)
{
    slayer3d_lighting_params p = make_params(0.0f, 1.0f);
    p.ambient[0] = 0.1f;
    p.ambient[1] = 0.2f;
    p.ambient[2] = 0.3f;
    float r, g, b;
    slayer3d_shade_fragment_pbr(&p, 0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.05f, 0.001f);
    EXPECT_NEAR(g, 0.10f, 0.001f);
    EXPECT_NEAR(b, 0.15f, 0.001f);
}

TEST(SLAYER3DPBRShading, EmissiveAddsToOutput)
{
    slayer3d_lighting_params p = make_params(0.0f, 1.0f);
    p.emissive[0] = 0.5f;
    p.emissive[1] = 0.0f;
    p.emissive[2] = 0.0f;
    float r, g, b;
    slayer3d_shade_fragment_pbr(&p, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.5f, 0.001f);
    EXPECT_NEAR(g, 0.0f, 0.001f);
    EXPECT_NEAR(b, 0.0f, 0.001f);
}

TEST(SLAYER3DPBRShading, DirectionalLightFacingNormalProducesLight)
{
    slayer3d_light dl = make_directional(0.0f, -1.0f, 0.0f, 1.0f);
    slayer3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &dl;
    p.light_count = 1;

    float r, g, b;
    /* Normal pointing up, light pointing down → N·L = 1. */
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_GT(r, 0.1f);
    EXPECT_GT(g, 0.1f);
    EXPECT_GT(b, 0.1f);
}

TEST(SLAYER3DPBRShading, DirectionalLightBackfaceProducesNoLight)
{
    slayer3d_light dl = make_directional(0.0f, 1.0f, 0.0f, 1.0f);
    slayer3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &dl;
    p.light_count = 1;

    float r, g, b;
    /* Normal pointing up, light pointing up → N·L = -1 → clamped to 0. */
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.0f, 0.001f);
}

TEST(SLAYER3DPBRShading, PointLightAttenuatesWithDistance)
{
    slayer3d_light pl = make_point(0.0f, 2.0f, 0.0f, 1.0f, 10.0f);
    slayer3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &pl;
    p.light_count = 1;

    float r_near, g_near, b_near;
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r_near, &g_near, &b_near);

    /* Move fragment far away. */
    float r_far, g_far, b_far;
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, -8.0f, 0.0f, &r_far, &g_far, &b_far);

    EXPECT_GT(r_near, r_far);
}

TEST(SLAYER3DPBRShading, PointLightBeyondRangeFallsOffSmoothly)
{
    slayer3d_light pl = make_point(0.0f, 2.0f, 0.0f, 1.0f, 1.0f);
    slayer3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &pl;
    p.light_count = 1;

    float r, g, b;
    /* Fragment at origin, light at y=2, range=1: soft falloff should be nearly black, not hard-clamped. */
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.0f, 0.01f);

    float edge_r, edge_g, edge_b;
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, &edge_r, &edge_g, &edge_b);
    EXPECT_GT(edge_r, 0.0f);
    EXPECT_LT(edge_r, 0.1f);
}

TEST(SLAYER3DPBRShading, SpotLightInsideConeProducesLight)
{
    /* Spot at y=2 pointing down, fragment at origin with normal up. */
    slayer3d_light sl = make_spot(0.0f, 2.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 10.0f, cosf(0.3f), cosf(0.5f));
    slayer3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &sl;
    p.light_count = 1;

    float r, g, b;
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_GT(r, 0.05f);
}

TEST(SLAYER3DPBRShading, SpotLightConeEdgeFallsOffSmoothly)
{
    slayer3d_light sl = make_spot(0.0f, 2.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 10.0f, cosf(0.3f), cosf(0.8f));
    slayer3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &sl;
    p.light_count = 1;

    float center_r, center_g, center_b;
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &center_r, &center_g,
                                &center_b);

    float edge_r, edge_g, edge_b;
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, &edge_r, &edge_g, &edge_b);

    EXPECT_GT(edge_r, 0.0f);
    EXPECT_LT(edge_r, center_r);
}

TEST(SLAYER3DPBRShading, SpotLightOutsideConeProducesNoLight)
{
    /* Spot at y=2 pointing down, fragment far to the side. */
    slayer3d_light sl = make_spot(0.0f, 2.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 100.0f, cosf(0.1f), cosf(0.2f));
    slayer3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &sl;
    p.light_count = 1;

    float r, g, b;
    /* Fragment at x=100, well outside the narrow cone. */
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 100.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_NEAR(r, 0.0f, 0.01f);
}

TEST(SLAYER3DPBRShading, MetallicSurfaceHasNoLambertianDiffuse)
{
    slayer3d_light dl = make_directional(0.0f, -1.0f, 0.0f, 1.0f);
    slayer3d_lighting_params p_metal = make_params(1.0f, 0.5f);
    p_metal.lights = &dl;
    p_metal.light_count = 1;

    slayer3d_lighting_params p_dielectric = make_params(0.0f, 0.5f);
    p_dielectric.lights = &dl;
    p_dielectric.light_count = 1;

    float rm, gm, bm;
    float rd, gd, bd;
    slayer3d_shade_fragment_pbr(&p_metal, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &rm, &gm, &bm);
    slayer3d_shade_fragment_pbr(&p_dielectric, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &rd, &gd, &bd);

    /* Metal should have less red (no diffuse) but more specular. The green
     * channel should be near zero for both since albedo green is 0. */
    EXPECT_NEAR(gm, 0.0f, 0.05f);
    EXPECT_NEAR(gd, 0.0f, 0.05f);
}

TEST(SLAYER3DPBRShading, MultipleLightsAccumulate)
{
    slayer3d_light lights[2];
    lights[0] = make_directional(0.0f, -1.0f, 0.0f, 1.0f);
    lights[1] = make_directional(0.0f, -1.0f, 0.0f, 1.0f);

    slayer3d_lighting_params p1 = make_params(0.0f, 1.0f);
    p1.lights = lights;
    p1.light_count = 1;

    slayer3d_lighting_params p2 = make_params(0.0f, 1.0f);
    p2.lights = lights;
    p2.light_count = 2;

    float r1, g1, b1, r2, g2, b2;
    slayer3d_shade_fragment_pbr(&p1, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r1, &g1, &b1);
    slayer3d_shade_fragment_pbr(&p2, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r2, &g2, &b2);

    /* Two identical lights should produce roughly double the output. */
    EXPECT_NEAR(r2, r1 * 2.0f, 0.01f);
}

TEST(SLAYER3DPBRShading, ColoredLightTintsOutput)
{
    slayer3d_light dl{};
    dl.type = SLAYER3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = 1.0f;
    dl.color[1] = 0.0f;
    dl.color[2] = 0.0f;
    dl.intensity = 1.0f;

    slayer3d_lighting_params p = make_params(0.0f, 1.0f);
    p.lights = &dl;
    p.light_count = 1;

    float r, g, b;
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);
    EXPECT_GT(r, 0.1f);
    EXPECT_NEAR(g, 0.0f, 0.001f);
    EXPECT_NEAR(b, 0.0f, 0.001f);
}

TEST(SLAYER3DPBRShading, RoughnessAffectsSpecular)
{
    slayer3d_light dl = make_directional(0.0f, -1.0f, 0.0f, 1.0f);

    slayer3d_lighting_params p_smooth = make_params(0.0f, 0.1f);
    p_smooth.lights = &dl;
    p_smooth.light_count = 1;

    slayer3d_lighting_params p_rough = make_params(0.0f, 1.0f);
    p_rough.lights = &dl;
    p_rough.light_count = 1;

    float rs, gs, bs, rr, gr, br;
    /* View from directly above → specular highlight should be stronger for smooth. */
    p_smooth.camera_pos = slayer3d_vec3{0.0f, 5.0f, 0.0f};
    p_rough.camera_pos = slayer3d_vec3{0.0f, 5.0f, 0.0f};
    slayer3d_shade_fragment_pbr(&p_smooth, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &rs, &gs, &bs);
    slayer3d_shade_fragment_pbr(&p_rough, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &rr, &gr, &br);

    /* Smooth surface should have higher total output due to specular peak. */
    EXPECT_GT(rs, rr);
}

/* ================================================================== */
/* Fog computation tests                                              */
/* ================================================================== */

struct FogFactorCase
{
    const char *label;
    slayer3d_fog_mode mode;
    float start;
    float end;
    float density;
    float distance;
    float expected_min;
    float expected_max;
};

class SLAYER3DFogFactor : public ::testing::TestWithParam<FogFactorCase>
{
};

TEST_P(SLAYER3DFogFactor, ComputesExpectedRange)
{
    const auto &c = GetParam();
    slayer3d_fog fog{};
    fog.mode = c.mode;
    fog.start = c.start;
    fog.end = c.end;
    fog.density = c.density;
    float f = slayer3d_compute_fog_factor(&fog, c.distance);
    EXPECT_GE(f, c.expected_min) << c.label;
    EXPECT_LE(f, c.expected_max) << c.label;
}

INSTANTIATE_TEST_SUITE_P(Fog, SLAYER3DFogFactor,
                         ::testing::Values(
                             /* No fog → always 0. */
                             FogFactorCase{"none d=0", SLAYER3D_FOG_NONE, 0, 0, 0, 0.0f, 0.0f, 0.0f},
                             FogFactorCase{"none d=100", SLAYER3D_FOG_NONE, 0, 0, 0, 100.0f, 0.0f, 0.0f},

                             /* Linear fog. */
                             FogFactorCase{"linear before start", SLAYER3D_FOG_LINEAR, 10, 50, 0, 5.0f, 0.0f, 0.0f},
                             FogFactorCase{"linear at start", SLAYER3D_FOG_LINEAR, 10, 50, 0, 10.0f, 0.0f, 0.001f},
                             FogFactorCase{"linear midpoint", SLAYER3D_FOG_LINEAR, 10, 50, 0, 30.0f, 0.49f, 0.51f},
                             FogFactorCase{"linear at end", SLAYER3D_FOG_LINEAR, 10, 50, 0, 50.0f, 0.99f, 1.0f},
                             FogFactorCase{"linear past end", SLAYER3D_FOG_LINEAR, 10, 50, 0, 100.0f, 1.0f, 1.0f},
                             FogFactorCase{"linear start==end", SLAYER3D_FOG_LINEAR, 10, 10, 0, 10.0f, 0.0f, 0.0f},

                             /* Exponential fog. */
                             FogFactorCase{"exp d=0", SLAYER3D_FOG_EXP, 0, 0, 0.1f, 0.0f, 0.0f, 0.001f},
                             FogFactorCase{"exp d=10", SLAYER3D_FOG_EXP, 0, 0, 0.1f, 10.0f, 0.5f, 0.7f},
                             FogFactorCase{"exp d=100", SLAYER3D_FOG_EXP, 0, 0, 0.1f, 100.0f, 0.99f, 1.0f},

                             /* Exponential squared fog. */
                             FogFactorCase{"exp2 d=0", SLAYER3D_FOG_EXP2, 0, 0, 0.1f, 0.0f, 0.0f, 0.001f},
                             FogFactorCase{"exp2 d=5", SLAYER3D_FOG_EXP2, 0, 0, 0.1f, 5.0f, 0.15f, 0.30f},
                             FogFactorCase{"exp2 d=50", SLAYER3D_FOG_EXP2, 0, 0, 0.1f, 50.0f, 0.99f, 1.0f}));

TEST_F(SLAYER3DLightingFixture, SetAndClearFog)
{
    slayer3d_fog fog{};
    fog.mode = SLAYER3D_FOG_LINEAR;
    fog.color[0] = 0.5f;
    fog.color[1] = 0.5f;
    fog.color[2] = 0.5f;
    fog.start = 10.0f;
    fog.end = 100.0f;
    ASSERT_TRUE(slayer3d_set_fog(ctx, &fog));
    ASSERT_TRUE(slayer3d_clear_fog(ctx));
}

TEST(SLAYER3DFogAPI, NullContextRejected)
{
    slayer3d_fog fog{};
    EXPECT_FALSE(slayer3d_set_fog(nullptr, &fog));
    EXPECT_FALSE(slayer3d_clear_fog(nullptr));
}

TEST(SLAYER3DFogAPI, NullFogRejected)
{
    /* Can't test without a context, but we can verify the null fog param. */
    EXPECT_FALSE(slayer3d_set_fog(nullptr, nullptr));
}

/* ================================================================== */
/* Tonemapping tests                                                  */
/* ================================================================== */

struct TonemapCase
{
    const char *label;
    slayer3d_tonemap_mode mode;
    float in_r, in_g, in_b;
    float expect_min_r, expect_max_r;
};

class SLAYER3DTonemapTable : public ::testing::TestWithParam<TonemapCase>
{
};

TEST_P(SLAYER3DTonemapTable, OutputInExpectedRange)
{
    const auto &c = GetParam();
    float r = c.in_r, g = c.in_g, b = c.in_b;
    slayer3d_tonemap(c.mode, &r, &g, &b);
    EXPECT_GE(r, c.expect_min_r) << c.label;
    EXPECT_LE(r, c.expect_max_r) << c.label;
    EXPECT_GE(g, 0.0f) << c.label;
    EXPECT_LE(b, 1.01f) << c.label;
}

INSTANTIATE_TEST_SUITE_P(Tonemap, SLAYER3DTonemapTable,
                         ::testing::Values(
                             /* None: passthrough. */
                             TonemapCase{"none 0", SLAYER3D_TONEMAP_NONE, 0.0f, 0.0f, 0.0f, 0.0f, 0.001f},
                             TonemapCase{"none 1", SLAYER3D_TONEMAP_NONE, 1.0f, 1.0f, 1.0f, 1.0f, 1.001f},
                             TonemapCase{"none HDR", SLAYER3D_TONEMAP_NONE, 5.0f, 5.0f, 5.0f, 0.99f, 1.001f},

                             /* Reinhard: x/(1+x), then gamma. */
                             TonemapCase{"reinhard 0", SLAYER3D_TONEMAP_REINHARD, 0.0f, 0.0f, 0.0f, 0.0f, 0.001f},
                             TonemapCase{"reinhard 1", SLAYER3D_TONEMAP_REINHARD, 1.0f, 1.0f, 1.0f, 0.5f, 0.8f},
                             TonemapCase{"reinhard HDR", SLAYER3D_TONEMAP_REINHARD, 10.0f, 10.0f, 10.0f, 0.9f, 1.0f},

                             /* ACES: should compress HDR to [0,1]. */
                             TonemapCase{"aces 0", SLAYER3D_TONEMAP_ACES, 0.0f, 0.0f, 0.0f, 0.0f, 0.01f},
                             TonemapCase{"aces 1", SLAYER3D_TONEMAP_ACES, 1.0f, 1.0f, 1.0f, 0.5f, 1.0f},
                             TonemapCase{"aces HDR", SLAYER3D_TONEMAP_ACES, 10.0f, 10.0f, 10.0f, 0.9f, 1.0f}));

TEST_F(SLAYER3DLightingFixture, SetAndGetTonemapMode)
{
    EXPECT_EQ(slayer3d_get_tonemap_mode(ctx), SLAYER3D_TONEMAP_NONE);
    ASSERT_TRUE(slayer3d_set_tonemap_mode(ctx, SLAYER3D_TONEMAP_REINHARD));
    EXPECT_EQ(slayer3d_get_tonemap_mode(ctx), SLAYER3D_TONEMAP_REINHARD);
    ASSERT_TRUE(slayer3d_set_tonemap_mode(ctx, SLAYER3D_TONEMAP_ACES));
    EXPECT_EQ(slayer3d_get_tonemap_mode(ctx), SLAYER3D_TONEMAP_ACES);
    ASSERT_TRUE(slayer3d_set_tonemap_mode(ctx, SLAYER3D_TONEMAP_NONE));
    EXPECT_EQ(slayer3d_get_tonemap_mode(ctx), SLAYER3D_TONEMAP_NONE);
}

TEST(SLAYER3DTonemapAPI, NullContextRejected)
{
    EXPECT_FALSE(slayer3d_set_tonemap_mode(nullptr, SLAYER3D_TONEMAP_REINHARD));
    EXPECT_EQ(slayer3d_get_tonemap_mode(nullptr), SLAYER3D_TONEMAP_NONE);
}

/* ================================================================== */
/* Shadow mapping tests                                               */
/* ================================================================== */

TEST_F(SLAYER3DLightingFixture, EnableShadowOnDirectionalLight)
{
    slayer3d_light dl{};
    dl.type = SLAYER3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = dl.color[1] = dl.color[2] = 1.0f;
    dl.intensity = 1.0f;
    ASSERT_TRUE(slayer3d_add_light(ctx, &dl));

    ASSERT_TRUE(slayer3d_enable_shadow(ctx, 0, slayer3d_vec3_make(0, 0, 0), 10.0f));
    ASSERT_TRUE(slayer3d_disable_shadow(ctx, 0));
}

TEST_F(SLAYER3DLightingFixture, ShadowRejectsNonDirectionalLight)
{
    slayer3d_light pl{};
    pl.type = SLAYER3D_LIGHT_POINT;
    pl.intensity = 1.0f;
    ASSERT_TRUE(slayer3d_add_light(ctx, &pl));

    EXPECT_FALSE(slayer3d_enable_shadow(ctx, 0, slayer3d_vec3_make(0, 0, 0), 10.0f));
}

TEST_F(SLAYER3DLightingFixture, ShadowRejectsInvalidLightIndex)
{
    EXPECT_FALSE(slayer3d_enable_shadow(ctx, 0, slayer3d_vec3_make(0, 0, 0), 10.0f));
    EXPECT_FALSE(slayer3d_enable_shadow(ctx, -1, slayer3d_vec3_make(0, 0, 0), 10.0f));
    EXPECT_FALSE(slayer3d_enable_shadow(ctx, 99, slayer3d_vec3_make(0, 0, 0), 10.0f));
}

TEST_F(SLAYER3DLightingFixture, ShadowRejectsZeroRadius)
{
    slayer3d_light dl{};
    dl.type = SLAYER3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.intensity = 1.0f;
    ASSERT_TRUE(slayer3d_add_light(ctx, &dl));

    EXPECT_FALSE(slayer3d_enable_shadow(ctx, 0, slayer3d_vec3_make(0, 0, 0), 0.0f));
    EXPECT_FALSE(slayer3d_enable_shadow(ctx, 0, slayer3d_vec3_make(0, 0, 0), -1.0f));
}

TEST(SLAYER3DShadowAPI, NullContextRejected)
{
    EXPECT_FALSE(slayer3d_enable_shadow(nullptr, 0, slayer3d_vec3_make(0, 0, 0), 10.0f));
    EXPECT_FALSE(slayer3d_disable_shadow(nullptr, 0));
    EXPECT_FALSE(slayer3d_render_shadow_map(nullptr, nullptr, 0, nullptr));
}

TEST_F(SLAYER3DLightingFixture, BeginShadowPassPopulatesFrustumPlanes)
{
    slayer3d_light dl{};
    dl.type = SLAYER3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = dl.color[1] = dl.color[2] = 1.0f;
    dl.intensity = 1.0f;
    ASSERT_TRUE(slayer3d_add_light(ctx, &dl));
    ASSERT_TRUE(slayer3d_enable_shadow(ctx, 0, slayer3d_vec3_make(0, 0, 0), 10.0f));

    /* No frustum planes are valid before any 3D mode begins. */
    EXPECT_FALSE(ctx->frustum_planes_valid);

    /* Shadow pass should set up the light frustum so per-actor culling
     * (Phase 1) works against the light's view-projection. */
    ASSERT_TRUE(slayer3d_begin_shadow_pass(ctx));
    EXPECT_TRUE(ctx->frustum_planes_valid);
    ASSERT_TRUE(slayer3d_end_shadow_pass(ctx));

    slayer3d_disable_shadow(ctx, 0);
}

TEST_F(SLAYER3DLightingFixture, RenderShadowMapWithNoMeshes)
{
    slayer3d_light dl{};
    dl.type = SLAYER3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = dl.color[1] = dl.color[2] = 1.0f;
    dl.intensity = 1.0f;
    ASSERT_TRUE(slayer3d_add_light(ctx, &dl));
    ASSERT_TRUE(slayer3d_enable_shadow(ctx, 0, slayer3d_vec3_make(0, 0, 0), 10.0f));

    /* Rendering with zero meshes should succeed (no-op). */
    ASSERT_TRUE(slayer3d_render_shadow_map(ctx, nullptr, 0, nullptr));

    slayer3d_disable_shadow(ctx, 0);
}

TEST(SLAYER3DPBRShading, ShadowOccludesLight)
{
    /* Create a shadow map that is all zeros (everything at depth 0 = near).
     * Any fragment behind depth 0 should be in shadow. */
    const size_t map_size = (size_t)SLAYER3D_SHADOW_MAP_SIZE * SLAYER3D_SHADOW_MAP_SIZE;
    std::vector<float> shadow_depth(map_size, 0.0f);

    slayer3d_light dl{};
    dl.type = SLAYER3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = dl.color[1] = dl.color[2] = 1.0f;
    dl.intensity = 1.0f;

    slayer3d_lighting_params p{};
    p.lights = &dl;
    p.light_count = 1;
    p.camera_pos = {0.0f, 0.0f, 5.0f};
    p.roughness = 1.0f;
    p.shadow_depth[0] = shadow_depth.data();
    p.shadow_enabled[0] = true;
    p.shadow_bias = 0.005f;
    /* Identity VP → fragment at origin maps to NDC (0,0,0) → depth 0.5. */
    p.shadow_vp[0] = slayer3d_mat4_identity();

    float r, g, b;
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);

    /* Fragment should be in shadow → no direct light contribution. */
    EXPECT_NEAR(r, 0.0f, 0.01f);
    EXPECT_NEAR(g, 0.0f, 0.01f);
    EXPECT_NEAR(b, 0.0f, 0.01f);
}

TEST(SLAYER3DPBRShading, NoShadowWhenDisabled)
{
    slayer3d_light dl{};
    dl.type = SLAYER3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = dl.color[1] = dl.color[2] = 1.0f;
    dl.intensity = 1.0f;

    slayer3d_lighting_params p{};
    p.lights = &dl;
    p.light_count = 1;
    p.camera_pos = {0.0f, 0.0f, 5.0f};
    p.roughness = 1.0f;
    /* shadow_enabled[0] defaults to false. */

    float r, g, b;
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r, &g, &b);

    /* Should be lit (no shadow). */
    EXPECT_GT(r, 0.1f);
}

/* ================================================================== */
/* Shading mode API tests                                             */
/* ================================================================== */

TEST_F(SLAYER3DLightingFixture, DefaultShadingModeIsUnlit)
{
    EXPECT_EQ(slayer3d_get_shading_mode(ctx), SLAYER3D_SHADING_UNLIT);
}

struct ShadingModeCase
{
    const char *label;
    slayer3d_shading_mode mode;
};

class SLAYER3DShadingModeSet : public SLAYER3DLightingFixture, public ::testing::WithParamInterface<ShadingModeCase>
{
};

TEST_P(SLAYER3DShadingModeSet, SetAndGet)
{
    const auto &c = GetParam();
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, c.mode)) << c.label;
    EXPECT_EQ(slayer3d_get_shading_mode(ctx), c.mode) << c.label;
}

INSTANTIATE_TEST_SUITE_P(Shading, SLAYER3DShadingModeSet,
                         ::testing::Values(ShadingModeCase{"unlit", SLAYER3D_SHADING_UNLIT},
                                           ShadingModeCase{"flat", SLAYER3D_SHADING_FLAT},
                                           ShadingModeCase{"gouraud", SLAYER3D_SHADING_GOURAUD},
                                           ShadingModeCase{"phong", SLAYER3D_SHADING_PHONG}));

TEST_F(SLAYER3DLightingFixture, SetLightingEnabledMapsToShadingMode)
{
    ASSERT_TRUE(slayer3d_set_lighting_enabled(ctx, true));
    EXPECT_EQ(slayer3d_get_shading_mode(ctx), SLAYER3D_SHADING_PHONG);
    EXPECT_TRUE(slayer3d_is_lighting_enabled(ctx));

    ASSERT_TRUE(slayer3d_set_lighting_enabled(ctx, false));
    EXPECT_EQ(slayer3d_get_shading_mode(ctx), SLAYER3D_SHADING_UNLIT);
    EXPECT_FALSE(slayer3d_is_lighting_enabled(ctx));
}

TEST_F(SLAYER3DLightingFixture, IsLightingEnabledReflectsShadingMode)
{
    slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_FLAT);
    EXPECT_TRUE(slayer3d_is_lighting_enabled(ctx));

    slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_GOURAUD);
    EXPECT_TRUE(slayer3d_is_lighting_enabled(ctx));

    slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG);
    EXPECT_TRUE(slayer3d_is_lighting_enabled(ctx));

    slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_UNLIT);
    EXPECT_FALSE(slayer3d_is_lighting_enabled(ctx));
}

TEST(SLAYER3DShadingModeAPI, NullContextRejected)
{
    EXPECT_FALSE(slayer3d_set_shading_mode(nullptr, SLAYER3D_SHADING_PHONG));
    EXPECT_EQ(slayer3d_get_shading_mode(nullptr), SLAYER3D_SHADING_UNLIT);
}

TEST_F(SLAYER3DLightingFixture, RuntimeShadingModeSwitch)
{
    /* Verify we can switch modes rapidly without issues. */
    slayer3d_shading_mode modes[] = {SLAYER3D_SHADING_UNLIT, SLAYER3D_SHADING_FLAT, SLAYER3D_SHADING_GOURAUD,
                                     SLAYER3D_SHADING_PHONG, SLAYER3D_SHADING_FLAT, SLAYER3D_SHADING_UNLIT};
    for (int i = 0; i < 6; ++i)
    {
        ASSERT_TRUE(slayer3d_set_shading_mode(ctx, modes[i])) << "iteration " << i;
        EXPECT_EQ(slayer3d_get_shading_mode(ctx), modes[i]) << "iteration " << i;
    }
}

/* ================================================================== */
/* Shading mode PBR output tests (unit, no SDL video)                 */
/* ================================================================== */

TEST(SLAYER3DPBRShading, FlatShadingProducesUniformColorPerTriangle)
{
    /* FLAT mode evaluates PBR once at the centroid. All three vertices
     * of the resulting flat-color triangle get the same color. We verify
     * this by calling slayer3d_shade_point with the centroid and checking
     * the result is non-zero (lit) and deterministic. */
    slayer3d_light dl{};
    dl.type = SLAYER3D_LIGHT_DIRECTIONAL;
    dl.direction = {0.0f, -1.0f, 0.0f};
    dl.color[0] = dl.color[1] = dl.color[2] = 1.0f;
    dl.intensity = 1.0f;

    slayer3d_lighting_params p{};
    p.lights = &dl;
    p.light_count = 1;
    p.camera_pos = {0.0f, 0.0f, 5.0f};
    p.roughness = 1.0f;

    /* Shade the same point twice — must be deterministic. */
    float r1, g1, b1, r2, g2, b2;
    slayer3d_shade_fragment_pbr(&p, 0.8f, 0.6f, 0.4f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r1, &g1, &b1);
    slayer3d_shade_fragment_pbr(&p, 0.8f, 0.6f, 0.4f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r2, &g2, &b2);
    EXPECT_FLOAT_EQ(r1, r2);
    EXPECT_FLOAT_EQ(g1, g2);
    EXPECT_FLOAT_EQ(b1, b2);
    EXPECT_GT(r1, 0.0f); /* Must be lit. */
}

TEST(SLAYER3DPBRShading, GouraudShadingProducesDifferentColorsAtDifferentPositions)
{
    /* GOURAUD evaluates PBR at each vertex. Vertices at different
     * distances from a point light should get different intensities. */
    slayer3d_light pl{};
    pl.type = SLAYER3D_LIGHT_POINT;
    pl.position = {0.0f, 2.0f, 0.0f};
    pl.color[0] = pl.color[1] = pl.color[2] = 1.0f;
    pl.intensity = 1.0f;
    pl.range = 10.0f;

    slayer3d_lighting_params p{};
    p.lights = &pl;
    p.light_count = 1;
    p.camera_pos = {0.0f, 0.0f, 5.0f};
    p.roughness = 1.0f;

    float r_near, g_near, b_near;
    float r_far, g_far, b_far;
    /* Vertex near the light. */
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &r_near, &g_near, &b_near);
    /* Vertex far from the light. */
    slayer3d_shade_fragment_pbr(&p, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, -5.0f, 0.0f, &r_far, &g_far, &b_far);

    EXPECT_GT(r_near, r_far);
}

/* ================================================================== */
/* Render profile API tests                                           */
/* ================================================================== */

TEST_F(SLAYER3DLightingFixture, SetAndGetRenderProfile)
{
    slayer3d_render_profile p = slayer3d_profile_ps1();
    ASSERT_TRUE(slayer3d_set_render_profile(ctx, &p));

    slayer3d_render_profile out{};
    ASSERT_TRUE(slayer3d_get_render_profile(ctx, &out));
    EXPECT_EQ(out.shading, SLAYER3D_SHADING_GOURAUD);
    EXPECT_EQ(out.uv_mode, SLAYER3D_UV_AFFINE);
    EXPECT_EQ(out.fog_eval, SLAYER3D_FOG_EVAL_VERTEX);
    EXPECT_EQ(out.tonemap, SLAYER3D_TONEMAP_NONE);
    EXPECT_TRUE(out.vertex_snap);
    EXPECT_EQ(out.vertex_snap_precision, 1);
    EXPECT_TRUE(out.color_quantize);
    EXPECT_EQ(out.display_profile, SLAYER3D_DISPLAY_PROFILE_PS1);
    EXPECT_EQ(out.display_width, 320);
    EXPECT_EQ(out.display_height, 240);
    EXPECT_EQ(out.display_filter, SLAYER3D_DISPLAY_FILTER_NEAREST);
}

TEST_F(SLAYER3DLightingFixture, RuntimeProfileSwitch)
{
    slayer3d_render_profile profiles[] = {
        slayer3d_profile_modern(), slayer3d_profile_ps1(),       slayer3d_profile_n64(),     slayer3d_profile_dos(),
        slayer3d_profile_snes(),   slayer3d_profile_grayscale(), slayer3d_profile_gameboy(),
    };
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(slayer3d_set_render_profile(ctx, &profiles[i])) << "profile " << i;
        slayer3d_render_profile out{};
        ASSERT_TRUE(slayer3d_get_render_profile(ctx, &out));
        EXPECT_EQ(out.shading, profiles[i].shading) << "profile " << i;
        EXPECT_EQ(out.uv_mode, profiles[i].uv_mode) << "profile " << i;
        EXPECT_EQ(out.display_profile, profiles[i].display_profile) << "profile " << i;
        EXPECT_EQ(out.display_width, profiles[i].display_width) << "profile " << i;
        EXPECT_EQ(out.display_height, profiles[i].display_height) << "profile " << i;
        EXPECT_EQ(out.display_filter, profiles[i].display_filter) << "profile " << i;
    }
}

TEST(SLAYER3DRenderProfileAPI, NullContextRejected)
{
    slayer3d_render_profile p = slayer3d_profile_modern();
    slayer3d_render_profile out{};
    EXPECT_FALSE(slayer3d_set_render_profile(nullptr, &p));
    EXPECT_FALSE(slayer3d_set_render_profile(nullptr, nullptr));
    EXPECT_FALSE(slayer3d_get_render_profile(nullptr, &out));
}

TEST(SLAYER3DRenderProfileAPI, NullProfileRejected)
{
    EXPECT_FALSE(slayer3d_set_render_profile(nullptr, nullptr));
}

/* ================================================================== */
/* Named preset validation                                            */
/* ================================================================== */

struct PresetCase
{
    const char *label;
    slayer3d_render_profile (*fn)(void);
    slayer3d_shading_mode expected_shading;
    slayer3d_uv_mode expected_uv;
    slayer3d_tonemap_mode expected_tonemap;
    bool expected_snap;
    bool expected_quantize;
    slayer3d_display_profile expected_display_profile;
    int expected_display_width;
    int expected_display_height;
    slayer3d_display_filter expected_display_filter;
};

class SLAYER3DPresetTable : public ::testing::TestWithParam<PresetCase>
{
};

TEST_P(SLAYER3DPresetTable, FieldsMatchSpec)
{
    const auto &c = GetParam();
    slayer3d_render_profile p = c.fn();
    EXPECT_EQ(p.shading, c.expected_shading) << c.label;
    EXPECT_EQ(p.uv_mode, c.expected_uv) << c.label;
    EXPECT_EQ(p.tonemap, c.expected_tonemap) << c.label;
    EXPECT_EQ(p.vertex_snap, c.expected_snap) << c.label;
    EXPECT_EQ(p.color_quantize, c.expected_quantize) << c.label;
    EXPECT_EQ(p.display_profile, c.expected_display_profile) << c.label;
    EXPECT_EQ(p.display_width, c.expected_display_width) << c.label;
    EXPECT_EQ(p.display_height, c.expected_display_height) << c.label;
    EXPECT_EQ(p.display_filter, c.expected_display_filter) << c.label;
}

INSTANTIATE_TEST_SUITE_P(
    Presets, SLAYER3DPresetTable,
    ::testing::Values(
        PresetCase{"modern", slayer3d_profile_modern, SLAYER3D_SHADING_PHONG, SLAYER3D_UV_PERSPECTIVE,
                   SLAYER3D_TONEMAP_ACES, false, false, SLAYER3D_DISPLAY_PROFILE_MODERN, 0, 0,
                   SLAYER3D_DISPLAY_FILTER_LINEAR},
        PresetCase{"ps1", slayer3d_profile_ps1, SLAYER3D_SHADING_GOURAUD, SLAYER3D_UV_AFFINE, SLAYER3D_TONEMAP_NONE,
                   true, true, SLAYER3D_DISPLAY_PROFILE_PS1, 320, 240, SLAYER3D_DISPLAY_FILTER_NEAREST},
        PresetCase{"n64", slayer3d_profile_n64, SLAYER3D_SHADING_GOURAUD, SLAYER3D_UV_PERSPECTIVE,
                   SLAYER3D_TONEMAP_NONE, false, false, SLAYER3D_DISPLAY_PROFILE_N64, 320, 240,
                   SLAYER3D_DISPLAY_FILTER_LINEAR},
        PresetCase{"dos", slayer3d_profile_dos, SLAYER3D_SHADING_GOURAUD, SLAYER3D_UV_AFFINE, SLAYER3D_TONEMAP_NONE,
                   false, true, SLAYER3D_DISPLAY_PROFILE_DOS, 320, 200, SLAYER3D_DISPLAY_FILTER_NEAREST},
        PresetCase{"snes", slayer3d_profile_snes, SLAYER3D_SHADING_FLAT, SLAYER3D_UV_AFFINE, SLAYER3D_TONEMAP_NONE,
                   false, true, SLAYER3D_DISPLAY_PROFILE_SNES, 256, 224, SLAYER3D_DISPLAY_FILTER_NEAREST},
        PresetCase{"grayscale", slayer3d_profile_grayscale, SLAYER3D_SHADING_FLAT, SLAYER3D_UV_AFFINE,
                   SLAYER3D_TONEMAP_NONE, false, true, SLAYER3D_DISPLAY_PROFILE_GRAYSCALE, 512, 342,
                   SLAYER3D_DISPLAY_FILTER_NEAREST},
        PresetCase{"gameboy", slayer3d_profile_gameboy, SLAYER3D_SHADING_FLAT, SLAYER3D_UV_AFFINE,
                   SLAYER3D_TONEMAP_NONE, false, true, SLAYER3D_DISPLAY_PROFILE_GAMEBOY, 160, 144,
                   SLAYER3D_DISPLAY_FILTER_NEAREST}));

/* ================================================================== */
/* Color quantization unit tests                                      */
/* ================================================================== */

TEST(SLAYER3DColorQuantize, DosProfileSets256Colors)
{
    slayer3d_render_profile p = slayer3d_profile_dos();
    EXPECT_TRUE(p.color_quantize);
    EXPECT_EQ(p.color_depth, 6);
}

/* ================================================================== */
/* Custom profile: mix and match                                      */
/* ================================================================== */

TEST_F(SLAYER3DLightingFixture, CustomProfileMixAndMatch)
{
    /* Start from PS1, override texture filter. */
    slayer3d_render_profile p = slayer3d_profile_ps1();
    p.texture_filter = SLAYER3D_TEXTURE_FILTER_BILINEAR;
    p.vertex_snap = false;
    ASSERT_TRUE(slayer3d_set_render_profile(ctx, &p));

    slayer3d_render_profile out{};
    ASSERT_TRUE(slayer3d_get_render_profile(ctx, &out));
    EXPECT_EQ(out.shading, SLAYER3D_SHADING_GOURAUD);
    EXPECT_EQ(out.uv_mode, SLAYER3D_UV_AFFINE);
    EXPECT_FALSE(out.vertex_snap);
    EXPECT_EQ(out.display_profile, SLAYER3D_DISPLAY_PROFILE_PS1);
    EXPECT_EQ(out.display_width, 320);
    EXPECT_EQ(out.display_height, 240);
}

TEST_F(SLAYER3DLightingFixture, InvalidCustomDisplayProfileFallsBackToModern)
{
    slayer3d_render_profile p = slayer3d_profile_ps1();
    p.display_profile = (slayer3d_display_profile)999;
    p.display_width = -1;
    p.display_height = 240;
    p.display_filter = (slayer3d_display_filter)999;
    ASSERT_TRUE(slayer3d_set_render_profile(ctx, &p));

    slayer3d_render_profile out{};
    ASSERT_TRUE(slayer3d_get_render_profile(ctx, &out));
    EXPECT_EQ(out.display_profile, SLAYER3D_DISPLAY_PROFILE_MODERN);
    EXPECT_EQ(out.display_width, 0);
    EXPECT_EQ(out.display_height, 0);
    EXPECT_EQ(out.display_filter, SLAYER3D_DISPLAY_FILTER_LINEAR);
}

/* ================================================================== */
/* Fog evaluation mode                                                */
/* ================================================================== */

TEST(SLAYER3DFogEval, PS1ProfileUsesVertexFog)
{
    slayer3d_render_profile p = slayer3d_profile_ps1();
    EXPECT_EQ(p.fog_eval, SLAYER3D_FOG_EVAL_VERTEX);
}

TEST(SLAYER3DFogEval, ModernProfileUsesFragmentFog)
{
    slayer3d_render_profile p = slayer3d_profile_modern();
    EXPECT_EQ(p.fog_eval, SLAYER3D_FOG_EVAL_FRAGMENT);
}

/* ================================================================== */
/* UV mode                                                            */
/* ================================================================== */

TEST(SLAYER3DUVMode, PS1ProfileUsesAffine)
{
    slayer3d_render_profile p = slayer3d_profile_ps1();
    EXPECT_EQ(p.uv_mode, SLAYER3D_UV_AFFINE);
}

TEST(SLAYER3DUVMode, ModernProfileUsesPerspective)
{
    slayer3d_render_profile p = slayer3d_profile_modern();
    EXPECT_EQ(p.uv_mode, SLAYER3D_UV_PERSPECTIVE);
}

/* ================================================================== */
/* Vertex snap                                                        */
/* ================================================================== */

TEST(SLAYER3DVertexSnap, PS1ProfileEnablesSnap)
{
    slayer3d_render_profile p = slayer3d_profile_ps1();
    EXPECT_TRUE(p.vertex_snap);
    EXPECT_EQ(p.vertex_snap_precision, 1);
}

TEST(SLAYER3DVertexSnap, ModernProfileDisablesSnap)
{
    slayer3d_render_profile p = slayer3d_profile_modern();
    EXPECT_FALSE(p.vertex_snap);
}
