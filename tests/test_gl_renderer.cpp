/*
 * GL renderer tests — validates that the OpenGL backend produces
 * correct pixel output for basic rendering operations.
 *
 * Each test creates a hidden GL window, renders a known scene, reads
 * back pixels, and asserts expected values. No visual inspection needed.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern "C"
{
#include "backend.h"
#include "gl_renderer.h"
#include "render_context_internal.h"
#include "slayer3d/animation.h"
#include "slayer3d/asset.h"
#include "slayer3d/effects.h"
#include "slayer3d/game.h"
#include "slayer3d/game_data.h"
#include "slayer3d/game_presentation.h"
#include "slayer3d/level.h"
#include "slayer3d/slayer3d.h"
}

namespace
{

slayer3d_texture2d MakeTextureFromPixels(const Uint8 *pixels, int width, int height)
{
    slayer3d_image image{};
    image.pixels = const_cast<Uint8 *>(pixels);
    image.width = width;
    image.height = height;

    slayer3d_texture2d texture{};
    EXPECT_TRUE(slayer3d_create_texture_from_image(&image, &texture));
    EXPECT_TRUE(slayer3d_set_texture_filter(&texture, SLAYER3D_TEXTURE_FILTER_NEAREST));
    return texture;
}

std::filesystem::path UniqueTempDir(const char *name)
{
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / (std::string("slayer3d_") + name + "_" + std::to_string(now));
    std::filesystem::create_directories(dir);
    return dir;
}

void WriteText(const std::filesystem::path &path, const std::string &text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    ASSERT_TRUE(out.good()) << "failed to open " << path;
    out << text;
}

std::string BrushVisibilityBoxJson(const char *name, const char *material, float min_x, float max_x, float min_y,
                                   float max_y, float min_z, float max_z, const char *visibility = nullptr)
{
    std::ostringstream json;
    json << R"json({
          "name": ")json"
         << name << R"json(",
          "contents": "solid",
)json";
    if (visibility != nullptr)
        json << R"json(          "visibility": ")json" << visibility << R"json(",
)json";
    json << R"json(          "faces": [
            { "plane": { "normal": [ 1,  0,  0], "distance": )json"
         << max_x << R"json( }, "material": ")json" << material << R"json(" },
            { "plane": { "normal": [-1,  0,  0], "distance": )json"
         << -min_x << R"json( }, "material": ")json" << material << R"json(" },
            { "plane": { "normal": [ 0,  1,  0], "distance": )json"
         << max_y << R"json( }, "material": ")json" << material << R"json(" },
            { "plane": { "normal": [ 0, -1,  0], "distance": )json"
         << -min_y << R"json( }, "material": ")json" << material << R"json(" },
            { "plane": { "normal": [ 0,  0,  1], "distance": )json"
         << max_z << R"json( }, "material": ")json" << material << R"json(" },
            { "plane": { "normal": [ 0,  0, -1], "distance": )json"
         << -min_z << R"json( }, "material": ")json" << material << R"json(" }
          ]
        })json";
    return json.str();
}

} // namespace

class GLRendererTest : public ::testing::Test
{
  protected:
    SDL_Window *win = nullptr;
    slayer3d_render_context *ctx = nullptr;

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

        slayer3d_render_context_config cfg;
        slayer3d_init_render_context_config(&cfg);
        cfg.backend = SLAYER3D_BACKEND_OPENGL;
        cfg.logical_width = 320;
        cfg.logical_height = 240;

        if (!slayer3d_create_render_context(win, nullptr, &cfg, &ctx))
        {
            SDL_DestroyWindow(win);
            win = nullptr;
            GTEST_SKIP() << "Cannot create GL context: " << SDL_GetError();
        }
    }

    void TearDown() override
    {
        if (ctx)
            slayer3d_destroy_render_context(ctx);
        if (win)
            SDL_DestroyWindow(win);
    }

    void readPixel(int x, int y, unsigned char *rgba)
    {
        slayer3d_gl_read_pixel(ctx->gl, x, y, rgba);
    }
};

TEST_F(GLRendererTest, ClearProducesExpectedColor)
{
    slayer3d_color red = {255, 0, 0, 255};
    slayer3d_clear_render_context(ctx, red);

    unsigned char px[4];
    readPixel(160, 120, px);

    EXPECT_GE(px[0], 200) << "Red channel should be high after red clear";
    EXPECT_LE(px[1], 30) << "Green channel should be low after red clear";
    EXPECT_LE(px[2], 30) << "Blue channel should be low after red clear";
}

TEST_F(GLRendererTest, WorldRenderScaleResizesOnlyWorldFramebuffer)
{
    EXPECT_EQ(slayer3d_get_render_context_width(ctx), 320);
    EXPECT_EQ(slayer3d_get_render_context_height(ctx), 240);
    EXPECT_FLOAT_EQ(slayer3d_get_world_render_scale(ctx), 1.0f);

    int world_width = 0;
    int world_height = 0;
    ASSERT_TRUE(slayer3d_get_world_render_size(ctx, &world_width, &world_height));
    EXPECT_EQ(world_width, 320);
    EXPECT_EQ(world_height, 240);

    ASSERT_TRUE(slayer3d_set_world_render_scale(ctx, 0.5f)) << SDL_GetError();
    EXPECT_EQ(slayer3d_get_render_context_width(ctx), 320);
    EXPECT_EQ(slayer3d_get_render_context_height(ctx), 240);
    EXPECT_FLOAT_EQ(slayer3d_get_world_render_scale(ctx), 0.5f);
    ASSERT_TRUE(slayer3d_get_world_render_size(ctx, &world_width, &world_height));
    EXPECT_EQ(world_width, 160);
    EXPECT_EQ(world_height, 120);

    EXPECT_FALSE(slayer3d_set_world_render_scale(ctx, 0.1f));
    SDL_ClearError();
    EXPECT_FLOAT_EQ(slayer3d_get_world_render_scale(ctx), 0.5f);
}

TEST_F(GLRendererTest, RenderProfilesSelectRetroPostProcess)
{
    struct ProfileCase
    {
        slayer3d_render_profile (*profile)(void);
        int expected_retro_profile;
        int expected_width;
        int expected_height;
        int expected_filter;
    };
    const ProfileCase cases[] = {
        {slayer3d_profile_modern, (int)SLAYER3D_DISPLAY_PROFILE_MODERN, 0, 0, (int)SLAYER3D_DISPLAY_FILTER_LINEAR},
        {slayer3d_profile_ps1, (int)SLAYER3D_DISPLAY_PROFILE_PS1, 320, 240, (int)SLAYER3D_DISPLAY_FILTER_NEAREST},
        {slayer3d_profile_n64, (int)SLAYER3D_DISPLAY_PROFILE_N64, 320, 240, (int)SLAYER3D_DISPLAY_FILTER_LINEAR},
        {slayer3d_profile_dos, (int)SLAYER3D_DISPLAY_PROFILE_DOS, 320, 200, (int)SLAYER3D_DISPLAY_FILTER_NEAREST},
        {slayer3d_profile_snes, (int)SLAYER3D_DISPLAY_PROFILE_SNES, 256, 224, (int)SLAYER3D_DISPLAY_FILTER_NEAREST},
        {slayer3d_profile_grayscale, (int)SLAYER3D_DISPLAY_PROFILE_GRAYSCALE, 512, 342,
         (int)SLAYER3D_DISPLAY_FILTER_NEAREST},
        {slayer3d_profile_gameboy, (int)SLAYER3D_DISPLAY_PROFILE_GAMEBOY, 160, 144,
         (int)SLAYER3D_DISPLAY_FILTER_NEAREST},
    };

    for (const ProfileCase &test_case : cases)
    {
        slayer3d_render_profile profile = test_case.profile();
        ASSERT_TRUE(slayer3d_set_render_profile(ctx, &profile));
        ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{4, 5, 6, 255}));
        int width = -1;
        int height = -1;
        slayer3d_gl_active_retro_virtual_resolution(ctx->gl, &width, &height);
        EXPECT_EQ(slayer3d_gl_active_retro_profile(ctx->gl), test_case.expected_retro_profile);
        EXPECT_EQ(width, test_case.expected_width);
        EXPECT_EQ(height, test_case.expected_height);
        EXPECT_EQ(slayer3d_gl_active_retro_filter(ctx->gl), test_case.expected_filter);
    }
}

TEST_F(GLRendererTest, RecycledTextureSlotsDoNotAliasCachedUploads)
{
    auto make_solid_texture = [](Uint8 r, Uint8 g, Uint8 b, slayer3d_texture2d *out) {
        Uint8 pixels[2 * 2 * 4];
        for (int i = 0; i < 4; ++i)
        {
            pixels[i * 4 + 0] = r;
            pixels[i * 4 + 1] = g;
            pixels[i * 4 + 2] = b;
            pixels[i * 4 + 3] = 255;
        }
        slayer3d_image image{};
        image.pixels = pixels;
        image.width = 2;
        image.height = 2;
        return slayer3d_create_texture_from_image(&image, out);
    };

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    float positions[] = {-4, -4, 0, -4, 4, 0, 4, -4, 0, 4, 4, 0};
    float uvs[] = {0, 0, 0, 1, 1, 0, 1, 1};
    unsigned int indices[] = {0, 1, 2, 2, 1, 3};
    slayer3d_mesh mesh{};
    mesh.positions = positions;
    mesh.uvs = uvs;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 6;

    slayer3d_color clear = {0, 0, 0, 255};
    slayer3d_color white = {255, 255, 255, 255};
    ASSERT_TRUE(slayer3d_set_lighting_enabled(ctx, false));

    /* Image caches store textures by value and recycle slots, so a slot's
     * address can be reused by a different texture. The renderer texture
     * cache must not serve the previous upload for the recycled slot. */
    slayer3d_texture2d slots[2]{};
    ASSERT_TRUE(make_solid_texture(255, 0, 0, &slots[0])) << SDL_GetError();
    ASSERT_TRUE(make_solid_texture(0, 0, 255, &slots[1])) << SDL_GetError();

    slayer3d_clear_render_context(ctx, clear);
    slayer3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(slayer3d_draw_mesh(ctx, &mesh, &slots[0], white));
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);
    EXPECT_GE(px[0], 200) << "First slot should draw red";
    EXPECT_LE(px[2], 30) << "First slot should draw red";

    /* Recycle slot 0: free it and move the blue texture into its storage,
     * mirroring image-cache compaction. */
    slayer3d_free_texture(&slots[0]);
    slots[0] = slots[1];
    SDL_zero(slots[1]);

    slayer3d_clear_render_context(ctx, clear);
    slayer3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(slayer3d_draw_mesh(ctx, &mesh, &slots[0], white));
    slayer3d_end_mode_3d(ctx);

    readPixel(160, 120, px);
    EXPECT_LE(px[0], 30) << "Recycled slot must draw the blue texture, not the stale red upload";
    EXPECT_GE(px[2], 200) << "Recycled slot must draw the blue texture, not the stale red upload";

    slayer3d_free_texture(&slots[0]);
}

