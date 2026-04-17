#include "sdl3d/math.h"

#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

static const float SDL3D_PI = 3.14159265358979323846f;

float sdl3d_degrees_to_radians(float degrees)
{
    return degrees * (SDL3D_PI / 180.0f);
}

float sdl3d_radians_to_degrees(float radians)
{
    return radians * (180.0f / SDL3D_PI);
}

sdl3d_vec3 sdl3d_vec3_make(float x, float y, float z)
{
    sdl3d_vec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

sdl3d_vec3 sdl3d_vec3_add(sdl3d_vec3 a, sdl3d_vec3 b)
{
    return sdl3d_vec3_make(a.x + b.x, a.y + b.y, a.z + b.z);
}

sdl3d_vec3 sdl3d_vec3_sub(sdl3d_vec3 a, sdl3d_vec3 b)
{
    return sdl3d_vec3_make(a.x - b.x, a.y - b.y, a.z - b.z);
}

sdl3d_vec3 sdl3d_vec3_scale(sdl3d_vec3 v, float s)
{
    return sdl3d_vec3_make(v.x * s, v.y * s, v.z * s);
}

sdl3d_vec3 sdl3d_vec3_negate(sdl3d_vec3 v)
{
    return sdl3d_vec3_make(-v.x, -v.y, -v.z);
}

float sdl3d_vec3_dot(sdl3d_vec3 a, sdl3d_vec3 b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

sdl3d_vec3 sdl3d_vec3_cross(sdl3d_vec3 a, sdl3d_vec3 b)
{
    return sdl3d_vec3_make((a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x));
}

float sdl3d_vec3_length_squared(sdl3d_vec3 v)
{
    return sdl3d_vec3_dot(v, v);
}

float sdl3d_vec3_length(sdl3d_vec3 v)
{
    return SDL_sqrtf(sdl3d_vec3_length_squared(v));
}

sdl3d_vec3 sdl3d_vec3_normalize(sdl3d_vec3 v)
{
    const float length_squared = sdl3d_vec3_length_squared(v);
    if (length_squared <= 0.0f)
    {
        return sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    }

    const float inverse_length = 1.0f / SDL_sqrtf(length_squared);
    return sdl3d_vec3_scale(v, inverse_length);
}

sdl3d_vec3 sdl3d_vec3_lerp(sdl3d_vec3 a, sdl3d_vec3 b, float t)
{
    return sdl3d_vec3_add(a, sdl3d_vec3_scale(sdl3d_vec3_sub(b, a), t));
}

sdl3d_vec4 sdl3d_vec4_make(float x, float y, float z, float w)
{
    sdl3d_vec4 v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.w = w;
    return v;
}

sdl3d_vec4 sdl3d_vec4_from_vec3(sdl3d_vec3 v, float w)
{
    return sdl3d_vec4_make(v.x, v.y, v.z, w);
}

sdl3d_vec4 sdl3d_vec4_add(sdl3d_vec4 a, sdl3d_vec4 b)
{
    return sdl3d_vec4_make(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}

sdl3d_vec4 sdl3d_vec4_scale(sdl3d_vec4 v, float s)
{
    return sdl3d_vec4_make(v.x * s, v.y * s, v.z * s, v.w * s);
}

sdl3d_vec4 sdl3d_vec4_lerp(sdl3d_vec4 a, sdl3d_vec4 b, float t)
{
    return sdl3d_vec4_make(a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t),
                           a.w + ((b.w - a.w) * t));
}

sdl3d_mat4 sdl3d_mat4_identity(void)
{
    sdl3d_mat4 m;
    for (int i = 0; i < 16; ++i)
    {
        m.m[i] = 0.0f;
    }
    m.m[0] = 1.0f;
    m.m[5] = 1.0f;
    m.m[10] = 1.0f;
    m.m[15] = 1.0f;
    return m;
}

/*
 * Column-major indexing helper: m[col * 4 + row].
 */
static float sdl3d_mat4_at(const sdl3d_mat4 *m, int row, int col)
{
    return m->m[(col * 4) + row];
}

static void sdl3d_mat4_set(sdl3d_mat4 *m, int row, int col, float value)
{
    m->m[(col * 4) + row] = value;
}

sdl3d_mat4 sdl3d_mat4_multiply(sdl3d_mat4 a, sdl3d_mat4 b)
{
    sdl3d_mat4 out;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                sum += sdl3d_mat4_at(&a, row, k) * sdl3d_mat4_at(&b, k, col);
            }
            sdl3d_mat4_set(&out, row, col, sum);
        }
    }
    return out;
}

