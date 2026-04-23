#include <gtest/gtest.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

extern "C"
{
#include "sdl3d/drawing3d.h"
#include "sdl3d/level.h"
#include "sdl3d/math.h"
}

namespace
{
sdl3d_sector MakeSquareSector(float min_x, float min_z, float max_x, float max_z)
{
    sdl3d_sector sector{};
    sector.points[0][0] = min_x;
    sector.points[0][1] = min_z;
    sector.points[1][0] = max_x;
    sector.points[1][1] = min_z;
    sector.points[2][0] = max_x;
    sector.points[2][1] = max_z;
    sector.points[3][0] = min_x;
    sector.points[3][1] = max_z;
    sector.num_points = 4;
    sector.floor_y = 0.0f;
    sector.ceil_y = 3.0f;
    sector.floor_material = 0;
    sector.ceil_material = 1;
    sector.wall_material = 2;
    return sector;
}

sdl3d_level_material MakeLevelMaterial(const char *texture)
{
    sdl3d_level_material material{};
    material.albedo[0] = 1.0f;
    material.albedo[1] = 1.0f;
    material.albedo[2] = 1.0f;
    material.albedo[3] = 1.0f;
    material.roughness = 1.0f;
    material.texture = texture;
    material.tex_scale = 4.0f;
    return material;
}

} // namespace

TEST(SDL3DLevelBuilder, BuildsIndependentSectorMaterialChunks)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f),
                                    MakeSquareSector(8.0f, 0.0f, 12.0f, 4.0f)};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(sectors, 2, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    EXPECT_EQ(level.model.material_count, 3);
    EXPECT_EQ(level.model.mesh_count, 6);

    for (int i = 0; i < level.model.mesh_count; ++i)
    {
        const sdl3d_mesh &mesh = level.model.meshes[i];
        EXPECT_TRUE(mesh.has_local_bounds);
        EXPECT_GT(mesh.vertex_count, 0);
        EXPECT_GT(mesh.index_count, 0);
        EXPECT_GE(mesh.material_index, 0);
        EXPECT_LT(mesh.material_index, level.model.material_count);
        EXPECT_LE(mesh.local_bounds.min.x, mesh.local_bounds.max.x);
        EXPECT_LE(mesh.local_bounds.min.y, mesh.local_bounds.max.y);
        EXPECT_LE(mesh.local_bounds.min.z, mesh.local_bounds.max.z);
    }

    sdl3d_free_level(&level);
}

TEST(SDL3DDrawModelCulling, SkipsOffscreenChunkBeforeMaterialValidation)
{
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    ASSERT_TRUE(SDL_CreateWindowAndRenderer("level culling", 64, 64, 0, &window, &renderer)) << SDL_GetError();

    sdl3d_render_context *ctx = nullptr;
    ASSERT_TRUE(sdl3d_create_render_context(window, renderer, nullptr, &ctx)) << SDL_GetError();

    sdl3d_camera3d cam{};
    cam.position = sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    cam.target = sdl3d_vec3_make(0.0f, 0.0f, -1.0f);
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 60.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;
    ASSERT_TRUE(sdl3d_begin_mode_3d(ctx, cam)) << SDL_GetError();

    sdl3d_model model{};
    sdl3d_mesh meshes[2] = {};

    meshes[0].material_index = 99;
    meshes[0].has_local_bounds = true;
    meshes[0].local_bounds.min = sdl3d_vec3_make(50.0f, -0.25f, -5.5f);
    meshes[0].local_bounds.max = sdl3d_vec3_make(52.0f, 0.25f, -4.5f);

    meshes[1].material_index = 99;
    meshes[1].has_local_bounds = true;
    meshes[1].local_bounds.min = sdl3d_vec3_make(-52.0f, -0.25f, -5.5f);
    meshes[1].local_bounds.max = sdl3d_vec3_make(-50.0f, 0.25f, -4.5f);

    model.meshes = meshes;
    model.mesh_count = 2;
    model.material_count = 0;

    SDL_ClearError();
    EXPECT_TRUE(
        sdl3d_draw_model(ctx, &model, sdl3d_vec3_make(0.0f, 0.0f, 0.0f), 1.0f, (sdl3d_color){255, 255, 255, 255}))
        << SDL_GetError();
    EXPECT_TRUE(sdl3d_end_mode_3d(ctx)) << SDL_GetError();

    sdl3d_destroy_render_context(ctx);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
