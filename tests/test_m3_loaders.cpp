/*
 * Comprehensive tests for M3: glTF/GLB loader, FBX loader, mipmap
 * generation + trilinear sampling, model dispatch, material parity.
 *
 * Desktop-only: requires filesystem access to test asset fixtures.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL_error.h>

extern "C"
{
#include "slayer3d/image.h"
#include "slayer3d/model.h"
#include "slayer3d/texture.h"
#include "texture_internal.h"
}

namespace
{

std::string asset(const char *rel)
{
    return std::string(SLAYER3D_TEST_ASSETS_DIR) + "/" + rel;
}

/* Helper: create a texture from raw pixel data. */
slayer3d_texture2d make_texture(Uint8 *pixels, int w, int h)
{
    slayer3d_image img{};
    img.pixels = pixels;
    img.width = w;
    img.height = h;
    slayer3d_texture2d tex{};
    slayer3d_create_texture_from_image(&img, &tex);
    return tex;
}

/* Helper: validate every mesh in a model has sane invariants. */
void assert_model_valid(const slayer3d_model &m, const char *label)
{
    SCOPED_TRACE(label);
    EXPECT_GT(m.mesh_count, 0);
    ASSERT_NE(m.meshes, nullptr);
    ASSERT_NE(m.source_path, nullptr);

    for (int i = 0; i < m.mesh_count; ++i)
    {
        SCOPED_TRACE(std::string("mesh ") + std::to_string(i));
        const slayer3d_mesh &mesh = m.meshes[i];
        ASSERT_NE(mesh.positions, nullptr);
        ASSERT_NE(mesh.indices, nullptr);
        EXPECT_GT(mesh.vertex_count, 0);
        EXPECT_GT(mesh.index_count, 0);
        EXPECT_EQ(mesh.index_count % 3, 0);

        for (int j = 0; j < mesh.index_count; ++j)
        {
            EXPECT_LT((int)mesh.indices[j], mesh.vertex_count) << "index " << j;
        }

        EXPECT_TRUE(mesh.material_index == -1 || (mesh.material_index >= 0 && mesh.material_index < m.material_count));
    }
}

} // namespace

/* ================================================================== */
/* Model dispatch: slayer3d_load_model_from_file                         */
/* ================================================================== */

struct LoadModelErrorCase
{
    const char *label;
    const char *path;
    bool path_is_null;
    bool out_is_null;
};

class SLAYER3DLoadModelErrors : public ::testing::TestWithParam<LoadModelErrorCase>
{
};

TEST_P(SLAYER3DLoadModelErrors, ReturnsFailure)
{
    const auto &c = GetParam();
    slayer3d_model m{};
    const char *p = c.path_is_null ? nullptr : c.path;
    slayer3d_model *o = c.out_is_null ? nullptr : &m;
    EXPECT_FALSE(slayer3d_load_model_from_file(p, o)) << c.label;
    if (!c.out_is_null)
    {
        EXPECT_EQ(m.mesh_count, 0);
        EXPECT_EQ(m.meshes, nullptr);
    }
}

INSTANTIATE_TEST_SUITE_P(Dispatch, SLAYER3DLoadModelErrors,
                         ::testing::Values(LoadModelErrorCase{"null path", nullptr, true, false},
                                           LoadModelErrorCase{"null out", "foo.obj", false, true},
                                           LoadModelErrorCase{"missing .obj", "/no/such.obj", false, false},
                                           LoadModelErrorCase{"missing .glb", "/no/such.glb", false, false},
                                           LoadModelErrorCase{"missing .gltf", "/no/such.gltf", false, false},
                                           LoadModelErrorCase{"missing .fbx", "/no/such.fbx", false, false},
                                           LoadModelErrorCase{"unknown ext", "model.xyz", false, false},
                                           LoadModelErrorCase{"empty path", "", false, false},
                                           LoadModelErrorCase{"no extension", "modelfile", false, false},
                                           LoadModelErrorCase{"dot only", ".obj", false, false}));

/* Case-insensitive extension dispatch — all should fail on missing
 * file, but they must reach the correct loader (not "unknown ext"). */
struct CaseInsensitiveCase
{
    const char *path;
};

class SLAYER3DExtensionCaseInsensitive : public ::testing::TestWithParam<CaseInsensitiveCase>
{
};

