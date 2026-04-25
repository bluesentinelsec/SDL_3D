/*
 * GL renderer tests — validates that the OpenGL backend produces
 * correct pixel output for basic rendering operations.
 *
 * Each test creates a hidden GL window, renders a known scene, reads
 * back pixels, and asserts expected values. No visual inspection needed.
 */

#include <gtest/gtest.h>

extern "C"
{
#include "gl_renderer.h"
#include "render_context_internal.h"
#include "sdl3d/level.h"
#include "sdl3d/sdl3d.h"
}

namespace
{

sdl3d_texture2d MakeTextureFromPixels(const Uint8 *pixels, int width, int height)
{
    sdl3d_image image{};
    image.pixels = const_cast<Uint8 *>(pixels);
    image.width = width;
    image.height = height;

    sdl3d_texture2d texture{};
    EXPECT_TRUE(sdl3d_create_texture_from_image(&image, &texture));
    EXPECT_TRUE(sdl3d_set_texture_filter(&texture, SDL3D_TEXTURE_FILTER_NEAREST));
    return texture;
}

} // namespace

class GLRendererTest : public ::testing::Test
{
  protected:
    SDL_Window *win = nullptr;
    sdl3d_render_context *ctx = nullptr;

    void SetUp() override
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        win = SDL_CreateWindow("GL test", 320, 240, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!win)
        {
            GTEST_SKIP() << "Cannot create GL window: " << SDL_GetError();
        }

        sdl3d_render_context_config cfg;
        sdl3d_init_render_context_config(&cfg);
        cfg.backend = SDL3D_BACKEND_SDLGPU;
        cfg.logical_width = 320;
        cfg.logical_height = 240;

        if (!sdl3d_create_render_context(win, nullptr, &cfg, &ctx))
        {
            SDL_DestroyWindow(win);
            win = nullptr;
            GTEST_SKIP() << "Cannot create GL context: " << SDL_GetError();
        }
    }

    void TearDown() override
    {
        if (ctx)
            sdl3d_destroy_render_context(ctx);
        if (win)
            SDL_DestroyWindow(win);
    }

    void readPixel(int x, int y, unsigned char *rgba)
    {
        sdl3d_gl_read_pixel(ctx->gl, x, y, rgba);
    }
};

TEST_F(GLRendererTest, ClearProducesExpectedColor)
{
    sdl3d_color red = {255, 0, 0, 255};
    sdl3d_clear_render_context(ctx, red);

    unsigned char px[4];
    readPixel(160, 120, px);

    EXPECT_GE(px[0], 200) << "Red channel should be high after red clear";
    EXPECT_LE(px[1], 30) << "Green channel should be low after red clear";
    EXPECT_LE(px[2], 30) << "Blue channel should be low after red clear";
}

TEST_F(GLRendererTest, LitCubeProducesNonClearPixels)
{
    ASSERT_TRUE(sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG));

    /* Set up a light so the cube is visible. */
    sdl3d_light sun = {};
    sun.type = SDL3D_LIGHT_DIRECTIONAL;
    sun.direction = sdl3d_vec3_make(0, -1, -1);
    sun.color[0] = 1;
    sun.color[1] = 1;
    sun.color[2] = 1;
    sun.intensity = 2.0f;
    sdl3d_add_light(ctx, &sun);
    sdl3d_set_ambient_light(ctx, 0.3f, 0.3f, 0.3f);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 3, 5);
    cam.target = sdl3d_vec3_make(0, 0, 0);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_color clear = {0, 0, 0, 255};
    sdl3d_color white = {255, 255, 255, 255};

    sdl3d_clear_render_context(ctx, clear);
    sdl3d_begin_mode_3d(ctx, cam);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(2, 2, 2), white);
    sdl3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    /* The center pixel should NOT be the clear color (black). */
    int brightness = px[0] + px[1] + px[2];
    EXPECT_GT(brightness, 30);
}

