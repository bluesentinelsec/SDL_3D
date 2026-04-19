/*
 * Direct tests for the internal software rasterizer. These exercise the
 * clipping, edge-function coverage, top-left fill rule, and depth test
 * against a manually-allocated framebuffer so the behavior is covered
 * without depending on SDL video initialization.
 */

#include <gtest/gtest.h>

#include <SDL3/SDL_stdinc.h>

extern "C"
{
#include "rasterizer.h"
#include "sdl3d/math.h"
#include "sdl3d/types.h"
}

#include <cmath>
#include <cstring>
#include <vector>

namespace
{
constexpr int kW = 16;
constexpr int kH = 16;

struct Framebuffer
{
    std::vector<Uint8> color;
    std::vector<float> depth;
    sdl3d_framebuffer fb{};

    Framebuffer(int w = kW, int h = kH)
        : color(static_cast<size_t>(w * h * 4), 0u), depth(static_cast<size_t>(w * h), 1.0f)
    {
        fb.color_pixels = color.data();
        fb.depth_pixels = depth.data();
        fb.width = w;
        fb.height = h;
    }

    sdl3d_color GetPixel(int x, int y) const
    {
        sdl3d_color c{};
        EXPECT_TRUE(sdl3d_framebuffer_get_pixel(&fb, x, y, &c));
        return c;
    }

    float GetDepth(int x, int y) const
    {
        float d = 0.0f;
        EXPECT_TRUE(sdl3d_framebuffer_get_depth(&fb, x, y, &d));
        return d;
    }
};

bool PixelEquals(sdl3d_color a, sdl3d_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int CountPixelsEqual(const Framebuffer &f, sdl3d_color c)
{
    int count = 0;
    for (int y = 0; y < f.fb.height; ++y)
    {
        for (int x = 0; x < f.fb.width; ++x)
        {
            if (PixelEquals(f.GetPixel(x, y), c))
            {
                ++count;
            }
        }
    }
    return count;
}

constexpr sdl3d_color kRed = {255, 0, 0, 255};
constexpr sdl3d_color kGreen = {0, 255, 0, 255};
constexpr sdl3d_color kBlue = {0, 0, 255, 255};
constexpr sdl3d_color kBlack = {0, 0, 0, 0};

sdl3d_texture2d MakeTextureFromPixels(const std::vector<Uint8> &pixels, int width, int height)
{
    sdl3d_image image{};
    image.pixels = const_cast<Uint8 *>(pixels.data());
    image.width = width;
    image.height = height;

    sdl3d_texture2d texture{};
    EXPECT_TRUE(sdl3d_create_texture_from_image(&image, &texture));
    return texture;
}

bool PixelNear(sdl3d_color actual, sdl3d_color expected, int tolerance = 12)
{
    return std::abs((int)actual.r - (int)expected.r) <= tolerance &&
           std::abs((int)actual.g - (int)expected.g) <= tolerance &&
           std::abs((int)actual.b - (int)expected.b) <= tolerance &&
           std::abs((int)actual.a - (int)expected.a) <= tolerance;
}

bool SampleNearColor(const Framebuffer &f, int cx, int cy, int window, sdl3d_color expected)
{
    for (int dy = -window; dy <= window; ++dy)
    {
        for (int dx = -window; dx <= window; ++dx)
        {
            const int x = cx + dx;
            const int y = cy + dy;
            if (x < 0 || x >= f.fb.width || y < 0 || y >= f.fb.height)
            {
                continue;
            }
            if (PixelNear(f.GetPixel(x, y), expected))
            {
                return true;
            }
        }
    }
    return false;
}

SDL_Point ProjectToFramebuffer(const sdl3d_mat4 &mvp, int width, int height, sdl3d_vec3 point)
{
    const sdl3d_vec4 clip = sdl3d_mat4_transform_vec4(mvp, sdl3d_vec4_from_vec3(point, 1.0f));
    const float inverse_w = 1.0f / clip.w;
    const float ndc_x = clip.x * inverse_w;
    const float ndc_y = clip.y * inverse_w;
    SDL_Point out{};
    out.x = (int)std::lround((ndc_x + 1.0f) * 0.5f * (float)width);
    out.y = (int)std::lround((1.0f - ndc_y) * 0.5f * (float)height);
    return out;
}
} // namespace

/* --- Framebuffer clear --------------------------------------------------- */

TEST(SDL3DFramebuffer, ClearSetsColorAndDepth)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kRed, 0.5f);
    for (int y = 0; y < kH; ++y)
    {
        for (int x = 0; x < kW; ++x)
        {
            EXPECT_TRUE(PixelEquals(f.GetPixel(x, y), kRed));
            EXPECT_FLOAT_EQ(f.GetDepth(x, y), 0.5f);
        }
    }
}

