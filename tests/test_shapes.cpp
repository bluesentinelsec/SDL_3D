/*
 * Integration tests for the immediate-mode shape primitives. Each
 * primitive renders to a real SDL render context and we check
 * coverage/visibility, winding correctness under backface culling, and
 * composition with the model matrix stack.
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
class SLAYER3DShapesFixture : public ::testing::Test
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
        if (!SDL_CreateWindowAndRenderer("SLAYER3D Shapes Test", width, height, 0, &w, &r))
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

constexpr slayer3d_color kBlack = {0, 0, 0, 255};
constexpr slayer3d_color kRed = {255, 0, 0, 255};
constexpr slayer3d_color kGreen = {0, 255, 0, 255};
constexpr slayer3d_color kBlue = {0, 0, 255, 255};

bool PixelEquals(slayer3d_color a, slayer3d_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int CountColor(slayer3d_render_context *ctx, slayer3d_color c)
{
    const int w = slayer3d_get_render_context_width(ctx);
    const int h = slayer3d_get_render_context_height(ctx);
    int n = 0;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            slayer3d_color px{};
            if (slayer3d_get_framebuffer_pixel(ctx, x, y, &px) && PixelEquals(px, c))
            {
                ++n;
            }
        }
    }
    return n;
}

slayer3d_camera3d MakeCamera(slayer3d_vec3 eye = {0.0f, 0.0f, 4.0f})
{
    slayer3d_camera3d cam{};
    cam.position = eye;
    cam.target = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;
    return cam;
}

struct ContextOwner
{
    slayer3d_render_context *ctx = nullptr;
    ~ContextOwner()
    {
        if (ctx)
        {
            slayer3d_destroy_render_context(ctx);
        }
    }
};
} // namespace

/* --- Argument validation ------------------------------------------------- */

TEST_F(SLAYER3DShapesFixture, RejectsNegativeCubeSize)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    SDL_ClearError();
    EXPECT_FALSE(slayer3d_draw_cube(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(-1, 1, 1), kRed));
    EXPECT_NE(*SDL_GetError(), '\0');
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));
}

TEST_F(SLAYER3DShapesFixture, RejectsBadSphereTessellation)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    EXPECT_FALSE(slayer3d_draw_sphere(c.ctx, slayer3d_vec3_make(0, 0, 0), 1.0f, 1, 8, kRed));
    EXPECT_FALSE(slayer3d_draw_sphere(c.ctx, slayer3d_vec3_make(0, 0, 0), 1.0f, 4, 2, kRed));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));
}

TEST_F(SLAYER3DShapesFixture, RejectsInvertedBoundingBox)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    slayer3d_bounding_box bad{slayer3d_vec3_make(1, 0, 0), slayer3d_vec3_make(0, 1, 1)};
    EXPECT_FALSE(slayer3d_draw_bounding_box(c.ctx, bad, kRed));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));
}

TEST_F(SLAYER3DShapesFixture, RejectsOutsideMode3d)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_draw_cube(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(1, 1, 1), kRed));
    EXPECT_NE(*SDL_GetError(), '\0');
}

/* --- Cube ---------------------------------------------------------------- */

TEST_F(SLAYER3DShapesFixture, SolidCubeCoversCenter)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    ASSERT_TRUE(slayer3d_draw_cube(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(1.0f, 1.0f, 1.0f), kRed));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
    const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
    slayer3d_color p{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
    EXPECT_TRUE(PixelEquals(p, kRed));
    EXPECT_GT(CountColor(c.ctx, kRed), 100);
}

TEST_F(SLAYER3DShapesFixture, CubeFacesAreOutwardUnderBackfaceCulling)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(c.ctx, true));
    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    ASSERT_TRUE(slayer3d_draw_cube(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(1.0f, 1.0f, 1.0f), kRed));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    /* With culling on and all faces outward, the front face is still visible. */
    const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
    const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
    slayer3d_color p{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
    EXPECT_TRUE(PixelEquals(p, kRed));
}