TEST_P(SLAYER3DExtensionCaseInsensitive, DispatchesCorrectly)
{
    slayer3d_model m{};
    EXPECT_FALSE(slayer3d_load_model_from_file(GetParam().path, &m));
    /* Should NOT contain "Unrecognized" — it should reach the loader. */
    std::string err = SDL_GetError();
    EXPECT_EQ(err.find("Unrecognized"), std::string::npos) << "path: " << GetParam().path << " err: " << err;
}

INSTANTIATE_TEST_SUITE_P(Dispatch, SLAYER3DExtensionCaseInsensitive,
                         ::testing::Values(CaseInsensitiveCase{"/no/FILE.OBJ"}, CaseInsensitiveCase{"/no/FILE.GLB"},
                                           CaseInsensitiveCase{"/no/FILE.GLTF"}, CaseInsensitiveCase{"/no/FILE.FBX"},
                                           CaseInsensitiveCase{"/no/File.Obj"}, CaseInsensitiveCase{"/no/Model.gLtF"}));

/* ================================================================== */
/* slayer3d_free_model edge cases                                        */
/* ================================================================== */

TEST(SLAYER3DFreeModel, NullPointerIsSafe)
{
    slayer3d_free_model(nullptr);
}

TEST(SLAYER3DFreeModel, ZeroInitializedIsIdempotent)
{
    slayer3d_model m{};
    slayer3d_free_model(&m);
    slayer3d_free_model(&m);
}

TEST(SLAYER3DFreeModel, PopulatedModelThenDoubleFree)
{
    slayer3d_model m{};
    ASSERT_TRUE(slayer3d_load_model_from_file(asset("models/cube_obj/cube.obj").c_str(), &m));
    EXPECT_GT(m.mesh_count, 0);
    slayer3d_free_model(&m);
    EXPECT_EQ(m.mesh_count, 0);
    EXPECT_EQ(m.meshes, nullptr);
    EXPECT_EQ(m.materials, nullptr);
    EXPECT_EQ(m.source_path, nullptr);
    /* Second free on zeroed struct must be safe. */
    slayer3d_free_model(&m);
}

TEST(SLAYER3DFreeModel, AllThreeFormatsFreeSafely)
{
    const char *paths[] = {
        "models/cube_obj/cube.obj",
        "models/box_glb/Box.glb",
        "models/cube_fbx/cube.fbx",
    };
    for (const char *rel : paths)
    {
        SCOPED_TRACE(rel);
        slayer3d_model m{};
        ASSERT_TRUE(slayer3d_load_model_from_file(asset(rel).c_str(), &m));
        slayer3d_free_model(&m);
        slayer3d_free_model(&m); /* double-free */
    }
}

/* ================================================================== */
/* glTF / GLB loader                                                  */
/* ================================================================== */

TEST(SLAYER3DGltfLoader, BoxGlbFullValidation)
{
    slayer3d_model model{};
    ASSERT_TRUE(slayer3d_load_model_from_file(asset("models/box_glb/Box.glb").c_str(), &model)) << SDL_GetError();
    assert_model_valid(model, "Box.glb");

    /* Normals present and unit-length. */
    ASSERT_NE(model.meshes[0].normals, nullptr);
    for (int i = 0; i < model.meshes[0].vertex_count; ++i)
    {
        const float *n = &model.meshes[0].normals[i * 3];
        float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        EXPECT_NEAR(len, 1.0f, 0.01f) << "normal " << i;
    }

    /* Mesh has a name. */
    EXPECT_NE(model.meshes[0].name, nullptr);

    slayer3d_free_model(&model);
}

TEST(SLAYER3DGltfLoader, BoxGlbMaterialProperties)
{
    slayer3d_model model{};
    ASSERT_TRUE(slayer3d_load_model_from_file(asset("models/box_glb/Box.glb").c_str(), &model)) << SDL_GetError();
    ASSERT_GE(model.material_count, 1);

    const slayer3d_material &mat = model.materials[0];

    /* Albedo RGBA all in [0,1]. */
    for (int c = 0; c < 4; ++c)
    {
        EXPECT_GE(mat.albedo[c], 0.0f) << "albedo[" << c << "]";
        EXPECT_LE(mat.albedo[c], 1.0f) << "albedo[" << c << "]";
    }

    /* PBR scalars in [0,1]. */
    EXPECT_GE(mat.metallic, 0.0f);
    EXPECT_LE(mat.metallic, 1.0f);
    EXPECT_GE(mat.roughness, 0.0f);
    EXPECT_LE(mat.roughness, 1.0f);

    /* Emissive in [0,∞) — at least non-negative. */
    for (int c = 0; c < 3; ++c)
    {
        EXPECT_GE(mat.emissive[c], 0.0f) << "emissive[" << c << "]";
    }

    /* Mesh references this material. */
    EXPECT_EQ(model.meshes[0].material_index, 0);

    slayer3d_free_model(&model);
}

