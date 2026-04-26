#include <gtest/gtest.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

extern "C"
{
#include "sdl3d/drawing3d.h"
#include "sdl3d/level.h"
#include "sdl3d/math.h"
#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"
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

const sdl3d_mesh *FindSectorMaterialMesh(const sdl3d_level &level, int sector_id, int material_index)
{
    for (int i = 0; i < level.model.mesh_count; ++i)
    {
        if (level.mesh_sector_ids[i] == sector_id && level.model.meshes[i].material_index == material_index)
        {
            return &level.model.meshes[i];
        }
    }
    return nullptr;
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

TEST(SDL3DSectorMetadata, PushVelocityReturnsAuthoredVectorAndNullZero)
{
    sdl3d_sector sector = MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f);
    sector.push_velocity[0] = 1.25f;
    sector.push_velocity[1] = -0.5f;
    sector.push_velocity[2] = 3.0f;

    sdl3d_vec3 velocity = sdl3d_sector_push_velocity(&sector);
    EXPECT_FLOAT_EQ(velocity.x, 1.25f);
    EXPECT_FLOAT_EQ(velocity.y, -0.5f);
    EXPECT_FLOAT_EQ(velocity.z, 3.0f);

    sdl3d_vec3 zero = sdl3d_sector_push_velocity(nullptr);
    EXPECT_FLOAT_EQ(zero.x, 0.0f);
    EXPECT_FLOAT_EQ(zero.y, 0.0f);
    EXPECT_FLOAT_EQ(zero.z, 0.0f);
}

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

TEST(SDL3DLevelBuilder, AllowsOpenCeilingBySkippingCeilingGeometry)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    sdl3d_sector sector = MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f);
    sdl3d_level level{};

    sector.ceil_material = -1;

    ASSERT_TRUE(sdl3d_build_level(&sector, 1, materials, 3, nullptr, 0, &level)) << SDL_GetError();
    EXPECT_EQ(level.model.mesh_count, 2);

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelBuilder, BakesLightmapAtlasAndMeshUVs)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial(nullptr), MakeLevelMaterial(nullptr),
                                              MakeLevelMaterial(nullptr)};
    const sdl3d_sector sector = MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f);
    const sdl3d_level_light light = {{2.0f, 2.0f, 2.0f}, {1.0f, 0.8f, 0.6f}, 3.0f, 8.0f};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(&sector, 1, materials, 3, &light, 1, &level)) << SDL_GetError();
    EXPECT_NE(level.lightmap_pixels, nullptr);
    EXPECT_GT(level.lightmap_width, 0);
    EXPECT_GT(level.lightmap_height, 0);
    EXPECT_NE(level.lightmap_texture.pixels, nullptr);

    for (int i = 0; i < level.model.mesh_count; ++i)
    {
        const sdl3d_mesh &mesh = level.model.meshes[i];
        ASSERT_NE(mesh.lightmap_uvs, nullptr);
        for (int v = 0; v < mesh.vertex_count; ++v)
        {
            EXPECT_GE(mesh.lightmap_uvs[v * 2 + 0], 0.0f);
            EXPECT_LE(mesh.lightmap_uvs[v * 2 + 0], 1.0f);
            EXPECT_GE(mesh.lightmap_uvs[v * 2 + 1], 0.0f);
            EXPECT_LE(mesh.lightmap_uvs[v * 2 + 1], 1.0f);
        }
    }

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelRuntimeGeometry, SetSectorGeometryUpdatesSectorAndMeshes)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f), MakeSquareSector(4.0f, 0.0f, 8.0f, 4.0f)};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(sectors, 2, materials, 3, nullptr, 0, &level)) << SDL_GetError();
    const sdl3d_mesh *old_floor_mesh = FindSectorMaterialMesh(level, 1, sectors[1].floor_material);
    ASSERT_NE(old_floor_mesh, nullptr);
    EXPECT_NEAR(old_floor_mesh->local_bounds.min.y, 0.0f, 1e-4f);

    sdl3d_sector_geometry geometry{};
    geometry.floor_y = 1.25f;
    geometry.ceil_y = 5.0f;
    geometry.floor_normal[1] = 1.0f;
    geometry.ceil_normal[1] = -1.0f;

    ASSERT_TRUE(sdl3d_level_set_sector_geometry(&level, sectors, 2, 1, &geometry, materials, 3, nullptr, 0))
        << SDL_GetError();

    EXPECT_FLOAT_EQ(sectors[1].floor_y, 1.25f);
    EXPECT_FLOAT_EQ(sectors[1].ceil_y, 5.0f);
    EXPECT_EQ(level.sector_count, 2);
    EXPECT_GT(level.portal_count, 0);

    const sdl3d_mesh *new_floor_mesh = FindSectorMaterialMesh(level, 1, sectors[1].floor_material);
    ASSERT_NE(new_floor_mesh, nullptr);
    EXPECT_NEAR(new_floor_mesh->local_bounds.min.y, 1.25f, 1e-4f);
    EXPECT_NEAR(new_floor_mesh->local_bounds.max.y, 1.25f, 1e-4f);

    bool portal_updated = false;
    for (int i = 0; i < level.portal_count; ++i)
    {
        const sdl3d_level_portal &portal = level.portals[i];
        if ((portal.sector_a == 0 && portal.sector_b == 1) || (portal.sector_a == 1 && portal.sector_b == 0))
        {
            portal_updated = true;
            EXPECT_NEAR(portal.floor_y, 1.25f, 1e-4f);
            EXPECT_NEAR(portal.ceil_y, 3.0f, 1e-4f);
        }
    }
    EXPECT_TRUE(portal_updated);

    EXPECT_EQ(sdl3d_level_find_walkable_sector(&level, sectors, 6.0f, 2.0f, 1.0f, 0.5f, 1.6f), 1);

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelRuntimeGeometry, SetSectorGeometrySupportsSlopedFloors)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 10.0f, 4.0f)};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(sectors, 1, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    sdl3d_sector_geometry geometry{};
    geometry.floor_y = 1.0f;
    geometry.ceil_y = 6.0f;
    geometry.floor_normal[0] = -0.19611613f;
    geometry.floor_normal[1] = 0.98058069f;
    geometry.ceil_normal[1] = -1.0f;

    ASSERT_TRUE(sdl3d_level_set_sector_geometry(&level, sectors, 1, 0, &geometry, materials, 3, nullptr, 0))
        << SDL_GetError();

    EXPECT_NEAR(sdl3d_sector_floor_at(&sectors[0], 0.0f, 2.0f), 0.0f, 1e-4f);
    EXPECT_NEAR(sdl3d_sector_floor_at(&sectors[0], 10.0f, 2.0f), 2.0f, 1e-4f);

    const sdl3d_mesh *floor_mesh = FindSectorMaterialMesh(level, 0, sectors[0].floor_material);
    ASSERT_NE(floor_mesh, nullptr);
    for (int i = 0; i < floor_mesh->vertex_count; ++i)
    {
        float x = floor_mesh->positions[i * 3 + 0];
        float y = floor_mesh->positions[i * 3 + 1];
        float z = floor_mesh->positions[i * 3 + 2];
        EXPECT_NEAR(y, sdl3d_sector_floor_at(&sectors[0], x, z), 1e-4f);
    }

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelRuntimeGeometry, SetSectorGeometryLeavesStateUnchangedOnInvalidUpdate)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f)};
    sdl3d_level level{};

    ASSERT_TRUE(sdl3d_build_level(sectors, 1, materials, 3, nullptr, 0, &level)) << SDL_GetError();
    const sdl3d_mesh *floor_mesh = FindSectorMaterialMesh(level, 0, sectors[0].floor_material);
    ASSERT_NE(floor_mesh, nullptr);
    const float old_floor_bound = floor_mesh->local_bounds.min.y;

    sdl3d_sector_geometry invalid_geometry{};
    invalid_geometry.floor_y = 3.0f;
    invalid_geometry.ceil_y = 3.0f;
    invalid_geometry.floor_normal[1] = 1.0f;
    invalid_geometry.ceil_normal[1] = -1.0f;

    EXPECT_FALSE(sdl3d_level_set_sector_geometry(&level, sectors, 1, 0, &invalid_geometry, materials, 3, nullptr, 0));
    EXPECT_FLOAT_EQ(sectors[0].floor_y, 0.0f);
    EXPECT_FLOAT_EQ(sectors[0].ceil_y, 3.0f);

    floor_mesh = FindSectorMaterialMesh(level, 0, sectors[0].floor_material);
    ASSERT_NE(floor_mesh, nullptr);
    EXPECT_NEAR(floor_mesh->local_bounds.min.y, old_floor_bound, 1e-4f);

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

