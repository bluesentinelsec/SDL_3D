#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>

#include <SDL3/SDL_error.h>

#include "sdl3d/model.h"

namespace
{

std::string asset(const char *rel)
{
    return std::string(SDL3D_TEST_ASSETS_DIR) + "/" + rel;
}

float minf(float a, float b)
{
    return a < b ? a : b;
}
float maxf(float a, float b)
{
    return a > b ? a : b;
}

} // namespace

TEST(SDL3DObjLoader, LoadsCubeWithExpectedShape)
{
    sdl3d_model model{};
    const std::string path = asset("models/cube_obj/cube.obj");
    ASSERT_TRUE(sdl3d_load_model_from_file(path.c_str(), &model)) << SDL_GetError();

    // The fixture is one object with one material, 6 quads → 12 triangles →
    // 36 expanded vertices.
    ASSERT_EQ(model.mesh_count, 1);
    EXPECT_EQ(model.meshes[0].vertex_count, 36);
    EXPECT_EQ(model.meshes[0].index_count, 36);

    // Positions should span the unit cube centered at origin.
    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;
    float min_z = 1e9f, max_z = -1e9f;
    for (int i = 0; i < model.meshes[0].vertex_count; ++i)
    {
        const float *p = &model.meshes[0].positions[i * 3];
        min_x = minf(min_x, p[0]);
        max_x = maxf(max_x, p[0]);
        min_y = minf(min_y, p[1]);
        max_y = maxf(max_y, p[1]);
        min_z = minf(min_z, p[2]);
        max_z = maxf(max_z, p[2]);
    }
    EXPECT_FLOAT_EQ(min_x, -0.5f);
    EXPECT_FLOAT_EQ(max_x, 0.5f);
    EXPECT_FLOAT_EQ(min_y, -0.5f);
    EXPECT_FLOAT_EQ(max_y, 0.5f);
    EXPECT_FLOAT_EQ(min_z, -0.5f);
    EXPECT_FLOAT_EQ(max_z, 0.5f);

    // Our fixture declares normals and uvs.
    ASSERT_NE(model.meshes[0].normals, nullptr);
    ASSERT_NE(model.meshes[0].uvs, nullptr);

    // Each face normal has unit length (authored that way).
    for (int i = 0; i < model.meshes[0].vertex_count; ++i)
    {
        const float *n = &model.meshes[0].normals[i * 3];
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        EXPECT_NEAR(len, 1.0f, 1e-5f);
    }

    sdl3d_free_model(&model);
}

TEST(SDL3DObjLoader, PicksUpMaterialFromSiblingMtl)
{
    sdl3d_model model{};
    const std::string path = asset("models/cube_obj/cube.obj");
    ASSERT_TRUE(sdl3d_load_model_from_file(path.c_str(), &model)) << SDL_GetError();

    ASSERT_EQ(model.material_count, 1);
    ASSERT_NE(model.materials[0].name, nullptr);
    EXPECT_STREQ(model.materials[0].name, "CubeMaterial");

    // Kd 0.8 0.6 0.4 from cube.mtl.
    EXPECT_NEAR(model.materials[0].albedo[0], 0.8f, 1e-5f);
    EXPECT_NEAR(model.materials[0].albedo[1], 0.6f, 1e-5f);
    EXPECT_NEAR(model.materials[0].albedo[2], 0.4f, 1e-5f);
    EXPECT_NEAR(model.materials[0].albedo[3], 1.0f, 1e-5f);

    // No texture maps declared in the fixture.
    EXPECT_EQ(model.materials[0].albedo_map, nullptr);
    EXPECT_EQ(model.materials[0].normal_map, nullptr);

    EXPECT_EQ(model.meshes[0].material_index, 0);

    sdl3d_free_model(&model);
}

TEST(SDL3DObjLoader, RejectsUnknownExtension)
{
    sdl3d_model model{};
    EXPECT_FALSE(sdl3d_load_model_from_file("whatever.xyz", &model));
    EXPECT_EQ(model.mesh_count, 0);
    EXPECT_EQ(model.meshes, nullptr);
}

TEST(SDL3DObjLoader, GltfAndFbxRejectMissingFiles)
{
    sdl3d_model m{};
    EXPECT_FALSE(sdl3d_load_model_from_file("foo.gltf", &m));
    EXPECT_FALSE(sdl3d_load_model_from_file("foo.glb", &m));
    EXPECT_FALSE(sdl3d_load_model_from_file("foo.fbx", &m));
}

TEST(SDL3DObjLoader, MissingFileFails)
{
    sdl3d_model m{};
    EXPECT_FALSE(sdl3d_load_model_from_file("/nonexistent/does-not-exist.obj", &m));
    EXPECT_EQ(m.mesh_count, 0);
}

TEST(SDL3DObjLoader, FreeIsIdempotent)
{
    sdl3d_model m{};
    sdl3d_free_model(&m);
    sdl3d_free_model(&m);
}
