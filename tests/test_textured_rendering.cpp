/*
 * Integration tests for textured mesh/model drawing. Covers software texture
 * sampling modes, perspective-correct UV interpolation, model texture path
 * resolution, cache reuse, and logical-presentation interop.
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
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{

class SDL3DTexturedFixture : public ::testing::Test
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
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        if (!SDL_CreateWindowAndRenderer("SDL3D Textured Test", width, height, 0, &window, &renderer))
        {
            ADD_FAILURE() << SDL_GetError();
            return;
        }
        window_.reset(window);
        renderer_.reset(renderer);
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

std::filesystem::path make_temp_dir(const char *leaf)
{
    const auto unique =
        std::to_string((long long)std::filesystem::file_time_type::clock::now().time_since_epoch().count());
    std::filesystem::path dir = std::filesystem::temp_directory_path() / (std::string(leaf) + "_" + unique);
    std::filesystem::create_directories(dir);
    return dir;
}

sdl3d_camera3d make_ortho(float view_height = 4.0f)
{
    sdl3d_camera3d camera{};
    camera.position = sdl3d_vec3_make(0.0f, 0.0f, 3.0f);
    camera.target = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    camera.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    camera.fovy = view_height;
    camera.projection = SDL3D_CAMERA_ORTHOGRAPHIC;
    return camera;
}

sdl3d_camera3d make_perspective(sdl3d_vec3 eye, sdl3d_vec3 target, float fovy_degrees = 60.0f)
{
    sdl3d_camera3d camera{};
    camera.position = eye;
    camera.target = target;
    camera.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    camera.fovy = fovy_degrees;
    camera.projection = SDL3D_CAMERA_PERSPECTIVE;
    return camera;
}

constexpr sdl3d_color kBlack = {0, 0, 0, 255};
constexpr sdl3d_color kWhite = {255, 255, 255, 255};
constexpr sdl3d_color kRed = {255, 0, 0, 255};
constexpr sdl3d_color kGreen = {0, 255, 0, 255};
constexpr sdl3d_color kBlue = {0, 0, 255, 255};
constexpr sdl3d_color kYellow = {255, 255, 0, 255};
constexpr sdl3d_color kMauve = {180, 90, 140, 255};

bool pixel_near(sdl3d_color actual, sdl3d_color expected, int tolerance = 12)
{
    return std::abs((int)actual.r - (int)expected.r) <= tolerance &&
           std::abs((int)actual.g - (int)expected.g) <= tolerance &&
           std::abs((int)actual.b - (int)expected.b) <= tolerance &&
           std::abs((int)actual.a - (int)expected.a) <= tolerance;
}

sdl3d_texture2d make_texture_from_pixels(const std::vector<Uint8> &pixels, int width, int height)
{
    sdl3d_image image{};
    image.pixels = const_cast<Uint8 *>(pixels.data());
    image.width = width;
    image.height = height;

    sdl3d_texture2d texture{};
    EXPECT_TRUE(sdl3d_create_texture_from_image(&image, &texture)) << SDL_GetError();
    return texture;
}

sdl3d_mesh make_textured_quad_mesh()
{
    sdl3d_mesh mesh{};

    static float positions[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f,
    };
    static float uvs[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
    };
    static unsigned int indices[] = {
        0, 1, 2, 0, 2, 3,
    };

    mesh.positions = positions;
    mesh.uvs = uvs;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 6;
    mesh.material_index = -1;
    return mesh;
}

SDL_Point project_to_screen(const sdl3d_render_context *context, sdl3d_camera3d camera, sdl3d_vec3 point)
{
    sdl3d_mat4 view{};
    sdl3d_mat4 projection{};
    EXPECT_TRUE(sdl3d_camera3d_compute_matrices(&camera, sdl3d_get_render_context_width(context),
                                                sdl3d_get_render_context_height(context), 0.01f, 1000.0f, &view,
                                                &projection));

    const sdl3d_mat4 vp = sdl3d_mat4_multiply(projection, view);
    const sdl3d_vec4 clip = sdl3d_mat4_transform_vec4(vp, sdl3d_vec4_from_vec3(point, 1.0f));
    const float inverse_w = 1.0f / clip.w;

    SDL_Point out{};
    out.x = (int)std::lround((clip.x * inverse_w + 1.0f) * 0.5f * (float)sdl3d_get_render_context_width(context));
    out.y = (int)std::lround((1.0f - clip.y * inverse_w) * 0.5f * (float)sdl3d_get_render_context_height(context));
    return out;
}

void write_text_file(const std::filesystem::path &path, const std::string &contents)
{
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.is_open());
    out << contents;
    ASSERT_TRUE(out.good());
}

std::filesystem::path write_model_fixture(const std::filesystem::path &dir, sdl3d_color texture_color)
{
    const std::filesystem::path texture_path = dir / "albedo.png";
    const std::filesystem::path mtl_path = dir / "quad.mtl";
    const std::filesystem::path obj_path = dir / "quad.obj";

    std::vector<Uint8> pixels = {
        texture_color.r,
        texture_color.g,
        texture_color.b,
        texture_color.a,
    };
    sdl3d_image image{};
    image.pixels = pixels.data();
    image.width = 1;
    image.height = 1;
    if (!sdl3d_save_image_png(&image, texture_path.string().c_str()))
    {
        ADD_FAILURE() << SDL_GetError();
    }

    write_text_file(mtl_path, "newmtl QuadMat\nKd 1.0 1.0 1.0\nmap_Kd albedo.png\n");
    write_text_file(obj_path, "mtllib quad.mtl\n"
                              "o Quad\n"
                              "v -1.0 -1.0 0.0\n"
                              "v  1.0 -1.0 0.0\n"
                              "v  1.0  1.0 0.0\n"
                              "v -1.0  1.0 0.0\n"
                              "vt 0.0 0.0\n"
                              "vt 1.0 0.0\n"
                              "vt 1.0 1.0\n"
                              "vt 0.0 1.0\n"
                              "usemtl QuadMat\n"
                              "f 1/1 2/2 3/3 4/4\n");

    return obj_path;
}

} // namespace

TEST(SDL3DTexturedNull, NullContextRejected)
{
    sdl3d_mesh mesh{};
    EXPECT_FALSE(sdl3d_draw_mesh(nullptr, &mesh, nullptr, kWhite));
}

TEST_F(SDL3DTexturedFixture, DrawMeshOutsideModeRejected)
{
    WindowRenderer wr(64, 64);
    ASSERT_TRUE(wr.ok());

    sdl3d_render_context *context = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &context));

    const sdl3d_mesh mesh = make_textured_quad_mesh();
    EXPECT_FALSE(sdl3d_draw_mesh(context, &mesh, nullptr, kWhite));

    sdl3d_destroy_render_context(context);
}

TEST_F(SDL3DTexturedFixture, NearestClampSamplerMapsQuadrantsCorrectly)
{
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());

    sdl3d_render_context *context = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &context));

    const std::vector<Uint8> pixels = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255,
    };
    sdl3d_texture2d texture = make_texture_from_pixels(pixels, 2, 2);
    ASSERT_TRUE(sdl3d_set_texture_filter(&texture, SDL3D_TEXTURE_FILTER_NEAREST));
    ASSERT_TRUE(sdl3d_set_texture_wrap(&texture, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP));

    const sdl3d_mesh mesh = make_textured_quad_mesh();

    ASSERT_TRUE(sdl3d_clear_render_context(context, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(context, make_ortho()));
    ASSERT_TRUE(sdl3d_draw_mesh(context, &mesh, &texture, kWhite)) << SDL_GetError();
    ASSERT_TRUE(sdl3d_end_mode_3d(context));

    sdl3d_color sample{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(context, 40, 40, &sample));
    EXPECT_TRUE(pixel_near(sample, kBlue));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(context, 88, 40, &sample));
    EXPECT_TRUE(pixel_near(sample, kYellow));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(context, 40, 88, &sample));
    EXPECT_TRUE(pixel_near(sample, kRed));
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(context, 88, 88, &sample));
    EXPECT_TRUE(pixel_near(sample, kGreen));

    sdl3d_free_texture(&texture);
    sdl3d_destroy_render_context(context);
}

TEST_F(SDL3DTexturedFixture, BilinearSamplerBlendsTexelsAtCenter)
{
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());

    sdl3d_render_context *context = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &context));

    const std::vector<Uint8> pixels = {
        0, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
    };
    sdl3d_texture2d texture = make_texture_from_pixels(pixels, 2, 2);
    ASSERT_TRUE(sdl3d_set_texture_filter(&texture, SDL3D_TEXTURE_FILTER_BILINEAR));

    const sdl3d_mesh mesh = make_textured_quad_mesh();

    ASSERT_TRUE(sdl3d_clear_render_context(context, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(context, make_ortho()));
    ASSERT_TRUE(sdl3d_draw_mesh(context, &mesh, &texture, kWhite)) << SDL_GetError();
    ASSERT_TRUE(sdl3d_end_mode_3d(context));

    sdl3d_color center{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(context, 64, 64, &center));
    EXPECT_NEAR((int)center.r, 64, 20);
    EXPECT_NEAR((int)center.g, 64, 20);
    EXPECT_NEAR((int)center.b, 64, 20);

    sdl3d_free_texture(&texture);
    sdl3d_destroy_render_context(context);
}

TEST_F(SDL3DTexturedFixture, PerspectiveCorrectUvInterpolationKeepsNearTexelDominant)
{
    WindowRenderer wr(128, 128);
    ASSERT_TRUE(wr.ok());

    sdl3d_render_context *context = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), nullptr, &context));

    const std::vector<Uint8> pixels = {
        255, 0, 0, 255, 0, 255, 0, 255,
    };
    sdl3d_texture2d texture = make_texture_from_pixels(pixels, 2, 1);
    ASSERT_TRUE(sdl3d_set_texture_filter(&texture, SDL3D_TEXTURE_FILTER_NEAREST));

    const float positions[] = {
        0.0f, 0.0f, -1.0f, 2.0f, 0.0f, -10.0f, 0.0f, 2.0f, -10.0f,
    };
    const float uvs[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
    };
    const unsigned int indices[] = {0, 1, 2};
    sdl3d_mesh mesh{};
    mesh.positions = const_cast<float *>(positions);
    mesh.uvs = const_cast<float *>(uvs);
    mesh.vertex_count = 3;
    mesh.indices = const_cast<unsigned int *>(indices);
    mesh.index_count = 3;
    mesh.material_index = -1;

    const sdl3d_camera3d camera =
        make_perspective(sdl3d_vec3_make(0.0f, 0.0f, 0.0f), sdl3d_vec3_make(0.0f, 0.0f, -1.0f), 60.0f);

    ASSERT_TRUE(sdl3d_clear_render_context(context, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(context, camera));
    ASSERT_TRUE(sdl3d_draw_mesh(context, &mesh, &texture, kWhite)) << SDL_GetError();
    ASSERT_TRUE(sdl3d_end_mode_3d(context));

    const SDL_Point p0 = project_to_screen(context, camera, sdl3d_vec3_make(0.0f, 0.0f, -1.0f));
    const SDL_Point p1 = project_to_screen(context, camera, sdl3d_vec3_make(2.0f, 0.0f, -10.0f));
    const SDL_Point p2 = project_to_screen(context, camera, sdl3d_vec3_make(0.0f, 2.0f, -10.0f));
    const int cx = (p0.x + p1.x + p2.x) / 3;
    const int cy = (p0.y + p1.y + p2.y) / 3;

    sdl3d_color sample{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(context, cx, cy, &sample));
    EXPECT_TRUE(pixel_near(sample, kRed));

    sdl3d_free_texture(&texture);
    sdl3d_destroy_render_context(context);
}

TEST_F(SDL3DTexturedFixture, DrawModelResolvesRelativeTexturePathAndUsesLogicalSize)
{
    WindowRenderer wr(320, 240);
    ASSERT_TRUE(wr.ok());

    sdl3d_render_context_config config;
    sdl3d_init_render_context_config(&config);
    config.logical_width = 64;
    config.logical_height = 64;
    config.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;

    sdl3d_render_context *context = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(wr.window(), wr.renderer(), &config, &context)) << SDL_GetError();
    ASSERT_EQ(64, sdl3d_get_render_context_width(context));
    ASSERT_EQ(64, sdl3d_get_render_context_height(context));

    const std::filesystem::path dir = make_temp_dir("textured_model");
    const std::filesystem::path obj_path = write_model_fixture(dir, kMauve);

    sdl3d_model model{};
    ASSERT_TRUE(sdl3d_load_model_from_file(obj_path.string().c_str(), &model)) << SDL_GetError();

    ASSERT_TRUE(sdl3d_clear_render_context(context, kBlack));
    ASSERT_TRUE(sdl3d_begin_mode_3d(context, make_ortho()));
    ASSERT_TRUE(sdl3d_draw_model(context, &model, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), 1.0f, kWhite)) << SDL_GetError();
    ASSERT_TRUE(sdl3d_end_mode_3d(context));

    sdl3d_color center{};
    ASSERT_TRUE(sdl3d_get_framebuffer_pixel(context, 32, 32, &center));
    EXPECT_TRUE(pixel_near(center, kMauve));

    ASSERT_TRUE(sdl3d_present_render_context(context)) << SDL_GetError();

    sdl3d_free_model(&model);
    sdl3d_destroy_render_context(context);
}