/* ================================================================== */
/* Sector queries (find_sector_at, walkable, support, point_inside)   */
/* ================================================================== */

namespace
{
sdl3d_sector MakeStackedSector(float min_x, float min_z, float max_x, float max_z, float floor_y, float ceil_y)
{
    sdl3d_sector s = MakeSquareSector(min_x, min_z, max_x, max_z);
    s.floor_y = floor_y;
    s.ceil_y = ceil_y;
    return s;
}

sdl3d_sector MakeRampSector(float min_x, float min_z, float max_x, float max_z)
{
    sdl3d_sector s = MakeSquareSector(min_x, min_z, max_x, max_z);
    s.floor_y = 1.0f;
    s.ceil_y = 5.0f;
    s.floor_normal[0] = -0.19611613f;
    s.floor_normal[1] = 0.98058069f;
    s.floor_normal[2] = 0.0f;
    return s;
}
} // namespace

TEST(SDL3DSectorQueries, SectorPlaneHelpersDefaultFlatNormals)
{
    sdl3d_sector sector = MakeStackedSector(0, 0, 10, 10, 2, 7);

    sdl3d_vec3 floor_normal = sdl3d_sector_floor_normal(&sector);
    sdl3d_vec3 ceil_normal = sdl3d_sector_ceil_normal(&sector);

    EXPECT_FLOAT_EQ(floor_normal.x, 0.0f);
    EXPECT_FLOAT_EQ(floor_normal.y, 1.0f);
    EXPECT_FLOAT_EQ(floor_normal.z, 0.0f);
    EXPECT_FLOAT_EQ(ceil_normal.x, 0.0f);
    EXPECT_FLOAT_EQ(ceil_normal.y, -1.0f);
    EXPECT_FLOAT_EQ(ceil_normal.z, 0.0f);
    EXPECT_FLOAT_EQ(sdl3d_sector_floor_at(&sector, 0.0f, 0.0f), 2.0f);
    EXPECT_FLOAT_EQ(sdl3d_sector_ceil_at(&sector, 10.0f, 10.0f), 7.0f);
}

