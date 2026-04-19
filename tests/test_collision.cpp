/*
 * Comprehensive tests for M6: collisions and spatial queries.
 */

#include <gtest/gtest.h>

#include <cmath>

extern "C"
{
#include "sdl3d/collision.h"
#include "sdl3d/math.h"
}

/* ================================================================== */
/* AABB-AABB                                                          */
/* ================================================================== */

struct AABBCase
{
    const char *label;
    sdl3d_bounding_box a, b;
    bool expected;
};

class SDL3DAABBAABB : public ::testing::TestWithParam<AABBCase>
{
};

TEST_P(SDL3DAABBAABB, Check)
{
    const auto &c = GetParam();
    EXPECT_EQ(sdl3d_check_aabb_aabb(c.a, c.b), c.expected) << c.label;
    EXPECT_EQ(sdl3d_check_aabb_aabb(c.b, c.a), c.expected) << c.label << " (reversed)";
}

static sdl3d_bounding_box bb(float x0, float y0, float z0, float x1, float y1, float z1)
{
    sdl3d_bounding_box b;
    b.min = sdl3d_vec3_make(x0, y0, z0);
    b.max = sdl3d_vec3_make(x1, y1, z1);
    return b;
}

INSTANTIATE_TEST_SUITE_P(Collision, SDL3DAABBAABB,
                         ::testing::Values(AABBCase{"overlap", bb(-1, -1, -1, 1, 1, 1), bb(0, 0, 0, 2, 2, 2), true},
                                           AABBCase{"touching", bb(0, 0, 0, 1, 1, 1), bb(1, 0, 0, 2, 1, 1), true},
                                           AABBCase{"separated x", bb(0, 0, 0, 1, 1, 1), bb(2, 0, 0, 3, 1, 1), false},
                                           AABBCase{"separated y", bb(0, 0, 0, 1, 1, 1), bb(0, 2, 0, 1, 3, 1), false},
                                           AABBCase{"separated z", bb(0, 0, 0, 1, 1, 1), bb(0, 0, 2, 1, 1, 3), false},
                                           AABBCase{"contained", bb(-2, -2, -2, 2, 2, 2), bb(-1, -1, -1, 1, 1, 1),
                                                    true},
                                           AABBCase{"identical", bb(0, 0, 0, 1, 1, 1), bb(0, 0, 0, 1, 1, 1), true},
                                           AABBCase{"zero-size", bb(0, 0, 0, 0, 0, 0), bb(0, 0, 0, 0, 0, 0), true}));

/* ================================================================== */
/* Sphere-Sphere                                                      */
/* ================================================================== */

struct SphereSphereCase
{
    const char *label;
    sdl3d_sphere a, b;
    bool expected;
};

class SDL3DSphereSphere : public ::testing::TestWithParam<SphereSphereCase>
{
};

TEST_P(SDL3DSphereSphere, Check)
{
    const auto &c = GetParam();
    EXPECT_EQ(sdl3d_check_sphere_sphere(c.a, c.b), c.expected) << c.label;
}

static sdl3d_sphere sp(float x, float y, float z, float r)
{
    sdl3d_sphere s;
    s.center = sdl3d_vec3_make(x, y, z);
    s.radius = r;
    return s;
}

INSTANTIATE_TEST_SUITE_P(Collision, SDL3DSphereSphere,
                         ::testing::Values(SphereSphereCase{"overlap", sp(0, 0, 0, 1), sp(1, 0, 0, 1), true},
                                           SphereSphereCase{"touching", sp(0, 0, 0, 1), sp(2, 0, 0, 1), true},
                                           SphereSphereCase{"separated", sp(0, 0, 0, 1), sp(3, 0, 0, 1), false},
                                           SphereSphereCase{"contained", sp(0, 0, 0, 5), sp(1, 0, 0, 1), true},
                                           SphereSphereCase{"coincident", sp(0, 0, 0, 1), sp(0, 0, 0, 1), true},
                                           SphereSphereCase{"zero radius", sp(0, 0, 0, 0), sp(0, 0, 0, 0), true},
                                           SphereSphereCase{"far apart", sp(-10, 0, 0, 1), sp(10, 0, 0, 1), false}));

/* ================================================================== */
/* AABB-Sphere                                                        */
/* ================================================================== */

struct AABBSphereCase
{
    const char *label;
    sdl3d_bounding_box box;
    sdl3d_sphere sphere;
    bool expected;
};

class SDL3DAABBSphere : public ::testing::TestWithParam<AABBSphereCase>
{
};