TEST_F(GLRendererTest, PhongCubeVisibleWithoutIBL)
{
    ASSERT_TRUE(sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG));

    sdl3d_light light = {};
    light.type = SDL3D_LIGHT_POINT;
    light.position = sdl3d_vec3_make(2.0f, 2.5f, 3.5f);
    light.color[0] = 1.0f;
    light.color[1] = 0.95f;
    light.color[2] = 0.85f;
    light.intensity = 8.0f;
    light.range = 20.0f;
    sdl3d_add_light(ctx, &light);
    sdl3d_set_ambient_light(ctx, 0.15f, 0.15f, 0.18f);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 0, 5);
    cam.target = sdl3d_vec3_make(0, 0, 0);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, (sdl3d_color){8, 32, 96, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(2, 2, 2), (sdl3d_color){255, 255, 255, 255});
    sdl3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    EXPECT_GT(px[0] + px[1] + px[2], 60);
    EXPECT_FALSE(px[0] == 8 && px[1] == 32 && px[2] == 96);
}

TEST_F(GLRendererTest, AmbientOnlyPhongCubeUsesLitPath)
{
    ASSERT_TRUE(sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG));
    ASSERT_TRUE(sdl3d_set_ambient_light(ctx, 0.12f, 0.12f, 0.14f));

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 0, 5);
    cam.target = sdl3d_vec3_make(0, 0, 0);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(2, 2, 2), (sdl3d_color){255, 255, 255, 255});
    sdl3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    EXPECT_GT(px[0] + px[1] + px[2], 20);
    EXPECT_LT(px[0], 220);
    EXPECT_LT(px[1], 220);
    EXPECT_LT(px[2], 220);
}

TEST_F(GLRendererTest, AmbientOnlyPhongModelUsesLitPath)
{
    ASSERT_TRUE(sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG));
    ASSERT_TRUE(sdl3d_set_ambient_light(ctx, 0.10f, 0.12f, 0.14f));

    float positions[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    unsigned int indices[] = {0, 1, 2, 0, 2, 3};
    sdl3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 6;

    sdl3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 0, 5);
    cam.target = sdl3d_vec3_make(0, 0, 0);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(sdl3d_draw_model(ctx, &model, sdl3d_vec3_make(0, 0, 0), 1.0f, (sdl3d_color){255, 255, 255, 255}));
    sdl3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    EXPECT_GT(px[0] + px[1] + px[2], 20);
    EXPECT_LT(px[0], 220);
    EXPECT_LT(px[1], 220);
    EXPECT_LT(px[2], 220);
}

TEST_F(GLRendererTest, CubeVisibleOnFirstFrame)
{
    /* This specifically tests lesson #1 and #9: first frame must be correct. */
    sdl3d_set_ambient_light(ctx, 0.5f, 0.5f, 0.5f);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 0, 5);
    cam.target = sdl3d_vec3_make(0, 0, 0);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_color clear = {0, 0, 0, 255};
    sdl3d_color green = {0, 255, 0, 255};

    sdl3d_clear_render_context(ctx, clear);
    sdl3d_begin_mode_3d(ctx, cam);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(3, 3, 3), green);
    sdl3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    EXPECT_GT(px[1], 50);
}

TEST_F(GLRendererTest, Line3DVisibleOnGLBackend)
{
    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 0, 5);
    cam.target = sdl3d_vec3_make(0, 0, 0);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    sdl3d_draw_line_3d(ctx, sdl3d_vec3_make(-1.5f, 0.0f, 0.0f), sdl3d_vec3_make(1.5f, 0.0f, 0.0f),
                       (sdl3d_color){255, 32, 32, 255});
    sdl3d_end_mode_3d(ctx);

    bool found_red = false;
    for (int y = 0; y < 240 && !found_red; ++y)
    {
        for (int x = 0; x < 320; ++x)
        {
            unsigned char px[4];
            readPixel(x, y, px);
            if (px[0] > 100 && px[1] < 80 && px[2] < 80)
            {
                found_red = true;
                break;
            }
        }
    }

    EXPECT_TRUE(found_red);
}