TEST(SDL3DSectorQueries, SectorFloorAtEvaluatesSlopePlane)
{
    sdl3d_sector ramp = MakeRampSector(0, 0, 10, 4);

    EXPECT_NEAR(sdl3d_sector_floor_at(&ramp, 0.0f, 2.0f), 0.0f, 1e-4f);
    EXPECT_NEAR(sdl3d_sector_floor_at(&ramp, 5.0f, 2.0f), 1.0f, 1e-4f);
    EXPECT_NEAR(sdl3d_sector_floor_at(&ramp, 10.0f, 2.0f), 2.0f, 1e-4f);
}

TEST(SDL3DLevelBuilder, SlopedFloorMeshUsesPlaneHeightsAndNormals)
{
    const sdl3d_level_material mats[] = {MakeLevelMaterial("a.png"), MakeLevelMaterial("b.png"),
                                         MakeLevelMaterial("c.png")};
    const sdl3d_sector ramp = MakeRampSector(0, 0, 10, 4);
    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(&ramp, 1, mats, 3, nullptr, 0, &level)) << SDL_GetError();

    const sdl3d_mesh *floor_mesh = nullptr;
    for (int i = 0; i < level.model.mesh_count; ++i)
    {
        if (level.model.meshes[i].material_index == ramp.floor_material)
        {
            floor_mesh = &level.model.meshes[i];
            break;
        }
    }

    ASSERT_NE(floor_mesh, nullptr);
    ASSERT_GE(floor_mesh->vertex_count, 4);
    for (int i = 0; i < floor_mesh->vertex_count; ++i)
    {
        float x = floor_mesh->positions[i * 3 + 0];
        float y = floor_mesh->positions[i * 3 + 1];
        float z = floor_mesh->positions[i * 3 + 2];
        EXPECT_NEAR(y, sdl3d_sector_floor_at(&ramp, x, z), 1e-4f);
        EXPECT_NEAR(floor_mesh->normals[i * 3 + 0], sdl3d_sector_floor_normal(&ramp).x, 1e-4f);
        EXPECT_NEAR(floor_mesh->normals[i * 3 + 1], sdl3d_sector_floor_normal(&ramp).y, 1e-4f);
        EXPECT_NEAR(floor_mesh->normals[i * 3 + 2], sdl3d_sector_floor_normal(&ramp).z, 1e-4f);
    }

    sdl3d_free_level(&level);
}

