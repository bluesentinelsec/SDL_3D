/*
 * Integration tests for the public 3D drawing API. Exercises mode
 * lifecycle, readback of drawn pixels, depth ordering, and logical
 * presentation interop against a real SDL window/renderer.
 */

#include <gtest/gtest.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

extern "C"
{
#include "sdl3d/sdl3d.h"
}

#include <memory>

namespace
{
class SDL3DDrawingFixture : public ::testing::Test
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
        if (!SDL_CreateWindowAndRenderer("SDL3D Drawing Test", width, height, 0, &w, &r))
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

sdl3d_camera3d MakeCamera(float fovy_degrees = 60.0f)
{
    sdl3d_camera3d cam{};
    cam.position = sdl3d_vec3_make(0.0f, 0.0f, 3.0f);
    cam.target = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = fovy_degrees;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;
    return cam;
}

sdl3d_camera3d MakeOrthoCamera(float view_height = 4.0f)
{
    sdl3d_camera3d cam{};
    cam.position = sdl3d_vec3_make(0.0f, 0.0f, 3.0f);
    cam.target = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = view_height;
    cam.projection = SDL3D_CAMERA_ORTHOGRAPHIC;
    return cam;
}

bool PixelEquals(sdl3d_color a, sdl3d_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int CountColor(sdl3d_render_context *ctx, sdl3d_color c)
{
    const int w = sdl3d_get_render_context_width(ctx);
    const int h = sdl3d_get_render_context_height(ctx);
    int n = 0;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            sdl3d_color px{};
            if (sdl3d_get_framebuffer_pixel(ctx, x, y, &px) && PixelEquals(px, c))
            {
                ++n;
            }
        }
    }
    return n;
}

constexpr sdl3d_color kBlack = {0, 0, 0, 255};
constexpr sdl3d_color kRed = {255, 0, 0, 255};
constexpr sdl3d_color kBlue = {0, 0, 255, 255};
} // namespace

/* --- Lifecycle ----------------------------------------------------------- */

TEST_F(SDL3DDrawingFixture, BeginEndModeRoundTrip)
{
    WindowRenderer wr;
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx)) << SDL_GetError();

    EXPECT_FALSE(sdl3d_is_in_mode_3d(ctx));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera())) << SDL_GetError();
    EXPECT_TRUE(sdl3d_is_in_mode_3d(ctx));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx)) << SDL_GetError();
    EXPECT_FALSE(sdl3d_is_in_mode_3d(ctx));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, DoubleBeginRejected)
{
    WindowRenderer wr;
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    EXPECT_NE(*SDL_GetError(), '\0');

    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));
    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, EndWithoutBeginRejected)
{
    WindowRenderer wr;
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_end_mode_3d(ctx));
    EXPECT_NE(*SDL_GetError(), '\0');

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, DrawOutsideModeRejected)
{
    WindowRenderer wr;
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kRed));
    EXPECT_NE(*SDL_GetError(), '\0');

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, SetDepthPlanesRejectedWhileInMode)
{
    WindowRenderer wr;
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_set_depth_planes(ctx, 0.1f, 500.0f));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_set_depth_planes(ctx, 0.2f, 600.0f));
    EXPECT_NE(*SDL_GetError(), '\0');
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    sdl3d_destroy_render_context(ctx);
}

// No SDL_Init: NULL-context guards are pure C checks and must not pull in
// the X11 video subsystem (which leaks a small allocation per process on
// Linux CI and trips ASan when no window is ever created).
TEST(SDL3DDrawing3DNull, NullContextIsRejected)
{
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_begin_mode_3d(nullptr, MakeCamera()));
    EXPECT_FALSE(sdl3d_end_mode_3d(nullptr));
    EXPECT_FALSE(sdl3d_draw_point_3d(nullptr, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kRed));
    EXPECT_FALSE(sdl3d_push_matrix(nullptr));
    EXPECT_FALSE(sdl3d_pop_matrix(nullptr));
    EXPECT_FALSE(sdl3d_translate(nullptr, 1.0f, 2.0f, 3.0f));
    EXPECT_FALSE(sdl3d_rotate(nullptr, sdl3d_vec3_make(0.0f, 1.0f, 0.0f), sdl3d_degrees_to_radians(45.0f)));
    EXPECT_FALSE(sdl3d_scale(nullptr, 2.0f, 2.0f, 2.0f));
    EXPECT_FALSE(sdl3d_set_backface_culling_enabled(nullptr, true));
    EXPECT_FALSE(sdl3d_set_wireframe_enabled(nullptr, true));
    EXPECT_FALSE(sdl3d_set_depth_planes(nullptr, 0.1f, 100.0f));
    EXPECT_FALSE(sdl3d_is_in_mode_3d(nullptr));
    EXPECT_FALSE(sdl3d_is_backface_culling_enabled(nullptr));
    EXPECT_FALSE(sdl3d_is_wireframe_enabled(nullptr));
}

