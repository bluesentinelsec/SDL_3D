/*
 * GL renderer tests — validates that the OpenGL backend produces
 * correct pixel output for basic rendering operations.
 *
 * Each test creates a hidden GL window, renders a known scene, reads
 * back pixels, and asserts expected values. No visual inspection needed.
 */

#include <gtest/gtest.h>

extern "C" {
#include "sdl3d/sdl3d.h"
#include "render_context_internal.h"
#include "gl_renderer.h"
}

class GLRendererTest : public ::testing::Test {
protected:
    SDL_Window *win = nullptr;
    sdl3d_render_context *ctx = nullptr;

    void SetUp() override {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        win = SDL_CreateWindow("GL test", 320, 240, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!win) {
            GTEST_SKIP() << "Cannot create GL window: " << SDL_GetError();
        }

        sdl3d_render_context_config cfg;
        sdl3d_init_render_context_config(&cfg);
        cfg.backend = SDL3D_BACKEND_SDLGPU;
        cfg.logical_width = 320;
        cfg.logical_height = 240;

        if (!sdl3d_create_render_context(win, nullptr, &cfg, &ctx)) {
            SDL_DestroyWindow(win);
            win = nullptr;
            GTEST_SKIP() << "Cannot create GL context: " << SDL_GetError();
        }
    }

    void TearDown() override {
        if (ctx) sdl3d_destroy_render_context(ctx);
        if (win) SDL_DestroyWindow(win);
    }

    void readPixel(int x, int y, unsigned char *rgba) {
        sdl3d_gl_read_pixel(ctx->gl, x, y, rgba);
    }
};

TEST_F(GLRendererTest, ClearProducesExpectedColor) {
    sdl3d_color red = {255, 0, 0, 255};
    sdl3d_clear_render_context(ctx, red);

    unsigned char px[4];
    readPixel(160, 120, px);

    EXPECT_GE(px[0], 200) << "Red channel should be high after red clear";
    EXPECT_LE(px[1], 30)  << "Green channel should be low after red clear";
    EXPECT_LE(px[2], 30)  << "Blue channel should be low after red clear";
}

TEST_F(GLRendererTest, LitCubeProducesNonClearPixels) {
    /* Set up a light so the cube is visible. */
    sdl3d_light sun = {};
    sun.type = SDL3D_LIGHT_DIRECTIONAL;
    sun.direction = sdl3d_vec3_make(0, -1, -1);
    sun.color[0] = 1; sun.color[1] = 1; sun.color[2] = 1;
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
    EXPECT_GT(brightness, 30) << "Center pixel should show lit cube, not clear color. "
                              << "Got RGBA=(" << (int)px[0] << "," << (int)px[1] << ","
                              << (int)px[2] << "," << (int)px[3] << ")";
}

TEST_F(GLRendererTest, CubeVisibleOnFirstFrame) {
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

    EXPECT_GT(px[1], 50) << "Green channel should be visible on first frame. "
                         << "Got RGBA=(" << (int)px[0] << "," << (int)px[1] << ","
                         << (int)px[2] << "," << (int)px[3] << ")";
}

TEST_F(GLRendererTest, BackfaceCullingShowsFrontFaces) {
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
    EXPECT_GT(px[0], 50) << "Front face should be visible with backface culling. "
                         << "Got RGBA=(" << (int)px[0] << "," << (int)px[1] << ","
                         << (int)px[2] << "," << (int)px[3] << ")";
}

TEST_F(GLRendererTest, ToggleRecreateProducesCorrectOutput) {
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

    EXPECT_GT(px2[2], 50) << "Blue should be visible after context recreation. "
                          << "Got RGBA=(" << (int)px2[0] << "," << (int)px2[1] << ","
                          << (int)px2[2] << "," << (int)px2[3] << ")";

    /* Both renders should produce similar results. */
    EXPECT_NEAR(px1[2], px2[2], 10) << "Output should match before and after recreation.";
}
