#include "sdl3d/collision.h"

#include <SDL3/SDL_stdinc.h>

#include "sdl3d/math.h"

static float sdl3d_col_minf(float a, float b)
{
    return a < b ? a : b;
}

static float sdl3d_col_maxf(float a, float b)
{
    return a > b ? a : b;
}

/* ------------------------------------------------------------------ */
/* Primitive-primitive tests                                           */
/* ------------------------------------------------------------------ */

bool sdl3d_check_aabb_aabb(sdl3d_bounding_box a, sdl3d_bounding_box b)
{
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z &&
           a.max.z >= b.min.z;
}

bool sdl3d_check_sphere_sphere(sdl3d_sphere a, sdl3d_sphere b)
{
    float dx = a.center.x - b.center.x;
    float dy = a.center.y - b.center.y;
    float dz = a.center.z - b.center.z;
    float dist_sq = dx * dx + dy * dy + dz * dz;
    float r_sum = a.radius + b.radius;
    return dist_sq <= r_sum * r_sum;
}

bool sdl3d_sphere_intersects_frustum(sdl3d_sphere sphere, const float planes[6][4])
{
    if (planes == NULL)
    {
        return true;
    }
    for (int i = 0; i < 6; ++i)
    {
        float distance = planes[i][0] * sphere.center.x + planes[i][1] * sphere.center.y +
                         planes[i][2] * sphere.center.z + planes[i][3];
        if (distance < -sphere.radius)
        {
            return false;
        }
    }
    return true;
}

bool sdl3d_check_aabb_sphere(sdl3d_bounding_box box, sdl3d_sphere sphere)
{
    /* Find the closest point on the AABB to the sphere center. */
    float cx = sdl3d_col_maxf(box.min.x, sdl3d_col_minf(sphere.center.x, box.max.x));
    float cy = sdl3d_col_maxf(box.min.y, sdl3d_col_minf(sphere.center.y, box.max.y));
    float cz = sdl3d_col_maxf(box.min.z, sdl3d_col_minf(sphere.center.z, box.max.z));
    float dx = cx - sphere.center.x;
    float dy = cy - sphere.center.y;
    float dz = cz - sphere.center.z;
    return (dx * dx + dy * dy + dz * dz) <= sphere.radius * sphere.radius;
}

/* ------------------------------------------------------------------ */
/* Ray tests                                                           */
/* ------------------------------------------------------------------ */

static sdl3d_ray_hit sdl3d_no_hit(void)
{
    sdl3d_ray_hit h;
    SDL_zerop(&h);
    return h;
}

sdl3d_ray_hit sdl3d_ray_vs_aabb(sdl3d_ray ray, sdl3d_bounding_box box)
{
    float tmin, tmax, tymin, tymax, tzmin, tzmax;
    float inv_dx = (ray.direction.x != 0.0f) ? 1.0f / ray.direction.x : 1e30f;
    float inv_dy = (ray.direction.y != 0.0f) ? 1.0f / ray.direction.y : 1e30f;
    float inv_dz = (ray.direction.z != 0.0f) ? 1.0f / ray.direction.z : 1e30f;

    if (inv_dx >= 0.0f)
    {
        tmin = (box.min.x - ray.position.x) * inv_dx;
        tmax = (box.max.x - ray.position.x) * inv_dx;
    }
    else
    {
        tmin = (box.max.x - ray.position.x) * inv_dx;
        tmax = (box.min.x - ray.position.x) * inv_dx;
    }

    if (inv_dy >= 0.0f)
    {
        tymin = (box.min.y - ray.position.y) * inv_dy;
        tymax = (box.max.y - ray.position.y) * inv_dy;
    }
    else
    {
        tymin = (box.max.y - ray.position.y) * inv_dy;
        tymax = (box.min.y - ray.position.y) * inv_dy;
    }

    if (tmin > tymax || tymin > tmax)
    {
        return sdl3d_no_hit();
    }
    tmin = sdl3d_col_maxf(tmin, tymin);
    tmax = sdl3d_col_minf(tmax, tymax);

    if (inv_dz >= 0.0f)
    {
        tzmin = (box.min.z - ray.position.z) * inv_dz;
        tzmax = (box.max.z - ray.position.z) * inv_dz;
    }
    else
    {
        tzmin = (box.max.z - ray.position.z) * inv_dz;
        tzmax = (box.min.z - ray.position.z) * inv_dz;
    }

    if (tmin > tzmax || tzmin > tmax)
    {
        return sdl3d_no_hit();
    }
    tmin = sdl3d_col_maxf(tmin, tzmin);
    tmax = sdl3d_col_minf(tmax, tzmax);

    if (tmax < 0.0f)
    {
        return sdl3d_no_hit();
    }

    {
        sdl3d_ray_hit h;
        float t = tmin >= 0.0f ? tmin : tmax;
        h.hit = true;
        h.distance = t;
        h.point.x = ray.position.x + ray.direction.x * t;
        h.point.y = ray.position.y + ray.direction.y * t;
        h.point.z = ray.position.z + ray.direction.z * t;
        /* Approximate normal from which face was hit. */
        h.normal = sdl3d_vec3_make(0, 0, 0);
        return h;
    }
}

