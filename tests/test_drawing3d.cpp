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

#include <cmath>
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

SDL_Point ProjectPointToFramebuffer(const sdl3d_render_context *ctx, sdl3d_camera3d camera, sdl3d_vec3 point)
{
    sdl3d_mat4 view{};
    sdl3d_mat4 projection{};
    EXPECT_TRUE(sdl3d_camera3d_compute_matrices(&camera, sdl3d_get_render_context_width(ctx),
                                                sdl3d_get_render_context_height(ctx), 0.01f, 1000.0f, &view,
                                                &projection));

    const sdl3d_mat4 view_projection = sdl3d_mat4_multiply(projection, view);
    const sdl3d_vec4 clip = sdl3d_mat4_transform_vec4(view_projection, sdl3d_vec4_from_vec3(point, 1.0f));
    const float inverse_w = 1.0f / clip.w;
    const float ndc_x = clip.x * inverse_w;
    const float ndc_y = clip.y * inverse_w;
    const float screen_x = (ndc_x + 1.0f) * 0.5f * (float)sdl3d_get_render_context_width(ctx);
    const float screen_y = (1.0f - ndc_y) * 0.5f * (float)sdl3d_get_render_context_height(ctx);

    SDL_Point out{};
    out.x = (int)std::lround(screen_x);
    out.y = (int)std::lround(screen_y);
    return out;
}

float FramebufferAspect(const sdl3d_render_context *ctx)
{
    return (float)sdl3d_get_render_context_width(ctx) / (float)sdl3d_get_render_context_height(ctx);
}

float OrthoHalfHeight(sdl3d_camera3d camera)
{
    return camera.fovy * 0.5f;
}

float OrthoHalfWidth(const sdl3d_render_context *ctx, sdl3d_camera3d camera)
{
    return OrthoHalfHeight(camera) * FramebufferAspect(ctx);
}

constexpr sdl3d_color kBlack = {0, 0, 0, 255};
constexpr sdl3d_color kRed = {255, 0, 0, 255};
constexpr sdl3d_color kBlue = {0, 0, 255, 255};

sdl3d_texture2d MakeSolidTexture(sdl3d_color color)
{
    Uint8 pixels[] = {color.r, color.g, color.b, color.a};
    sdl3d_image image{};
    image.pixels = pixels;
    image.width = 1;
    image.height = 1;

    sdl3d_texture2d texture{};
    EXPECT_TRUE(sdl3d_create_texture_from_image(&image, &texture)) << SDL_GetError();
    return texture;
}
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
    EXPECT_FALSE(sdl3d_draw_rect_overlay(nullptr, 0.0f, 0.0f, 10.0f, 10.0f, kRed));
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