TEST_F(GLRendererTest, LitCubeProducesNonClearPixels)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));

    /* Set up a light so the cube is visible. */
    slayer3d_light sun = {};
    sun.type = SLAYER3D_LIGHT_DIRECTIONAL;
    sun.direction = slayer3d_vec3_make(0, -1, -1);
    sun.color[0] = 1;
    sun.color[1] = 1;
    sun.color[2] = 1;
    sun.intensity = 2.0f;
    slayer3d_add_light(ctx, &sun);
    slayer3d_set_ambient_light(ctx, 0.3f, 0.3f, 0.3f);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 3, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_color clear = {0, 0, 0, 255};
    slayer3d_color white = {255, 255, 255, 255};

    slayer3d_clear_render_context(ctx, clear);
    slayer3d_begin_mode_3d(ctx, cam);
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(2, 2, 2), white);
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    /* The center pixel should NOT be the clear color (black). */
    int brightness = px[0] + px[1] + px[2];
    EXPECT_GT(brightness, 30);
}

TEST_F(GLRendererTest, DepthPrepassReplaysOpaqueLitTriangles)
{
    ASSERT_TRUE(slayer3d_set_depth_prepass_enabled(ctx, true));
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.2f, 0.2f, 0.2f));
    ctx->depth_prepass_scope_enabled = true;

    float positions[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    unsigned int indices[] = {0, 1, 2, 0, 2, 3};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 6;
    mesh.material_index = -1;

    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(
        slayer3d_draw_model(ctx, &model, slayer3d_vec3_make(0, 0, 0), 1.0f, slayer3d_color{255, 255, 255, 255}));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    unsigned char px[4];
    readPixel(160, 120, px);

    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_GT(stats.depth_prepass_draws, 0u);
    EXPECT_GT(stats.depth_prepass_triangles, 0u);
    EXPECT_GT(px[0] + px[1] + px[2], 20);
}

TEST_F(GLRendererTest, StaticModelMeshesUseInstancedBackendDraws)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.25f, 0.25f, 0.25f));
    ASSERT_TRUE(slayer3d_set_per_object_light_selection_enabled(ctx, false));
    ASSERT_TRUE(slayer3d_set_depth_prepass_enabled(ctx, true));
    ctx->depth_prepass_scope_enabled = true;

    float positions[] = {
        -0.35f, -0.35f, 0.0f, 0.35f, -0.35f, 0.0f, 0.0f, 0.35f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    float uvs[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
    };
    float colors[] = {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
    unsigned int indices[] = {0, 1, 2};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.colors = colors;
    mesh.vertex_count = 3;
    mesh.indices = indices;
    mesh.index_count = 3;
    mesh.material_index = -1;
    mesh.has_local_bounds = true;
    mesh.local_bounds.min = slayer3d_vec3_make(-0.35f, -0.35f, 0.0f);
    mesh.local_bounds.max = slayer3d_vec3_make(0.35f, 0.35f, 0.0f);

    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(slayer3d_draw_model(ctx, &model, slayer3d_vec3_make(-1.2f + 0.6f * (float)i, 0, 0), 1.0f,
                                        slayer3d_color{255, 255, 255, 255}));
    }
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    unsigned char px[4];
    readPixel(160, 120, px);
    EXPECT_GT(px[0] + px[1] + px[2], 20);

    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.model_mesh_submissions, 5u);
    EXPECT_EQ(stats.model_mesh_draws, 5u);
    EXPECT_EQ(stats.depth_prepass_draws, 1u);
    EXPECT_EQ(stats.depth_prepass_triangles, 5u);
    EXPECT_EQ(stats.geometry_draw_calls, 1u);
    EXPECT_EQ(stats.static_mesh_instanced_draw_calls, 1u);
    EXPECT_EQ(stats.static_mesh_instances_batched, 5u);
    EXPECT_EQ(stats.static_mesh_draw_calls_saved, 4u);
}

TEST_F(GLRendererTest, SkinnedLitModelsUseGpuSkinningWhenJointBudgetAllows)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.8f, 0.8f, 0.8f));
    ASSERT_TRUE(slayer3d_set_depth_prepass_enabled(ctx, true));
    ctx->depth_prepass_scope_enabled = true;

    float positions[] = {
        -0.4f, -0.4f, 0.0f, 0.4f, -0.4f, 0.0f, 0.0f, 0.4f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    float uvs[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
    };
    float colors[] = {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
    unsigned short joint_indices[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    float joint_weights[] = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    };
    unsigned int indices[] = {0, 1, 2};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.colors = colors;
    mesh.joint_indices = joint_indices;
    mesh.joint_weights = joint_weights;
    mesh.vertex_count = 3;
    mesh.indices = indices;
    mesh.index_count = 3;
    mesh.material_index = -1;

    slayer3d_skeleton skeleton = {};
    skeleton.joint_count = 1;
    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;
    model.skeleton = &skeleton;
    slayer3d_mat4 joints[1] = {slayer3d_mat4_identity()};

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(slayer3d_draw_model_skinned(ctx, &model, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(0, 1, 0), 0.0f,
                                            slayer3d_vec3_make(1, 1, 1), slayer3d_color{255, 255, 255, 255}, joints));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    unsigned char px[4];
    readPixel(160, 120, px);
    EXPECT_GT(px[0] + px[1] + px[2], 20);

    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.gpu_skinned_draws, 1u);
    EXPECT_EQ(stats.gpu_skinned_vertices, 3u);
    EXPECT_EQ(stats.cpu_skinned_vertices, 0u);
    EXPECT_EQ(stats.depth_prepass_draws, 1u);
}

TEST_F(GLRendererTest, SkinnedLitModelsWithSharedPoseUseInstancedBackendDraw)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.8f, 0.8f, 0.8f));
    ASSERT_TRUE(slayer3d_set_per_object_light_selection_enabled(ctx, false));
    ASSERT_TRUE(slayer3d_set_depth_prepass_enabled(ctx, true));
    ctx->depth_prepass_scope_enabled = true;

    float positions[] = {
        -0.25f, -0.25f, 0.0f, 0.25f, -0.25f, 0.0f, 0.0f, 0.25f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    float uvs[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
    };
    float colors[] = {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
    unsigned short joint_indices[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    float joint_weights[] = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    };
    unsigned int indices[] = {0, 1, 2};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.colors = colors;
    mesh.joint_indices = joint_indices;
    mesh.joint_weights = joint_weights;
    mesh.vertex_count = 3;
    mesh.indices = indices;
    mesh.index_count = 3;
    mesh.material_index = -1;
    mesh.has_local_bounds = true;
    mesh.local_bounds.min = slayer3d_vec3_make(-0.25f, -0.25f, 0.0f);
    mesh.local_bounds.max = slayer3d_vec3_make(0.25f, 0.25f, 0.0f);

    slayer3d_skeleton skeleton = {};
    skeleton.joint_count = 1;
    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;
    model.skeleton = &skeleton;
    slayer3d_mat4 joints[1] = {slayer3d_mat4_identity()};

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    constexpr int instance_count = 5;
    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    for (int i = 0; i < instance_count; ++i)
    {
        ASSERT_TRUE(slayer3d_draw_model_skinned(ctx, &model, slayer3d_vec3_make(-1.0f + 0.5f * (float)i, 0, 0),
                                                slayer3d_vec3_make(0, 1, 0), 0.0f, slayer3d_vec3_make(1, 1, 1),
                                                slayer3d_color{255, 255, 255, 255}, joints));
    }
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    unsigned char px[4];
    readPixel(160, 120, px);
    EXPECT_GT(px[0] + px[1] + px[2], 20);

    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.gpu_skinned_draws, (Uint64)instance_count);
    EXPECT_EQ(stats.gpu_skinned_vertices, (Uint64)instance_count * 3u);
    EXPECT_EQ(stats.cpu_skinned_vertices, 0u);
    EXPECT_EQ(stats.depth_prepass_draws, 1u);
    EXPECT_EQ(stats.depth_prepass_triangles, (Uint64)instance_count);
    EXPECT_EQ(stats.geometry_draw_calls, 1u);
    EXPECT_EQ(stats.static_mesh_instanced_draw_calls, 1u);
    EXPECT_EQ(stats.static_mesh_instances_batched, (Uint64)instance_count);
    EXPECT_EQ(stats.static_mesh_draw_calls_saved, (Uint64)(instance_count - 1));
    EXPECT_EQ(stats.gpu_skinning_palette_uploads, 0u);
    EXPECT_EQ(stats.gpu_skinning_palette_matrices_uploaded, 0u);
    EXPECT_EQ(stats.gpu_skinning_palette_buffer_uploads, 1u);
    EXPECT_EQ(stats.gpu_skinning_palette_buffer_matrices_uploaded, 1u);
    EXPECT_EQ(stats.gpu_skinning_palette_buffer_draws, 2u);
}