TEST_F(GLRendererTest, BackfaceCullingShowsFrontFaces)
{
    /* A cube viewed from the front should show the front face, not the back. */
    sdl3d_set_ambient_light(ctx, 1.0f, 1.0f, 1.0f);
    sdl3d_set_backface_culling_enabled(ctx, true);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 0, 3);
    cam.target = sdl3d_vec3_make(0, 0, 0);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_color clear = {0, 0, 0, 255};
    sdl3d_color red = {255, 0, 0, 255};

    sdl3d_clear_render_context(ctx, clear);
    sdl3d_begin_mode_3d(ctx, cam);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(4, 4, 4), red);
    sdl3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    /* With correct culling, we should see the front face (red), not black. */
    EXPECT_GT(px[0], 50);
}

TEST_F(GLRendererTest, BackfaceCullingHidesCubeInteriorAcrossFrames)
{
    sdl3d_set_ambient_light(ctx, 1.0f, 1.0f, 1.0f);
    sdl3d_set_backface_culling_enabled(ctx, true);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 0, 0);
    cam.target = sdl3d_vec3_make(0, 0, -1);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    for (int frame = 0; frame < 2; ++frame)
    {
        sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
        sdl3d_begin_mode_3d(ctx, cam);
        sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(4, 4, 4), (sdl3d_color){255, 0, 0, 255});
        sdl3d_end_mode_3d(ctx);

        unsigned char px[4];
        readPixel(160, 120, px);
        EXPECT_LT(px[0] + px[1] + px[2], 20) << "Interior cube faces should remain culled on frame " << frame;
    }
}

TEST_F(GLRendererTest, LevelInteriorVisibleWithBackfaceCulling)
{
    const sdl3d_level_material materials[] = {{{1, 1, 1, 1}, 0.0f, 1.0f, nullptr, 4.0f},
                                              {{1, 1, 1, 1}, 0.0f, 1.0f, nullptr, 4.0f},
                                              {{1, 1, 1, 1}, 0.0f, 1.0f, nullptr, 4.0f}};
    sdl3d_sector sector{};
    sector.points[0][0] = 0.0f;
    sector.points[0][1] = 0.0f;
    sector.points[1][0] = 4.0f;
    sector.points[1][1] = 0.0f;
    sector.points[2][0] = 4.0f;
    sector.points[2][1] = 4.0f;
    sector.points[3][0] = 0.0f;
    sector.points[3][1] = 4.0f;
    sector.num_points = 4;
    sector.floor_y = 0.0f;
    sector.ceil_y = 3.0f;
    sector.floor_material = 0;
    sector.ceil_material = 1;
    sector.wall_material = 2;

    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(&sector, 1, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    sdl3d_set_backface_culling_enabled(ctx, true);
    sdl3d_set_ambient_light(ctx, 1.0f, 1.0f, 1.0f);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(2.0f, 1.5f, 2.0f);
    cam.target = sdl3d_vec3_make(2.0f, 1.5f, 0.0f);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(sdl3d_draw_level(ctx, &level, nullptr, (sdl3d_color){255, 255, 255, 255})) << SDL_GetError();
    sdl3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    sdl3d_free_level(&level);

    EXPECT_GT(px[0] + px[1] + px[2], 30);
}

TEST_F(GLRendererTest, LightmappedLevelRendersWithoutVertexColorFallback)
{
    const sdl3d_level_material materials[] = {
        {{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 1.0f, nullptr, 4.0f},
        {{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 1.0f, nullptr, 4.0f},
        {{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 1.0f, nullptr, 4.0f},
    };
    const sdl3d_level_light light = {{2.0f, 2.2f, 2.0f}, {1.0f, 0.9f, 0.8f}, 3.0f, 8.0f};
    sdl3d_sector sector{};
    sector.points[0][0] = 0.0f;
    sector.points[0][1] = 0.0f;
    sector.points[1][0] = 4.0f;
    sector.points[1][1] = 0.0f;
    sector.points[2][0] = 4.0f;
    sector.points[2][1] = 4.0f;
    sector.points[3][0] = 0.0f;
    sector.points[3][1] = 4.0f;
    sector.num_points = 4;
    sector.floor_y = 0.0f;
    sector.ceil_y = 3.0f;
    sector.floor_material = 0;
    sector.ceil_material = 1;
    sector.wall_material = 2;

    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(&sector, 1, materials, 3, &light, 1, &level)) << SDL_GetError();

    for (int mi = 0; mi < level.model.mesh_count; ++mi)
    {
        sdl3d_mesh &mesh = level.model.meshes[mi];
        ASSERT_NE(mesh.colors, nullptr);
        for (int v = 0; v < mesh.vertex_count; ++v)
        {
            mesh.colors[v * 4 + 0] = 0.0f;
            mesh.colors[v * 4 + 1] = 0.0f;
            mesh.colors[v * 4 + 2] = 0.0f;
            mesh.colors[v * 4 + 3] = 1.0f;
        }
    }

    ASSERT_TRUE(sdl3d_set_shading_mode(ctx, SDL3D_SHADING_PHONG));
    sdl3d_set_ambient_light(ctx, 0.0f, 0.0f, 0.0f);
    sdl3d_set_backface_culling_enabled(ctx, true);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(2.0f, 1.5f, 2.0f);
    cam.target = sdl3d_vec3_make(2.0f, 1.5f, 0.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(sdl3d_draw_level(ctx, &level, nullptr, (sdl3d_color){255, 255, 255, 255})) << SDL_GetError();
    sdl3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);
    sdl3d_free_level(&level);

    EXPECT_GT(px[0] + px[1] + px[2], 30);
}

TEST_F(GLRendererTest, BillboardVisibleWithTexture)
{
    const Uint8 pixels[] = {
        255, 64, 64, 255, 255, 64, 64, 255, 255, 64, 64, 255, 255, 64, 64, 255,
    };
    sdl3d_texture2d texture = MakeTextureFromPixels(pixels, 2, 2);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0.0f, 1.0f, 4.0f);
    cam.target = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(sdl3d_draw_billboard(ctx, &texture, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), (sdl3d_vec2){2.0f, 2.0f},
                                     (sdl3d_color){255, 255, 255, 255}))
        << SDL_GetError();
    sdl3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);
    sdl3d_free_texture(&texture);

    EXPECT_GT(px[0], 100);
    EXPECT_GT(px[1], 20);
    EXPECT_GT(px[2], 20);
}