TEST_P(SDL3DAABBSphere, Check)
{
    const auto &c = GetParam();
    EXPECT_EQ(sdl3d_check_aabb_sphere(c.box, c.sphere), c.expected) << c.label;
}

INSTANTIATE_TEST_SUITE_P(
    Collision, SDL3DAABBSphere,
    ::testing::Values(AABBSphereCase{"inside", bb(-1, -1, -1, 1, 1, 1), sp(0, 0, 0, 0.5f), true},
                      AABBSphereCase{"touching face", bb(0, 0, 0, 1, 1, 1), sp(2, 0.5f, 0.5f, 1), true},
                      AABBSphereCase{"separated", bb(0, 0, 0, 1, 1, 1), sp(3, 0.5f, 0.5f, 1), false},
                      AABBSphereCase{"corner touch", bb(0, 0, 0, 1, 1, 1), sp(2, 2, 2, 1.733f), true},
                      AABBSphereCase{"corner miss", bb(0, 0, 0, 1, 1, 1), sp(2, 2, 2, 1.7f), false}));

/* ================================================================== */
/* Ray-AABB                                                           */
/* ================================================================== */

struct RayAABBCase
{
    const char *label;
    sdl3d_ray ray;
    sdl3d_bounding_box box;
    bool expect_hit;
};

class SDL3DRayAABB : public ::testing::TestWithParam<RayAABBCase>
{
};

static sdl3d_ray make_ray(float ox, float oy, float oz, float dx, float dy, float dz)
{
    sdl3d_ray r;
    r.position = sdl3d_vec3_make(ox, oy, oz);
    r.direction = sdl3d_vec3_make(dx, dy, dz);
    return r;
}

TEST_P(SDL3DRayAABB, HitOrMiss)
{
    const auto &c = GetParam();
    sdl3d_ray_hit h = sdl3d_ray_vs_aabb(c.ray, c.box);
    EXPECT_EQ(h.hit, c.expect_hit) << c.label;
    if (h.hit)
    {
        EXPECT_GE(h.distance, 0.0f) << c.label;
    }
}

INSTANTIATE_TEST_SUITE_P(
    Collision, SDL3DRayAABB,
    ::testing::Values(RayAABBCase{"direct hit", make_ray(-5, 0.5f, 0.5f, 1, 0, 0), bb(0, 0, 0, 1, 1, 1), true},
                      RayAABBCase{"miss above", make_ray(-5, 2, 0.5f, 1, 0, 0), bb(0, 0, 0, 1, 1, 1), false},
                      RayAABBCase{"miss behind", make_ray(5, 0.5f, 0.5f, 1, 0, 0), bb(0, 0, 0, 1, 1, 1), false},
                      RayAABBCase{"inside", make_ray(0.5f, 0.5f, 0.5f, 1, 0, 0), bb(0, 0, 0, 1, 1, 1), true},
                      RayAABBCase{"along y", make_ray(0.5f, -5, 0.5f, 0, 1, 0), bb(0, 0, 0, 1, 1, 1), true},
                      RayAABBCase{"along z", make_ray(0.5f, 0.5f, -5, 0, 0, 1), bb(0, 0, 0, 1, 1, 1), true},
                      RayAABBCase{"diagonal hit", make_ray(-2, -2, -2, 1, 1, 1), bb(0, 0, 0, 1, 1, 1), true}));

/* ================================================================== */
/* Ray-Sphere                                                         */
/* ================================================================== */

struct RaySphereCase
{
    const char *label;
    sdl3d_ray ray;
    sdl3d_sphere sphere;
    bool expect_hit;
};

class SDL3DRaySphere : public ::testing::TestWithParam<RaySphereCase>
{
};

TEST_P(SDL3DRaySphere, HitOrMiss)
{
    const auto &c = GetParam();
    sdl3d_ray_hit h = sdl3d_ray_vs_sphere(c.ray, c.sphere);
    EXPECT_EQ(h.hit, c.expect_hit) << c.label;
    if (h.hit)
    {
        EXPECT_GE(h.distance, 0.0f) << c.label;
        /* Hit point should be on the sphere surface. */
        float dx = h.point.x - c.sphere.center.x;
        float dy = h.point.y - c.sphere.center.y;
        float dz = h.point.z - c.sphere.center.z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        EXPECT_NEAR(dist, c.sphere.radius, 0.01f) << c.label;
    }
}