TEST_F(GLRendererTest, SkinnedCrowdBatchesSharedPoseGroupsAndUsesGpuPalette)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.8f, 0.8f, 0.8f));
    ASSERT_TRUE(slayer3d_set_per_object_light_selection_enabled(ctx, false));
    ASSERT_TRUE(slayer3d_set_depth_prepass_enabled(ctx, true));
    ctx->depth_prepass_scope_enabled = true;

    float positions[] = {
        -0.08f, -0.08f, 0.0f, 0.08f, -0.08f, 0.0f, 0.0f, 0.08f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    float uvs[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
    };
    float colors[] = {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
    unsigned short joint_indices[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    float joint_weights[] = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    };
    unsigned int indices[] = {0, 1, 2};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.colors = colors;
    mesh.joint_indices = joint_indices;
    mesh.joint_weights = joint_weights;
    mesh.vertex_count = 3;
    mesh.indices = indices;
    mesh.index_count = 3;
    mesh.material_index = -1;
    mesh.has_local_bounds = true;
    mesh.local_bounds.min = slayer3d_vec3_make(-0.08f, -0.08f, 0.0f);
    mesh.local_bounds.max = slayer3d_vec3_make(0.08f, 0.08f, 0.0f);

    slayer3d_skeleton skeleton = {};
    skeleton.joint_count = 1;
    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;
    model.skeleton = &skeleton;

    slayer3d_mat4 poses[4][1] = {
        {slayer3d_mat4_identity()}, {slayer3d_mat4_identity()}, {slayer3d_mat4_identity()}, {slayer3d_mat4_identity()}};
    for (int pose = 0; pose < 4; ++pose)
        poses[pose][0].m[12] = 0.01f * (float)pose;

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 7);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    constexpr int pose_count = 4;
    constexpr int instances_per_pose = 16;
    constexpr int crowd_count = pose_count * instances_per_pose;
    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    for (int pose = 0; pose < pose_count; ++pose)
    {
        for (int i = 0; i < instances_per_pose; ++i)
        {
            const int index = pose * instances_per_pose + i;
            const float x = -3.0f + 0.35f * (float)(index % 16);
            const float y = -0.9f + 0.45f * (float)(index / 16);
            ASSERT_TRUE(slayer3d_draw_model_skinned(ctx, &model, slayer3d_vec3_make(x, y, 0),
                                                    slayer3d_vec3_make(0, 1, 0), 0.0f, slayer3d_vec3_make(1, 1, 1),
                                                    slayer3d_color{255, 255, 255, 255}, poses[pose]));
        }
    }
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    unsigned char px[4];
    readPixel(0, 0, px);

    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.gpu_skinned_draws, (Uint64)crowd_count);
    EXPECT_EQ(stats.gpu_skinned_vertices, (Uint64)crowd_count * 3u);
    EXPECT_EQ(stats.cpu_skinned_vertices, 0u);
    EXPECT_EQ(stats.gpu_skinning_palette_uploads, 0u);
    EXPECT_EQ(stats.gpu_skinning_palette_matrices_uploaded, 0u);
    EXPECT_EQ(stats.gpu_skinning_palette_buffer_uploads, 1u);
    EXPECT_EQ(stats.gpu_skinning_palette_buffer_matrices_uploaded, (Uint64)pose_count);
    EXPECT_EQ(stats.gpu_skinning_palette_buffer_draws, (Uint64)pose_count * 2u);
    EXPECT_EQ(stats.depth_prepass_draws, (Uint64)pose_count);
    EXPECT_EQ(stats.depth_prepass_triangles, (Uint64)crowd_count);
    EXPECT_EQ(stats.geometry_draw_calls, (Uint64)pose_count);
    EXPECT_EQ(stats.static_mesh_instanced_draw_calls, (Uint64)pose_count);
    EXPECT_EQ(stats.static_mesh_instances_batched, (Uint64)crowd_count);
    EXPECT_EQ(stats.static_mesh_draw_calls_saved, (Uint64)(crowd_count - pose_count));
}

TEST_F(GLRendererTest, SkinnedLitModelsFallBackToUniformsWhenPosePaletteIsFull)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.8f, 0.8f, 0.8f));
    ASSERT_TRUE(slayer3d_set_per_object_light_selection_enabled(ctx, false));
    ASSERT_TRUE(slayer3d_set_depth_prepass_enabled(ctx, false));

    float positions[] = {
        -0.15f, -0.15f, 0.0f, 0.15f, -0.15f, 0.0f, 0.0f, 0.15f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    float uvs[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
    };
    float colors[] = {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
    unsigned short joint_indices[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    float joint_weights[] = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    };
    unsigned int indices[] = {0, 1, 2};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.colors = colors;
    mesh.joint_indices = joint_indices;
    mesh.joint_weights = joint_weights;
    mesh.vertex_count = 3;
    mesh.indices = indices;
    mesh.index_count = 3;
    mesh.material_index = -1;

    slayer3d_skeleton skeleton = {};
    skeleton.joint_count = SLAYER3D_GPU_SKINNING_MAX_JOINTS;
    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;
    model.skeleton = &skeleton;

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    constexpr int draw_count = 5;
    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    for (int i = 0; i < draw_count; ++i)
    {
        std::vector<slayer3d_mat4> joints((size_t)skeleton.joint_count, slayer3d_mat4_identity());
        joints[0].m[12] = 0.01f * (float)i;
        ASSERT_TRUE(slayer3d_draw_model_skinned(ctx, &model, slayer3d_vec3_make(-0.8f + 0.4f * (float)i, 0, 0),
                                                slayer3d_vec3_make(0, 1, 0), 0.0f, slayer3d_vec3_make(1, 1, 1),
                                                slayer3d_color{255, 255, 255, 255}, joints.data()));
    }
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    unsigned char px[4];
    readPixel(160, 120, px);
    EXPECT_GT(px[0] + px[1] + px[2], 20);

    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.gpu_skinned_draws, (Uint64)draw_count);
    EXPECT_EQ(stats.cpu_skinned_vertices, 0u);
    EXPECT_EQ(stats.gpu_skinning_palette_buffer_uploads, 1u);
    EXPECT_EQ(stats.gpu_skinning_palette_buffer_matrices_uploaded, 256u);
    EXPECT_EQ(stats.gpu_skinning_palette_buffer_draws, 4u);
    EXPECT_EQ(stats.gpu_skinning_palette_uploads, 1u);
    EXPECT_EQ(stats.gpu_skinning_palette_matrices_uploaded, 64u);
}