TEST(SLAYER3DGltfLoader, MissingFileErrorContainsPath)
{
    slayer3d_model m{};
    EXPECT_FALSE(slayer3d_load_model_from_file("/nonexistent/model.glb", &m));
    std::string err = SDL_GetError();
    EXPECT_NE(err.find("model.glb"), std::string::npos) << "error: " << err;
}

/* ================================================================== */
/* FBX loader                                                         */
/* ================================================================== */

TEST(SLAYER3DFbxLoader, CubeFullValidation)
{
    slayer3d_model model{};
    ASSERT_TRUE(slayer3d_load_model_from_file(asset("models/cube_fbx/cube.fbx").c_str(), &model)) << SDL_GetError();
    assert_model_valid(model, "cube.fbx");

    /* Normals present and unit-length. */
    ASSERT_NE(model.meshes[0].normals, nullptr);
    for (int i = 0; i < model.meshes[0].vertex_count; ++i)
    {
        const float *n = &model.meshes[0].normals[i * 3];
        float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        EXPECT_NEAR(len, 1.0f, 0.01f) << "normal " << i;
    }

    /* Mesh has a name. */
    EXPECT_NE(model.meshes[0].name, nullptr);

    slayer3d_free_model(&model);
}

TEST(SLAYER3DFbxLoader, CubeMaterialProperties)
{
    slayer3d_model model{};
    ASSERT_TRUE(slayer3d_load_model_from_file(asset("models/cube_fbx/cube.fbx").c_str(), &model)) << SDL_GetError();

    if (model.material_count > 0)
    {
        const slayer3d_material &mat = model.materials[0];
        for (int c = 0; c < 4; ++c)
        {
            EXPECT_GE(mat.albedo[c], 0.0f) << "albedo[" << c << "]";
            EXPECT_LE(mat.albedo[c], 1.0f) << "albedo[" << c << "]";
        }
        EXPECT_GE(mat.metallic, 0.0f);
        EXPECT_LE(mat.metallic, 1.0f);
        EXPECT_GE(mat.roughness, 0.0f);
        EXPECT_LE(mat.roughness, 1.0f);
    }

    slayer3d_free_model(&model);
}

TEST(SLAYER3DFbxLoader, MissingFileErrorContainsPath)
{
    slayer3d_model m{};
    EXPECT_FALSE(slayer3d_load_model_from_file("/nonexistent/model.fbx", &m));
    std::string err = SDL_GetError();
    EXPECT_NE(err.find("model.fbx"), std::string::npos) << "error: " << err;
}

TEST(SLAYER3DFbxLoader, FreeIsIdempotent)
{
    slayer3d_model m{};
    slayer3d_free_model(&m);
    slayer3d_free_model(&m);
}

/* ================================================================== */
/* Material import parity across all three loaders                    */
/* ================================================================== */

struct FormatCase
{
    const char *label;
    const char *rel_path;
};

class SLAYER3DMaterialParityTable : public ::testing::TestWithParam<FormatCase>
{
};

TEST_P(SLAYER3DMaterialParityTable, MaterialFieldsInValidRanges)
{
    const auto &c = GetParam();
    slayer3d_model m{};
    ASSERT_TRUE(slayer3d_load_model_from_file(asset(c.rel_path).c_str(), &m)) << SDL_GetError();

    for (int i = 0; i < m.material_count; ++i)
    {
        SCOPED_TRACE(std::string(c.label) + " material " + std::to_string(i));
        const slayer3d_material &mat = m.materials[i];

        for (int ch = 0; ch < 4; ++ch)
        {
            EXPECT_GE(mat.albedo[ch], 0.0f);
            EXPECT_LE(mat.albedo[ch], 1.0f);
        }
        EXPECT_GE(mat.metallic, 0.0f);
        EXPECT_LE(mat.metallic, 1.0f);
        EXPECT_GE(mat.roughness, 0.0f);
        EXPECT_LE(mat.roughness, 1.0f);
        for (int ch = 0; ch < 3; ++ch)
        {
            EXPECT_GE(mat.emissive[ch], 0.0f);
        }
    }

    /* Every mesh material_index is -1 or in [0, material_count). */
    for (int i = 0; i < m.mesh_count; ++i)
    {
        int mi = m.meshes[i].material_index;
        EXPECT_TRUE(mi == -1 || (mi >= 0 && mi < m.material_count))
            << c.label << " mesh " << i << " material_index=" << mi;
    }

    slayer3d_free_model(&m);
}

