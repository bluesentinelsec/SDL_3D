/*
 * Integration tests for per-vertex color triangles and perspective-correct
 * attribute interpolation. Exercises the slayer3d_draw_triangle_3d_ex path:
 * corner colors, linear (orthographic) midpoint blend, perspective
 * correctness under heavy foreshortening, wireframe color interpolation,
 * and argument validation.
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
class SLAYER3DVertexColorFixture : public ::testing::Test
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
        SDL_Window *w = nullptr;
        SDL_Renderer *r = nullptr;
        if (!SDL_CreateWindowAndRenderer("SLAYER3D VertexColor Test", width, height, 0, &w, &r))
        {
            ADD_FAILURE() << SDL_GetError();
            return;
        }
        window_.reset(w);
        renderer_.reset(r);
    }

    bool ok() const
    {
        return window_ && renderer_;
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

slayer3d_camera3d MakePerspective(slayer3d_vec3 eye, slayer3d_vec3 target, float fovy_degrees = 60.0f)
{
    slayer3d_camera3d cam{};
    cam.position = eye;
    cam.target = target;
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = fovy_degrees;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;
    return cam;
}

slayer3d_camera3d MakeOrtho(float view_height = 4.0f)
{
    slayer3d_camera3d cam{};
    cam.position = slayer3d_vec3_make(0.0f, 0.0f, 3.0f);
    cam.target = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = view_height;
    cam.projection = SLAYER3D_CAMERA_ORTHOGRAPHIC;
    return cam;
}

constexpr slayer3d_color kBlack = {0, 0, 0, 255};
constexpr slayer3d_color kRed = {255, 0, 0, 255};
constexpr slayer3d_color kGreen = {0, 255, 0, 255};
constexpr slayer3d_color kBlue = {0, 0, 255, 255};

SDL_Point ProjectToScreen(const slayer3d_render_context *ctx, slayer3d_camera3d camera, slayer3d_vec3 point)
{
    slayer3d_mat4 view{};
    slayer3d_mat4 projection{};
    EXPECT_TRUE(slayer3d_camera3d_compute_matrices(&camera, slayer3d_get_render_context_width(ctx),
                                                   slayer3d_get_render_context_height(ctx), 0.01f, 1000.0f, &view,
                                                   &projection));
    const slayer3d_mat4 vp = slayer3d_mat4_multiply(projection, view);
    const slayer3d_vec4 clip = slayer3d_mat4_transform_vec4(vp, slayer3d_vec4_from_vec3(point, 1.0f));
    const float inv_w = 1.0f / clip.w;
    const float ndc_x = clip.x * inv_w;
    const float ndc_y = clip.y * inv_w;
    SDL_Point out{};
    out.x = (int)std::lround((ndc_x + 1.0f) * 0.5f * (float)slayer3d_get_render_context_width(ctx));
    out.y = (int)std::lround((1.0f - ndc_y) * 0.5f * (float)slayer3d_get_render_context_height(ctx));
    return out;
}

/*
 * Read the first covered pixel within a small square window around (cx, cy).
 * Used to sample colors near a projected vertex without having to know the
 * exact rasterization seam placement.
 */