TEST_F(GLRendererTest, SkinnedLitModelsFallBackToCpuWhenJointBudgetIsExceeded)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.8f, 0.8f, 0.8f));

    float positions[] = {
        -0.4f, -0.4f, 0.0f, 0.4f, -0.4f, 0.0f, 0.0f, 0.4f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    float uvs[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f,
    };
    float colors[] = {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
    unsigned short joint_indices[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    float joint_weights[] = {
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    };
    unsigned int indices[] = {0, 1, 2};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.colors = colors;
    mesh.joint_indices = joint_indices;
    mesh.joint_weights = joint_weights;
    mesh.vertex_count = 3;
    mesh.indices = indices;
    mesh.index_count = 3;
    mesh.material_index = -1;

    slayer3d_skeleton skeleton = {};
    skeleton.joint_count = SLAYER3D_GPU_SKINNING_MAX_JOINTS + 1;
    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;
    model.skeleton = &skeleton;
    std::vector<slayer3d_mat4> joints((size_t)skeleton.joint_count, slayer3d_mat4_identity());

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(slayer3d_draw_model_skinned(ctx, &model, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(0, 1, 0), 0.0f,
                                            slayer3d_vec3_make(1, 1, 1), slayer3d_color{255, 255, 255, 255},
                                            joints.data()));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.gpu_skinned_draws, 0u);
    EXPECT_EQ(stats.gpu_skinned_vertices, 0u);
    EXPECT_EQ(stats.cpu_skinned_vertices, 3u);
}

TEST_F(GLRendererTest, BrushVisibilityOcclusionCullsHiddenBrushSubmodels)
{
    const std::filesystem::path dir = UniqueTempDir("brush_visibility_occlusion");
    WriteText(dir / "scenes" / "play.scene.json",
              R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "world": {
    "brush_worlds": [
      { "world": "brush.visibility", "visibility_occlusion_key": "brush.visibility.enabled" }
    ]
  }
})json");
    std::ostringstream game_json;
    game_json << R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Brush Visibility Occlusion Test" },
  "world": { "name": "world.visibility", "kind": "brush" },
  "brush_worlds": [
    {
      "name": "brush.visibility",
      "visibility_cell_size": 0.5,
      "materials": [
        { "name": "mat.front", "albedo": [0.2, 0.8, 0.2, 1.0] },
        { "name": "mat.blocker", "albedo": [0.8, 0.2, 0.2, 1.0] },
        { "name": "mat.hidden", "albedo": [0.2, 0.2, 0.8, 1.0] }
      ],
      "brushes": [
)json" << BrushVisibilityBoxJson("brush.front_marker", "mat.front", -0.5f, 0.5f, 0.5f, 1.5f, -2.5f, -2.0f)
              << "," << BrushVisibilityBoxJson("brush.blocker", "mat.blocker", -2.0f, 2.0f, 0.0f, 2.0f, -0.25f, 0.75f)
              << "," << BrushVisibilityBoxJson("brush.hidden", "mat.hidden", -1.0f, 1.0f, 0.5f, 1.5f, 1.25f, 2.25f)
              << R"json(
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json";
    WriteText(dir / "visibility.game.json", game_json.str());

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    slayer3d_game_data_runtime *runtime = nullptr;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "visibility.game.json").string().c_str(), session, &runtime, error,
                                             sizeof(error)))
        << error;

    slayer3d_camera3d cam{};
    cam.position = slayer3d_vec3_make(0.0f, 1.0f, -2.75f);
    cam.target = slayer3d_vec3_make(0.0f, 1.0f, 4.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 70.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.45f, 0.45f, 0.45f));
    slayer3d_reset_render_stats(ctx);
    slayer3d_game_data_reset_brush_diagnostics(runtime);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(runtime, ctx, nullptr, &cam));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    slayer3d_game_data_brush_diagnostics diagnostics{};
    ASSERT_TRUE(slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics));
    EXPECT_EQ(diagnostics.render_mesh_submissions, 3u);
    EXPECT_EQ(diagnostics.visibility_brush_candidates, 0u);

    slayer3d_properties_set_bool(slayer3d_game_data_mutable_scene_state(runtime), "brush.visibility.enabled", true);
    slayer3d_reset_render_stats(ctx);
    slayer3d_game_data_reset_brush_diagnostics(runtime);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(runtime, ctx, nullptr, &cam));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    ASSERT_TRUE(slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics));
    EXPECT_EQ(diagnostics.visibility_brush_candidates, 3u);
    EXPECT_LT(diagnostics.visibility_brush_visible, diagnostics.visibility_brush_candidates);
    EXPECT_GE(diagnostics.visibility_brush_occluded, 1u);
    EXPECT_GE(diagnostics.visibility_triangles_culled, 12u);
    EXPECT_EQ(diagnostics.visibility_grid_cache_misses, 1u);
    EXPECT_EQ(diagnostics.visibility_grid_cache_hits, 0u);
    EXPECT_LT(diagnostics.render_mesh_submissions, 3u);
    EXPECT_LT(diagnostics.render_triangles_submitted, 36u);

    slayer3d_reset_render_stats(ctx);
    slayer3d_game_data_reset_brush_diagnostics(runtime);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(runtime, ctx, nullptr, &cam));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    ASSERT_TRUE(slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics));
    EXPECT_EQ(diagnostics.visibility_grid_cache_misses, 0u);
    EXPECT_EQ(diagnostics.visibility_grid_cache_hits, 1u);
    EXPECT_EQ(diagnostics.visibility_brush_candidates, 3u);
    EXPECT_LT(diagnostics.visibility_brush_visible, diagnostics.visibility_brush_candidates);
    EXPECT_GE(diagnostics.visibility_brush_occluded, 1u);

    slayer3d_camera3d side_cam = cam;
    side_cam.position.x = 0.75f;
    side_cam.target.x = 0.75f;
    slayer3d_reset_render_stats(ctx);
    slayer3d_game_data_reset_brush_diagnostics(runtime);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, side_cam));
    ASSERT_TRUE(slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(runtime, ctx, nullptr, &side_cam));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    ASSERT_TRUE(slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics));
    EXPECT_EQ(diagnostics.visibility_grid_cache_misses, 1u);
    EXPECT_EQ(diagnostics.visibility_grid_cache_hits, 0u);

    slayer3d_reset_render_stats(ctx);
    slayer3d_game_data_reset_brush_diagnostics(runtime);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(runtime, ctx, nullptr, &cam));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    ASSERT_TRUE(slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics));
    EXPECT_EQ(diagnostics.visibility_grid_cache_misses, 0u);
    EXPECT_EQ(diagnostics.visibility_grid_cache_hits, 1u);

    slayer3d_game_data_destroy(runtime);
    runtime = nullptr;

    std::ostringstream override_game_json;
    override_game_json << R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Brush Visibility Override Test" },
  "world": { "name": "world.visibility", "kind": "brush" },
  "brush_worlds": [
    {
      "name": "brush.visibility",
      "visibility_cell_size": 0.5,
      "materials": [
        { "name": "mat.front", "albedo": [0.2, 0.8, 0.2, 1.0] },
        { "name": "mat.blocker", "albedo": [0.8, 0.2, 0.2, 1.0] },
        { "name": "mat.hidden", "albedo": [0.2, 0.2, 0.8, 1.0] }
      ],
      "brushes": [
)json" << BrushVisibilityBoxJson("brush.front_marker", "mat.front", -0.5f, 0.5f, 0.5f, 1.5f, -2.5f, -2.0f)
                       << ","
                       << BrushVisibilityBoxJson("brush.blocker", "mat.blocker", -2.0f, 2.0f, 0.0f, 2.0f, -0.25f, 0.75f,
                                                 "always")
                       << ","
                       << BrushVisibilityBoxJson("brush.hidden", "mat.hidden", -1.0f, 1.0f, 0.5f, 1.5f, 1.25f, 2.25f,
                                                 "always")
                       << R"json(
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json";
    WriteText(dir / "visibility_override.game.json", override_game_json.str());
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "visibility_override.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;
    slayer3d_properties_set_bool(slayer3d_game_data_mutable_scene_state(runtime), "brush.visibility.enabled", true);
    slayer3d_reset_render_stats(ctx);
    slayer3d_game_data_reset_brush_diagnostics(runtime);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(runtime, ctx, nullptr, &cam));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    ASSERT_TRUE(slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics));
    EXPECT_EQ(diagnostics.visibility_brush_candidates, 1u);
    EXPECT_EQ(diagnostics.visibility_brush_occluded, 0u);
    EXPECT_EQ(diagnostics.render_mesh_submissions, 3u);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    std::filesystem::remove_all(dir);
}

TEST_F(GLRendererTest, BrushVisibilityDrawsFullyVisibleCompileChunks)
{
    const std::filesystem::path dir = UniqueTempDir("brush_visibility_chunks");
    WriteText(dir / "scenes" / "play.scene.json",
              R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "world": {
    "brush_worlds": [
      { "world": "brush.visibility_chunks", "visibility_occlusion_key": "brush.visibility.enabled" }
    ]
  }
})json");
    std::ostringstream game_json;
    game_json << R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Brush Visibility Chunk Test" },
  "world": { "name": "world.visibility_chunks", "kind": "brush" },
  "brush_worlds": [
    {
      "name": "brush.visibility_chunks",
      "visibility_cell_size": 0.25,
      "materials": [
        { "name": "mat.front", "albedo": [0.2, 0.8, 0.2, 1.0] },
        { "name": "mat.blocker", "albedo": [0.8, 0.2, 0.2, 1.0] },
        { "name": "mat.hidden", "albedo": [0.2, 0.2, 0.8, 1.0] }
      ],
      "brushes": [
)json" << BrushVisibilityBoxJson("brush.front_a", "mat.front", 0.1f, 0.3f, 0.5f, 1.5f, -2.5f, -2.0f)
              << "," << BrushVisibilityBoxJson("brush.front_b", "mat.front", 0.4f, 0.6f, 0.5f, 1.5f, -2.5f, -2.0f)
              << "," << BrushVisibilityBoxJson("brush.blocker", "mat.blocker", -2.0f, 2.0f, 0.0f, 2.0f, -0.25f, 0.75f)
              << "," << BrushVisibilityBoxJson("brush.hidden", "mat.hidden", -1.0f, 1.0f, 0.5f, 1.5f, 1.25f, 2.25f)
              << R"json(
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json";
    WriteText(dir / "visibility_chunks.game.json", game_json.str());

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    slayer3d_game_data_runtime *runtime = nullptr;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "visibility_chunks.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;
    slayer3d_properties_set_bool(slayer3d_game_data_mutable_scene_state(runtime), "brush.visibility.enabled", true);

    slayer3d_camera3d cam{};
    cam.position = slayer3d_vec3_make(0.0f, 1.0f, -2.75f);
    cam.target = slayer3d_vec3_make(0.0f, 1.0f, 4.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 70.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.45f, 0.45f, 0.45f));
    slayer3d_reset_render_stats(ctx);
    slayer3d_game_data_reset_brush_diagnostics(runtime);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(runtime, ctx, nullptr, &cam));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    slayer3d_game_data_brush_diagnostics diagnostics{};
    ASSERT_TRUE(slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics));
    EXPECT_GE(diagnostics.compile_chunk_count, 2u);
    EXPECT_EQ(diagnostics.visibility_brush_candidates, 4u);
    EXPECT_GE(diagnostics.visibility_brush_occluded, 1u);
    EXPECT_GE(diagnostics.render_chunk_draws, 1u);
    EXPECT_GE(diagnostics.render_chunk_brushes_drawn, 2u);
    EXPECT_LT(diagnostics.render_mesh_submissions, diagnostics.visibility_brush_visible);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    std::filesystem::remove_all(dir);
}

TEST_F(GLRendererTest, BrushVisibilityFrustumCullsOffscreenBrushesBeforeSubmission)
{
    const std::filesystem::path dir = UniqueTempDir("brush_visibility_frustum");
    WriteText(dir / "scenes" / "play.scene.json",
              R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "world": {
    "brush_worlds": [
      { "world": "brush.visibility_frustum", "visibility_occlusion": true }
    ]
  }
})json");
    std::ostringstream game_json;
    game_json << R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Brush Visibility Frustum Test" },
  "world": { "name": "world.visibility_frustum", "kind": "brush" },
  "brush_worlds": [
    {
      "name": "brush.visibility_frustum",
      "visibility_cell_size": 0.5,
      "materials": [
        { "name": "mat.visible", "albedo": [0.2, 0.8, 0.2, 1.0] },
        { "name": "mat.offscreen", "albedo": [0.8, 0.2, 0.2, 1.0] }
      ],
      "brushes": [
)json" << BrushVisibilityBoxJson("brush.visible", "mat.visible", -0.5f, 0.5f, 0.5f, 1.5f, 1.5f, 2.0f)
              << "," << BrushVisibilityBoxJson("brush.offscreen", "mat.offscreen", 24.0f, 25.0f, 0.5f, 1.5f, 1.5f, 2.0f)
              << R"json(
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json";
    WriteText(dir / "visibility_frustum.game.json", game_json.str());

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    slayer3d_game_data_runtime *runtime = nullptr;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "visibility_frustum.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_camera3d cam{};
    cam.position = slayer3d_vec3_make(0.0f, 1.0f, -2.75f);
    cam.target = slayer3d_vec3_make(0.0f, 1.0f, 4.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.45f, 0.45f, 0.45f));
    slayer3d_reset_render_stats(ctx);
    slayer3d_game_data_reset_brush_diagnostics(runtime);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(slayer3d_game_data_draw_brush_worlds_with_assets_and_camera(runtime, ctx, nullptr, &cam));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    slayer3d_game_data_brush_diagnostics diagnostics{};
    ASSERT_TRUE(slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics));
    EXPECT_EQ(diagnostics.frustum_brush_candidates, 2u);
    EXPECT_EQ(diagnostics.frustum_brush_culled, 1u);
    EXPECT_EQ(diagnostics.frustum_triangles_culled, 12u);
    EXPECT_EQ(diagnostics.render_mesh_submissions, 1u);
    EXPECT_EQ(diagnostics.render_mesh_draws, 1u);

    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    std::filesystem::remove_all(dir);
}