TEST(SDL3DSectorQueries, FindSectorAtPicksHighestFloorContaining)
{
    /* Two sectors at the same XZ but stacked: lower (floor=0,ceil=1) and
     * upper (floor=2,ceil=4). feet_y=0.5 sits in the lower; feet_y=2.5 in
     * the upper; feet_y between them returns -1. */
    const sdl3d_level_material mats[] = {MakeLevelMaterial("a.png"), MakeLevelMaterial("b.png"),
                                         MakeLevelMaterial("c.png")};
    const sdl3d_sector sectors[] = {MakeStackedSector(0, 0, 4, 4, 0, 1), MakeStackedSector(0, 0, 4, 4, 2, 4)};
    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(sectors, 2, mats, 3, nullptr, 0, &level));

    EXPECT_EQ(sdl3d_level_find_sector_at(&level, sectors, 2, 2, 0.5f), 0);
    EXPECT_EQ(sdl3d_level_find_sector_at(&level, sectors, 2, 2, 2.5f), 1);
    EXPECT_EQ(sdl3d_level_find_sector_at(&level, sectors, 2, 2, 1.5f), -1);
    EXPECT_EQ(sdl3d_level_find_sector_at(&level, sectors, 20, 20, 0.5f), -1);

    sdl3d_free_level(&level);
}

TEST(SDL3DSectorQueries, QueriesUseSlopedFloorHeightAtPoint)
{
    const sdl3d_level_material mats[] = {MakeLevelMaterial("a.png"), MakeLevelMaterial("b.png"),
                                         MakeLevelMaterial("c.png")};
    const sdl3d_sector sectors[] = {MakeRampSector(0, 0, 10, 4)};
    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(sectors, 1, mats, 3, nullptr, 0, &level));

    EXPECT_EQ(sdl3d_level_find_sector_at(&level, sectors, 1.0f, 2.0f, 0.25f), 0);
    EXPECT_EQ(sdl3d_level_find_sector_at(&level, sectors, 9.0f, 2.0f, 0.25f), -1);
    EXPECT_EQ(sdl3d_level_find_walkable_sector(&level, sectors, 9.0f, 2.0f, 1.6f, 0.4f, 1.6f), 0);
    EXPECT_EQ(sdl3d_level_find_walkable_sector(&level, sectors, 9.0f, 2.0f, 0.0f, 0.4f, 1.6f), -1);
    EXPECT_TRUE(sdl3d_level_point_inside(&level, sectors, 9.0f, 2.0f, 2.0f));
    EXPECT_FALSE(sdl3d_level_point_inside(&level, sectors, 9.0f, 1.0f, 2.0f));

    sdl3d_free_level(&level);
}

