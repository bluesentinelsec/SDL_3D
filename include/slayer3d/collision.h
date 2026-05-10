#ifndef SLAYER3D_COLLISION_H
#define SLAYER3D_COLLISION_H

#include <stdbool.h>

#include "slayer3d/model.h"
#include "slayer3d/scene.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct slayer3d_sphere
    {
        slayer3d_vec3 center;
        float radius;
    } slayer3d_sphere;

    typedef struct slayer3d_ray_hit
    {
        bool hit;
        float distance;
        slayer3d_vec3 point;
        slayer3d_vec3 normal;
    } slayer3d_ray_hit;

    typedef struct slayer3d_scene_hit
    {
        bool hit;
        float distance;
        slayer3d_vec3 point;
        slayer3d_vec3 normal;
        const slayer3d_actor *actor;
    } slayer3d_scene_hit;

    /* ============================================================== */
    /* Primitive-primitive tests                                       */
    /* ============================================================== */

    bool slayer3d_check_aabb_aabb(slayer3d_bounding_box a, slayer3d_bounding_box b);
    bool slayer3d_check_sphere_sphere(slayer3d_sphere a, slayer3d_sphere b);
    bool slayer3d_check_aabb_sphere(slayer3d_bounding_box box, slayer3d_sphere sphere);

    /*
     * Test a sphere against 6 normalized frustum planes (a,b,c,d) where
     * the inside half-space satisfies a*x + b*y + c*z + d >= 0. Returns
     * true when the sphere is at least partially inside the frustum.
     *
     * The plane array is read-only despite the lack of `const`; pre-C23
     * pedantic mode rejects the deep-const conversion that would
     * otherwise require callers to cast their non-const local arrays.
     */
    bool slayer3d_sphere_intersects_frustum(slayer3d_sphere sphere, float planes[6][4]);

    /* ============================================================== */
    /* Ray tests                                                      */
    /* ============================================================== */

    slayer3d_ray_hit slayer3d_ray_vs_aabb(slayer3d_ray ray, slayer3d_bounding_box box);
    slayer3d_ray_hit slayer3d_ray_vs_sphere(slayer3d_ray ray, slayer3d_sphere sphere);
    slayer3d_ray_hit slayer3d_ray_vs_triangle(slayer3d_ray ray, slayer3d_vec3 v0, slayer3d_vec3 v1, slayer3d_vec3 v2);

    /*
     * Test a ray against all triangles in a mesh (brute-force).
     * Returns the closest hit, or hit=false if no intersection.
     */
    slayer3d_ray_hit slayer3d_ray_vs_mesh(slayer3d_ray ray, const slayer3d_mesh *mesh);

    /* ============================================================== */
    /* Bounding volume helpers                                        */
    /* ============================================================== */

    slayer3d_bounding_box slayer3d_compute_mesh_aabb(const slayer3d_mesh *mesh);

    /* ============================================================== */
    /* Scene-level queries                                            */
    /* ============================================================== */

    /*
     * Cast a ray against all visible actors in the scene. Tests each
     * actor's mesh AABB first, then triangles on hit. Returns the
     * closest intersection with the actor that was hit.
     */
    slayer3d_scene_hit slayer3d_scene_raycast(const slayer3d_scene *scene, slayer3d_ray ray);

#ifdef __cplusplus
}
#endif

#endif