sdl3d_vec4 sdl3d_mat4_transform_vec4(sdl3d_mat4 m, sdl3d_vec4 v)
{
    sdl3d_vec4 out;
    out.x = (sdl3d_mat4_at(&m, 0, 0) * v.x) + (sdl3d_mat4_at(&m, 0, 1) * v.y) + (sdl3d_mat4_at(&m, 0, 2) * v.z) +
            (sdl3d_mat4_at(&m, 0, 3) * v.w);
    out.y = (sdl3d_mat4_at(&m, 1, 0) * v.x) + (sdl3d_mat4_at(&m, 1, 1) * v.y) + (sdl3d_mat4_at(&m, 1, 2) * v.z) +
            (sdl3d_mat4_at(&m, 1, 3) * v.w);
    out.z = (sdl3d_mat4_at(&m, 2, 0) * v.x) + (sdl3d_mat4_at(&m, 2, 1) * v.y) + (sdl3d_mat4_at(&m, 2, 2) * v.z) +
            (sdl3d_mat4_at(&m, 2, 3) * v.w);
    out.w = (sdl3d_mat4_at(&m, 3, 0) * v.x) + (sdl3d_mat4_at(&m, 3, 1) * v.y) + (sdl3d_mat4_at(&m, 3, 2) * v.z) +
            (sdl3d_mat4_at(&m, 3, 3) * v.w);
    return out;
}

sdl3d_mat4 sdl3d_mat4_translate(sdl3d_vec3 translation)
{
    sdl3d_mat4 m = sdl3d_mat4_identity();
    sdl3d_mat4_set(&m, 0, 3, translation.x);
    sdl3d_mat4_set(&m, 1, 3, translation.y);
    sdl3d_mat4_set(&m, 2, 3, translation.z);
    return m;
}

sdl3d_mat4 sdl3d_mat4_scale(sdl3d_vec3 scale)
{
    sdl3d_mat4 m = sdl3d_mat4_identity();
    sdl3d_mat4_set(&m, 0, 0, scale.x);
    sdl3d_mat4_set(&m, 1, 1, scale.y);
    sdl3d_mat4_set(&m, 2, 2, scale.z);
    return m;
}

sdl3d_mat4 sdl3d_mat4_rotate(sdl3d_vec3 axis, float angle_radians)
{
    /* Rodrigues rotation formula, right-handed. */
    const sdl3d_vec3 n = sdl3d_vec3_normalize(axis);
    const float c = SDL_cosf(angle_radians);
    const float s = SDL_sinf(angle_radians);
    const float one_minus_c = 1.0f - c;

    sdl3d_mat4 m = sdl3d_mat4_identity();
    sdl3d_mat4_set(&m, 0, 0, c + (n.x * n.x * one_minus_c));
    sdl3d_mat4_set(&m, 0, 1, (n.x * n.y * one_minus_c) - (n.z * s));
    sdl3d_mat4_set(&m, 0, 2, (n.x * n.z * one_minus_c) + (n.y * s));

    sdl3d_mat4_set(&m, 1, 0, (n.y * n.x * one_minus_c) + (n.z * s));
    sdl3d_mat4_set(&m, 1, 1, c + (n.y * n.y * one_minus_c));
    sdl3d_mat4_set(&m, 1, 2, (n.y * n.z * one_minus_c) - (n.x * s));

    sdl3d_mat4_set(&m, 2, 0, (n.z * n.x * one_minus_c) - (n.y * s));
    sdl3d_mat4_set(&m, 2, 1, (n.z * n.y * one_minus_c) + (n.x * s));
    sdl3d_mat4_set(&m, 2, 2, c + (n.z * n.z * one_minus_c));
    return m;
}