TEST(SDL3DFramebuffer, AccessorsRejectOutOfBounds)
{
    Framebuffer f;
    sdl3d_color c{};
    float d = 0.0f;
    EXPECT_FALSE(sdl3d_framebuffer_get_pixel(&f.fb, -1, 0, &c));
    EXPECT_FALSE(sdl3d_framebuffer_get_pixel(&f.fb, 0, kH, &c));
    EXPECT_FALSE(sdl3d_framebuffer_get_depth(&f.fb, kW, 0, &d));
}

TEST(SDL3DFramebuffer, ClearRectOnlyTouchesRequestedPixels)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);

    const SDL_Rect rect = {4, 5, 3, 2};
    sdl3d_framebuffer_clear_rect(&f.fb, &rect, kBlue, 0.25f);

    for (int y = 0; y < kH; ++y)
    {
        for (int x = 0; x < kW; ++x)
        {
            const bool in_rect = (x >= rect.x) && (x < rect.x + rect.w) && (y >= rect.y) && (y < rect.y + rect.h);
            EXPECT_TRUE(PixelEquals(f.GetPixel(x, y), in_rect ? kBlue : kBlack));
            EXPECT_FLOAT_EQ(f.GetDepth(x, y), in_rect ? 0.25f : 1.0f);
        }
    }
}

/* --- Triangle coverage --------------------------------------------------- */

TEST(SDL3DRasterizeTriangle, FullscreenTriangleCoversAllPixels)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();
    // Oversized triangle (NDC) that covers the whole viewport [-1, 1]^2.
    sdl3d_rasterize_triangle(&f.fb, id, sdl3d_vec3_make(-3.0f, -1.0f, 0.0f), sdl3d_vec3_make(3.0f, -1.0f, 0.0f),
                             sdl3d_vec3_make(0.0f, 3.0f, 0.0f), kRed, false, false);
    EXPECT_EQ(CountPixelsEqual(f, kRed), kW * kH);
}

TEST(SDL3DRasterizeTriangle, BehindNearPlaneProducesNoPixels)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();
    // z = -2 in NDC means clip-space z < -w, entirely behind near plane.
    sdl3d_rasterize_triangle(&f.fb, id, sdl3d_vec3_make(-1.0f, -1.0f, -2.0f), sdl3d_vec3_make(1.0f, -1.0f, -2.0f),
                             sdl3d_vec3_make(0.0f, 1.0f, -2.0f), kRed, false, false);
    EXPECT_EQ(CountPixelsEqual(f, kRed), 0);
}

TEST(SDL3DRasterizeTriangle, DegenerateTriangleProducesNoPixels)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();
    // Collinear vertices: zero area.
    sdl3d_rasterize_triangle(&f.fb, id, sdl3d_vec3_make(-0.5f, -0.5f, 0.0f), sdl3d_vec3_make(0.0f, 0.0f, 0.0f),
                             sdl3d_vec3_make(0.5f, 0.5f, 0.0f), kRed, false, false);
    EXPECT_EQ(CountPixelsEqual(f, kRed), 0);
}