TEST(SDL3DSectorQueries, FindWalkableAcceptsStepUpButRejectsTooHigh)
{
    /* Stair: low (floor=0) adjacent to mid (floor=1.0). With step=1.1
     * the mid is reachable from the low. With step=0.5 it is not. */
    const sdl3d_level_material mats[] = {MakeLevelMaterial("a.png"), MakeLevelMaterial("b.png"),
                                         MakeLevelMaterial("c.png")};
    const sdl3d_sector sectors[] = {MakeStackedSector(0, 0, 4, 4, 0, 4), MakeStackedSector(4, 0, 8, 4, 1, 5)};
    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(sectors, 2, mats, 3, nullptr, 0, &level));

    /* From the low sector (feet at floor 0), the mid sector is one step up. */
    EXPECT_EQ(sdl3d_level_find_walkable_sector(&level, sectors, 6, 2, 0.0f, 1.1f, 1.6f), 1);
    EXPECT_EQ(sdl3d_level_find_walkable_sector(&level, sectors, 6, 2, 0.0f, 0.5f, 1.6f), -1);

    /* Headroom rejection: a 1.0-tall sector should be rejected for a 1.6 player. */
    const sdl3d_sector tight[] = {MakeStackedSector(0, 0, 4, 4, 0, 1.0f)};
    sdl3d_level tight_level{};
    ASSERT_TRUE(sdl3d_build_level(tight, 1, mats, 3, nullptr, 0, &tight_level));
    EXPECT_EQ(sdl3d_level_find_walkable_sector(&tight_level, tight, 2, 2, 0.0f, 1.0f, 1.6f), -1);
    sdl3d_free_level(&tight_level);

    sdl3d_free_level(&level);
}

TEST(SDL3DSectorQueries, FindSupportPicksHighestFloorAtOrBelow)
{
    /* Stacked: floors at 0 and 2. From feet_y=3 the support is the
     * upper floor (2); from feet_y=1.5 the support is the lower (0);
     * from feet_y=-1 there is no support. */
    const sdl3d_level_material mats[] = {MakeLevelMaterial("a.png"), MakeLevelMaterial("b.png"),
                                         MakeLevelMaterial("c.png")};
    const sdl3d_sector sectors[] = {MakeStackedSector(0, 0, 4, 4, 0, 1.7f), MakeStackedSector(0, 0, 4, 4, 2, 4)};
    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(sectors, 2, mats, 3, nullptr, 0, &level));

    EXPECT_EQ(sdl3d_level_find_support_sector(&level, sectors, 2, 2, 3.0f, 1.6f), 1);
    EXPECT_EQ(sdl3d_level_find_support_sector(&level, sectors, 2, 2, 1.5f, 1.6f), 0);
    EXPECT_EQ(sdl3d_level_find_support_sector(&level, sectors, 2, 2, -1.0f, 1.6f), -1);

    sdl3d_free_level(&level);
}

TEST(SDL3DSectorQueries, PointInsideRespectsXZAndY)
{
    const sdl3d_level_material mats[] = {MakeLevelMaterial("a.png"), MakeLevelMaterial("b.png"),
                                         MakeLevelMaterial("c.png")};
    const sdl3d_sector sectors[] = {MakeStackedSector(0, 0, 4, 4, 0, 3)};
    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(sectors, 1, mats, 3, nullptr, 0, &level));

    EXPECT_TRUE(sdl3d_level_point_inside(&level, sectors, 2, 1.5f, 2));
    EXPECT_FALSE(sdl3d_level_point_inside(&level, sectors, 2, 5.0f, 2));  /* above ceil */
    EXPECT_FALSE(sdl3d_level_point_inside(&level, sectors, 2, -1.0f, 2)); /* below floor */
    EXPECT_FALSE(sdl3d_level_point_inside(&level, sectors, 20, 1.5f, 2)); /* outside xz */

    sdl3d_free_level(&level);
}