TEST_F(SDL3DDrawingFixture, MatrixStackMutatorsRejectedOutsideMode)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_push_matrix(ctx));
    EXPECT_NE(*SDL_GetError(), '\0');

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_pop_matrix(ctx));
    EXPECT_NE(*SDL_GetError(), '\0');

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_translate(ctx, 1.0f, 0.0f, 0.0f));
    EXPECT_NE(*SDL_GetError(), '\0');

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_rotate(ctx, sdl3d_vec3_make(0.0f, 0.0f, 1.0f), sdl3d_degrees_to_radians(90.0f)));
    EXPECT_NE(*SDL_GetError(), '\0');

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_scale(ctx, 2.0f, 1.0f, 1.0f));
    EXPECT_NE(*SDL_GetError(), '\0');

    sdl3d_destroy_render_context(ctx);
}

/* --- Drawing / readback -------------------------------------------------- */

TEST_F(SDL3DDrawingFixture, DrawPointAtOriginPaintsCenterPixel)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    // The world-space origin projects to the center of the viewport.
    const int cx = sdl3d_get_render_context_width(ctx) / 2;
    const int cy = sdl3d_get_render_context_height(ctx) / 2;
    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, cx, cy, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));

    ASSERT_TRUE(sdl3d_present_render_context(ctx));
    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, DrawTriangleFacingCameraPaintsPixels)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    ASSERT_TRUE(sdl3d_draw_triangle_3d(ctx, sdl3d_vec3_make(-0.5f, -0.5f, 0.0f), sdl3d_vec3_make(0.5f, -0.5f, 0.0f),
                                       sdl3d_vec3_make(0.0f, 0.5f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    EXPECT_GT(CountColor(ctx, kRed), 0);
    // Center pixel should be inside the triangle.
    const int cx = sdl3d_get_render_context_width(ctx) / 2;
    const int cy = sdl3d_get_render_context_height(ctx) / 2;
    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, cx, cy, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, TranslateMovesPointUnderOrthographicCamera)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeOrthoCamera()));
    ASSERT_TRUE(sdl3d_translate(ctx, 1.0f, 0.0f, 0.0f));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 48, 32, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 32, 32, &c));
    EXPECT_TRUE(PixelEquals(c, kBlack));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, RotateTransformsPointsAroundOrigin)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeOrthoCamera()));
    ASSERT_TRUE(sdl3d_rotate(ctx, sdl3d_vec3_make(0.0f, 0.0f, 1.0f), sdl3d_degrees_to_radians(90.0f)));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(1.0f, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 32, 16, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 48, 32, &c));
    EXPECT_TRUE(PixelEquals(c, kBlack));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, ScaleTransformsPointsUnderOrthographicCamera)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeOrthoCamera()));
    ASSERT_TRUE(sdl3d_scale(ctx, 2.0f, 1.0f, 1.0f));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.5f, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 48, 32, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 40, 32, &c));
    EXPECT_TRUE(PixelEquals(c, kBlack));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, PushPopRestoresPreviousTransform)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeOrthoCamera()));
    ASSERT_TRUE(sdl3d_translate(ctx, 0.5f, 0.0f, 0.0f));
    ASSERT_TRUE(sdl3d_push_matrix(ctx));
    ASSERT_TRUE(sdl3d_translate(ctx, 0.5f, 0.0f, 0.0f));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_pop_matrix(ctx));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kBlue));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 48, 32, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 40, 32, &c));
    EXPECT_TRUE(PixelEquals(c, kBlue));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, MatrixStackGrowsDynamicallyAndRejectsRootPop)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeOrthoCamera()));
    for (int i = 0; i < 40; ++i)
    {
        ASSERT_TRUE(sdl3d_push_matrix(ctx));
    }
    for (int i = 0; i < 40; ++i)
    {
        ASSERT_TRUE(sdl3d_pop_matrix(ctx));
    }

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_pop_matrix(ctx));
    EXPECT_NE(*SDL_GetError(), '\0');

    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));
    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, RotateRejectsZeroAxis)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_rotate(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), sdl3d_degrees_to_radians(45.0f)));
    EXPECT_NE(*SDL_GetError(), '\0');
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, BackfaceCullingRejectsBackfacingTriangle)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    EXPECT_FALSE(sdl3d_is_backface_culling_enabled(ctx));
    ASSERT_TRUE(sdl3d_set_backface_culling_enabled(ctx, true));
    EXPECT_TRUE(sdl3d_is_backface_culling_enabled(ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    ASSERT_TRUE(sdl3d_draw_triangle_3d(ctx, sdl3d_vec3_make(0.0f, 0.5f, 0.0f), sdl3d_vec3_make(0.5f, -0.5f, 0.0f),
                                       sdl3d_vec3_make(-0.5f, -0.5f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    EXPECT_EQ(CountColor(ctx, kRed), 0);

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, WireframeLeavesTriangleInteriorUntouched)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_set_wireframe_enabled(ctx, true));
    EXPECT_TRUE(sdl3d_is_wireframe_enabled(ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    ASSERT_TRUE(sdl3d_draw_triangle_3d(ctx, sdl3d_vec3_make(-0.5f, -0.5f, 0.0f), sdl3d_vec3_make(0.5f, -0.5f, 0.0f),
                                       sdl3d_vec3_make(0.0f, 0.5f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    EXPECT_GT(CountColor(ctx, kRed), 0);

    const int cx = sdl3d_get_render_context_width(ctx) / 2;
    const int cy = sdl3d_get_render_context_height(ctx) / 2;
    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, cx, cy, &c));
    EXPECT_TRUE(PixelEquals(c, kBlack));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, DepthOrderingNearOverFar)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    // Far triangle (z=-1) first, then near triangle (z=+1). Near must win.
    ASSERT_TRUE(sdl3d_draw_triangle_3d(ctx, sdl3d_vec3_make(-1.0f, -1.0f, -1.0f), sdl3d_vec3_make(1.0f, -1.0f, -1.0f),
                                       sdl3d_vec3_make(0.0f, 1.0f, -1.0f), kBlue));
    ASSERT_TRUE(sdl3d_draw_triangle_3d(ctx, sdl3d_vec3_make(-1.0f, -1.0f, 1.0f), sdl3d_vec3_make(1.0f, -1.0f, 1.0f),
                                       sdl3d_vec3_make(0.0f, 1.0f, 1.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    const int cx = sdl3d_get_render_context_width(ctx) / 2;
    const int cy = sdl3d_get_render_context_height(ctx) / 2;
    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, cx, cy, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));
    EXPECT_EQ(CountColor(ctx, kBlue), 0);

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, ClearResetsDepthBuffer)
{
    WindowRenderer wr(32, 32);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    float d = 0.0f;
    ASSERT_TRUE(sdl3d_get_framebuffer_depth(ctx, 0, 0, &d));
    EXPECT_FLOAT_EQ(d, 1.0f);

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, ScissorRestrictsDrawingToClippedRegion)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    const int cx = sdl3d_get_render_context_width(ctx) / 2;
    const int cy = sdl3d_get_render_context_height(ctx) / 2;
    const SDL_Rect scissor = {cx, cy, 1, 1};
    ASSERT_TRUE(sdl3d_set_scissor_rect(ctx, &scissor));
    EXPECT_TRUE(sdl3d_is_scissor_enabled(ctx));

    SDL_Rect queried{};
    ASSERT_TRUE(sdl3d_get_scissor_rect(ctx, &queried));
    EXPECT_EQ(queried.x, scissor.x);
    EXPECT_EQ(queried.y, scissor.y);
    EXPECT_EQ(queried.w, scissor.w);
    EXPECT_EQ(queried.h, scissor.h);

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    EXPECT_EQ(CountColor(ctx, kRed), 1);
    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, cx, cy, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, cx - 1, cy, &c));
    EXPECT_TRUE(PixelEquals(c, kBlack));

    ASSERT_TRUE(sdl3d_set_scissor_rect(ctx, nullptr));
    EXPECT_FALSE(sdl3d_is_scissor_enabled(ctx));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, ClearRectClearsOnlyRequestedRegionAndResetsDepth)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    const SDL_Rect rect = {28, 28, 8, 8};
    ASSERT_TRUE(sdl3d_clear_render_context_rect(ctx, &rect, kBlue));

    sdl3d_color cleared{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 32, 32, &cleared));
    EXPECT_TRUE(PixelEquals(cleared, kBlue));

    sdl3d_color untouched{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 4, 4, &untouched));
    EXPECT_TRUE(PixelEquals(untouched, kBlack));

    float depth = 0.0f;
    ASSERT_TRUE(sdl3d_get_framebuffer_depth(ctx, 32, 32, &depth));
    EXPECT_FLOAT_EQ(depth, 1.0f);

    sdl3d_destroy_render_context(ctx);
}

/* --- Logical presentation interop --------------------------------------- */

TEST_F(SDL3DDrawingFixture, LogicalLetterboxRendersAtLogicalResolution)
{
    WindowRenderer wr(320, 240);
    ASSERT_TRUE(wr.ok());

    sdl3d_render_context_config config;
    sdl3d_init_render_context_config(&config);
    // Pick a logical size different from the window (and a different aspect)
    // to exercise the letterbox path.
    config.logical_width = 64;
    config.logical_height = 64;
    config.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;

    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), &config, &ctx)) << SDL_GetError();

    // The 3D backbuffer should match the logical size.
    ASSERT_EQ(64, sdl3d_get_render_context_width(ctx));
    ASSERT_EQ(64, sdl3d_get_render_context_height(ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, MakeCamera()));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 32, 32, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));

    // Present through SDL's letterbox pipeline to prove interop.
    ASSERT_TRUE(sdl3d_present_render_context(ctx)) << SDL_GetError();

    sdl3d_destroy_render_context(ctx);
}