bool SampleNear(const slayer3d_render_context *ctx, int cx, int cy, int window, slayer3d_color *out)
{
    const int w = slayer3d_get_render_context_width(ctx);
    const int h = slayer3d_get_render_context_height(ctx);
    for (int dy = -window; dy <= window; ++dy)
    {
        for (int dx = -window; dx <= window; ++dx)
        {
            const int x = cx + dx;
            const int y = cy + dy;
            if (x < 0 || x >= w || y < 0 || y >= h)
            {
                continue;
            }
            slayer3d_color c{};
            if (slayer3d_get_framebuffer_pixel(ctx, x, y, &c) && !(c.r == 0 && c.g == 0 && c.b == 0))
            {
                *out = c;
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_F(SLAYER3DVertexColorFixture, DrawOutsideModeRejected)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_draw_triangle_3d_ex(ctx, slayer3d_vec3_make(-1, -1, 0), slayer3d_vec3_make(1, -1, 0),
                                              slayer3d_vec3_make(0, 1, 0), kRed, kGreen, kBlue));
    EXPECT_NE(*SDL_GetError(), '\0');

    slayer3d_destroy_render_context(ctx);
}

TEST(SLAYER3DVertexColorNull, NullContextRejected)
{
    SDL_ClearError();
    EXPECT_FALSE(slayer3d_draw_triangle_3d_ex(nullptr, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(1, 0, 0),
                                              slayer3d_vec3_make(0, 1, 0), kRed, kGreen, kBlue));
}

TEST_F(SLAYER3DVertexColorFixture, CornersTakeVertexColorsUnderOrtho)
{
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, kBlack));
    const slayer3d_camera3d cam = MakeOrtho(4.0f);
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));

    /*
     * Keep the triangle well inside half-width even on steeply portrait
     * framebuffers (Android emulators hand us device aspect, not the 1:1 we
     * ask for). half-width = half-height * aspect = 2 * aspect; coords in
     * [-0.6, 0.6] stay visible for aspect >= 0.3.
     */
    const slayer3d_vec3 v0 = slayer3d_vec3_make(-0.6f, -0.6f, 0.0f);
    const slayer3d_vec3 v1 = slayer3d_vec3_make(0.6f, -0.6f, 0.0f);
    const slayer3d_vec3 v2 = slayer3d_vec3_make(0.0f, 0.6f, 0.0f);
    ASSERT_TRUE(slayer3d_draw_triangle_3d_ex(ctx, v0, v1, v2, kRed, kGreen, kBlue));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    const SDL_Point p0 = ProjectToScreen(ctx, cam, v0);
    const SDL_Point p1 = ProjectToScreen(ctx, cam, v1);
    const SDL_Point p2 = ProjectToScreen(ctx, cam, v2);

    slayer3d_color near_v0{}, near_v1{}, near_v2{};
    ASSERT_TRUE(SampleNear(ctx, p0.x, p0.y, 3, &near_v0));
    ASSERT_TRUE(SampleNear(ctx, p1.x, p1.y, 3, &near_v1));
    ASSERT_TRUE(SampleNear(ctx, p2.x, p2.y, 3, &near_v2));

    EXPECT_GT(near_v0.r, 200);
    EXPECT_LT(near_v0.g, 60);
    EXPECT_LT(near_v0.b, 60);

    EXPECT_LT(near_v1.r, 60);
    EXPECT_GT(near_v1.g, 200);
    EXPECT_LT(near_v1.b, 60);

    EXPECT_LT(near_v2.r, 60);
    EXPECT_LT(near_v2.g, 60);
    EXPECT_GT(near_v2.b, 200);

    slayer3d_destroy_render_context(ctx);
}

TEST_F(SLAYER3DVertexColorFixture, OrthoMidpointIsLinearBlend)
{
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, kBlack));
    const slayer3d_camera3d cam = MakeOrtho(4.0f);
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));

    /*
     * Orthographic has constant w, so perspective-correct interpolation
     * collapses to linear screen-space interpolation. The midpoint of the
     * bottom edge (v0 red, v1 green) should be close to (127, 127, 0).
     */
    const slayer3d_vec3 v0 = slayer3d_vec3_make(-0.6f, -0.5f, 0.0f);
    const slayer3d_vec3 v1 = slayer3d_vec3_make(0.6f, -0.5f, 0.0f);
    const slayer3d_vec3 v2 = slayer3d_vec3_make(0.0f, 0.6f, 0.0f);
    ASSERT_TRUE(slayer3d_draw_triangle_3d_ex(ctx, v0, v1, v2, kRed, kGreen, kBlack));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    const SDL_Point p0 = ProjectToScreen(ctx, cam, v0);
    const SDL_Point p1 = ProjectToScreen(ctx, cam, v1);
    const int mid_x = (p0.x + p1.x) / 2;
    const int mid_y = (p0.y + p1.y) / 2 - 1; /* one row above the edge so we sample filled interior */

    slayer3d_color mid{};
    ASSERT_TRUE(SampleNear(ctx, mid_x, mid_y, 2, &mid));

    EXPECT_NEAR((int)mid.r, 127, 20);
    EXPECT_NEAR((int)mid.g, 127, 20);
    EXPECT_LT(mid.b, 20);

    slayer3d_destroy_render_context(ctx);
}