TEST_F(SDL3DDrawingFixture, OverlayRectHonorsScissorOnSoftwareBackend)
{
    WindowRenderer wr(32, 32);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));

    SDL_Rect scissor = {8, 8, 8, 8};
    ASSERT_TRUE(sdl3d_set_scissor_rect(ctx, &scissor));
    ASSERT_TRUE(sdl3d_draw_rect_overlay(ctx, 0.0f, 0.0f, 16.0f, 16.0f, kRed));

    sdl3d_color px{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 4, 4, &px));
    EXPECT_TRUE(PixelEquals(px, kBlack));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 10, 10, &px));
    EXPECT_TRUE(PixelEquals(px, kRed));

    sdl3d_destroy_render_context(ctx);
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
    const sdl3d_camera3d camera = MakeOrthoCamera();
    const float translate_x = OrthoHalfWidth(ctx, camera) * 0.5f;

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, camera));
    ASSERT_TRUE(sdl3d_translate(ctx, translate_x, 0.0f, 0.0f));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    const SDL_Point translated = ProjectPointToFramebuffer(ctx, camera, sdl3d_vec3_make(translate_x, 0.0f, 0.0f));
    const SDL_Point origin = ProjectPointToFramebuffer(ctx, camera, sdl3d_vec3_make(0.0f, 0.0f, 0.0f));

    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, translated.x, translated.y, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, origin.x, origin.y, &c));
    EXPECT_TRUE(PixelEquals(c, kBlack));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, RotateTransformsPointsAroundOrigin)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));
    const sdl3d_camera3d camera = MakeOrthoCamera();
    const float radius = std::fmin(OrthoHalfWidth(ctx, camera), OrthoHalfHeight(camera)) * 0.5f;

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, camera));
    ASSERT_TRUE(sdl3d_rotate(ctx, sdl3d_vec3_make(0.0f, 0.0f, 1.0f), sdl3d_degrees_to_radians(90.0f)));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(radius, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    const SDL_Point rotated = ProjectPointToFramebuffer(ctx, camera, sdl3d_vec3_make(0.0f, radius, 0.0f));
    const SDL_Point unrotated = ProjectPointToFramebuffer(ctx, camera, sdl3d_vec3_make(radius, 0.0f, 0.0f));

    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, rotated.x, rotated.y, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, unrotated.x, unrotated.y, &c));
    EXPECT_TRUE(PixelEquals(c, kBlack));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, ScaleTransformsPointsUnderOrthographicCamera)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));
    const sdl3d_camera3d camera = MakeOrthoCamera();
    const float scaled_x = OrthoHalfWidth(ctx, camera) * 0.5f;
    const float local_x = scaled_x * 0.5f;

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, camera));
    ASSERT_TRUE(sdl3d_scale(ctx, 2.0f, 1.0f, 1.0f));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(local_x, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    const SDL_Point scaled = ProjectPointToFramebuffer(ctx, camera, sdl3d_vec3_make(scaled_x, 0.0f, 0.0f));
    const SDL_Point unscaled = ProjectPointToFramebuffer(ctx, camera, sdl3d_vec3_make(local_x, 0.0f, 0.0f));

    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, scaled.x, scaled.y, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, unscaled.x, unscaled.y, &c));
    EXPECT_TRUE(PixelEquals(c, kBlack));

    sdl3d_destroy_render_context(ctx);
}

TEST_F(SDL3DDrawingFixture, PushPopRestoresPreviousTransform)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));
    const sdl3d_camera3d camera = MakeOrthoCamera();
    const float base_x = OrthoHalfWidth(ctx, camera) * 0.25f;
    const float pushed_x = base_x * 2.0f;

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, camera));
    ASSERT_TRUE(sdl3d_translate(ctx, base_x, 0.0f, 0.0f));
    ASSERT_TRUE(sdl3d_push_matrix(ctx));
    ASSERT_TRUE(sdl3d_translate(ctx, base_x, 0.0f, 0.0f));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kRed));
    ASSERT_TRUE(sdl3d_pop_matrix(ctx));
    ASSERT_TRUE(sdl3d_draw_point_3d(ctx, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), kBlue));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    const SDL_Point red_point = ProjectPointToFramebuffer(ctx, camera, sdl3d_vec3_make(pushed_x, 0.0f, 0.0f));
    const SDL_Point blue_point = ProjectPointToFramebuffer(ctx, camera, sdl3d_vec3_make(base_x, 0.0f, 0.0f));

    sdl3d_color c{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, red_point.x, red_point.y, &c));
    EXPECT_TRUE(PixelEquals(c, kRed));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, blue_point.x, blue_point.y, &c));
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