INSTANTIATE_TEST_SUITE_P(Parity, SLAYER3DMaterialParityTable,
                         ::testing::Values(FormatCase{"OBJ", "models/cube_obj/cube.obj"},
                                           FormatCase{"GLB", "models/box_glb/Box.glb"},
                                           FormatCase{"FBX", "models/cube_fbx/cube.fbx"}));

/* ================================================================== */
/* Mipmap generation                                                  */
/* ================================================================== */

struct MipCountCase
{
    const char *label;
    int width;
    int height;
    int expected_levels;
};

class SLAYER3DMipCount : public ::testing::TestWithParam<MipCountCase>
{
};

TEST_P(SLAYER3DMipCount, GeneratesExpectedLevelCount)
{
    const auto &c = GetParam();
    std::vector<Uint8> pixels((size_t)c.width * c.height * 4, 128);
    slayer3d_texture2d tex = make_texture(pixels.data(), c.width, c.height);
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_TRILINEAR));

    EXPECT_EQ(tex.mip_count, c.expected_levels) << c.label;

    /* Verify chain dimensions halve correctly. */
    int w = c.width, h = c.height;
    for (int i = 0; i < tex.mip_count; ++i)
    {
        EXPECT_EQ(tex.mip_levels[i].width, w) << c.label << " level " << i;
        EXPECT_EQ(tex.mip_levels[i].height, h) << c.label << " level " << i;
        ASSERT_NE(tex.mip_levels[i].pixels, nullptr) << c.label << " level " << i;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }

    /* Last level is always 1x1. */
    EXPECT_EQ(tex.mip_levels[tex.mip_count - 1].width, 1);
    EXPECT_EQ(tex.mip_levels[tex.mip_count - 1].height, 1);

    /* Level 0 aliases base pixels. */
    EXPECT_EQ(tex.mip_levels[0].pixels, tex.pixels);

    slayer3d_free_texture(&tex);
}

INSTANTIATE_TEST_SUITE_P(Mipmap, SLAYER3DMipCount,
                         ::testing::Values(MipCountCase{"1x1", 1, 1, 1}, MipCountCase{"2x2", 2, 2, 2},
                                           MipCountCase{"4x4", 4, 4, 3}, MipCountCase{"8x8", 8, 8, 4},
                                           MipCountCase{"16x16", 16, 16, 5}, MipCountCase{"256x256", 256, 256, 9},
                                           MipCountCase{"3x5 NPOT", 3, 5, 3}, MipCountCase{"1x8 tall", 1, 8, 4},
                                           MipCountCase{"8x1 wide", 8, 1, 4}, MipCountCase{"7x3 NPOT", 7, 3, 3}));

TEST(SLAYER3DMipmap, BoxFilterAveragesCorrectly)
{
    Uint8 pixels[] = {
        255, 0,   0,   255, /* red */
        0,   255, 0,   255, /* green */
        0,   0,   255, 255, /* blue */
        255, 255, 255, 255, /* white */
    };
    slayer3d_texture2d tex = make_texture(pixels, 2, 2);
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_TRILINEAR));
    ASSERT_EQ(tex.mip_count, 2);

    const Uint8 *mip1 = tex.mip_levels[1].pixels;
    /* (255+0+0+255)/4=127.5→128, same for G and B. */
    EXPECT_NEAR(mip1[0], 128, 1);
    EXPECT_NEAR(mip1[1], 128, 1);
    EXPECT_NEAR(mip1[2], 128, 1);
    EXPECT_EQ(mip1[3], 255);

    slayer3d_free_texture(&tex);
}