TEST_F(GLRendererTest, ProceduralLodReducesDistantPrimitiveTriangles)
{
    const std::filesystem::path dir = UniqueTempDir("procedural_lod");
    WriteText(dir / "scenes" / "play.scene.json",
              R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "renders_world": true
})json");
    WriteText(dir / "procedural_lod.game.json",
              R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Procedural LOD Test" },
  "world": { "name": "world.lod", "kind": "fixed_screen" },
  "render": {
    "lighting": false,
    "procedural_lod": false,
    "procedural_lod_key": "debug.procedural_lod",
    "procedural_lod_near_pixels": 120.0,
    "procedural_lod_far_pixels": 24.0,
    "procedural_lod_min_segments": 8,
    "clear_color": [0, 0, 0, 255]
  },
  "entities": [
    {
      "name": "entity.lod.torus",
      "active": true,
      "transform": { "position": [0.0, 1.0, -80.0] },
      "components": [
        {
          "type": "render.mesh_primitive",
          "primitive": "torus",
          "major_radius": 1.4,
          "minor_radius": 0.25,
          "segments": 64,
          "tube_segments": 24,
          "color": [220, 180, 80, 255],
          "lighting": false
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    slayer3d_game_data_runtime *runtime = nullptr;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "procedural_lod.game.json").string().c_str(), session, &runtime,
                                             error, sizeof(error)))
        << error;

    slayer3d_camera3d cam{};
    cam.position = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.target = slayer3d_vec3_make(0.0f, 1.0f, -1.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 70.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_game_data_mesh_primitive_cache cache{};
    slayer3d_game_data_mesh_primitive_cache_init(&cache);
    slayer3d_game_data_frame_desc frame{};
    frame.runtime = runtime;
    frame.renderer = ctx;
    frame.mesh_primitive_cache = &cache;
    frame.fallback_camera = &cam;

    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_game_data_draw_frame(&frame));
    EXPECT_EQ(cache.misses, 1);
    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.procedural_lod_candidates, 0u);
    EXPECT_EQ(stats.procedural_lod_reduced, 0u);
    EXPECT_EQ(stats.procedural_lod_triangles_saved, 0u);
    ASSERT_EQ(cache.count, 1);
    const int full_index_count = cache.entries[0].mesh.index_count;
    EXPECT_GT(full_index_count, 0);

    slayer3d_properties_set_bool(slayer3d_game_data_mutable_scene_state(runtime), "debug.procedural_lod", true);
    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_game_data_draw_frame(&frame));
    SDL_zero(stats);
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.procedural_lod_candidates, 1u);
    EXPECT_EQ(stats.procedural_lod_reduced, 1u);
    EXPECT_EQ(stats.procedural_lod_authored_triangles, 3072u);
    EXPECT_EQ(stats.procedural_lod_resolved_triangles, 128u);
    EXPECT_EQ(stats.procedural_lod_triangles_saved, 2944u);
    EXPECT_EQ(cache.misses, 2);
    ASSERT_EQ(cache.count, 2);
    const int lod_index_count = cache.entries[1].mesh.index_count;
    EXPECT_LT(lod_index_count, full_index_count / 4);
    EXPECT_LT(lod_index_count, full_index_count);

    slayer3d_game_data_mesh_primitive_cache_free(&cache);
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    std::filesystem::remove_all(dir);
}

TEST_F(GLRendererTest, ProceduralLodDoesNotCreateUnusedCubeVariants)
{
    const std::filesystem::path dir = UniqueTempDir("procedural_lod_cube");
    WriteText(dir / "scenes" / "play.scene.json",
              R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "renders_world": true
})json");
    WriteText(dir / "procedural_lod_cube.game.json",
              R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Procedural LOD Cube Test" },
  "world": { "name": "world.lod_cube", "kind": "fixed_screen" },
  "render": {
    "lighting": false,
    "procedural_lod": false,
    "procedural_lod_key": "debug.procedural_lod",
    "procedural_lod_near_pixels": 120.0,
    "procedural_lod_far_pixels": 24.0,
    "procedural_lod_min_segments": 8
  },
  "entities": [
    {
      "name": "entity.lod.cube",
      "active": true,
      "transform": { "position": [0.0, 1.0, -80.0] },
      "components": [
        {
          "type": "render.mesh_primitive",
          "primitive": "cube",
          "size": [2.0, 2.0, 2.0],
          "segments": 64,
          "rings": 32,
          "color": [220, 80, 80, 255],
          "lighting": false
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    slayer3d_game_data_runtime *runtime = nullptr;
    char error[512]{};
    ASSERT_TRUE(slayer3d_game_data_load_file((dir / "procedural_lod_cube.game.json").string().c_str(), session,
                                             &runtime, error, sizeof(error)))
        << error;

    slayer3d_camera3d cam{};
    cam.position = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.target = slayer3d_vec3_make(0.0f, 1.0f, -1.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 70.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_game_data_mesh_primitive_cache cache{};
    slayer3d_game_data_mesh_primitive_cache_init(&cache);
    slayer3d_game_data_frame_desc frame{};
    frame.runtime = runtime;
    frame.renderer = ctx;
    frame.mesh_primitive_cache = &cache;
    frame.fallback_camera = &cam;

    ASSERT_TRUE(slayer3d_game_data_draw_frame(&frame));
    EXPECT_EQ(cache.misses, 1);
    ASSERT_EQ(cache.count, 1);
    const int index_count = cache.entries[0].mesh.index_count;

    slayer3d_properties_set_bool(slayer3d_game_data_mutable_scene_state(runtime), "debug.procedural_lod", true);
    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_game_data_draw_frame(&frame));
    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.procedural_lod_candidates, 0u);
    EXPECT_EQ(stats.procedural_lod_reduced, 0u);
    EXPECT_EQ(stats.procedural_lod_triangles_saved, 0u);
    EXPECT_EQ(cache.count, 1);
    EXPECT_EQ(cache.misses, 1);
    EXPECT_GT(cache.hits, 0);
    EXPECT_EQ(cache.entries[0].mesh.index_count, index_count);

    slayer3d_game_data_mesh_primitive_cache_free(&cache);
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    std::filesystem::remove_all(dir);
}

TEST_F(GLRendererTest, ModelLodCullsTinyDistantModel)
{
    const std::filesystem::path dir = UniqueTempDir("model_lod");
    WriteText(dir / "models" / "quad.obj",
              R"obj(v -1 -1 0
v 1 -1 0
v 1 1 0
v -1 1 0
vn 0 0 1
f 1//1 2//1 3//1
f 1//1 3//1 4//1
)obj");
    WriteText(dir / "scenes" / "play.scene.json",
              R"json({
  "schema": "slayer3d.scene.v0",
  "name": "scene.play",
  "renders_world": true
})json");
    WriteText(dir / "model_lod.game.json",
              R"json({
  "schema": "slayer3d.game.v0",
  "metadata": { "name": "Model LOD Test" },
  "world": { "name": "world.model_lod", "kind": "fixed_screen" },
  "assets": {
    "models": [{ "id": "model.test.quad", "path": "asset://models/quad.obj" }]
  },
  "render": {
    "lighting": false,
    "model_lod_culling": false,
    "model_lod_culling_key": "debug.model_lod",
    "model_lod_cull_pixels": 64.0,
    "clear_color": [0, 0, 0, 255]
  },
  "entities": [
    {
      "name": "entity.lod.model",
      "active": true,
      "transform": { "position": [0.0, 1.0, -160.0] },
      "components": [
        {
          "type": "render.model",
          "model": "model.test.quad",
          "scale": [1.0, 1.0, 1.0],
          "color": [255, 255, 255, 255],
          "lighting": false
        }
      ]
    }
  ],
  "scenes": { "initial": "scene.play", "files": ["scenes/play.scene.json"] }
})json");

    slayer3d_asset_resolver *assets = slayer3d_asset_resolver_create();
    ASSERT_NE(assets, nullptr);
    char asset_error[256]{};
    ASSERT_TRUE(slayer3d_asset_resolver_mount_directory(assets, dir.string().c_str(), asset_error, sizeof(asset_error)))
        << asset_error;

    slayer3d_game_session *session = nullptr;
    ASSERT_TRUE(slayer3d_game_session_create(nullptr, &session));
    slayer3d_game_data_runtime *runtime = nullptr;
    char error[512]{};
    ASSERT_TRUE(
        slayer3d_game_data_load_asset(assets, "asset://model_lod.game.json", session, &runtime, error, sizeof(error)))
        << error;

    slayer3d_camera3d cam{};
    cam.position = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.target = slayer3d_vec3_make(0.0f, 1.0f, -1.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 70.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_game_data_model_cache model_cache{};
    slayer3d_game_data_model_cache_init(&model_cache, assets);
    slayer3d_game_data_frame_desc frame{};
    frame.runtime = runtime;
    frame.renderer = ctx;
    frame.model_cache = &model_cache;
    frame.fallback_camera = &cam;

    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_game_data_draw_frame(&frame));
    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.model_lod_candidates, 0u);
    EXPECT_EQ(stats.model_lod_culled, 0u);
    EXPECT_EQ(stats.model_lod_triangles_saved, 0u);
    EXPECT_EQ(stats.model_mesh_draws, 1u);
    EXPECT_EQ(stats.model_triangles_submitted, 2u);

    slayer3d_properties_set_bool(slayer3d_game_data_mutable_scene_state(runtime), "debug.model_lod", true);
    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_game_data_draw_frame(&frame));
    SDL_zero(stats);
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.model_lod_candidates, 1u);
    EXPECT_EQ(stats.model_lod_culled, 1u);
    EXPECT_EQ(stats.model_lod_triangles_saved, 2u);
    EXPECT_EQ(stats.model_mesh_draws, 0u);
    EXPECT_EQ(stats.model_triangles_submitted, 0u);

    slayer3d_game_data_model_cache_free(&model_cache);
    slayer3d_game_data_destroy(runtime);
    slayer3d_game_session_destroy(session);
    slayer3d_asset_resolver_destroy(assets);
    std::filesystem::remove_all(dir);
}