sdl3d_ray_hit sdl3d_ray_vs_sphere(sdl3d_ray ray, sdl3d_sphere sphere)
{
    float ox = ray.position.x - sphere.center.x;
    float oy = ray.position.y - sphere.center.y;
    float oz = ray.position.z - sphere.center.z;
    float a = ray.direction.x * ray.direction.x + ray.direction.y * ray.direction.y + ray.direction.z * ray.direction.z;
    float b = 2.0f * (ox * ray.direction.x + oy * ray.direction.y + oz * ray.direction.z);
    float c = ox * ox + oy * oy + oz * oz - sphere.radius * sphere.radius;
    float disc = b * b - 4.0f * a * c;

    if (disc < 0.0f)
    {
        return sdl3d_no_hit();
    }

    {
        float sqrt_disc = SDL_sqrtf(disc);
        float t0 = (-b - sqrt_disc) / (2.0f * a);
        float t1 = (-b + sqrt_disc) / (2.0f * a);
        float t;
        sdl3d_ray_hit h;

        if (t0 >= 0.0f)
        {
            t = t0;
        }
        else if (t1 >= 0.0f)
        {
            t = t1;
        }
        else
        {
            return sdl3d_no_hit();
        }

        h.hit = true;
        h.distance = t;
        h.point.x = ray.position.x + ray.direction.x * t;
        h.point.y = ray.position.y + ray.direction.y * t;
        h.point.z = ray.position.z + ray.direction.z * t;
        h.normal = sdl3d_vec3_normalize(sdl3d_vec3_sub(h.point, sphere.center));
        return h;
    }
}

sdl3d_ray_hit sdl3d_ray_vs_triangle(sdl3d_ray ray, sdl3d_vec3 v0, sdl3d_vec3 v1, sdl3d_vec3 v2)
{
    /* Möller–Trumbore intersection algorithm. */
    sdl3d_vec3 e1 = sdl3d_vec3_sub(v1, v0);
    sdl3d_vec3 e2 = sdl3d_vec3_sub(v2, v0);
    sdl3d_vec3 h = sdl3d_vec3_cross(ray.direction, e2);
    float a = sdl3d_vec3_dot(e1, h);

    if (a > -1e-7f && a < 1e-7f)
    {
        return sdl3d_no_hit();
    }

    {
        float f = 1.0f / a;
        sdl3d_vec3 s = sdl3d_vec3_sub(ray.position, v0);
        float u = f * sdl3d_vec3_dot(s, h);
        sdl3d_vec3 q;
        float v, t;
        sdl3d_ray_hit hit;

        if (u < 0.0f || u > 1.0f)
        {
            return sdl3d_no_hit();
        }

        q = sdl3d_vec3_cross(s, e1);
        v = f * sdl3d_vec3_dot(ray.direction, q);
        if (v < 0.0f || u + v > 1.0f)
        {
            return sdl3d_no_hit();
        }

        t = f * sdl3d_vec3_dot(e2, q);
        if (t < 0.0f)
        {
            return sdl3d_no_hit();
        }

        hit.hit = true;
        hit.distance = t;
        hit.point.x = ray.position.x + ray.direction.x * t;
        hit.point.y = ray.position.y + ray.direction.y * t;
        hit.point.z = ray.position.z + ray.direction.z * t;
        hit.normal = sdl3d_vec3_normalize(sdl3d_vec3_cross(e1, e2));
        return hit;
    }
}

/* ------------------------------------------------------------------ */
/* Ray-mesh (brute-force)                                              */
/* ------------------------------------------------------------------ */