TEST_F(SLAYER3DShapesFixture, CubeFacesAreOutwardFromEachDirection)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));
    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(c.ctx, true));

    /*
     * Orbit the camera around each of the six cardinal directions. With
     * all faces outward-CCW, every direction must leave the center pixel
     * covered by the cube. If any face were inverted this would fail
     * from exactly one direction.
     */
    const slayer3d_vec3 eyes[] = {
        slayer3d_vec3_make(0, 0, 4),  slayer3d_vec3_make(0, 0, -4),     slayer3d_vec3_make(4, 0, 0),
        slayer3d_vec3_make(-4, 0, 0), slayer3d_vec3_make(0, 4, 0.001f), slayer3d_vec3_make(0, -4, 0.001f),
    };
    for (const auto &eye : eyes)
    {
        ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
        ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera(eye)));
        ASSERT_TRUE(slayer3d_draw_cube(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(1.0f, 1.0f, 1.0f), kRed));
        ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

        const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
        const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
        slayer3d_color p{};
        ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
        EXPECT_TRUE(PixelEquals(p, kRed))
            << "Eye (" << eye.x << ", " << eye.y << ", " << eye.z << ") shows the cube as missing.";
    }
}

TEST_F(SLAYER3DShapesFixture, CubeWiresLeaveInteriorBlank)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    ASSERT_TRUE(
        slayer3d_draw_cube_wires(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(1.0f, 1.0f, 1.0f), kGreen));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    EXPECT_GT(CountColor(c.ctx, kGreen), 0);

    const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
    const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
    slayer3d_color p{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
    EXPECT_TRUE(PixelEquals(p, kBlack));
}

/* --- Plane / grid / ray -------------------------------------------------- */

TEST_F(SLAYER3DShapesFixture, PlaneVisibleFromAbove)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(c.ctx, true));
    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));

    slayer3d_camera3d cam = MakeCamera(slayer3d_vec3_make(0, 4, 0.001f));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, cam));
    slayer3d_vec2 size{2.0f, 2.0f};
    ASSERT_TRUE(slayer3d_draw_plane(c.ctx, slayer3d_vec3_make(0, 0, 0), size, kBlue));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
    const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
    slayer3d_color p{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
    EXPECT_TRUE(PixelEquals(p, kBlue));
}

TEST_F(SLAYER3DShapesFixture, PlaneCulledFromBelow)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(c.ctx, true));
    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));

    slayer3d_camera3d cam = MakeCamera(slayer3d_vec3_make(0, -4, 0.001f));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, cam));
    slayer3d_vec2 size{2.0f, 2.0f};
    ASSERT_TRUE(slayer3d_draw_plane(c.ctx, slayer3d_vec3_make(0, 0, 0), size, kBlue));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    EXPECT_EQ(CountColor(c.ctx, kBlue), 0);
}

TEST_F(SLAYER3DShapesFixture, GridDrawsLines)
{
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    slayer3d_camera3d cam = MakeCamera(slayer3d_vec3_make(0, 3, 3));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, cam));
    ASSERT_TRUE(slayer3d_draw_grid(c.ctx, 10, 0.5f, kGreen));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    EXPECT_GT(CountColor(c.ctx, kGreen), 20);
}

TEST_F(SLAYER3DShapesFixture, GridRejectsBadArguments)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    EXPECT_FALSE(slayer3d_draw_grid(c.ctx, 0, 1.0f, kGreen));
    EXPECT_FALSE(slayer3d_draw_grid(c.ctx, 4, 0.0f, kGreen));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));
}

TEST_F(SLAYER3DShapesFixture, RayDrawsLineSegment)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    slayer3d_ray ray{slayer3d_vec3_make(-0.6f, 0.0f, 0.0f), slayer3d_vec3_make(1.2f, 0.0f, 0.0f)};
    ASSERT_TRUE(slayer3d_draw_ray(c.ctx, ray, kRed));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    EXPECT_GT(CountColor(c.ctx, kRed), 10);
}

TEST_F(SLAYER3DShapesFixture, BoundingBoxDrawsTwelveEdges)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    slayer3d_bounding_box box{slayer3d_vec3_make(-0.5f, -0.5f, -0.5f), slayer3d_vec3_make(0.5f, 0.5f, 0.5f)};
    ASSERT_TRUE(slayer3d_draw_bounding_box(c.ctx, box, kGreen));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    EXPECT_GT(CountColor(c.ctx, kGreen), 0);

    const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
    const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
    slayer3d_color p{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
    EXPECT_TRUE(PixelEquals(p, kBlack));
}