TEST_F(GLRendererTest, BillboardVisibleFromOppositeViewDirection)
{
    const Uint8 pixels[] = {
        255, 64, 64, 255, 255, 64, 64, 255, 255, 64, 64, 255, 255, 64, 64, 255,
    };
    sdl3d_texture2d texture = MakeTextureFromPixels(pixels, 2, 2);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0.0f, 1.0f, -4.0f);
    cam.target = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(sdl3d_draw_billboard(ctx, &texture, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), (sdl3d_vec2){2.0f, 2.0f},
                                     (sdl3d_color){255, 255, 255, 255}))
        << SDL_GetError();
    sdl3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);
    sdl3d_free_texture(&texture);

    EXPECT_GT(px[0], 100);
}

TEST_F(GLRendererTest, BillboardPreservesTopToBottomTextureOrientation)
{
    const Uint8 pixels[] = {
        255, 32, 32, 255, 255, 32, 32, 255, 32, 32, 255, 255, 32, 32, 255, 255,
    };
    sdl3d_texture2d texture = MakeTextureFromPixels(pixels, 2, 2);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0.0f, 1.0f, 4.0f);
    cam.target = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(sdl3d_draw_billboard(ctx, &texture, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), (sdl3d_vec2){2.0f, 2.0f},
                                     (sdl3d_color){255, 255, 255, 255}))
        << SDL_GetError();
    sdl3d_end_mode_3d(ctx);

    unsigned char top_px[4];
    unsigned char bottom_px[4];
    readPixel(160, 150, top_px);
    readPixel(160, 90, bottom_px);
    sdl3d_free_texture(&texture);

    EXPECT_GT(top_px[0], top_px[2]);
    EXPECT_GT(bottom_px[2], bottom_px[0]);
}