TEST_F(SLAYER3DVertexColorFixture, FlatColorMatchesFlatPath)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    const slayer3d_color mauve = {180, 90, 140, 255};
    const slayer3d_camera3d cam = MakeOrtho(4.0f);
    const slayer3d_vec3 v0 = slayer3d_vec3_make(-0.6f, -0.5f, 0.0f);
    const slayer3d_vec3 v1 = slayer3d_vec3_make(0.6f, -0.5f, 0.0f);
    const slayer3d_vec3 v2 = slayer3d_vec3_make(0.0f, 0.5f, 0.0f);

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(slayer3d_draw_triangle_3d_ex(ctx, v0, v1, v2, mauve, mauve, mauve));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    int matches = 0;
    int covered = 0;
    const int w = slayer3d_get_render_context_width(ctx);
    const int h = slayer3d_get_render_context_height(ctx);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            slayer3d_color px{};
            if (!slayer3d_get_framebuffer_pixel(ctx, x, y, &px))
            {
                continue;
            }
            if (px.r == kBlack.r && px.g == kBlack.g && px.b == kBlack.b)
            {
                continue;
            }
            ++covered;
            /*
             * A 1 ULP drift in the perspective-correct path vs. a flat
             * write is acceptable; all three channels should round-trip
             * within one unit of the input color.
             */
            if (std::abs((int)px.r - (int)mauve.r) <= 1 && std::abs((int)px.g - (int)mauve.g) <= 1 &&
                std::abs((int)px.b - (int)mauve.b) <= 1)
            {
                ++matches;
            }
        }
    }

    EXPECT_GT(covered, 100);
    EXPECT_EQ(matches, covered);

    slayer3d_destroy_render_context(ctx);
}

TEST_F(SLAYER3DVertexColorFixture, PerspectiveCorrectNearVertexDominates)
{
    /*
     * Heavy-foreshortening triangle: v0 near the camera with 1/w large,
     * v1/v2 far with 1/w small. All three vertices project into the frame.
     * With perspective-correct interpolation, the near vertex color
     * dominates a much larger fraction of the triangle's screen area than
     * a screen-space-linear blend would predict; we assert the center of
     * the screen-projected triangle is strongly red-weighted.
     */
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, kBlack));
    const slayer3d_camera3d cam =
        MakePerspective(slayer3d_vec3_make(0.0f, 0.0f, 0.0f), slayer3d_vec3_make(0.0f, 0.0f, -1.0f), 60.0f);
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));

    /*
     * v1/v2 at z=-10 project to screen-x = (x / 10) / tan(fov_h/2); keep
     * x=2 so the vertex stays inside the horizontal frustum even on a
     * portrait aspect (~0.56 on Android), where horizontal half-FOV shrinks
     * relative to vertical.
     */
    const slayer3d_vec3 v0 = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
    const slayer3d_vec3 v1 = slayer3d_vec3_make(2.0f, 0.0f, -10.0f);
    const slayer3d_vec3 v2 = slayer3d_vec3_make(0.0f, 2.0f, -10.0f);
    ASSERT_TRUE(slayer3d_draw_triangle_3d_ex(ctx, v0, v1, v2, kRed, kGreen, kGreen));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    /*
     * Sample the screen centroid of the three projected vertices. Under
     * perspective correctness the red channel at this point is ~182
     * (worked out from clip-w's ~1 / ~10 / ~10). A screen-linear blend
     * would give red ~85, so we require red > 140 and green < 120 to
     * confirm the pipeline is PC rather than linear.
     */
    const SDL_Point p0 = ProjectToScreen(ctx, cam, v0);
    const SDL_Point p1 = ProjectToScreen(ctx, cam, v1);
    const SDL_Point p2 = ProjectToScreen(ctx, cam, v2);
    const int cx = (p0.x + p1.x + p2.x) / 3;
    const int cy = (p0.y + p1.y + p2.y) / 3;

    slayer3d_color center{};
    ASSERT_TRUE(SampleNear(ctx, cx, cy, 2, &center));

    EXPECT_GT((int)center.r, 140);
    EXPECT_LT((int)center.g, 120);
    EXPECT_LT((int)center.b, 20);

    slayer3d_destroy_render_context(ctx);
}