/* --- Sphere / cylinder / capsule ---------------------------------------- */

TEST_F(SLAYER3DShapesFixture, SolidSphereHasOutwardFaces)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(c.ctx, true));
    const slayer3d_vec3 eyes[] = {
        slayer3d_vec3_make(0, 0, 4),  slayer3d_vec3_make(0, 0, -4),     slayer3d_vec3_make(4, 0, 0),
        slayer3d_vec3_make(-4, 0, 0), slayer3d_vec3_make(0, 4, 0.001f),
    };
    for (const auto &eye : eyes)
    {
        ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
        ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera(eye)));
        ASSERT_TRUE(slayer3d_draw_sphere(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.7f, 10, 18, kRed));
        ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

        const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
        const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
        slayer3d_color p{};
        ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
        EXPECT_TRUE(PixelEquals(p, kRed))
            << "Eye (" << eye.x << ", " << eye.y << ", " << eye.z << ") shows the sphere as missing.";
    }
}

TEST_F(SLAYER3DShapesFixture, TexturedSphereDrawsWithLocalRotation)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    Uint8 pixels[] = {
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    };
    slayer3d_image image{};
    image.width = 2;
    image.height = 2;
    image.pixels = pixels;

    slayer3d_texture2d texture{};
    ASSERT_TRUE(slayer3d_create_texture_from_image(&image, &texture));

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    ASSERT_TRUE(slayer3d_draw_sphere_textured(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.7f, 10, 18,
                                              slayer3d_vec3_make(0, 0, 1), 0.75f, &texture, kBlue));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
    const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
    slayer3d_color p{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
    EXPECT_FALSE(PixelEquals(p, kBlack));

    slayer3d_free_texture(&texture);
}

TEST_F(SLAYER3DShapesFixture, SphereWiresDrawWithoutFilling)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    ASSERT_TRUE(slayer3d_draw_sphere_wires(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.7f, 6, 12, kGreen));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    const int total = slayer3d_get_render_context_width(c.ctx) * slayer3d_get_render_context_height(c.ctx);
    const int green = CountColor(c.ctx, kGreen);
    EXPECT_GT(green, 0);
    EXPECT_LT(green, total / 2);
}

TEST_F(SLAYER3DShapesFixture, SolidCylinderHasOutwardFaces)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(c.ctx, true));

    const slayer3d_vec3 eyes[] = {
        slayer3d_vec3_make(0, 0, 4),
        slayer3d_vec3_make(4, 0, 0),
        slayer3d_vec3_make(0, 4, 0.001f),
        slayer3d_vec3_make(0, -4, 0.001f),
    };
    for (const auto &eye : eyes)
    {
        ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
        ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera(eye)));
        ASSERT_TRUE(slayer3d_draw_cylinder(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.5f, 0.5f, 1.2f, 16, kRed));
        ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

        const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
        const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
        slayer3d_color p{};
        ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
        EXPECT_TRUE(PixelEquals(p, kRed))
            << "Eye (" << eye.x << ", " << eye.y << ", " << eye.z << ") shows the cylinder as missing.";
    }
}

TEST_F(SLAYER3DShapesFixture, ConeDrawsAsTruncatedCylinder)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(c.ctx, true));
    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    /* Cone: top radius 0, bottom radius 0.6. */
    ASSERT_TRUE(slayer3d_draw_cylinder(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.0f, 0.6f, 1.2f, 16, kRed));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
    const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
    slayer3d_color p{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
    EXPECT_TRUE(PixelEquals(p, kRed));
}

TEST_F(SLAYER3DShapesFixture, SolidCapsuleHasOutwardFaces)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(c.ctx, true));

    const slayer3d_vec3 eyes[] = {
        slayer3d_vec3_make(0, 0, 4),
        slayer3d_vec3_make(4, 0, 0),
        slayer3d_vec3_make(0, 4, 0.001f),
        slayer3d_vec3_make(0, -4, 0.001f),
    };
    for (const auto &eye : eyes)
    {
        ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
        ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera(eye)));
        ASSERT_TRUE(slayer3d_draw_capsule(c.ctx, slayer3d_vec3_make(0.0f, -0.4f, 0.0f),
                                          slayer3d_vec3_make(0.0f, 0.4f, 0.0f), 0.35f, 16, 6, kRed));
        ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

        const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
        const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
        slayer3d_color p{};
        ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
        EXPECT_TRUE(PixelEquals(p, kRed))
            << "Eye (" << eye.x << ", " << eye.y << ", " << eye.z << ") shows the capsule as missing.";
    }
}