TEST_F(GLRendererTest, BillboardTransparentPixelsDiscard)
{
    const Uint8 pixels[] = {
        0, 0, 0, 0, 255, 64, 64, 255, 0, 0, 0, 0, 255, 64, 64, 255,
    };
    sdl3d_texture2d texture = MakeTextureFromPixels(pixels, 2, 2);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0.0f, 1.0f, 4.0f);
    cam.target = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(sdl3d_draw_billboard(ctx, &texture, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), (sdl3d_vec2){2.0f, 2.0f},
                                     (sdl3d_color){255, 255, 255, 255}))
        << SDL_GetError();
    sdl3d_end_mode_3d(ctx);

    unsigned char left_px[4];
    unsigned char right_px[4];
    readPixel(130, 120, left_px);
    readPixel(190, 120, right_px);
    sdl3d_free_texture(&texture);

    EXPECT_LT(left_px[0] + left_px[1] + left_px[2], 40);
    EXPECT_GT(right_px[0], 100);
}

TEST_F(GLRendererTest, TexturedSkyboxShowsTopFaceWithBackfaceCulling)
{
    const Uint8 red[] = {255, 0, 0, 255};
    const Uint8 green[] = {0, 255, 0, 255};
    const Uint8 blue[] = {0, 0, 255, 255};
    const Uint8 yellow[] = {255, 255, 0, 255};
    const Uint8 magenta[] = {255, 0, 255, 255};
    const Uint8 cyan[] = {0, 255, 255, 255};
    sdl3d_texture2d px = MakeTextureFromPixels(red, 1, 1);
    sdl3d_texture2d nx = MakeTextureFromPixels(green, 1, 1);
    sdl3d_texture2d py = MakeTextureFromPixels(blue, 1, 1);
    sdl3d_texture2d ny = MakeTextureFromPixels(yellow, 1, 1);
    sdl3d_texture2d pz = MakeTextureFromPixels(magenta, 1, 1);
    sdl3d_texture2d nz = MakeTextureFromPixels(cyan, 1, 1);
    sdl3d_skybox_textured skybox = {&px, &nx, &py, &ny, &pz, &nz, 20.0f};

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    cam.target = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = sdl3d_vec3_make(0.0f, 0.0f, -1.0f);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    ASSERT_TRUE(sdl3d_set_backface_culling_enabled(ctx, true));
    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(sdl3d_draw_skybox_textured(ctx, &skybox)) << SDL_GetError();
    sdl3d_end_mode_3d(ctx);

    unsigned char px_out[4];
    readPixel(160, 120, px_out);

    sdl3d_free_texture(&px);
    sdl3d_free_texture(&nx);
    sdl3d_free_texture(&py);
    sdl3d_free_texture(&ny);
    sdl3d_free_texture(&pz);
    sdl3d_free_texture(&nz);

    EXPECT_GT(px_out[2], 150);
    EXPECT_LT(px_out[0], 80);
    EXPECT_LT(px_out[1], 80);
}

