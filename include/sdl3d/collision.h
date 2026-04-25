#ifndef SDL3D_COLLISION_H
#define SDL3D_COLLISION_H

#include <stdbool.h>

#include "sdl3d/model.h"
#include "sdl3d/scene.h"
#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct sdl3d_sphere
    {
        sdl3d_vec3 center;
        float radius;
    } sdl3d_sphere;

    typedef struct sdl3d_ray_hit
    {
        bool hit;
        float distance;
        sdl3d_vec3 point;
        sdl3d_vec3 normal;
    } sdl3d_ray_hit;

    typedef struct sdl3d_scene_hit
    {
        bool hit;
        float distance;
        sdl3d_vec3 point;
        sdl3d_vec3 normal;
        const sdl3d_actor *actor;
    } sdl3d_scene_hit;

    /* ============================================================== */
    /* Primitive-primitive tests                                       */
    /* ============================================================== */

    bool sdl3d_check_aabb_aabb(sdl3d_bounding_box a, sdl3d_bounding_box b);
    bool sdl3d_check_sphere_sphere(sdl3d_sphere a, sdl3d_sphere b);
    bool sdl3d_check_aabb_sphere(sdl3d_bounding_box box, sdl3d_sphere sphere);

    /*
     * Test a sphere against 6 normalized frustum planes (a,b,c,d) where
     * the inside half-space satisfies a*x + b*y + c*z + d >= 0. Returns
     * true when the sphere is at least partially inside the frustum.
     */
    bool sdl3d_sphere_intersects_frustum(sdl3d_sphere sphere, const float planes[6][4]);

    /* ============================================================== */
    /* Ray tests                                                      */
    /* ============================================================== */

    sdl3d_ray_hit sdl3d_ray_vs_aabb(sdl3d_ray ray, sdl3d_bounding_box box);
    sdl3d_ray_hit sdl3d_ray_vs_sphere(sdl3d_ray ray, sdl3d_sphere sphere);
    sdl3d_ray_hit sdl3d_ray_vs_triangle(sdl3d_ray ray, sdl3d_vec3 v0, sdl3d_vec3 v1, sdl3d_vec3 v2);

    /*
     * Test a ray against all triangles in a mesh (brute-force).
     * Returns the closest hit, or hit=false if no intersection.
     */
    sdl3d_ray_hit sdl3d_ray_vs_mesh(sdl3d_ray ray, const sdl3d_mesh *mesh);

    /* ============================================================== */
    /* Bounding volume helpers                                        */
    /* ============================================================== */

    sdl3d_bounding_box sdl3d_compute_mesh_aabb(const sdl3d_mesh *mesh);

    /* ============================================================== */
    /* Scene-level queries                                            */
    /* ============================================================== */

    /*
     * Cast a ray against all visible actors in the scene. Tests each
     * actor's mesh AABB first, then triangles on hit. Returns the
     * closest intersection with the actor that was hit.
     */
    sdl3d_scene_hit sdl3d_scene_raycast(const sdl3d_scene *scene, sdl3d_ray ray);

#ifdef __cplusplus
}
#endif

#endif