TEST(SLAYER3DMipmap, BoxFilterAsymmetric1xN)
{
    /* 1x4 texture: when width is already 1, the box filter samples
     * the same column twice for the x-axis. */
    Uint8 pixels[] = {
        100, 100, 100, 255, 200, 200, 200, 255, 100, 100, 100, 255, 200, 200, 200, 255,
    };
    slayer3d_texture2d tex = make_texture(pixels, 1, 4);
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_TRILINEAR));

    /* 1x4 → 1x2 → 1x1 = 3 levels. */
    ASSERT_EQ(tex.mip_count, 3);
    EXPECT_EQ(tex.mip_levels[1].width, 1);
    EXPECT_EQ(tex.mip_levels[1].height, 2);

    /* Level 1, pixel 0: avg of rows 0,1 = (100+200)/2 = 150. */
    EXPECT_NEAR(tex.mip_levels[1].pixels[0], 150, 1);
    /* Level 1, pixel 1: avg of rows 2,3 = (100+200)/2 = 150. */
    EXPECT_NEAR(tex.mip_levels[1].pixels[4], 150, 1);

    slayer3d_free_texture(&tex);
}

/* ================================================================== */
/* set_texture_filter edge cases                                      */
/* ================================================================== */

TEST(SLAYER3DTextureFilter, NullTextureRejected)
{
    EXPECT_FALSE(slayer3d_set_texture_filter(nullptr, SLAYER3D_TEXTURE_FILTER_BILINEAR));
    EXPECT_FALSE(slayer3d_set_texture_filter(nullptr, SLAYER3D_TEXTURE_FILTER_TRILINEAR));
}

struct FilterSwitchCase
{
    const char *label;
    slayer3d_texture_filter from;
    slayer3d_texture_filter to;
    bool expect_mips_after;
};

class SLAYER3DFilterSwitch : public ::testing::TestWithParam<FilterSwitchCase>
{
};

TEST_P(SLAYER3DFilterSwitch, MipStateCorrectAfterSwitch)
{
    const auto &c = GetParam();
    std::vector<Uint8> pixels(4 * 4 * 4, 128);
    slayer3d_texture2d tex = make_texture(pixels.data(), 4, 4);

    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, c.from));
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, c.to));

    if (c.expect_mips_after)
    {
        EXPECT_GT(tex.mip_count, 0) << c.label;
        EXPECT_NE(tex.mip_levels, nullptr) << c.label;
    }
    else
    {
        EXPECT_EQ(tex.mip_count, 0) << c.label;
        EXPECT_EQ(tex.mip_levels, nullptr) << c.label;
    }

    slayer3d_free_texture(&tex);
}

INSTANTIATE_TEST_SUITE_P(Filter, SLAYER3DFilterSwitch,
                         ::testing::Values(FilterSwitchCase{"bilinear→trilinear", SLAYER3D_TEXTURE_FILTER_BILINEAR,
                                                            SLAYER3D_TEXTURE_FILTER_TRILINEAR, true},
                                           FilterSwitchCase{"nearest→trilinear", SLAYER3D_TEXTURE_FILTER_NEAREST,
                                                            SLAYER3D_TEXTURE_FILTER_TRILINEAR, true},
                                           FilterSwitchCase{"trilinear→bilinear", SLAYER3D_TEXTURE_FILTER_TRILINEAR,
                                                            SLAYER3D_TEXTURE_FILTER_BILINEAR, false},
                                           FilterSwitchCase{"trilinear→nearest", SLAYER3D_TEXTURE_FILTER_TRILINEAR,
                                                            SLAYER3D_TEXTURE_FILTER_NEAREST, false},
                                           FilterSwitchCase{"trilinear→trilinear (idempotent)",
                                                            SLAYER3D_TEXTURE_FILTER_TRILINEAR,
                                                            SLAYER3D_TEXTURE_FILTER_TRILINEAR, true},
                                           FilterSwitchCase{"bilinear→bilinear", SLAYER3D_TEXTURE_FILTER_BILINEAR,
                                                            SLAYER3D_TEXTURE_FILTER_BILINEAR, false},
                                           FilterSwitchCase{"nearest→nearest", SLAYER3D_TEXTURE_FILTER_NEAREST,
                                                            SLAYER3D_TEXTURE_FILTER_NEAREST, false}));

