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
struct TestVec3
{
    float x;
    float y;
    float z;
};

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

TestVec3 LoadPosition(const sdl3d_mesh &mesh, int vertex_index)
{
    return {mesh.positions[vertex_index * 3], mesh.positions[vertex_index * 3 + 1],
            mesh.positions[vertex_index * 3 + 2]};
}

TestVec3 LoadNormal(const sdl3d_mesh &mesh, int vertex_index)
{
    return {mesh.normals[vertex_index * 3], mesh.normals[vertex_index * 3 + 1], mesh.normals[vertex_index * 3 + 2]};
}

TestVec3 Subtract(TestVec3 a, TestVec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

TestVec3 Cross(TestVec3 a, TestVec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float Dot(TestVec3 a, TestVec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

TestVec3 Normalize(TestVec3 v)
{
    const float len = SDL_sqrtf(Dot(v, v));
    if (len <= 0.000001f)
    {
        return {0.0f, 0.0f, 0.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
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

TEST(SDL3DLevelBuilder, MeshSectorIdsMatchSectorCount)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f),
                                    MakeSquareSector(8.0f, 0.0f, 12.0f, 4.0f)};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(sectors, 2, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    EXPECT_EQ(level.sector_count, 2);
    ASSERT_NE(level.mesh_sector_ids, nullptr);

    for (int i = 0; i < level.model.mesh_count; ++i)
    {
        EXPECT_GE(level.mesh_sector_ids[i], 0);
        EXPECT_LT(level.mesh_sector_ids[i], level.sector_count);
    }

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelBuilder, InteriorFacingTrianglesMatchStoredNormals)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f)};
    const TestVec3 room_center = {2.0f, 1.5f, 2.0f};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(sectors, 1, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    for (int mesh_index = 0; mesh_index < level.model.mesh_count; ++mesh_index)
    {
        const sdl3d_mesh &mesh = level.model.meshes[mesh_index];
        ASSERT_NE(mesh.positions, nullptr);
        ASSERT_NE(mesh.normals, nullptr);
        ASSERT_NE(mesh.indices, nullptr);

        for (int i = 0; i < mesh.index_count; i += 3)
        {
            const int i0 = static_cast<int>(mesh.indices[i]);
            const int i1 = static_cast<int>(mesh.indices[i + 1]);
            const int i2 = static_cast<int>(mesh.indices[i + 2]);

            const TestVec3 p0 = LoadPosition(mesh, i0);
            const TestVec3 p1 = LoadPosition(mesh, i1);
            const TestVec3 p2 = LoadPosition(mesh, i2);
            const TestVec3 tri_center = {(p0.x + p1.x + p2.x) / 3.0f, (p0.y + p1.y + p2.y) / 3.0f,
                                         (p0.z + p1.z + p2.z) / 3.0f};
            const TestVec3 face_normal = Normalize(Cross(Subtract(p1, p0), Subtract(p2, p0)));
            const TestVec3 to_interior = Normalize(Subtract(room_center, tri_center));

            const TestVec3 n0 = LoadNormal(mesh, i0);
            const TestVec3 n1 = LoadNormal(mesh, i1);
            const TestVec3 n2 = LoadNormal(mesh, i2);
            const TestVec3 avg_normal =
                Normalize({(n0.x + n1.x + n2.x) / 3.0f, (n0.y + n1.y + n2.y) / 3.0f, (n0.z + n1.z + n2.z) / 3.0f});

            EXPECT_GT(Dot(face_normal, to_interior), 0.70f) << "Triangle should face the playable interior";
            EXPECT_GT(Dot(face_normal, avg_normal), 0.95f) << "Stored normals should match front-face winding";
        }
    }

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelBuilder, DetectsPortalsBetweenAdjacentSectors)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    /* Two rooms sharing an edge at x=4, z=[1..3]. */
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f), MakeSquareSector(4.0f, 1.0f, 8.0f, 3.0f)};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(sectors, 2, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    EXPECT_GT(level.portal_count, 0);
    ASSERT_NE(level.portals, nullptr);

    /* Verify at least one portal connects sector 0 and 1. */
    bool found = false;
    for (int i = 0; i < level.portal_count; i++)
    {
        const sdl3d_level_portal &p = level.portals[i];
        if ((p.sector_a == 0 && p.sector_b == 1) || (p.sector_a == 1 && p.sector_b == 0))
        {
            found = true;
            EXPECT_LE(p.floor_y, p.ceil_y);
        }
    }
    EXPECT_TRUE(found) << "Expected portal between sector 0 and 1";

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelBuilder, NoPortalsBetweenDisjointSectors)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    /* Two rooms with no shared edge. */
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f),
                                    MakeSquareSector(10.0f, 0.0f, 14.0f, 4.0f)};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(sectors, 2, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    EXPECT_EQ(level.portal_count, 0);

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelVisibility, FindSectorReturnsCorrectSector)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f), MakeSquareSector(4.0f, 0.0f, 8.0f, 4.0f)};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(sectors, 2, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    EXPECT_EQ(sdl3d_level_find_sector(&level, sectors, 2.0f, 2.0f), 0);
    EXPECT_EQ(sdl3d_level_find_sector(&level, sectors, 6.0f, 2.0f), 1);
    EXPECT_EQ(sdl3d_level_find_sector(&level, sectors, 20.0f, 20.0f), -1);

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelVisibility, VisibilityFromSectorZeroSeesNeighbor)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    /* Three rooms in a line: [0]--[1]--[2] */
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f), MakeSquareSector(4.0f, 0.0f, 8.0f, 4.0f),
                                    MakeSquareSector(8.0f, 0.0f, 12.0f, 4.0f)};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(sectors, 3, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    bool visible[3] = {};
    sdl3d_visibility_result vis;
    vis.sector_visible = visible;
    vis.visible_count = 0;

    /* Camera in sector 0, looking toward +X (toward sectors 1 and 2). */
    sdl3d_vec3 cam_pos = sdl3d_vec3_make(2.0f, 1.5f, 2.0f);
    sdl3d_vec3 cam_dir = sdl3d_vec3_make(1.0f, 0.0f, 0.0f);

    /* No frustum planes — just test BFS reachability. */
    sdl3d_level_compute_visibility(&level, 0, cam_pos, cam_dir, nullptr, &vis);

    EXPECT_TRUE(visible[0]) << "Current sector should be visible";
    EXPECT_TRUE(visible[1]) << "Adjacent sector should be visible";
    /* Sector 2 should also be reachable through sector 1. */
    EXPECT_TRUE(visible[2]) << "Sector 2 reachable through sector 1";
    EXPECT_EQ(vis.visible_count, 3);

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelVisibility, OutsideSectorsFallsBackToAllVisible)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f)};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(sectors, 1, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    bool visible[1] = {};
    sdl3d_visibility_result vis;
    vis.sector_visible = visible;
    vis.visible_count = 0;

    sdl3d_vec3 cam_pos = sdl3d_vec3_make(100.0f, 1.5f, 100.0f);
    sdl3d_vec3 cam_dir = sdl3d_vec3_make(1.0f, 0.0f, 0.0f);

    sdl3d_level_compute_visibility(&level, -1, cam_pos, cam_dir, nullptr, &vis);

    EXPECT_TRUE(visible[0]) << "Fallback should mark all sectors visible";
    EXPECT_EQ(vis.visible_count, 1);

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