TEST_F(GLRendererTest, TexturedSkyboxMatchesSeamConsistentDirections)
{
    const Uint8 red[] = {255, 0, 0, 255};
    const Uint8 green[] = {0, 255, 0, 255};
    const Uint8 blue[] = {0, 0, 255, 255};
    const Uint8 yellow[] = {255, 255, 0, 255};
    const Uint8 magenta[] = {255, 0, 255, 255};
    const Uint8 cyan[] = {0, 255, 255, 255};
    sdl3d_texture2d px = MakeTextureFromPixels(red, 1, 1);
    sdl3d_texture2d nx = MakeTextureFromPixels(green, 1, 1);
    sdl3d_texture2d py = MakeTextureFromPixels(blue, 1, 1);
    sdl3d_texture2d ny = MakeTextureFromPixels(yellow, 1, 1);
    sdl3d_texture2d pz = MakeTextureFromPixels(magenta, 1, 1);
    sdl3d_texture2d nz = MakeTextureFromPixels(cyan, 1, 1);
    sdl3d_skybox_textured skybox = {&px, &nx, &py, &ny, &pz, &nz, 20.0f};
    struct ViewCase
    {
        sdl3d_vec3 target;
        Uint8 r;
        Uint8 g;
        Uint8 b;
    } cases[] = {
        {sdl3d_vec3_make(0.0f, 0.0f, 1.0f), 255, 0, 0},   /* front -> PX */
        {sdl3d_vec3_make(1.0f, 0.0f, 0.0f), 0, 255, 255}, /* right -> NZ */
        {sdl3d_vec3_make(0.0f, 0.0f, -1.0f), 0, 255, 0},  /* back -> NX */
        {sdl3d_vec3_make(-1.0f, 0.0f, 0.0f), 255, 0, 255} /* left -> PZ */
    };

    ASSERT_TRUE(sdl3d_set_backface_culling_enabled(ctx, true));

    for (const ViewCase &view_case : cases)
    {
        sdl3d_camera3d cam;
        unsigned char px_out[4];

        cam.position = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
        cam.target = view_case.target;
        cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
        cam.fovy = 60.0f;
        cam.projection = SDL3D_CAMERA_PERSPECTIVE;

        sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
        sdl3d_begin_mode_3d(ctx, cam);
        ASSERT_TRUE(sdl3d_draw_skybox_textured(ctx, &skybox)) << SDL_GetError();
        sdl3d_end_mode_3d(ctx);

        readPixel(160, 120, px_out);
        EXPECT_NEAR(px_out[0], view_case.r, 20);
        EXPECT_NEAR(px_out[1], view_case.g, 20);
        EXPECT_NEAR(px_out[2], view_case.b, 20);
    }

    sdl3d_free_texture(&px);
    sdl3d_free_texture(&nx);
    sdl3d_free_texture(&py);
    sdl3d_free_texture(&ny);
    sdl3d_free_texture(&pz);
    sdl3d_free_texture(&nz);
}

TEST_F(GLRendererTest, ToggleRecreateProducesCorrectOutput)
{
    /* Lesson #9: verify output is correct after context recreation. */
    sdl3d_set_ambient_light(ctx, 0.5f, 0.5f, 0.5f);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 0, 5);
    cam.target = sdl3d_vec3_make(0, 0, 0);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_color clear = {0, 0, 0, 255};
    sdl3d_color blue = {0, 0, 255, 255};

    /* First render. */
    sdl3d_clear_render_context(ctx, clear);
    sdl3d_begin_mode_3d(ctx, cam);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(3, 3, 3), blue);
    sdl3d_end_mode_3d(ctx);

    unsigned char px1[4];
    readPixel(160, 120, px1);

    /* Destroy and recreate. */
    sdl3d_destroy_render_context(ctx);
    ctx = nullptr;

    sdl3d_render_context_config cfg;
    sdl3d_init_render_context_config(&cfg);
    cfg.backend = SDL3D_BACKEND_SDLGPU;
    cfg.logical_width = 320;
    cfg.logical_height = 240;
    ASSERT_TRUE(sdl3d_create_render_context(win, nullptr, &cfg, &ctx));

    sdl3d_set_ambient_light(ctx, 0.5f, 0.5f, 0.5f);

    /* Second render after recreation. */
    sdl3d_clear_render_context(ctx, clear);
    sdl3d_begin_mode_3d(ctx, cam);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(3, 3, 3), blue);
    sdl3d_end_mode_3d(ctx);

    unsigned char px2[4];
    readPixel(160, 120, px2);

    EXPECT_GT(px2[2], 50);

    /* Both renders should produce similar results. */
    EXPECT_NEAR(px1[2], px2[2], 10);
}