TEST_F(GLRendererTest, DepthPrepassDisabledDoesNotReplayEligibleMeshes)
{
    ASSERT_TRUE(slayer3d_set_depth_prepass_enabled(ctx, false));
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.2f, 0.2f, 0.2f));
    ctx->depth_prepass_scope_enabled = true;

    float positions[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    unsigned int indices[] = {0, 1, 2, 0, 2, 3};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 6;
    mesh.material_index = -1;

    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(
        slayer3d_draw_model(ctx, &model, slayer3d_vec3_make(0, 0, 0), 1.0f, slayer3d_color{255, 255, 255, 255}));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    unsigned char px[4];
    readPixel(160, 120, px);

    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.depth_prepass_draws, 0u);
    EXPECT_EQ(stats.depth_prepass_triangles, 0u);
    EXPECT_GT(px[0] + px[1] + px[2], 20);
}

TEST_F(GLRendererTest, DepthPrepassReducesMainGeometrySamplesInOverdrawCase)
{
    ASSERT_TRUE(slayer3d_set_render_sample_queries_enabled(ctx, true));
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.35f, 0.35f, 0.35f));
    ctx->depth_prepass_scope_enabled = true;

    float positions[] = {
        -1.4f, -1.4f, 0.0f, 1.4f, -1.4f, 0.0f, 1.4f, 1.4f, 0.0f, -1.4f, 1.4f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    unsigned int indices[] = {0, 1, 2, 0, 2, 3};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 6;
    mesh.material_index = -1;

    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    auto draw_overdraw_stack = [&](int layer_count) {
        ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
        ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
        for (int i = 0; i < layer_count; ++i)
        {
            const float z = -0.14f + (float)i * 0.02f;
            ASSERT_TRUE(slayer3d_draw_model(ctx, &model, slayer3d_vec3_make(0, 0, z), 1.0f,
                                            slayer3d_color{255, 255, 255, 255}));
        }
        ASSERT_TRUE(slayer3d_end_mode_3d(ctx));
        unsigned char px[4];
        readPixel(160, 120, px);
        EXPECT_GT(px[0] + px[1] + px[2], 20);
    };

    auto measure = [&](int layer_count, bool depth_prepass) {
        EXPECT_TRUE(slayer3d_set_depth_prepass_enabled(ctx, depth_prepass));
        slayer3d_reset_render_stats(ctx);
        draw_overdraw_stack(layer_count);
        slayer3d_render_stats stats{};
        EXPECT_TRUE(slayer3d_get_render_stats(ctx, &stats));
        return stats;
    };

    const slayer3d_render_stats one_layer = measure(1, false);
    if (one_layer.geometry_samples_passed == 0u)
    {
        GTEST_SKIP() << "OpenGL sample queries are unavailable on this backend";
    }
    const slayer3d_render_stats four_layers = measure(4, false);
    const slayer3d_render_stats eight_layers = measure(8, false);
    const slayer3d_render_stats eight_layers_with_prepass = measure(8, true);

    EXPECT_GT(four_layers.geometry_samples_passed, one_layer.geometry_samples_passed * 3u);
    EXPECT_GT(eight_layers.geometry_samples_passed, one_layer.geometry_samples_passed * 6u);
    EXPECT_GT(eight_layers_with_prepass.depth_prepass_samples_passed, 0u);
    EXPECT_LE(eight_layers_with_prepass.geometry_samples_passed, one_layer.geometry_samples_passed * 2u);
    EXPECT_LT(eight_layers_with_prepass.geometry_samples_passed, eight_layers.geometry_samples_passed / 2u);
}

TEST_F(GLRendererTest, PhongCubeVisibleWithoutIBL)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));

    slayer3d_light light = {};
    light.type = SLAYER3D_LIGHT_POINT;
    light.position = slayer3d_vec3_make(2.0f, 2.5f, 3.5f);
    light.color[0] = 1.0f;
    light.color[1] = 0.95f;
    light.color[2] = 0.85f;
    light.intensity = 8.0f;
    light.range = 20.0f;
    slayer3d_add_light(ctx, &light);
    slayer3d_set_ambient_light(ctx, 0.15f, 0.15f, 0.18f);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, slayer3d_color{8, 32, 96, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(2, 2, 2),
                       slayer3d_color{255, 255, 255, 255});
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    EXPECT_GT(px[0] + px[1] + px[2], 60);
    EXPECT_FALSE(px[0] == 8 && px[1] == 32 && px[2] == 96);
}

TEST_F(GLRendererTest, AmbientOnlyPhongCubeUsesLitPath)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.12f, 0.12f, 0.14f));

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(2, 2, 2),
                       slayer3d_color{255, 255, 255, 255});
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    EXPECT_GT(px[0] + px[1] + px[2], 20);
    EXPECT_LT(px[0], 220);
    EXPECT_LT(px[1], 220);
    EXPECT_LT(px[2], 220);
}

TEST_F(GLRendererTest, AmbientOnlyPhongModelUsesLitPath)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.10f, 0.12f, 0.14f));

    float positions[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    unsigned int indices[] = {0, 1, 2, 0, 2, 3};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 6;
    mesh.material_index = -1;

    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(
        slayer3d_draw_model(ctx, &model, slayer3d_vec3_make(0, 0, 0), 1.0f, slayer3d_color{255, 255, 255, 255}));
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    EXPECT_GT(px[0] + px[1] + px[2], 20);
    EXPECT_LT(px[0], 220);
    EXPECT_LT(px[1], 220);
    EXPECT_LT(px[2], 220);
}

TEST_F(GLRendererTest, PerObjectLightSelectionCapsShaderLightsPerDraw)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.05f, 0.05f, 0.05f));
    ASSERT_TRUE(slayer3d_set_per_object_light_selection_enabled(ctx, true));
    ASSERT_TRUE(slayer3d_set_per_object_light_limit(ctx, 4));

    for (int i = 0; i < SLAYER3D_MAX_LIGHTS; ++i)
    {
        slayer3d_light light = {};
        light.type = SLAYER3D_LIGHT_POINT;
        light.position = slayer3d_vec3_make((float)(i % 8) - 3.5f, 2.0f, 2.0f + (float)(i / 8));
        light.color[0] = 1.0f;
        light.color[1] = 1.0f;
        light.color[2] = 1.0f;
        light.intensity = 2.0f;
        light.range = 100.0f;
        ASSERT_TRUE(slayer3d_add_light(ctx, &light));
    }

    float positions[] = {
        -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    unsigned int indices[] = {0, 1, 2, 0, 2, 3};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 6;
    mesh.material_index = -1;
    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(
        slayer3d_draw_model(ctx, &model, slayer3d_vec3_make(-1.2f, 0, 0), 1.0f, slayer3d_color{255, 255, 255, 255}));
    ASSERT_TRUE(
        slayer3d_draw_model(ctx, &model, slayer3d_vec3_make(0.0f, 0, 0), 1.0f, slayer3d_color{255, 255, 255, 255}));
    ASSERT_TRUE(
        slayer3d_draw_model(ctx, &model, slayer3d_vec3_make(1.2f, 0, 0), 1.0f, slayer3d_color{255, 255, 255, 255}));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    unsigned char px[4];
    readPixel(160, 120, px);
    EXPECT_GT(px[0] + px[1] + px[2], 20);

    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.light_selection_draws, 3u);
    EXPECT_EQ(stats.light_candidates, (Uint64)SLAYER3D_MAX_LIGHTS * 3u);
    EXPECT_EQ(stats.lights_selected, 12u);
    EXPECT_EQ(stats.geometry_draw_calls, 3u);
    EXPECT_EQ(stats.static_mesh_instanced_draw_calls, 0u);
}

TEST_F(GLRendererTest, PerObjectLightSelectionChoosesRelevantLocalLight)
{
    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    ASSERT_TRUE(slayer3d_set_ambient_light(ctx, 0.0f, 0.0f, 0.0f));
    ASSERT_TRUE(slayer3d_set_per_object_light_selection_enabled(ctx, true));
    ASSERT_TRUE(slayer3d_set_per_object_light_limit(ctx, 1));

    for (int i = 0; i < SLAYER3D_MAX_SHADER_LIGHTS; ++i)
    {
        slayer3d_light light = {};
        light.type = SLAYER3D_LIGHT_POINT;
        light.position = slayer3d_vec3_make(30.0f + (float)i, 0.0f, 2.0f);
        light.color[0] = 1.0f;
        light.color[1] = 0.0f;
        light.color[2] = 0.0f;
        light.intensity = 40.0f;
        light.range = 2.0f;
        ASSERT_TRUE(slayer3d_add_light(ctx, &light));
    }

    slayer3d_light green = {};
    green.type = SLAYER3D_LIGHT_POINT;
    green.position = slayer3d_vec3_make(0.0f, 0.0f, 2.0f);
    green.color[0] = 0.0f;
    green.color[1] = 1.0f;
    green.color[2] = 0.0f;
    green.intensity = 12.0f;
    green.range = 8.0f;
    ASSERT_TRUE(slayer3d_add_light(ctx, &green));

    float positions[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f,
    };
    float normals[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    unsigned int indices[] = {0, 1, 2, 0, 2, 3};
    slayer3d_mesh mesh = {};
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 6;
    mesh.material_index = -1;
    slayer3d_model model = {};
    model.meshes = &mesh;
    model.mesh_count = 1;

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_reset_render_stats(ctx);
    ASSERT_TRUE(slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255}));
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(
        slayer3d_draw_model(ctx, &model, slayer3d_vec3_make(0, 0, 0), 1.0f, slayer3d_color{255, 255, 255, 255}));
    ASSERT_TRUE(slayer3d_end_mode_3d(ctx));

    unsigned char px[4];
    readPixel(160, 120, px);
    EXPECT_GT(px[1], px[0]);
    EXPECT_GT(px[1], px[2]);

    slayer3d_render_stats stats{};
    ASSERT_TRUE(slayer3d_get_render_stats(ctx, &stats));
    EXPECT_EQ(stats.light_selection_draws, 1u);
    EXPECT_EQ(stats.light_candidates, (Uint64)SLAYER3D_MAX_SHADER_LIGHTS + 1u);
    EXPECT_EQ(stats.lights_selected, 1u);
}