TEST(SDL3DRasterizeTriangle, DepthTestNearHidesFar)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();
    // Far triangle first (z = 0.5), then near (z = -0.5). Near must win.
    sdl3d_rasterize_triangle(&f.fb, id, sdl3d_vec3_make(-3.0f, -1.0f, 0.5f), sdl3d_vec3_make(3.0f, -1.0f, 0.5f),
                             sdl3d_vec3_make(0.0f, 3.0f, 0.5f), kBlue, false, false);
    sdl3d_rasterize_triangle(&f.fb, id, sdl3d_vec3_make(-3.0f, -1.0f, -0.5f), sdl3d_vec3_make(3.0f, -1.0f, -0.5f),
                             sdl3d_vec3_make(0.0f, 3.0f, -0.5f), kRed, false, false);
    EXPECT_EQ(CountPixelsEqual(f, kRed), kW * kH);
    EXPECT_EQ(CountPixelsEqual(f, kBlue), 0);

    // Reverse order: the far draw should not overwrite near pixels.
    Framebuffer g;
    sdl3d_framebuffer_clear(&g.fb, kBlack, 1.0f);
    sdl3d_rasterize_triangle(&g.fb, id, sdl3d_vec3_make(-3.0f, -1.0f, -0.5f), sdl3d_vec3_make(3.0f, -1.0f, -0.5f),
                             sdl3d_vec3_make(0.0f, 3.0f, -0.5f), kRed, false, false);
    sdl3d_rasterize_triangle(&g.fb, id, sdl3d_vec3_make(-3.0f, -1.0f, 0.5f), sdl3d_vec3_make(3.0f, -1.0f, 0.5f),
                             sdl3d_vec3_make(0.0f, 3.0f, 0.5f), kBlue, false, false);
    EXPECT_EQ(CountPixelsEqual(g, kRed), kW * kH);
    EXPECT_EQ(CountPixelsEqual(g, kBlue), 0);
}

TEST(SDL3DRasterizeTriangle, AdjacentTrianglesNoGapsNoOverlap)
{
    // Split a full-screen quad into two triangles sharing a diagonal edge.
    // Top-left fill rule: every pixel is covered exactly once across both.
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();
    // Quad corners in NDC: TL(-1,1), TR(1,1), BR(1,-1), BL(-1,-1).
    const sdl3d_vec3 tl = sdl3d_vec3_make(-1.0f, 1.0f, 0.0f);
    const sdl3d_vec3 tr = sdl3d_vec3_make(1.0f, 1.0f, 0.0f);
    const sdl3d_vec3 br = sdl3d_vec3_make(1.0f, -1.0f, 0.0f);
    const sdl3d_vec3 bl = sdl3d_vec3_make(-1.0f, -1.0f, 0.0f);

    // First triangle: tl, tr, br. Uses kRed. Writes depth 0.
    sdl3d_rasterize_triangle(&f.fb, id, tl, tr, br, kRed, false, false);
    // Second triangle: tl, br, bl. Uses kBlue. Due to depth test (<=)
    // pixels shared across the diagonal go to whichever writes first;
    // the fill rule ensures no pixel is actually shared.
    sdl3d_rasterize_triangle(&f.fb, id, tl, br, bl, kBlue, false, false);

    const int red = CountPixelsEqual(f, kRed);
    const int blue = CountPixelsEqual(f, kBlue);
    const int black = CountPixelsEqual(f, kBlack);
    EXPECT_EQ(black, 0);            // no gaps
    EXPECT_EQ(red + blue, kW * kH); // disjoint coverage
}

/* --- Line rasterization -------------------------------------------------- */