TEST(SDL3DSectorQueries, NullArgsReturnSafeDefaults)
{
    EXPECT_EQ(sdl3d_level_find_sector_at(nullptr, nullptr, 0, 0, 0), -1);
    EXPECT_EQ(sdl3d_level_find_walkable_sector(nullptr, nullptr, 0, 0, 0, 1, 1), -1);
    EXPECT_EQ(sdl3d_level_find_support_sector(nullptr, nullptr, 0, 0, 0, 1), -1);
    EXPECT_FALSE(sdl3d_level_point_inside(nullptr, nullptr, 0, 0, 0));
}

namespace
{
struct SectorSignalCapture
{
    int count = 0;
    int sector_id = -99;
    int previous_sector_id = -99;
    int ambient_id = -99;
    int previous_ambient_id = -99;
};

void CaptureEnteredSector(void *userdata, int signal_id, const sdl3d_properties *payload)
{
    auto *capture = static_cast<SectorSignalCapture *>(userdata);
    EXPECT_EQ(signal_id, SDL3D_SIGNAL_ENTERED_SECTOR);
    ASSERT_NE(capture, nullptr);
    ASSERT_NE(payload, nullptr);

    capture->count++;
    capture->sector_id = sdl3d_properties_get_int(payload, "sector_id", -99);
    capture->previous_sector_id = sdl3d_properties_get_int(payload, "previous_sector_id", -99);
    capture->ambient_id = sdl3d_properties_get_int(payload, "ambient_sound_id", -99);
    capture->previous_ambient_id = sdl3d_properties_get_int(payload, "previous_ambient_sound_id", -99);
}
} // namespace

TEST(SDL3DSectorWatcher, EmitsEnteredSectorWithAmbientPayload)
{
    const sdl3d_level_material mats[] = {MakeLevelMaterial("a.png"), MakeLevelMaterial("b.png"),
                                         MakeLevelMaterial("c.png")};
    sdl3d_sector sectors[] = {MakeStackedSector(0, 0, 4, 4, 0, 4), MakeStackedSector(4, 0, 8, 4, 0, 4)};
    sectors[0].ambient_sound_id = 7;
    sectors[1].ambient_sound_id = 11;
    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(sectors, 2, mats, 3, nullptr, 0, &level));

    sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
    ASSERT_NE(bus, nullptr);
    SectorSignalCapture capture;
    ASSERT_NE(sdl3d_signal_connect(bus, SDL3D_SIGNAL_ENTERED_SECTOR, CaptureEnteredSector, &capture), 0);

    sdl3d_sector_watcher watcher{};
    sdl3d_sector_watcher_init(&watcher);

    EXPECT_TRUE(sdl3d_sector_watcher_update(&watcher, &level, sectors, sdl3d_vec3_make(2, 0, 2), bus));
    EXPECT_EQ(capture.count, 1);
    EXPECT_EQ(capture.sector_id, 0);
    EXPECT_EQ(capture.previous_sector_id, -1);
    EXPECT_EQ(capture.ambient_id, 7);
    EXPECT_EQ(capture.previous_ambient_id, -1);

    EXPECT_FALSE(sdl3d_sector_watcher_update(&watcher, &level, sectors, sdl3d_vec3_make(2.5f, 0, 2), bus));
    EXPECT_EQ(capture.count, 1);

    EXPECT_TRUE(sdl3d_sector_watcher_update(&watcher, &level, sectors, sdl3d_vec3_make(6, 0, 2), bus));
    EXPECT_EQ(capture.count, 2);
    EXPECT_EQ(capture.sector_id, 1);
    EXPECT_EQ(capture.previous_sector_id, 0);
    EXPECT_EQ(capture.ambient_id, 11);
    EXPECT_EQ(capture.previous_ambient_id, 7);

    sdl3d_signal_bus_destroy(bus);
    sdl3d_free_level(&level);
}