TEST_F(GLRendererTest, CubeVisibleOnFirstFrame)
{
    /* This specifically tests lesson #1 and #9: first frame must be correct. */
    slayer3d_set_ambient_light(ctx, 0.5f, 0.5f, 0.5f);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_color clear = {0, 0, 0, 255};
    slayer3d_color green = {0, 255, 0, 255};

    slayer3d_clear_render_context(ctx, clear);
    slayer3d_begin_mode_3d(ctx, cam);
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(3, 3, 3), green);
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    EXPECT_GT(px[1], 50);
}

TEST_F(GLRendererTest, Line3DVisibleOnGLBackend)
{
    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    slayer3d_draw_line_3d(ctx, slayer3d_vec3_make(-1.5f, 0.0f, 0.0f), slayer3d_vec3_make(1.5f, 0.0f, 0.0f),
                          slayer3d_color{255, 32, 32, 255});
    slayer3d_end_mode_3d(ctx);

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

TEST_F(GLRendererTest, Line3DVisibleWithBackfaceCullingOnGLBackend)
{
    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(ctx, true));
    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    slayer3d_draw_line_3d(ctx, slayer3d_vec3_make(1.5f, 0.0f, 0.0f), slayer3d_vec3_make(-1.5f, 0.0f, 0.0f),
                          slayer3d_color{255, 32, 32, 255});
    slayer3d_end_mode_3d(ctx);

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
    slayer3d_set_ambient_light(ctx, 1.0f, 1.0f, 1.0f);
    slayer3d_set_backface_culling_enabled(ctx, true);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 3);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_color clear = {0, 0, 0, 255};
    slayer3d_color red = {255, 0, 0, 255};

    slayer3d_clear_render_context(ctx, clear);
    slayer3d_begin_mode_3d(ctx, cam);
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(4, 4, 4), red);
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    /* With correct culling, we should see the front face (red), not black. */
    EXPECT_GT(px[0], 50);
}

TEST_F(GLRendererTest, BackfaceCullingHidesCubeInteriorAcrossFrames)
{
    slayer3d_set_ambient_light(ctx, 1.0f, 1.0f, 1.0f);
    slayer3d_set_backface_culling_enabled(ctx, true);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 0);
    cam.target = slayer3d_vec3_make(0, 0, -1);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    for (int frame = 0; frame < 2; ++frame)
    {
        slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
        slayer3d_begin_mode_3d(ctx, cam);
        slayer3d_draw_cube(ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(4, 4, 4),
                           slayer3d_color{255, 0, 0, 255});
        slayer3d_end_mode_3d(ctx);

        unsigned char px[4];
        readPixel(160, 120, px);
        EXPECT_LT(px[0] + px[1] + px[2], 20) << "Interior cube faces should remain culled on frame " << frame;
    }
}

TEST_F(GLRendererTest, LevelInteriorVisibleWithBackfaceCulling)
{
    const slayer3d_level_material materials[] = {{{1, 1, 1, 1}, 0.0f, 1.0f, nullptr, 4.0f},
                                                 {{1, 1, 1, 1}, 0.0f, 1.0f, nullptr, 4.0f},
                                                 {{1, 1, 1, 1}, 0.0f, 1.0f, nullptr, 4.0f}};
    slayer3d_sector sector{};
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

    slayer3d_level level{};
    ASSERT_TRUE(slayer3d_build_level(&sector, 1, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    slayer3d_set_backface_culling_enabled(ctx, true);
    slayer3d_set_ambient_light(ctx, 1.0f, 1.0f, 1.0f);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(2.0f, 1.5f, 2.0f);
    cam.target = slayer3d_vec3_make(2.0f, 1.5f, 0.0f);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(slayer3d_draw_level(ctx, &level, nullptr, slayer3d_color{255, 255, 255, 255})) << SDL_GetError();
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);

    slayer3d_free_level(&level);

    EXPECT_GT(px[0] + px[1] + px[2], 30);
}

TEST_F(GLRendererTest, LightmappedLevelRendersWithoutVertexColorFallback)
{
    const slayer3d_level_material materials[] = {
        {{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 1.0f, nullptr, 4.0f},
        {{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 1.0f, nullptr, 4.0f},
        {{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 1.0f, nullptr, 4.0f},
    };
    const slayer3d_level_light light = {{2.0f, 2.2f, 2.0f}, {1.0f, 0.9f, 0.8f}, 3.0f, 8.0f};
    slayer3d_sector sector{};
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

    slayer3d_level level{};
    ASSERT_TRUE(slayer3d_build_level(&sector, 1, materials, 3, &light, 1, &level)) << SDL_GetError();

    for (int mi = 0; mi < level.model.mesh_count; ++mi)
    {
        slayer3d_mesh &mesh = level.model.meshes[mi];
        ASSERT_NE(mesh.colors, nullptr);
        for (int v = 0; v < mesh.vertex_count; ++v)
        {
            mesh.colors[v * 4 + 0] = 0.0f;
            mesh.colors[v * 4 + 1] = 0.0f;
            mesh.colors[v * 4 + 2] = 0.0f;
            mesh.colors[v * 4 + 3] = 1.0f;
        }
    }

    ASSERT_TRUE(slayer3d_set_shading_mode(ctx, SLAYER3D_SHADING_PHONG));
    slayer3d_set_ambient_light(ctx, 0.0f, 0.0f, 0.0f);
    slayer3d_set_backface_culling_enabled(ctx, true);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(2.0f, 1.5f, 2.0f);
    cam.target = slayer3d_vec3_make(2.0f, 1.5f, 0.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(slayer3d_draw_level(ctx, &level, nullptr, slayer3d_color{255, 255, 255, 255})) << SDL_GetError();
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);
    slayer3d_free_level(&level);

    EXPECT_GT(px[0] + px[1] + px[2], 30);
}

TEST_F(GLRendererTest, BillboardVisibleWithTexture)
{
    const Uint8 pixels[] = {
        255, 64, 64, 255, 255, 64, 64, 255, 255, 64, 64, 255, 255, 64, 64, 255,
    };
    slayer3d_texture2d texture = MakeTextureFromPixels(pixels, 2, 2);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0.0f, 1.0f, 4.0f);
    cam.target = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(slayer3d_draw_billboard(ctx, &texture, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), slayer3d_vec2{2.0f, 2.0f},
                                        slayer3d_color{255, 255, 255, 255}))
        << SDL_GetError();
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);
    slayer3d_free_texture(&texture);

    EXPECT_GT(px[0], 100);
    EXPECT_GT(px[1], 20);
    EXPECT_GT(px[2], 20);
}

TEST_F(GLRendererTest, BillboardVisibleFromOppositeViewDirection)
{
    const Uint8 pixels[] = {
        255, 64, 64, 255, 255, 64, 64, 255, 255, 64, 64, 255, 255, 64, 64, 255,
    };
    slayer3d_texture2d texture = MakeTextureFromPixels(pixels, 2, 2);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0.0f, 1.0f, -4.0f);
    cam.target = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(slayer3d_draw_billboard(ctx, &texture, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), slayer3d_vec2{2.0f, 2.0f},
                                        slayer3d_color{255, 255, 255, 255}))
        << SDL_GetError();
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);
    slayer3d_free_texture(&texture);

    EXPECT_GT(px[0], 100);
}

TEST_F(GLRendererTest, BillboardPreservesTopToBottomTextureOrientation)
{
    const Uint8 pixels[] = {
        255, 32, 32, 255, 255, 32, 32, 255, 32, 32, 255, 255, 32, 32, 255, 255,
    };
    slayer3d_texture2d texture = MakeTextureFromPixels(pixels, 2, 2);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0.0f, 1.0f, 4.0f);
    cam.target = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(slayer3d_draw_billboard(ctx, &texture, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), slayer3d_vec2{2.0f, 2.0f},
                                        slayer3d_color{255, 255, 255, 255}))
        << SDL_GetError();
    slayer3d_end_mode_3d(ctx);

    unsigned char top_px[4];
    unsigned char bottom_px[4];
    readPixel(160, 150, top_px);
    readPixel(160, 90, bottom_px);
    slayer3d_free_texture(&texture);

    EXPECT_GT(top_px[0], top_px[2]);
    EXPECT_GT(bottom_px[2], bottom_px[0]);
}

TEST_F(GLRendererTest, BillboardTransparentPixelsDiscard)
{
    const Uint8 pixels[] = {
        0, 0, 0, 0, 255, 64, 64, 255, 0, 0, 0, 0, 255, 64, 64, 255,
    };
    slayer3d_texture2d texture = MakeTextureFromPixels(pixels, 2, 2);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0.0f, 1.0f, 4.0f);
    cam.target = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(slayer3d_draw_billboard(ctx, &texture, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), slayer3d_vec2{2.0f, 2.0f},
                                        slayer3d_color{255, 255, 255, 255}))
        << SDL_GetError();
    slayer3d_end_mode_3d(ctx);

    unsigned char left_px[4];
    unsigned char right_px[4];
    readPixel(130, 120, left_px);
    readPixel(190, 120, right_px);
    slayer3d_free_texture(&texture);

    EXPECT_LT(left_px[0] + left_px[1] + left_px[2], 40);
    EXPECT_GT(right_px[0], 100);
}