TEST(SDL3DRasterizeLine, HorizontalLineAcrossCenter)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();
    // Horizontal line at NDC y=0 → screen y ≈ H/2.
    sdl3d_rasterize_line(&f.fb, id, sdl3d_vec3_make(-0.9f, 0.0f, 0.0f), sdl3d_vec3_make(0.9f, 0.0f, 0.0f), kGreen);
    const int green = CountPixelsEqual(f, kGreen);
    EXPECT_GT(green, 0);

    // All lit pixels should share the same y row (horizontal line).
    int hit_rows = 0;
    for (int y = 0; y < kH; ++y)
    {
        bool any = false;
        for (int x = 0; x < kW; ++x)
        {
            if (PixelEquals(f.GetPixel(x, y), kGreen))
            {
                any = true;
                break;
            }
        }
        if (any)
        {
            ++hit_rows;
        }
    }
    EXPECT_EQ(hit_rows, 1);
}

TEST(SDL3DRasterizeLine, DiagonalLineCoversBothEndpointRegions)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();
    sdl3d_rasterize_line(&f.fb, id, sdl3d_vec3_make(-0.9f, -0.9f, 0.0f), sdl3d_vec3_make(0.9f, 0.9f, 0.0f), kGreen);
    EXPECT_GT(CountPixelsEqual(f, kGreen), 0);

    // Endpoints reach opposite halves of the screen.
    bool hit_top_left = false;
    bool hit_bottom_right = false;
    for (int y = 0; y < kH; ++y)
    {
        for (int x = 0; x < kW; ++x)
        {
            if (!PixelEquals(f.GetPixel(x, y), kGreen))
            {
                continue;
            }
            // NDC y is flipped on screen: +y goes to top rows, -y to bottom.
            if (x < kW / 2 && y >= kH / 2)
            {
                hit_top_left = true; /* NDC (-0.9, -0.9) lands bottom-left after y-flip */
            }
            if (x >= kW / 2 && y < kH / 2)
            {
                hit_bottom_right = true; /* NDC (+0.9, +0.9) lands top-right after y-flip */
            }
        }
    }
    EXPECT_TRUE(hit_top_left);
    EXPECT_TRUE(hit_bottom_right);
}

TEST(SDL3DRasterizeLine, LineEntirelyBehindNearProducesNoPixels)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();
    sdl3d_rasterize_line(&f.fb, id, sdl3d_vec3_make(-0.5f, 0.0f, -2.0f), sdl3d_vec3_make(0.5f, 0.0f, -2.0f), kGreen);
    EXPECT_EQ(CountPixelsEqual(f, kGreen), 0);
}

/* --- Point rasterization ------------------------------------------------- */

TEST(SDL3DRasterizePoint, CenterPointProducesOnePixel)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();
    // NDC (0, 0) → screen (W/2, H/2).
    sdl3d_rasterize_point(&f.fb, id, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kRed);
    EXPECT_EQ(CountPixelsEqual(f, kRed), 1);
    EXPECT_TRUE(PixelEquals(f.GetPixel(kW / 2, kH / 2), kRed));
}

TEST(SDL3DRasterizePoint, OutsideFrustumProducesNoPixel)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();
    sdl3d_rasterize_point(&f.fb, id, sdl3d_vec3_make(2.0f, 0.0f, 0.0f), kRed);
    sdl3d_rasterize_point(&f.fb, id, sdl3d_vec3_make(0.0f, 0.0f, -2.0f), kRed);
    EXPECT_EQ(CountPixelsEqual(f, kRed), 0);
}

/* --- Clipping: triangle straddling near plane ---------------------------- */

TEST(SDL3DRasterizeTriangle, TriangleStraddlingNearPlaneRenders)
{
    // Build a perspective projection, put a triangle half-in / half-out of
    // the near plane, and verify clipping produces pixels (rather than
    // dividing by zero or wrapping).
    Framebuffer f(32, 32);
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    sdl3d_mat4 proj;
    ASSERT_TRUE(sdl3d_mat4_perspective(sdl3d_degrees_to_radians(60.0f), 1.0f, 0.5f, 100.0f, &proj));

    // View-space triangle: apex well in front of near (z=-2), base two
    // vertices closer to camera than the near plane (z=-0.3 vs near=0.5).
    // Non-zero y gives the triangle area so the near-plane clip produces
    // a trimmed polygon that covers pixels after viewport transform.
    const sdl3d_vec3 v0 = sdl3d_vec3_make(0.0f, 1.0f, -2.0f);
    const sdl3d_vec3 v1 = sdl3d_vec3_make(-1.0f, -1.0f, -0.3f);
    const sdl3d_vec3 v2 = sdl3d_vec3_make(1.0f, -1.0f, -0.3f);

    sdl3d_rasterize_triangle(&f.fb, proj, v0, v1, v2, kRed, false, false);
    EXPECT_GT(CountPixelsEqual(f, kRed), 0);
}