TEST(SDL3DSectorWatcher, NullArgsAreSafe)
{
    sdl3d_sector_watcher watcher{};
    sdl3d_sector_watcher_init(&watcher);

    EXPECT_FALSE(sdl3d_sector_watcher_update(nullptr, nullptr, nullptr, sdl3d_vec3_make(0, 0, 0), nullptr));
    EXPECT_FALSE(sdl3d_sector_watcher_update(&watcher, nullptr, nullptr, sdl3d_vec3_make(0, 0, 0), nullptr));
    EXPECT_EQ(watcher.current_sector, -1);
    EXPECT_EQ(watcher.current_ambient_id, -1);
    EXPECT_EQ(watcher.entered_signal_id, SDL3D_SIGNAL_ENTERED_SECTOR);
}

/* ================================================================== */
/* Frustum extraction                                                 */
/* ================================================================== */

TEST(SDL3DExtractFrustumPlanes, IdentityVPYieldsUnitCubeBoundary)
{
    /* For VP = identity, the frustum is the clip-space cube [-1,1]^3. */
    sdl3d_mat4 vp = sdl3d_mat4_identity();
    float planes[6][4];
    sdl3d_extract_frustum_planes(vp, planes);

    /* Plane d should be 1 for each face of the unit cube. */
    for (int i = 0; i < 6; ++i)
    {
        EXPECT_NEAR(planes[i][3], 1.0f, 1e-4f) << "plane " << i;
    }
}

/* ================================================================== */
/* Light sampling                                                     */
/* ================================================================== */

TEST(SDL3DLevelSampleLight, AccumulatesAndAttenuates)
{
    sdl3d_level_light l{};
    l.position[0] = 0;
    l.position[1] = 0;
    l.position[2] = 0;
    l.color[0] = 1.0f;
    l.color[1] = 0.0f;
    l.color[2] = 0.0f;
    l.intensity = 1.0f;
    l.range = 4.0f;

    /* At the center, fully lit (clamped to 255). */
    sdl3d_color at_center = sdl3d_level_sample_light(&l, 1, sdl3d_vec3_make(0.001f, 0, 0));
    EXPECT_GT(at_center.r, 200);
    EXPECT_EQ(at_center.g, 0);

    /* Outside range, no light. */
    sdl3d_color far_away = sdl3d_level_sample_light(&l, 1, sdl3d_vec3_make(10, 0, 0));
    EXPECT_EQ(far_away.r, 0);

    /* Halfway, attenuated. */
    sdl3d_color mid = sdl3d_level_sample_light(&l, 1, sdl3d_vec3_make(2, 0, 0));
    EXPECT_GT(mid.r, 0);
    EXPECT_LT(mid.r, at_center.r);
}

TEST(SDL3DLevelSampleLight, NullOrEmptyReturnsBlack)
{
    sdl3d_color c = sdl3d_level_sample_light(nullptr, 0, sdl3d_vec3_make(0, 0, 0));
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
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

/* ================================================================== */
/* Visibility from camera convenience                                 */
/* ================================================================== */

TEST(SDL3DLevelVisibility, ComputeVisibilityFromCameraBasic)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    /* Two adjacent sectors sharing an edge at x=4. */
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f), MakeSquareSector(4.0f, 0.0f, 8.0f, 4.0f)};
    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(sectors, 2, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    bool visible[2] = {};
    sdl3d_visibility_result vis;
    vis.sector_visible = visible;
    vis.visible_count = 0;

    sdl3d_camera3d cam{};
    cam.position = sdl3d_vec3_make(2.0f, 1.5f, 2.0f);
    cam.target = sdl3d_vec3_make(6.0f, 1.5f, 2.0f); /* looking toward sector 1 */
    cam.up = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fovy = 75.0f;
    cam.projection = SDL3D_CAMERA_PERSPECTIVE;

    sdl3d_level_compute_visibility_from_camera(&level, sectors, &cam, 1280, 720, 0.01f, 1000.0f, &vis);

    EXPECT_TRUE(visible[0]);
    EXPECT_TRUE(visible[1]);
    EXPECT_EQ(vis.visible_count, 2);

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelVisibility, ComputeVisibilityFromCameraNullSafe)
{
    sdl3d_visibility_result vis{};
    sdl3d_camera3d cam{};
    /* Should not crash. */
    sdl3d_level_compute_visibility_from_camera(nullptr, nullptr, &cam, 1280, 720, 0.01f, 1000.0f, &vis);
    sdl3d_level_compute_visibility_from_camera(nullptr, nullptr, nullptr, 1280, 720, 0.01f, 1000.0f, &vis);
}