TEST_F(SLAYER3DShapesFixture, CapsuleCollapsesToSphereWhenStartEqualsEnd)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(c.ctx, true));
    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    const slayer3d_vec3 p0 = slayer3d_vec3_make(0, 0, 0);
    ASSERT_TRUE(slayer3d_draw_capsule(c.ctx, p0, p0, 0.6f, 16, 6, kRed));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
    const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
    slayer3d_color p{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
    EXPECT_TRUE(PixelEquals(p, kRed));
}

TEST_F(SLAYER3DShapesFixture, CapsuleAcceptsArbitraryAxis)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(c.ctx, true));
    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    /* Capsule along X axis, in front of camera. */
    ASSERT_TRUE(slayer3d_draw_capsule(c.ctx, slayer3d_vec3_make(-0.5f, 0.0f, 0.0f),
                                      slayer3d_vec3_make(0.5f, 0.0f, 0.0f), 0.3f, 16, 6, kRed));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
    const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
    slayer3d_color p{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
    EXPECT_TRUE(PixelEquals(p, kRed));
}

TEST_F(SLAYER3DShapesFixture, TorusPyramidAndWedgeRender)
{
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(c.ctx, true));

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    ASSERT_TRUE(slayer3d_draw_torus(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.55f, 0.18f, 24, 8, kRed));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));
    EXPECT_GT(CountColor(c.ctx, kRed), 100);

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    ASSERT_TRUE(
        slayer3d_draw_pyramid(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(1.2f, 1.2f, 1.2f), kGreen));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));
    EXPECT_GT(CountColor(c.ctx, kGreen), 100);

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    ASSERT_TRUE(slayer3d_draw_wedge(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(1.2f, 1.0f, 1.2f), kBlue));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));
    EXPECT_GT(CountColor(c.ctx, kBlue), 100);
}

TEST_F(SLAYER3DShapesFixture, AdditionalPlaceholderPrimitivesRender)
{
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    ASSERT_TRUE(slayer3d_draw_quad(c.ctx, slayer3d_vec3_make(-0.7f, 0.7f, 0.0f), {0.7f, 0.5f}, kRed));
    ASSERT_TRUE(slayer3d_draw_disc(c.ctx, slayer3d_vec3_make(0.7f, 0.7f, 0.0f), 0.32f, 18, kGreen));
    ASSERT_TRUE(slayer3d_draw_hemisphere(c.ctx, slayer3d_vec3_make(-0.7f, -0.35f, 0.0f), 0.42f, 6, 18, kBlue));
    ASSERT_TRUE(slayer3d_draw_rounded_box(c.ctx, slayer3d_vec3_make(0.65f, -0.35f, 0.0f),
                                          slayer3d_vec3_make(0.55f, 0.55f, 0.55f), 0.14f, 3, kRed));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));
    EXPECT_GT(CountColor(c.ctx, kRed), 40);
    EXPECT_GT(CountColor(c.ctx, kGreen), 20);
    EXPECT_GT(CountColor(c.ctx, kBlue), 20);

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    ASSERT_TRUE(slayer3d_draw_tube_segment(c.ctx, slayer3d_vec3_make(-0.55f, 0.0f, 0.0f), 0.45f, 0.1f, 3.1415927f, 16,
                                           8, kGreen));
    ASSERT_TRUE(slayer3d_draw_arrow(c.ctx, slayer3d_vec3_make(0.65f, 0.0f, 0.0f), 0.28f, 1.1f, 16, kBlue));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));
    EXPECT_GT(CountColor(c.ctx, kGreen), 20);
    EXPECT_GT(CountColor(c.ctx, kBlue), 20);
}