sdl3d_ray_hit sdl3d_ray_vs_mesh(sdl3d_ray ray, const sdl3d_mesh *mesh)
{
    sdl3d_ray_hit closest = sdl3d_no_hit();
    int tri_count;
    bool indexed;

    if (mesh == NULL || mesh->positions == NULL || mesh->vertex_count <= 0)
    {
        return closest;
    }

    indexed = mesh->indices != NULL;
    tri_count = indexed ? (mesh->index_count / 3) : (mesh->vertex_count / 3);

    for (int i = 0; i < tri_count; ++i)
    {
        unsigned int i0 = indexed ? mesh->indices[i * 3 + 0] : (unsigned int)(i * 3 + 0);
        unsigned int i1 = indexed ? mesh->indices[i * 3 + 1] : (unsigned int)(i * 3 + 1);
        unsigned int i2 = indexed ? mesh->indices[i * 3 + 2] : (unsigned int)(i * 3 + 2);
        sdl3d_vec3 v0, v1, v2;
        sdl3d_ray_hit h;

        if ((int)i0 >= mesh->vertex_count || (int)i1 >= mesh->vertex_count || (int)i2 >= mesh->vertex_count)
        {
            continue;
        }

        v0 = sdl3d_vec3_make(mesh->positions[i0 * 3], mesh->positions[i0 * 3 + 1], mesh->positions[i0 * 3 + 2]);
        v1 = sdl3d_vec3_make(mesh->positions[i1 * 3], mesh->positions[i1 * 3 + 1], mesh->positions[i1 * 3 + 2]);
        v2 = sdl3d_vec3_make(mesh->positions[i2 * 3], mesh->positions[i2 * 3 + 1], mesh->positions[i2 * 3 + 2]);

        h = sdl3d_ray_vs_triangle(ray, v0, v1, v2);
        if (h.hit && (!closest.hit || h.distance < closest.distance))
        {
            closest = h;
        }
    }

    return closest;
}

/* ------------------------------------------------------------------ */
/* Bounding volume helpers                                             */
/* ------------------------------------------------------------------ */

sdl3d_bounding_box sdl3d_compute_mesh_aabb(const sdl3d_mesh *mesh)
{
    sdl3d_bounding_box box;
    box.min = sdl3d_vec3_make(0, 0, 0);
    box.max = sdl3d_vec3_make(0, 0, 0);

    if (mesh == NULL || mesh->positions == NULL || mesh->vertex_count <= 0)
    {
        return box;
    }

    box.min = sdl3d_vec3_make(mesh->positions[0], mesh->positions[1], mesh->positions[2]);
    box.max = box.min;

    for (int i = 1; i < mesh->vertex_count; ++i)
    {
        float x = mesh->positions[i * 3 + 0];
        float y = mesh->positions[i * 3 + 1];
        float z = mesh->positions[i * 3 + 2];
        if (x < box.min.x)
        {
            box.min.x = x;
        }
        if (y < box.min.y)
        {
            box.min.y = y;
        }
        if (z < box.min.z)
        {
            box.min.z = z;
        }
        if (x > box.max.x)
        {
            box.max.x = x;
        }
        if (y > box.max.y)
        {
            box.max.y = y;
        }
        if (z > box.max.z)
        {
            box.max.z = z;
        }
    }

    return box;
}

/* ------------------------------------------------------------------ */
/* Scene-level raycast                                                 */
/* ------------------------------------------------------------------ */

sdl3d_scene_hit sdl3d_scene_raycast(const sdl3d_scene *scene, sdl3d_ray ray)
{
    sdl3d_scene_hit result;
    int count;
    int i;

    SDL_zerop(&result);

    if (scene == NULL)
    {
        return result;
    }

    count = sdl3d_scene_get_actor_count(scene);

    for (i = 0; i < count; ++i)
    {
        const sdl3d_actor *actor = sdl3d_scene_get_actor_at(scene, i);
        const sdl3d_model *model;
        int m;

        if (actor == NULL || !sdl3d_actor_is_visible(actor))
        {
            continue;
        }

        model = sdl3d_actor_get_model(actor);
        if (model == NULL || model->meshes == NULL)
        {
            continue;
        }

        {
            sdl3d_vec3 actor_pos = sdl3d_actor_get_position(actor);
            sdl3d_ray local_ray;
            local_ray.position.x = ray.position.x - actor_pos.x;
            local_ray.position.y = ray.position.y - actor_pos.y;
            local_ray.position.z = ray.position.z - actor_pos.z;
            local_ray.direction = ray.direction;

            for (m = 0; m < model->mesh_count; ++m)
            {
                sdl3d_bounding_box aabb = sdl3d_compute_mesh_aabb(&model->meshes[m]);
                sdl3d_ray_hit aabb_hit = sdl3d_ray_vs_aabb(local_ray, aabb);
                sdl3d_ray_hit mesh_hit;

                if (!aabb_hit.hit)
                {
                    continue;
                }

                mesh_hit = sdl3d_ray_vs_mesh(local_ray, &model->meshes[m]);
                if (mesh_hit.hit && (!result.hit || mesh_hit.distance < result.distance))
                {
                    result.hit = true;
                    result.distance = mesh_hit.distance;
                    result.point.x = mesh_hit.point.x + actor_pos.x;
                    result.point.y = mesh_hit.point.y + actor_pos.y;
                    result.point.z = mesh_hit.point.z + actor_pos.z;
                    result.normal = mesh_hit.normal;
                    result.actor = actor;
                }
            }
        }
    }

    return result;
}