INSTANTIATE_TEST_SUITE_P(
    Collision, SDL3DRaySphere,
    ::testing::Values(RaySphereCase{"direct hit", make_ray(-5, 0, 0, 1, 0, 0), sp(0, 0, 0, 1), true},
                      RaySphereCase{"miss above", make_ray(-5, 2, 0, 1, 0, 0), sp(0, 0, 0, 1), false},
                      RaySphereCase{"miss behind", make_ray(5, 0, 0, 1, 0, 0), sp(0, 0, 0, 1), false},
                      RaySphereCase{"inside", make_ray(0, 0, 0, 1, 0, 0), sp(0, 0, 0, 5), true},
                      RaySphereCase{"tangent", make_ray(-5, 1, 0, 1, 0, 0), sp(0, 0, 0, 1), true},
                      RaySphereCase{"far sphere", make_ray(0, 0, 0, 0, 0, -1), sp(0, 0, -100, 1), true}));

/* ================================================================== */
/* Ray-Triangle (Möller–Trumbore)                                     */
/* ================================================================== */

TEST(SDL3DRayTriangle, HitsFrontFace)
{
    sdl3d_vec3 v0 = sdl3d_vec3_make(-1, -1, 0);
    sdl3d_vec3 v1 = sdl3d_vec3_make(1, -1, 0);
    sdl3d_vec3 v2 = sdl3d_vec3_make(0, 1, 0);
    sdl3d_ray ray = make_ray(0, 0, -5, 0, 0, 1);

    sdl3d_ray_hit h = sdl3d_ray_vs_triangle(ray, v0, v1, v2);
    EXPECT_TRUE(h.hit);
    EXPECT_NEAR(h.distance, 5.0f, 0.01f);
    EXPECT_NEAR(h.point.z, 0.0f, 0.01f);
}

TEST(SDL3DRayTriangle, MissesOutside)
{
    sdl3d_vec3 v0 = sdl3d_vec3_make(-1, -1, 0);
    sdl3d_vec3 v1 = sdl3d_vec3_make(1, -1, 0);
    sdl3d_vec3 v2 = sdl3d_vec3_make(0, 1, 0);
    sdl3d_ray ray = make_ray(5, 5, -5, 0, 0, 1);

    sdl3d_ray_hit h = sdl3d_ray_vs_triangle(ray, v0, v1, v2);
    EXPECT_FALSE(h.hit);
}

TEST(SDL3DRayTriangle, MissesBehind)
{
    sdl3d_vec3 v0 = sdl3d_vec3_make(-1, -1, 0);
    sdl3d_vec3 v1 = sdl3d_vec3_make(1, -1, 0);
    sdl3d_vec3 v2 = sdl3d_vec3_make(0, 1, 0);
    sdl3d_ray ray = make_ray(0, 0, 5, 0, 0, 1);

    sdl3d_ray_hit h = sdl3d_ray_vs_triangle(ray, v0, v1, v2);
    EXPECT_FALSE(h.hit);
}

TEST(SDL3DRayTriangle, ParallelMisses)
{
    sdl3d_vec3 v0 = sdl3d_vec3_make(-1, -1, 0);
    sdl3d_vec3 v1 = sdl3d_vec3_make(1, -1, 0);
    sdl3d_vec3 v2 = sdl3d_vec3_make(0, 1, 0);
    sdl3d_ray ray = make_ray(0, 0, -5, 1, 0, 0);

    sdl3d_ray_hit h = sdl3d_ray_vs_triangle(ray, v0, v1, v2);
    EXPECT_FALSE(h.hit);
}

/* ================================================================== */
/* Ray-Mesh                                                           */
/* ================================================================== */

TEST(SDL3DRayMesh, HitsClosestTriangle)
{
    /* Two triangles: one at z=0, one at z=2. Ray from z=-5 along +z. */
    float positions[] = {
        -1, -1, 0, 1, -1, 0, 0, 1, 0, /* tri 0 at z=0 */
        -1, -1, 2, 1, -1, 2, 0, 1, 2, /* tri 1 at z=2 */
    };
    unsigned int indices[] = {0, 1, 2, 3, 4, 5};
    sdl3d_mesh mesh{};
    mesh.positions = positions;
    mesh.vertex_count = 6;
    mesh.indices = indices;
    mesh.index_count = 6;

    sdl3d_ray ray = make_ray(0, 0, -5, 0, 0, 1);
    sdl3d_ray_hit h = sdl3d_ray_vs_mesh(ray, &mesh);
    EXPECT_TRUE(h.hit);
    EXPECT_NEAR(h.distance, 5.0f, 0.01f);
    EXPECT_NEAR(h.point.z, 0.0f, 0.01f);
}