TEST_F(SLAYER3DShapesFixture, NewPrimitiveWiresDrawWithoutFilling)
{
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    ASSERT_TRUE(slayer3d_draw_torus_wires(c.ctx, slayer3d_vec3_make(-0.8f, 0, 0), 0.35f, 0.12f, 16, 6, kGreen));
    ASSERT_TRUE(slayer3d_draw_pyramid_wires(c.ctx, slayer3d_vec3_make(0.6f, 0, 0), slayer3d_vec3_make(0.7f, 0.8f, 0.7f),
                                            kGreen));
    ASSERT_TRUE(slayer3d_draw_wedge_wires(c.ctx, slayer3d_vec3_make(0, -0.9f, 0), slayer3d_vec3_make(0.7f, 0.6f, 0.7f),
                                          kGreen));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    const int total = slayer3d_get_render_context_width(c.ctx) * slayer3d_get_render_context_height(c.ctx);
    const int green = CountColor(c.ctx, kGreen);
    EXPECT_GT(green, 20);
    EXPECT_LT(green, total / 2);
}

TEST_F(SLAYER3DShapesFixture, RejectsBadNewPrimitiveArguments)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));
    EXPECT_FALSE(slayer3d_draw_torus(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.0f, 0.1f, 8, 6, kRed));
    EXPECT_FALSE(slayer3d_draw_torus(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.5f, 0.1f, 2, 6, kRed));
    EXPECT_FALSE(slayer3d_draw_torus_wires(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.5f, 0.1f, 8, 2, kRed));
    EXPECT_FALSE(
        slayer3d_draw_pyramid(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(-1.0f, 1.0f, 1.0f), kRed));
    EXPECT_FALSE(slayer3d_draw_wedge(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(1.0f, -1.0f, 1.0f), kRed));
    EXPECT_FALSE(slayer3d_draw_disc(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.5f, 2, kRed));
    EXPECT_FALSE(slayer3d_draw_hemisphere(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.5f, 1, 8, kRed));
    EXPECT_FALSE(slayer3d_draw_tube_segment(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.5f, 0.1f, 0.0f, 8, 6, kRed));
    EXPECT_FALSE(slayer3d_draw_rounded_box(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(1.0f, 1.0f, 1.0f),
                                           0.1f, 0, kRed));
    EXPECT_FALSE(slayer3d_draw_arrow(c.ctx, slayer3d_vec3_make(0, 0, 0), 0.0f, 1.0f, 8, kRed));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));
}

/* --- Matrix-stack composition ------------------------------------------- */

TEST_F(SLAYER3DShapesFixture, CubeHonorsModelMatrixStack)
{
    WindowRenderer wr(96, 96);
    ASSERT_TRUE(wr.ok());
    ContextOwner c;
    ASSERT_TRUE(slayer3d_create_render_context(wr.window(), wr.renderer(), nullptr, &c.ctx));

    ASSERT_TRUE(slayer3d_clear_render_context(c.ctx, kBlack));
    ASSERT_TRUE(slayer3d_begin_mode_3d(c.ctx, MakeCamera()));

    /*
     * Shift a small cube off-origin via the matrix stack and confirm it
     * no longer covers the screen center. Y-axis translation is aspect-
     * independent under a perspective camera, so the test is portable
     * across portrait/landscape window sizes we may encounter in CI.
     */
    ASSERT_TRUE(slayer3d_push_matrix(c.ctx));
    ASSERT_TRUE(slayer3d_translate(c.ctx, 0.0f, 0.8f, 0.0f));
    ASSERT_TRUE(slayer3d_draw_cube(c.ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(0.4f, 0.4f, 0.4f), kRed));
    ASSERT_TRUE(slayer3d_pop_matrix(c.ctx));
    ASSERT_TRUE(slayer3d_end_mode_3d(c.ctx));

    const int cx = slayer3d_get_render_context_width(c.ctx) / 2;
    const int cy = slayer3d_get_render_context_height(c.ctx) / 2;
    slayer3d_color p{};
    ASSERT_TRUE(slayer3d_get_framebuffer_pixel(c.ctx, cx, cy, &p));
    EXPECT_TRUE(PixelEquals(p, kBlack));

    /* But the cube is drawn somewhere. */
    EXPECT_GT(CountColor(c.ctx, kRed), 10);
}