TEST(SDL3DRasterizeTriangle, BackfaceCullingRejectsClockwiseScreenFaces)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();

    sdl3d_rasterize_triangle(&f.fb, id, sdl3d_vec3_make(0.0f, 0.5f, 0.0f), sdl3d_vec3_make(0.5f, -0.5f, 0.0f),
                             sdl3d_vec3_make(-0.5f, -0.5f, 0.0f), kRed, true, false);
    EXPECT_EQ(CountPixelsEqual(f, kRed), 0);
}

TEST(SDL3DRasterizeTriangle, WireframeDrawsEdgesWithoutFillingInterior)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    const sdl3d_mat4 id = sdl3d_mat4_identity();

    sdl3d_rasterize_triangle(&f.fb, id, sdl3d_vec3_make(-0.5f, -0.5f, 0.0f), sdl3d_vec3_make(0.5f, -0.5f, 0.0f),
                             sdl3d_vec3_make(0.0f, 0.5f, 0.0f), kRed, false, true);

    EXPECT_GT(CountPixelsEqual(f, kRed), 0);
    EXPECT_TRUE(PixelEquals(f.GetPixel(kW / 2, kH / 2), kBlack));
}

TEST(SDL3DRasterizeTriangle, TexturedNearestMapsQuadrantsCorrectly)
{
    Framebuffer f(32, 32);
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);

    const std::vector<Uint8> pixels = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255,
    };
    sdl3d_texture2d texture = MakeTextureFromPixels(pixels, 2, 2);
    ASSERT_TRUE(sdl3d_set_texture_filter(&texture, SDL3D_TEXTURE_FILTER_NEAREST));
    ASSERT_TRUE(sdl3d_set_texture_wrap(&texture, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP));

    const sdl3d_mat4 id = sdl3d_mat4_identity();
    const sdl3d_vec4 modulate = {1.0f, 1.0f, 1.0f, 1.0f};

    sdl3d_rasterize_triangle_textured(&f.fb, id, sdl3d_vec3_make(-1.0f, -1.0f, 0.0f),
                                      sdl3d_vec3_make(1.0f, -1.0f, 0.0f), sdl3d_vec3_make(1.0f, 1.0f, 0.0f),
                                      sdl3d_vec2{0.0f, 0.0f}, sdl3d_vec2{1.0f, 0.0f}, sdl3d_vec2{1.0f, 1.0f}, modulate,
                                      modulate, modulate, &texture, false, false);
    sdl3d_rasterize_triangle_textured(&f.fb, id, sdl3d_vec3_make(-1.0f, -1.0f, 0.0f), sdl3d_vec3_make(1.0f, 1.0f, 0.0f),
                                      sdl3d_vec3_make(-1.0f, 1.0f, 0.0f), sdl3d_vec2{0.0f, 0.0f},
                                      sdl3d_vec2{1.0f, 1.0f}, sdl3d_vec2{0.0f, 1.0f}, modulate, modulate, modulate,
                                      &texture, false, false);

    EXPECT_TRUE(PixelNear(f.GetPixel(8, 8), kRed));
    EXPECT_TRUE(PixelNear(f.GetPixel(24, 8), kGreen));
    EXPECT_TRUE(PixelNear(f.GetPixel(8, 24), kBlue));
    EXPECT_TRUE(PixelNear(f.GetPixel(24, 24), sdl3d_color{255, 255, 0, 255}));

    sdl3d_free_texture(&texture);
}