TEST(SLAYER3DTextureFilter, RepeatedCycleDoesNotLeak)
{
    std::vector<Uint8> pixels(4 * 4 * 4, 128);
    slayer3d_texture2d tex = make_texture(pixels.data(), 4, 4);

    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_TRILINEAR));
        EXPECT_GT(tex.mip_count, 0);
        ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_NEAREST));
        EXPECT_EQ(tex.mip_count, 0);
    }

    slayer3d_free_texture(&tex);
}

TEST(SLAYER3DTextureFilter, InvalidFilterValueRejected)
{
    std::vector<Uint8> pixels(4, 255);
    slayer3d_texture2d tex = make_texture(pixels.data(), 1, 1);
    EXPECT_FALSE(slayer3d_set_texture_filter(&tex, (slayer3d_texture_filter)-1));
    EXPECT_FALSE(slayer3d_set_texture_filter(&tex, (slayer3d_texture_filter)3));
    EXPECT_FALSE(slayer3d_set_texture_filter(&tex, (slayer3d_texture_filter)99));
    slayer3d_free_texture(&tex);
}

TEST(SLAYER3DTextureFilter, FreeWithActiveMipChain)
{
    std::vector<Uint8> pixels(8 * 8 * 4, 200);
    slayer3d_texture2d tex = make_texture(pixels.data(), 8, 8);
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_TRILINEAR));
    EXPECT_EQ(tex.mip_count, 4);
    /* Free while mips are active — must not leak. */
    slayer3d_free_texture(&tex);
    EXPECT_EQ(tex.pixels, nullptr);
    EXPECT_EQ(tex.mip_levels, nullptr);
    EXPECT_EQ(tex.mip_count, 0);
}

/* ================================================================== */
/* Trilinear sampling via slayer3d_texture_sample_rgba                   */
/* ================================================================== */

/*
 * Build a 4x4 texture where level 0 is solid red (255,0,0,255) and
 * level 1 (2x2) will be solid red, level 2 (1x1) will be solid red.
 * This lets us verify sampling returns red at any LOD.
 */
TEST(SLAYER3DTrilinearSampling, SolidColorSameAtAllLODs)
{
    std::vector<Uint8> pixels(4 * 4 * 4);
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = 255;
        pixels[i + 1] = 0;
        pixels[i + 2] = 0;
        pixels[i + 3] = 255;
    }
    slayer3d_texture2d tex = make_texture(pixels.data(), 4, 4);
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_TRILINEAR));

    float lods[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
    for (float lod : lods)
    {
        float r, g, b, a;
        slayer3d_texture_sample_rgba(&tex, 0.5f, 0.5f, lod, &r, &g, &b, &a);
        EXPECT_NEAR(r, 1.0f, 0.02f) << "LOD=" << lod;
        EXPECT_NEAR(g, 0.0f, 0.02f) << "LOD=" << lod;
        EXPECT_NEAR(b, 0.0f, 0.02f) << "LOD=" << lod;
        EXPECT_NEAR(a, 1.0f, 0.02f) << "LOD=" << lod;
    }

    slayer3d_free_texture(&tex);
}

struct LODClampCase
{
    const char *label;
    float lod;
};

class SLAYER3DTrilinearLODClamp : public ::testing::TestWithParam<LODClampCase>
{
};

TEST_P(SLAYER3DTrilinearLODClamp, ClampedLODDoesNotCrash)
{
    const auto &c = GetParam();
    std::vector<Uint8> pixels(4 * 4 * 4, 128);
    slayer3d_texture2d tex = make_texture(pixels.data(), 4, 4);
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_TRILINEAR));

    float r, g, b, a;
    slayer3d_texture_sample_rgba(&tex, 0.5f, 0.5f, c.lod, &r, &g, &b, &a);

    /* All channels should be ~0.502 (128/255). Just verify no crash and sane range. */
    EXPECT_GE(r, 0.0f) << c.label;
    EXPECT_LE(r, 1.0f) << c.label;
    EXPECT_GE(a, 0.0f) << c.label;
    EXPECT_LE(a, 1.0f) << c.label;

    slayer3d_free_texture(&tex);
}

INSTANTIATE_TEST_SUITE_P(Sampling, SLAYER3DTrilinearLODClamp,
                         ::testing::Values(LODClampCase{"LOD -1.0", -1.0f}, LODClampCase{"LOD -100.0", -100.0f},
                                           LODClampCase{"LOD 0.0", 0.0f}, LODClampCase{"LOD 0.5", 0.5f},
                                           LODClampCase{"LOD 1.0", 1.0f}, LODClampCase{"LOD 1.999", 1.999f},
                                           LODClampCase{"LOD 2.0 (max)", 2.0f}, LODClampCase{"LOD 10.0 (over)", 10.0f},
                                           LODClampCase{"LOD 1000.0", 1000.0f}));