TEST_F(SDL3DDrawingFixture, BillboardRespondsToPointLight)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &ctx));

    sdl3d_texture2d texture = MakeSolidTexture({80, 80, 80, 255});
    sdl3d_camera3d cam{};
    cam.position = sdl3d_vec3_make(0.0f, 1.0f, 4.0f);
    cam.target = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(sdl3d_draw_billboard(ctx, &texture, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), (sdl3d_vec2){2.0f, 2.0f},
                                     (sdl3d_color){255, 255, 255, 255}));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    sdl3d_color unlit{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 48, 48, &unlit));

    sdl3d_light light{};
    light.type = SDL3D_LIGHT_POINT;
    light.position = sdl3d_vec3_make(0.0f, 1.0f, 3.0f);
    light.color[0] = 1.0f;
    light.color[1] = 0.25f;
    light.color[2] = 0.1f;
    light.intensity = 12.0f;
    light.range = 8.0f;
    ASSERT_TRUE(sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG));
    ASSERT_TRUE(sdl3d_set_ambient_light(ctx, 0.0f, 0.0f, 0.0f));
    ASSERT_TRUE(sdl3d_clear_lights(ctx));
    ASSERT_TRUE(sdl3d_add_light(ctx, &light));

    ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(sdl3d_draw_billboard(ctx, &texture, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), (sdl3d_vec2){2.0f, 2.0f},
                                     (sdl3d_color){255, 255, 255, 255}));
    ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

    sdl3d_color lit{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 48, 48, &lit));

    EXPECT_GT(lit.r, unlit.r);
    EXPECT_GT(lit.r, lit.g);

    sdl3d_free_texture(&texture);
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

TEST_F(SDL3DDrawingFixture, SoftwareSkyboxMatchesExpectedDirections)
{
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());

    sdl3d_render_context_config config;
    sdl3d_init_render_context_config(&config);
    config.backend = SDL3D_BACKEND_SOFTWARE;

    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), &config, &ctx)) << SDL_GetError();

    sdl3d_texture2d px = MakeSolidTexture({255, 0, 0, 255});
    sdl3d_texture2d nx = MakeSolidTexture({0, 255, 0, 255});
    sdl3d_texture2d py = MakeSolidTexture({0, 0, 255, 255});
    sdl3d_texture2d ny = MakeSolidTexture({255, 255, 0, 255});
    sdl3d_texture2d pz = MakeSolidTexture({255, 0, 255, 255});
    sdl3d_texture2d nz = MakeSolidTexture({0, 255, 255, 255});
    sdl3d_skybox_textured skybox = {&px, &nx, &py, &ny, &pz, &nz, 20.0f};

    struct ViewCase
    {
        sdl3d_vec3 target;
        sdl3d_vec3 up;
        sdl3d_color expected;
    } cases[] = {
        {sdl3d_vec3_make(0.0f, 0.0f, 1.0f), sdl3d_vec3_make(0.0f, 1.0f, 0.0f), {255, 0, 0, 255}},
        {sdl3d_vec3_make(1.0f, 0.0f, 0.0f), sdl3d_vec3_make(0.0f, 1.0f, 0.0f), {0, 255, 255, 255}},
        {sdl3d_vec3_make(0.0f, 0.0f, -1.0f), sdl3d_vec3_make(0.0f, 1.0f, 0.0f), {0, 255, 0, 255}},
        {sdl3d_vec3_make(-1.0f, 0.0f, 0.0f), sdl3d_vec3_make(0.0f, 1.0f, 0.0f), {255, 0, 255, 255}},
        {sdl3d_vec3_make(0.0f, 1.0f, 0.0f), sdl3d_vec3_make(0.0f, 0.0f, -1.0f), {0, 0, 255, 255}},
    };

    for (const ViewCase &view_case : cases)
    {
        sdl3d_camera3d cam{};
        sdl3d_color sample{};

        cam.position = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
        cam.target = view_case.target;
        cam.up = view_case.up;
        cam.fovy = 60.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        ASSERT_TRUE(sdl3d_clear_render_context(ctx, kBlack));
        ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, cam));
        ASSERT_TRUE(sdl3d_draw_skybox_textured(ctx, &skybox)) << SDL_GetError();
        ASSERT_TRUE(sdl3d_end_mode_3d(ctx));

        ASSERT_TRUE(sdl3d_get_framebuffer_pixel(ctx, 64, 64, &sample));
        EXPECT_TRUE(PixelEquals(sample, view_case.expected));
    }

    sdl3d_free_texture(&px);
    sdl3d_free_texture(&nx);
    sdl3d_free_texture(&py);
    sdl3d_free_texture(&ny);
    sdl3d_free_texture(&pz);
    sdl3d_free_texture(&nz);
    sdl3d_destroy_render_context(ctx);
}