bool sdl3d_mat4_perspective(float fovy_radians, float aspect, float near_plane, float far_plane, sdl3d_mat4 *out_matrix)
{
    if (out_matrix == NULL)
    {
        return SDL_InvalidParamError("out_matrix");
    }

    if (!(aspect > 0.0f))
    {
        return SDL_SetError("Perspective aspect must be positive.");
    }

    if (!(fovy_radians > 0.0f) || !(fovy_radians < SDL3D_PI))
    {
        return SDL_SetError("Perspective fovy_radians must lie in (0, PI).");
    }

    if (!(near_plane > 0.0f) || !(far_plane > near_plane))
    {
        return SDL_SetError("Perspective requires 0 < near_plane < far_plane.");
    }

    const float f = 1.0f / SDL_tanf(fovy_radians * 0.5f);
    const float depth_range = near_plane - far_plane; /* negative */

    sdl3d_mat4 m;
    for (int i = 0; i < 16; ++i)
    {
        m.m[i] = 0.0f;
    }

    sdl3d_mat4_set(&m, 0, 0, f / aspect);
    sdl3d_mat4_set(&m, 1, 1, f);
    sdl3d_mat4_set(&m, 2, 2, (far_plane + near_plane) / depth_range);
    sdl3d_mat4_set(&m, 2, 3, (2.0f * far_plane * near_plane) / depth_range);
    sdl3d_mat4_set(&m, 3, 2, -1.0f);

    *out_matrix = m;
    return true;
}

bool sdl3d_mat4_orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane,
                             sdl3d_mat4 *out_matrix)
{
    if (out_matrix == NULL)
    {
        return SDL_InvalidParamError("out_matrix");
    }

    if (!(right > left) || !(top > bottom) || !(far_plane > near_plane))
    {
        return SDL_SetError("Orthographic requires left<right, bottom<top, near<far.");
    }

    const float rl = right - left;
    const float tb = top - bottom;
    const float fn = far_plane - near_plane;

    sdl3d_mat4 m;
    for (int i = 0; i < 16; ++i)
    {
        m.m[i] = 0.0f;
    }

    sdl3d_mat4_set(&m, 0, 0, 2.0f / rl);
    sdl3d_mat4_set(&m, 1, 1, 2.0f / tb);
    sdl3d_mat4_set(&m, 2, 2, -2.0f / fn);
    sdl3d_mat4_set(&m, 0, 3, -(right + left) / rl);
    sdl3d_mat4_set(&m, 1, 3, -(top + bottom) / tb);
    sdl3d_mat4_set(&m, 2, 3, -(far_plane + near_plane) / fn);
    sdl3d_mat4_set(&m, 3, 3, 1.0f);

    *out_matrix = m;
    return true;
}

bool sdl3d_mat4_look_at(sdl3d_vec3 eye, sdl3d_vec3 target, sdl3d_vec3 up, sdl3d_mat4 *out_matrix)
{
    if (out_matrix == NULL)
    {
        return SDL_InvalidParamError("out_matrix");
    }

    const sdl3d_vec3 forward = sdl3d_vec3_sub(target, eye);
    if (sdl3d_vec3_length_squared(forward) <= 0.0f)
    {
        return SDL_SetError("look_at: eye and target must differ.");
    }

    const sdl3d_vec3 f = sdl3d_vec3_normalize(forward);
    const sdl3d_vec3 s_raw = sdl3d_vec3_cross(f, up);
    if (sdl3d_vec3_length_squared(s_raw) <= 0.0f)
    {
        return SDL_SetError("look_at: up must not be parallel to (target - eye).");
    }

    const sdl3d_vec3 s = sdl3d_vec3_normalize(s_raw);
    const sdl3d_vec3 u = sdl3d_vec3_cross(s, f);

    sdl3d_mat4 m = sdl3d_mat4_identity();
    sdl3d_mat4_set(&m, 0, 0, s.x);
    sdl3d_mat4_set(&m, 0, 1, s.y);
    sdl3d_mat4_set(&m, 0, 2, s.z);
    sdl3d_mat4_set(&m, 1, 0, u.x);
    sdl3d_mat4_set(&m, 1, 1, u.y);
    sdl3d_mat4_set(&m, 1, 2, u.z);
    sdl3d_mat4_set(&m, 2, 0, -f.x);
    sdl3d_mat4_set(&m, 2, 1, -f.y);
    sdl3d_mat4_set(&m, 2, 2, -f.z);
    sdl3d_mat4_set(&m, 0, 3, -sdl3d_vec3_dot(s, eye));
    sdl3d_mat4_set(&m, 1, 3, -sdl3d_vec3_dot(u, eye));
    sdl3d_mat4_set(&m, 2, 3, sdl3d_vec3_dot(f, eye));
    *out_matrix = m;
    return true;
}