TEST(SLAYER3DTrilinearSampling, LODInterpolatesBetweenLevels)
{
    /*
     * 2x2 texture: level 0 is solid white (255,255,255,255),
     * level 1 (1x1) will also be white from box filter.
     * Use a 4x4 with a gradient so levels differ.
     */
    Uint8 pixels[4 * 4 * 4];
    /* Level 0: top-left quadrant=black, rest=white. After box filter,
     * level 1 will be ~(192,192,192). */
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            int idx = (y * 4 + x) * 4;
            Uint8 v = (x < 2 && y < 2) ? 0 : 255;
            pixels[idx] = pixels[idx + 1] = pixels[idx + 2] = v;
            pixels[idx + 3] = 255;
        }

    slayer3d_texture2d tex = make_texture(pixels, 4, 4);
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_TRILINEAR));
    ASSERT_EQ(tex.mip_count, 3);

    /* Sample center at LOD 0 — should be near the boundary of black/white. */
    float r0, g0, b0, a0;
    slayer3d_texture_sample_rgba(&tex, 0.5f, 0.5f, 0.0f, &r0, &g0, &b0, &a0);

    /* Sample center at LOD 1 — level 1 is 2x2, more averaged. */
    float r1, g1, b1, a1;
    slayer3d_texture_sample_rgba(&tex, 0.5f, 0.5f, 1.0f, &r1, &g1, &b1, &a1);

    /* Sample at LOD 0.5 — should be between LOD 0 and LOD 1 results. */
    float rh, gh, bh, ah;
    slayer3d_texture_sample_rgba(&tex, 0.5f, 0.5f, 0.5f, &rh, &gh, &bh, &ah);

    /* The interpolated value should be between the two endpoints (or equal). */
    float lo = std::min(r0, r1);
    float hi = std::max(r0, r1);
    EXPECT_GE(rh, lo - 0.02f);
    EXPECT_LE(rh, hi + 0.02f);

    slayer3d_free_texture(&tex);
}

TEST(SLAYER3DTrilinearSampling, FallsBackToNonTrilinearFor1x1)
{
    /* 1x1 texture with trilinear: mip_count=1, so the trilinear path
     * should fall through to bilinear/nearest. */
    Uint8 pixels[] = {42, 84, 126, 255};
    slayer3d_texture2d tex = make_texture(pixels, 1, 1);
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_TRILINEAR));
    EXPECT_EQ(tex.mip_count, 1);

    float r, g, b, a;
    slayer3d_texture_sample_rgba(&tex, 0.5f, 0.5f, 0.0f, &r, &g, &b, &a);
    EXPECT_NEAR(r, 42.0f / 255.0f, 0.02f);
    EXPECT_NEAR(g, 84.0f / 255.0f, 0.02f);
    EXPECT_NEAR(b, 126.0f / 255.0f, 0.02f);
    EXPECT_NEAR(a, 1.0f, 0.02f);

    slayer3d_free_texture(&tex);
}

/* ================================================================== */
/* Non-trilinear sampling paths (nearest / bilinear via sample_rgba)  */
/* ================================================================== */

struct SamplerCase
{
    const char *label;
    slayer3d_texture_filter filter;
    float u;
    float v;
};

class SLAYER3DSamplerPaths : public ::testing::TestWithParam<SamplerCase>
{
};

TEST_P(SLAYER3DSamplerPaths, ReturnsValidColor)
{
    const auto &c = GetParam();
    /* 2x2 checkerboard: (0,0)=red, (1,0)=green, (0,1)=blue, (1,1)=white. */
    Uint8 pixels[] = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    };
    slayer3d_texture2d tex = make_texture(pixels, 2, 2);
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, c.filter));

    float r, g, b, a;
    slayer3d_texture_sample_rgba(&tex, c.u, c.v, 0.0f, &r, &g, &b, &a);
    EXPECT_GE(r, 0.0f) << c.label;
    EXPECT_LE(r, 1.0f) << c.label;
    EXPECT_GE(g, 0.0f) << c.label;
    EXPECT_LE(g, 1.0f) << c.label;
    EXPECT_GE(b, 0.0f) << c.label;
    EXPECT_LE(b, 1.0f) << c.label;
    EXPECT_GE(a, 0.0f) << c.label;
    EXPECT_LE(a, 1.0f) << c.label;

    slayer3d_free_texture(&tex);
}