TEST(SDL3DRayMesh, MissesEmptyMesh)
{
    sdl3d_ray ray = make_ray(0, 0, -5, 0, 0, 1);
    sdl3d_ray_hit h = sdl3d_ray_vs_mesh(ray, nullptr);
    EXPECT_FALSE(h.hit);
}

/* ================================================================== */
/* Compute mesh AABB                                                  */
/* ================================================================== */

TEST(SDL3DMeshAABB, ComputesCorrectBounds)
{
    float positions[] = {-1, -2, -3, 4, 5, 6, 0, 0, 0};
    sdl3d_mesh mesh{};
    mesh.positions = positions;
    mesh.vertex_count = 3;

    sdl3d_bounding_box aabb = sdl3d_compute_mesh_aabb(&mesh);
    EXPECT_FLOAT_EQ(aabb.min.x, -1);
    EXPECT_FLOAT_EQ(aabb.min.y, -2);
    EXPECT_FLOAT_EQ(aabb.min.z, -3);
    EXPECT_FLOAT_EQ(aabb.max.x, 4);
    EXPECT_FLOAT_EQ(aabb.max.y, 5);
    EXPECT_FLOAT_EQ(aabb.max.z, 6);
}

TEST(SDL3DMeshAABB, NullMeshReturnsZero)
{
    sdl3d_bounding_box aabb = sdl3d_compute_mesh_aabb(nullptr);
    EXPECT_FLOAT_EQ(aabb.min.x, 0);
    EXPECT_FLOAT_EQ(aabb.max.x, 0);
}

/* ================================================================== */
/* Scene raycast                                                      */
/* ================================================================== */

TEST(SDL3DSceneRaycast, NullSceneReturnsNoHit)
{
    sdl3d_ray ray = make_ray(0, 0, -5, 0, 0, 1);
    sdl3d_scene_hit h = sdl3d_scene_raycast(nullptr, ray);
    EXPECT_FALSE(h.hit);
}

TEST(SDL3DSceneRaycast, EmptySceneReturnsNoHit)
{
    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_ray ray = make_ray(0, 0, -5, 0, 0, 1);
    sdl3d_scene_hit h = sdl3d_scene_raycast(scene, ray);
    EXPECT_FALSE(h.hit);
    sdl3d_destroy_scene(scene);
}

TEST(SDL3DSceneRaycast, HitsActorMesh)
{
    float positions[] = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
    unsigned int indices[] = {0, 1, 2};
    sdl3d_mesh meshes[1] = {};
    meshes[0].positions = positions;
    meshes[0].vertex_count = 3;
    meshes[0].indices = indices;
    meshes[0].index_count = 3;
    meshes[0].material_index = -1;

    sdl3d_model model{};
    model.meshes = meshes;
    model.mesh_count = 1;

    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);
    sdl3d_actor_set_position(actor, sdl3d_vec3_make(0, 0, 5));

    /* Ray from origin along +z should hit the triangle at z=5. */
    sdl3d_ray ray = make_ray(0, 0, 0, 0, 0, 1);
    sdl3d_scene_hit h = sdl3d_scene_raycast(scene, ray);
    EXPECT_TRUE(h.hit);
    EXPECT_NEAR(h.point.z, 5.0f, 0.01f);
    EXPECT_EQ(h.actor, actor);

    sdl3d_destroy_scene(scene);
}

TEST(SDL3DSceneRaycast, InvisibleActorSkipped)
{
    float positions[] = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
    unsigned int indices[] = {0, 1, 2};
    sdl3d_mesh meshes[1] = {};
    meshes[0].positions = positions;
    meshes[0].vertex_count = 3;
    meshes[0].indices = indices;
    meshes[0].index_count = 3;
    meshes[0].material_index = -1;

    sdl3d_model model{};
    model.meshes = meshes;
    model.mesh_count = 1;

    sdl3d_scene *scene = sdl3d_create_scene();
    sdl3d_actor *actor = sdl3d_scene_add_actor(scene, &model);
    sdl3d_actor_set_visible(actor, false);

    sdl3d_ray ray = make_ray(0, 0, -5, 0, 0, 1);
    sdl3d_scene_hit h = sdl3d_scene_raycast(scene, ray);
    EXPECT_FALSE(h.hit);

    sdl3d_destroy_scene(scene);
}