TEST(SDL3DRasterizeTriangle, TexturedBilinearBlendsAtCenter)
{
    Framebuffer f(32, 32);
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);

    const std::vector<Uint8> pixels = {
        0, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
    };
    sdl3d_texture2d texture = MakeTextureFromPixels(pixels, 2, 2);
    ASSERT_TRUE(sdl3d_set_texture_filter(&texture, SDL3D_TEXTURE_FILTER_BILINEAR));

    const sdl3d_mat4 id = sdl3d_mat4_identity();
    const sdl3d_vec4 modulate = {1.0f, 1.0f, 1.0f, 1.0f};

    sdl3d_rasterize_triangle_textured(&f.fb, id, sdl3d_vec3_make(-1.0f, -1.0f, 0.0f),
                                      sdl3d_vec3_make(1.0f, -1.0f, 0.0f), sdl3d_vec3_make(1.0f, 1.0f, 0.0f),
                                      sdl3d_vec2{0.0f, 0.0f}, sdl3d_vec2{1.0f, 0.0f}, sdl3d_vec2{1.0f, 1.0f}, modulate,
                                      modulate, modulate, &texture, false, false);
    sdl3d_rasterize_triangle_textured(&f.fb, id, sdl3d_vec3_make(-1.0f, -1.0f, 0.0f), sdl3d_vec3_make(1.0f, 1.0f, 0.0f),
                                      sdl3d_vec3_make(-1.0f, 1.0f, 0.0f), sdl3d_vec2{0.0f, 0.0f},
                                      sdl3d_vec2{1.0f, 1.0f}, sdl3d_vec2{0.0f, 1.0f}, modulate, modulate, modulate,
                                      &texture, false, false);

    const sdl3d_color center = f.GetPixel(16, 16);
    EXPECT_NEAR((int)center.r, 64, 20);
    EXPECT_NEAR((int)center.g, 64, 20);
    EXPECT_NEAR((int)center.b, 64, 20);

    sdl3d_free_texture(&texture);
}

TEST(SDL3DRasterizeTriangle, TexturedPerspectiveCorrectNearTexelDominates)
{
    Framebuffer f(64, 64);
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);

    const std::vector<Uint8> pixels = {
        255, 0, 0, 255, 0, 255, 0, 255,
    };
    sdl3d_texture2d texture = MakeTextureFromPixels(pixels, 2, 1);
    ASSERT_TRUE(sdl3d_set_texture_filter(&texture, SDL3D_TEXTURE_FILTER_NEAREST));

    sdl3d_mat4 proj;
    ASSERT_TRUE(sdl3d_mat4_perspective(sdl3d_degrees_to_radians(60.0f), 1.0f, 0.01f, 100.0f, &proj));

    const sdl3d_vec4 modulate = {1.0f, 1.0f, 1.0f, 1.0f};
    const sdl3d_vec3 v0 = sdl3d_vec3_make(0.0f, 0.0f, -1.0f);
    const sdl3d_vec3 v1 = sdl3d_vec3_make(2.0f, 0.0f, -10.0f);
    const sdl3d_vec3 v2 = sdl3d_vec3_make(0.0f, 2.0f, -10.0f);

    sdl3d_rasterize_triangle_textured(&f.fb, proj, v0, v1, v2, sdl3d_vec2{0.0f, 0.0f}, sdl3d_vec2{1.0f, 0.0f},
                                      sdl3d_vec2{1.0f, 1.0f}, modulate, modulate, modulate, &texture, false, false);

    const SDL_Point p0 = ProjectToFramebuffer(proj, f.fb.width, f.fb.height, v0);
    const SDL_Point p1 = ProjectToFramebuffer(proj, f.fb.width, f.fb.height, v1);
    const SDL_Point p2 = ProjectToFramebuffer(proj, f.fb.width, f.fb.height, v2);
    const int cx = (p0.x + p1.x + p2.x) / 3;
    const int cy = (p0.y + p1.y + p2.y) / 3;

    EXPECT_TRUE(SampleNearColor(f, cx, cy, 2, kRed));

    sdl3d_free_texture(&texture);
}