TEST_F(SLAYER3DVertexColorFixture, AlphaInterpolates)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, kBlack));
    const slayer3d_camera3d cam = MakeOrtho(4.0f);
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));

    const slayer3d_color red_opaque = {255, 0, 0, 255};
    const slayer3d_color red_transparent = {255, 0, 0, 0};
    const slayer3d_vec3 v0 = slayer3d_vec3_make(-0.6f, -0.5f, 0.0f);
    const slayer3d_vec3 v1 = slayer3d_vec3_make(0.6f, -0.5f, 0.0f);
    const slayer3d_vec3 v2 = slayer3d_vec3_make(0.0f, 0.6f, 0.0f);
    ASSERT_TRUE(slayer3d_draw_triangle_3d_ex(ctx, v0, v1, v2, red_opaque, red_transparent, red_opaque));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    const SDL_Point p0 = ProjectToScreen(ctx, cam, v0);
    const SDL_Point p1 = ProjectToScreen(ctx, cam, v1);
    const int mid_x = (p0.x + p1.x) / 2;
    const int mid_y = (p0.y + p1.y) / 2 - 1;

    slayer3d_color mid{};
    ASSERT_TRUE(SampleNear(ctx, mid_x, mid_y, 2, &mid));

    EXPECT_GT((int)mid.r, 200);
    EXPECT_NEAR((int)mid.a, 127, 30);

    slayer3d_destroy_render_context(ctx);
}

TEST_F(SLAYER3DVertexColorFixture, WireframeInterpolatesAlongEdges)
{
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());
    slayer3d_render_context *ctx = nullptr;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(slayer3d_set_wireframe_enabled(ctx, true));
    const slayer3d_camera3d cam = MakeOrtho(4.0f);
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));

    const slayer3d_vec3 v0 = slayer3d_vec3_make(-0.6f, -0.5f, 0.0f);
    const slayer3d_vec3 v1 = slayer3d_vec3_make(0.6f, -0.5f, 0.0f);
    const slayer3d_vec3 v2 = slayer3d_vec3_make(0.0f, 0.6f, 0.0f);
    ASSERT_TRUE(slayer3d_draw_triangle_3d_ex(ctx, v0, v1, v2, kRed, kGreen, kBlack));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));
    ASSERT_TRUE(slayer3d_set_wireframe_enabled(ctx, false));

    int red_pixels = 0;
    int green_pixels = 0;
    const int w = slayer3d_get_render_context_width(ctx);
    const int h = slayer3d_get_render_context_height(ctx);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            slayer3d_color px{};
            if (!slayer3d_get_framebuffer_pixel(ctx, x, y, &px))
            {
                continue;
            }
            if (px.r > 200 && px.g < 40)
            {
                ++red_pixels;
            }
            if (px.g > 200 && px.r < 40)
            {
                ++green_pixels;
            }
        }
    }

    EXPECT_GT(red_pixels, 0);
    EXPECT_GT(green_pixels, 0);

    slayer3d_destroy_render_context(ctx);
}