INSTANTIATE_TEST_SUITE_P(
    Sampling, SLAYER3DSamplerPaths,
    ::testing::Values(SamplerCase{"nearest center", SLAYER3D_TEXTURE_FILTER_NEAREST, 0.5f, 0.5f},
                      SamplerCase{"nearest origin", SLAYER3D_TEXTURE_FILTER_NEAREST, 0.0f, 0.0f},
                      SamplerCase{"nearest (1,1)", SLAYER3D_TEXTURE_FILTER_NEAREST, 1.0f, 1.0f},
                      SamplerCase{"bilinear center", SLAYER3D_TEXTURE_FILTER_BILINEAR, 0.5f, 0.5f},
                      SamplerCase{"bilinear origin", SLAYER3D_TEXTURE_FILTER_BILINEAR, 0.0f, 0.0f},
                      SamplerCase{"bilinear (1,1)", SLAYER3D_TEXTURE_FILTER_BILINEAR, 1.0f, 1.0f},
                      SamplerCase{"bilinear negative UV", SLAYER3D_TEXTURE_FILTER_BILINEAR, -0.5f, -0.5f},
                      SamplerCase{"bilinear UV > 1", SLAYER3D_TEXTURE_FILTER_BILINEAR, 1.5f, 1.5f},
                      SamplerCase{"nearest negative UV", SLAYER3D_TEXTURE_FILTER_NEAREST, -0.25f, -0.25f},
                      SamplerCase{"nearest UV > 1", SLAYER3D_TEXTURE_FILTER_NEAREST, 2.0f, 2.0f}));

/* ================================================================== */
/* Wrap mode interaction with sampling                                */
/* ================================================================== */

TEST(SLAYER3DTextureSampling, RepeatWrapWithTrilinear)
{
    /* 4x4 solid green texture with REPEAT wrap. */
    std::vector<Uint8> pixels(4 * 4 * 4);
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = 0;
        pixels[i + 1] = 200;
        pixels[i + 2] = 0;
        pixels[i + 3] = 255;
    }
    slayer3d_texture2d tex = make_texture(pixels.data(), 4, 4);
    ASSERT_TRUE(slayer3d_set_texture_wrap(&tex, SLAYER3D_TEXTURE_WRAP_REPEAT, SLAYER3D_TEXTURE_WRAP_REPEAT));
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_TRILINEAR));

    /* Sample with UV outside [0,1] — repeat should wrap. */
    float r, g, b, a;
    slayer3d_texture_sample_rgba(&tex, 2.5f, -0.5f, 0.5f, &r, &g, &b, &a);
    EXPECT_NEAR(g, 200.0f / 255.0f, 0.05f);
    EXPECT_NEAR(r, 0.0f, 0.05f);

    slayer3d_free_texture(&tex);
}

TEST(SLAYER3DTextureSampling, ClampWrapWithTrilinear)
{
    /* 4x4 solid blue texture with CLAMP wrap. */
    std::vector<Uint8> pixels(4 * 4 * 4);
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = 0;
        pixels[i + 1] = 0;
        pixels[i + 2] = 200;
        pixels[i + 3] = 255;
    }
    slayer3d_texture2d tex = make_texture(pixels.data(), 4, 4);
    /* Clamp is the default, but be explicit. */
    ASSERT_TRUE(slayer3d_set_texture_wrap(&tex, SLAYER3D_TEXTURE_WRAP_CLAMP, SLAYER3D_TEXTURE_WRAP_CLAMP));
    ASSERT_TRUE(slayer3d_set_texture_filter(&tex, SLAYER3D_TEXTURE_FILTER_TRILINEAR));

    float r, g, b, a;
    slayer3d_texture_sample_rgba(&tex, 5.0f, -3.0f, 1.0f, &r, &g, &b, &a);
    EXPECT_NEAR(b, 200.0f / 255.0f, 0.05f);
    EXPECT_NEAR(r, 0.0f, 0.05f);

    slayer3d_free_texture(&tex);
}