/* ================================================================== */
/* Point trace                                                        */
/* ================================================================== */

TEST(SDL3DLevelTrace, TraceInsideSectorReachesEnd)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 20.0f, 20.0f)};
    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(sectors, 1, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    sdl3d_vec3 origin = sdl3d_vec3_make(10.0f, 1.5f, 10.0f);
    sdl3d_vec3 dir = sdl3d_vec3_make(1.0f, 0.0f, 0.0f);
    sdl3d_level_trace_result r = sdl3d_level_trace_point(&level, sectors, origin, dir, 2.0f);

    EXPECT_FALSE(r.hit);
    EXPECT_NEAR(r.end_point.x, 12.0f, 0.3f);
    EXPECT_FLOAT_EQ(r.fraction, 1.0f);

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelTrace, TraceExitsSectorHits)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f)};
    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(sectors, 1, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    sdl3d_vec3 origin = sdl3d_vec3_make(2.0f, 1.5f, 2.0f);
    sdl3d_vec3 dir = sdl3d_vec3_make(1.0f, 0.0f, 0.0f);
    sdl3d_level_trace_result r = sdl3d_level_trace_point(&level, sectors, origin, dir, 20.0f);

    EXPECT_TRUE(r.hit);
    /* Should stop near x=4 (the sector boundary). */
    EXPECT_GT(r.end_point.x, 3.5f);
    EXPECT_LT(r.end_point.x, 4.5f);
    EXPECT_GT(r.fraction, 0.0f);
    EXPECT_LT(r.fraction, 1.0f);

    sdl3d_free_level(&level);
}

TEST(SDL3DLevelTrace, TraceNullSafe)
{
    sdl3d_vec3 o = sdl3d_vec3_make(0, 0, 0);
    sdl3d_vec3 d = sdl3d_vec3_make(1, 0, 0);
    sdl3d_level_trace_result r = sdl3d_level_trace_point(nullptr, nullptr, o, d, 10.0f);
    EXPECT_FALSE(r.hit);
    EXPECT_FLOAT_EQ(r.fraction, 0.0f);
}

TEST(SDL3DLevelTrace, TraceZeroDistanceReturnsOrigin)
{
    const sdl3d_level_material materials[] = {MakeLevelMaterial("floor.png"), MakeLevelMaterial("ceil.png"),
                                              MakeLevelMaterial("wall.png")};
    const sdl3d_sector sectors[] = {MakeSquareSector(0.0f, 0.0f, 4.0f, 4.0f)};
    sdl3d_level level{};
    ASSERT_TRUE(sdl3d_build_level(sectors, 1, materials, 3, nullptr, 0, &level)) << SDL_GetError();

    sdl3d_vec3 origin = sdl3d_vec3_make(2.0f, 1.5f, 2.0f);
    sdl3d_vec3 dir = sdl3d_vec3_make(1.0f, 0.0f, 0.0f);
    sdl3d_level_trace_result r = sdl3d_level_trace_point(&level, sectors, origin, dir, 0.0f);

    EXPECT_FALSE(r.hit);
    EXPECT_FLOAT_EQ(r.fraction, 0.0f);

    sdl3d_free_level(&level);
}