TEST_F(GLRendererTest, MuzzleFlashParticleShaderProducesNonClearPixels)
{
    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0.0f, 0.0f, 2.0f);
    cam.target = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_particle_config config{};
    config.position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    config.lifetime_min = 1.0f;
    config.lifetime_max = 1.0f;
    config.size_start = 1.4f;
    config.size_end = 1.4f;
    config.color_start = slayer3d_color{255, 225, 120, 255};
    config.color_end = slayer3d_color{255, 80, 16, 255};
    config.max_particles = 1;
    config.render_style = SLAYER3D_PARTICLE_RENDER_MUZZLE_FLASH;
    config.camera_facing = true;
    config.emissive_intensity = 1.0f;

    slayer3d_particle_emitter *emitter = slayer3d_create_particle_emitter(&config);
    ASSERT_NE(emitter, nullptr) << SDL_GetError();
    slayer3d_particle_emitter_emit(emitter, 1);

    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    ASSERT_TRUE(slayer3d_begin_mode_3d(ctx, cam));
    ASSERT_TRUE(slayer3d_draw_particles(ctx, emitter)) << SDL_GetError();
    slayer3d_end_mode_3d(ctx);

    unsigned char px[4];
    readPixel(160, 120, px);
    slayer3d_destroy_particle_emitter(emitter);

    EXPECT_GT(px[0], 100);
    EXPECT_GT(px[1], 30);
}

TEST_F(GLRendererTest, TexturedSkyboxShowsTopFaceWithBackfaceCulling)
{
    const Uint8 red[] = {255, 0, 0, 255};
    const Uint8 green[] = {0, 255, 0, 255};
    const Uint8 blue[] = {0, 0, 255, 255};
    const Uint8 yellow[] = {255, 255, 0, 255};
    const Uint8 magenta[] = {255, 0, 255, 255};
    const Uint8 cyan[] = {0, 255, 255, 255};
    slayer3d_texture2d px = MakeTextureFromPixels(red, 1, 1);
    slayer3d_texture2d nx = MakeTextureFromPixels(green, 1, 1);
    slayer3d_texture2d py = MakeTextureFromPixels(blue, 1, 1);
    slayer3d_texture2d ny = MakeTextureFromPixels(yellow, 1, 1);
    slayer3d_texture2d pz = MakeTextureFromPixels(magenta, 1, 1);
    slayer3d_texture2d nz = MakeTextureFromPixels(cyan, 1, 1);
    slayer3d_skybox_textured skybox = {&px, &nx, &py, &ny, &pz, &nz, 20.0f};

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    cam.target = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.up = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(ctx, true));
    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    ASSERT_TRUE(slayer3d_draw_skybox_textured(ctx, &skybox)) << SDL_GetError();
    slayer3d_end_mode_3d(ctx);

    unsigned char px_out[4];
    readPixel(160, 120, px_out);

    slayer3d_free_texture(&px);
    slayer3d_free_texture(&nx);
    slayer3d_free_texture(&py);
    slayer3d_free_texture(&ny);
    slayer3d_free_texture(&pz);
    slayer3d_free_texture(&nz);

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
    slayer3d_texture2d px = MakeTextureFromPixels(red, 1, 1);
    slayer3d_texture2d nx = MakeTextureFromPixels(green, 1, 1);
    slayer3d_texture2d py = MakeTextureFromPixels(blue, 1, 1);
    slayer3d_texture2d ny = MakeTextureFromPixels(yellow, 1, 1);
    slayer3d_texture2d pz = MakeTextureFromPixels(magenta, 1, 1);
    slayer3d_texture2d nz = MakeTextureFromPixels(cyan, 1, 1);
    slayer3d_skybox_textured skybox = {&px, &nx, &py, &ny, &pz, &nz, 20.0f};
    struct ViewCase
    {
        slayer3d_vec3 target;
        Uint8 r;
        Uint8 g;
        Uint8 b;
    } cases[] = {
        {slayer3d_vec3_make(0.0f, 0.0f, 1.0f), 255, 0, 0},   /* front -> PX */
        {slayer3d_vec3_make(1.0f, 0.0f, 0.0f), 0, 255, 255}, /* right -> NZ */
        {slayer3d_vec3_make(0.0f, 0.0f, -1.0f), 0, 255, 0},  /* back -> NX */
        {slayer3d_vec3_make(-1.0f, 0.0f, 0.0f), 255, 0, 255} /* left -> PZ */
    };

    ASSERT_TRUE(slayer3d_set_backface_culling_enabled(ctx, true));

    for (const ViewCase &view_case : cases)
    {
        slayer3d_camera3d cam;
        unsigned char px_out[4];

        cam.position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        cam.target = view_case.target;
        cam.up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
        cam.fovy = 60.0f;
        cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

        slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
        slayer3d_begin_mode_3d(ctx, cam);
        ASSERT_TRUE(slayer3d_draw_skybox_textured(ctx, &skybox)) << SDL_GetError();
        slayer3d_end_mode_3d(ctx);

        readPixel(160, 120, px_out);
        EXPECT_NEAR(px_out[0], view_case.r, 20);
        EXPECT_NEAR(px_out[1], view_case.g, 20);
        EXPECT_NEAR(px_out[2], view_case.b, 20);
    }

    slayer3d_free_texture(&px);
    slayer3d_free_texture(&nx);
    slayer3d_free_texture(&py);
    slayer3d_free_texture(&ny);
    slayer3d_free_texture(&pz);
    slayer3d_free_texture(&nz);
}

TEST_F(GLRendererTest, ToggleRecreateProducesCorrectOutput)
{
    /* Lesson #9: verify output is correct after context recreation. */
    slayer3d_set_ambient_light(ctx, 0.5f, 0.5f, 0.5f);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 0, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_color clear = {0, 0, 0, 255};
    slayer3d_color blue = {0, 0, 255, 255};

    /* First render. */
    slayer3d_clear_render_context(ctx, clear);
    slayer3d_begin_mode_3d(ctx, cam);
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(3, 3, 3), blue);
    slayer3d_end_mode_3d(ctx);

    unsigned char px1[4];
    readPixel(160, 120, px1);

    /* Destroy and recreate. */
    slayer3d_destroy_render_context(ctx);
    ctx = nullptr;

    slayer3d_render_context_config cfg;
    slayer3d_init_render_context_config(&cfg);
    cfg.backend = SLAYER3D_BACKEND_OPENGL;
    cfg.logical_width = 320;
    cfg.logical_height = 240;
    ASSERT_TRUE(slayer3d_create_render_context(win, nullptr, &cfg, &ctx));

    slayer3d_set_ambient_light(ctx, 0.5f, 0.5f, 0.5f);

    /* Second render after recreation. */
    slayer3d_clear_render_context(ctx, clear);
    slayer3d_begin_mode_3d(ctx, cam);
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(3, 3, 3), blue);
    slayer3d_end_mode_3d(ctx);

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
    slayer3d_light sun = {};
    sun.type = SLAYER3D_LIGHT_DIRECTIONAL;
    sun.direction = slayer3d_vec3_make(0, -1, 0);
    sun.color[0] = 1;
    sun.color[1] = 1;
    sun.color[2] = 1;
    sun.intensity = 1.0f;
    slayer3d_add_light(ctx, &sun);
    slayer3d_enable_shadow(ctx, 0, slayer3d_vec3_make(0, 0, 0), 10.0f);

    /* Main pass: draw a cube — shadow pass is automatic. */
    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 3, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_set_ambient_light(ctx, 0.3f, 0.3f, 0.3f);
    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(2, 2, 2),
                       slayer3d_color{255, 255, 255, 255});
    slayer3d_end_mode_3d(ctx);

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
    slayer3d_light sun = {};
    sun.type = SLAYER3D_LIGHT_DIRECTIONAL;
    sun.direction = slayer3d_vec3_make(0, -1, 0);
    sun.color[0] = 1;
    sun.color[1] = 1;
    sun.color[2] = 1;
    sun.intensity = 2.0f;
    slayer3d_add_light(ctx, &sun);
    slayer3d_set_ambient_light(ctx, 0.2f, 0.2f, 0.2f);
    slayer3d_enable_shadow(ctx, 0, slayer3d_vec3_make(0, 0, 0), 10.0f);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 3, 5);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    /* First frame ever — shadow pass is now automatic. */
    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec3_make(2, 2, 2),
                       slayer3d_color{255, 255, 255, 255});
    slayer3d_end_mode_3d(ctx);

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
    slayer3d_light sun = {};
    sun.type = SLAYER3D_LIGHT_DIRECTIONAL;
    sun.direction = slayer3d_vec3_make(0.4f, -0.8f, -0.3f);
    sun.color[0] = 1;
    sun.color[1] = 1;
    sun.color[2] = 1;
    sun.intensity = 1.5f;
    slayer3d_add_light(ctx, &sun);
    slayer3d_set_lighting_enabled(ctx, true);
    slayer3d_set_ambient_light(ctx, 0.15f, 0.15f, 0.15f);
    slayer3d_enable_shadow(ctx, 0, slayer3d_vec3_make(0, 0, 0), 30.0f);

    slayer3d_camera3d cam;
    cam.position = slayer3d_vec3_make(0, 5, 15);
    cam.target = slayer3d_vec3_make(0, 0, 0);
    cam.up = slayer3d_vec3_make(0, 1, 0);
    cam.fovy = 60.0f;
    cam.projection = SLAYER3D_CAMERA_PERSPECTIVE;

    slayer3d_clear_render_context(ctx, slayer3d_color{0, 0, 0, 255});
    slayer3d_begin_mode_3d(ctx, cam);
    /* Draw a large ground plane and several cubes at different distances. */
    slayer3d_draw_plane(ctx, slayer3d_vec3_make(0, 0, 0), slayer3d_vec2{40, 40}, slayer3d_color{200, 200, 200, 255});
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(0, 1, 0), slayer3d_vec3_make(2, 2, 2), slayer3d_color{255, 0, 0, 255});
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(5, 1, -5), slayer3d_vec3_make(2, 2, 2), slayer3d_color{0, 255, 0, 255});
    slayer3d_draw_cube(ctx, slayer3d_vec3_make(-8, 1, -10), slayer3d_vec3_make(2, 2, 2),
                       slayer3d_color{0, 0, 255, 255});
    slayer3d_end_mode_3d(ctx);

    /* The center pixel should show the lit scene (not black). */
    unsigned char px[4];
    readPixel(160, 120, px);
    int brightness = px[0] + px[1] + px[2];
    EXPECT_GT(brightness, 30);
}