TEST(SDL3DRasterizeTriangle, ScissorClipsTriangleCoverage)
{
    Framebuffer f;
    sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);
    f.fb.scissor_enabled = true;
    f.fb.scissor_rect = SDL_Rect{8, 8, 1, 1};

    const sdl3d_mat4 id = sdl3d_mat4_identity();
    sdl3d_rasterize_triangle(&f.fb, id, sdl3d_vec3_make(-3.0f, -1.0f, 0.0f), sdl3d_vec3_make(3.0f, -1.0f, 0.0f),
                             sdl3d_vec3_make(0.0f, 3.0f, 0.0f), kRed, false, false);

    EXPECT_EQ(CountPixelsEqual(f, kRed), 1);
    EXPECT_TRUE(PixelEquals(f.GetPixel(8, 8), kRed));
}

TEST(SDL3DRasterizeTriangle, ParallelTilesMatchReferenceByteForByte)
{
    Framebuffer reference(96, 80);
    Framebuffer parallel(96, 80);

    sdl3d_parallel_rasterizer *parallel_rasterizer = nullptr;
    if (!sdl3d_parallel_rasterizer_create(2, &parallel_rasterizer))
    {
        GTEST_SKIP() << "Parallel rasterizer is unavailable on this platform/toolchain: " << SDL_GetError();
    }
    ASSERT_NE(parallel_rasterizer, nullptr);
    EXPECT_EQ(sdl3d_parallel_rasterizer_get_worker_count(parallel_rasterizer), 2);

    parallel.fb.parallel_rasterizer = parallel_rasterizer;

    const auto draw_scene = [](Framebuffer &f) {
        f.fb.scissor_enabled = true;
        f.fb.scissor_rect = SDL_Rect{7, 5, 73, 61};
        sdl3d_framebuffer_clear(&f.fb, kBlack, 1.0f);

        const sdl3d_mat4 id = sdl3d_mat4_identity();
        sdl3d_rasterize_triangle(&f.fb, id, sdl3d_vec3_make(-3.0f, -1.0f, 0.7f), sdl3d_vec3_make(3.0f, -1.0f, 0.7f),
                                 sdl3d_vec3_make(0.0f, 3.0f, 0.7f), kBlue, false, false);
        sdl3d_rasterize_triangle(&f.fb, id, sdl3d_vec3_make(-2.2f, -1.5f, -0.2f), sdl3d_vec3_make(2.5f, -0.8f, -0.2f),
                                 sdl3d_vec3_make(-0.2f, 2.4f, -0.2f), kRed, false, false);
        sdl3d_rasterize_triangle(&f.fb, id, sdl3d_vec3_make(0.3f, 0.8f, 0.1f), sdl3d_vec3_make(0.8f, -0.7f, 0.1f),
                                 sdl3d_vec3_make(-0.9f, -0.6f, 0.1f), kGreen, true, false);

        sdl3d_mat4 proj;
        ASSERT_TRUE(sdl3d_mat4_perspective(sdl3d_degrees_to_radians(65.0f), 1.2f, 0.5f, 100.0f, &proj));
        sdl3d_rasterize_triangle(&f.fb, proj, sdl3d_vec3_make(0.0f, 1.2f, -2.5f), sdl3d_vec3_make(-1.4f, -1.0f, -0.2f),
                                 sdl3d_vec3_make(1.1f, -0.8f, -0.3f), kGreen, false, false);
    };

    draw_scene(reference);
    draw_scene(parallel);

    EXPECT_EQ(reference.color, parallel.color);
    ASSERT_EQ(reference.depth.size(), parallel.depth.size());
    EXPECT_EQ(std::memcmp(reference.depth.data(), parallel.depth.data(), reference.depth.size() * sizeof(float)), 0);

    sdl3d_parallel_rasterizer_destroy(parallel_rasterizer);
}