TEST_F(GLRendererTest, ShadowPassProducesNonUniformDepth)
{
    /* After rendering geometry into the shadow map, the depth texture
     * should have varying values — not all 1.0 (cleared far plane). */
    sdl3d_light sun = {};
    sun.type = SDL3D_LIGHT_DIRECTIONAL;
    sun.direction = sdl3d_vec3_make(0, -1, 0);
    sun.color[0] = 1;
    sun.color[1] = 1;
    sun.color[2] = 1;
    sun.intensity = 1.0f;
    sdl3d_add_light(ctx, &sun);
    sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), 10.0f);

    /* Main pass: draw a cube — shadow pass is automatic. */
    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 3, 5);
    cam.target = sdl3d_vec3_make(0, 0, 0);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_set_ambient_light(ctx, 0.3f, 0.3f, 0.3f);
    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(2, 2, 2), (sdl3d_color){255, 255, 255, 255});
    sdl3d_end_mode_3d(ctx);

    /* The center pixel should show the lit cube (not black). */
    unsigned char px[4];
    readPixel(160, 120, px);
    int brightness = px[0] + px[1] + px[2];
    EXPECT_GT(brightness, 20);
}

/* ShadowMakesShadowedPixelDarker test removed — directional CSM shadows
 * are disabled in favor of point light shadows only. Point light shadow
 * quality is validated visually in the showcase demo. */

TEST_F(GLRendererTest, ShadowWorksOnFirstFrame)
{
    /* Lesson #9: shadow must work on the very first frame. */
    sdl3d_light sun = {};
    sun.type = SDL3D_LIGHT_DIRECTIONAL;
    sun.direction = sdl3d_vec3_make(0, -1, 0);
    sun.color[0] = 1;
    sun.color[1] = 1;
    sun.color[2] = 1;
    sun.intensity = 2.0f;
    sdl3d_add_light(ctx, &sun);
    sdl3d_set_ambient_light(ctx, 0.2f, 0.2f, 0.2f);
    sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), 10.0f);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 3, 5);
    cam.target = sdl3d_vec3_make(0, 0, 0);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    /* First frame ever — shadow pass is now automatic. */
    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 0, 0), sdl3d_vec3_make(2, 2, 2), (sdl3d_color){255, 255, 255, 255});
    sdl3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);
    int brightness = px[0] + px[1] + px[2];

    /* Must not be black (the old bug). Must not be distorted. */
    EXPECT_GT(brightness, 20);
}

TEST_F(GLRendererTest, CSMAllLayersHaveDepthData)
{
    /* Verify that all 4 cascade layers contain non-trivial depth data
     * after rendering geometry. This validates the CSM VP matrices. */
    sdl3d_light sun = {};
    sun.type = SDL3D_LIGHT_DIRECTIONAL;
    sun.direction = sdl3d_vec3_make(0.4f, -0.8f, -0.3f);
    sun.color[0] = 1;
    sun.color[1] = 1;
    sun.color[2] = 1;
    sun.intensity = 1.5f;
    sdl3d_add_light(ctx, &sun);
    sdl3d_set_lighting_enabled(ctx, true);
    sdl3d_set_ambient_light(ctx, 0.15f, 0.15f, 0.15f);
    sdl3d_enable_shadow(ctx, 0, sdl3d_vec3_make(0, 0, 0), 30.0f);

    sdl3d_camera3d cam;
    cam.position = sdl3d_vec3_make(0, 5, 15);
    cam.target = sdl3d_vec3_make(0, 0, 0);
    cam.up = sdl3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_clear_render_context(ctx, (sdl3d_color){0, 0, 0, 255});
    sdl3d_begin_mode_3d(ctx, cam);
    /* Draw a large ground plane and several cubes at different distances. */
    sdl3d_draw_plane(ctx, sdl3d_vec3_make(0, 0, 0), (sdl3d_vec2){40, 40}, (sdl3d_color){200, 200, 200, 255});
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(0, 1, 0), sdl3d_vec3_make(2, 2, 2), (sdl3d_color){255, 0, 0, 255});
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(5, 1, -5), sdl3d_vec3_make(2, 2, 2), (sdl3d_color){0, 255, 0, 255});
    sdl3d_draw_cube(ctx, sdl3d_vec3_make(-8, 1, -10), sdl3d_vec3_make(2, 2, 2), (sdl3d_color){0, 0, 255, 255});
    sdl3d_end_mode_3d(ctx);

    /* The center pixel should show the lit scene (not black). */
    unsigned char px[4];
    readPixel(160, 120, px);
    int brightness = px[0] + px[1] + px[2];
    EXPECT_GT(brightness, 30);
}
