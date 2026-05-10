#include "slayer3d/math.h"

#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

static const float SLAYER3D_PI = 3.14159265358979323846f;

float slayer3d_degrees_to_radians(float degrees)
{
    return degrees * (SLAYER3D_PI / 180.0f);
}

float slayer3d_radians_to_degrees(float radians)
{
    return radians * (180.0f / SLAYER3D_PI);
}

slayer3d_vec3 slayer3d_vec3_make(float x, float y, float z)
{
    slayer3d_vec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

slayer3d_vec3 slayer3d_vec3_add(slayer3d_vec3 a, slayer3d_vec3 b)
{
    return slayer3d_vec3_make(a.x + b.x, a.y + b.y, a.z + b.z);
}

slayer3d_vec3 slayer3d_vec3_sub(slayer3d_vec3 a, slayer3d_vec3 b)
{
    return slayer3d_vec3_make(a.x - b.x, a.y - b.y, a.z - b.z);
}

slayer3d_vec3 slayer3d_vec3_scale(slayer3d_vec3 v, float s)
{
    return slayer3d_vec3_make(v.x * s, v.y * s, v.z * s);
}

slayer3d_vec3 slayer3d_vec3_negate(slayer3d_vec3 v)
{
    return slayer3d_vec3_make(-v.x, -v.y, -v.z);
}

float slayer3d_vec3_dot(slayer3d_vec3 a, slayer3d_vec3 b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

slayer3d_vec3 slayer3d_vec3_cross(slayer3d_vec3 a, slayer3d_vec3 b)
{
    return slayer3d_vec3_make((a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x));
}

float slayer3d_vec3_length_squared(slayer3d_vec3 v)
{
    return slayer3d_vec3_dot(v, v);
}

float slayer3d_vec3_length(slayer3d_vec3 v)
{
    return SDL_sqrtf(slayer3d_vec3_length_squared(v));
}

slayer3d_vec3 slayer3d_vec3_normalize(slayer3d_vec3 v)
{
    const float length_squared = slayer3d_vec3_length_squared(v);
    if (length_squared <= 0.0f)
    {
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    }

    const float inverse_length = 1.0f / SDL_sqrtf(length_squared);
    return slayer3d_vec3_scale(v, inverse_length);
}

slayer3d_vec3 slayer3d_vec3_lerp(slayer3d_vec3 a, slayer3d_vec3 b, float t)
{
    return slayer3d_vec3_add(a, slayer3d_vec3_scale(slayer3d_vec3_sub(b, a), t));
}

slayer3d_vec4 slayer3d_vec4_make(float x, float y, float z, float w)
{
    slayer3d_vec4 v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.w = w;
    return v;
}

slayer3d_vec4 slayer3d_vec4_from_vec3(slayer3d_vec3 v, float w)
{
    return slayer3d_vec4_make(v.x, v.y, v.z, w);
}

slayer3d_vec4 slayer3d_vec4_add(slayer3d_vec4 a, slayer3d_vec4 b)
{
    return slayer3d_vec4_make(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}

slayer3d_vec4 slayer3d_vec4_scale(slayer3d_vec4 v, float s)
{
    return slayer3d_vec4_make(v.x * s, v.y * s, v.z * s, v.w * s);
}

slayer3d_vec4 slayer3d_vec4_lerp(slayer3d_vec4 a, slayer3d_vec4 b, float t)
{
    return slayer3d_vec4_make(a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t),
                              a.w + ((b.w - a.w) * t));
}

slayer3d_mat4 slayer3d_mat4_identity(void)
{
    slayer3d_mat4 m;
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
static float slayer3d_mat4_at(const slayer3d_mat4 *m, int row, int col)
{
    return m->m[(col * 4) + row];
}

static void slayer3d_mat4_set(slayer3d_mat4 *m, int row, int col, float value)
{
    m->m[(col * 4) + row] = value;
}

slayer3d_mat4 slayer3d_mat4_multiply(slayer3d_mat4 a, slayer3d_mat4 b)
{
    slayer3d_mat4 out;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                sum += slayer3d_mat4_at(&a, row, k) * slayer3d_mat4_at(&b, k, col);
            }
            slayer3d_mat4_set(&out, row, col, sum);
        }
    }
    return out;
}

slayer3d_vec4 slayer3d_mat4_transform_vec4(slayer3d_mat4 m, slayer3d_vec4 v)
{
    slayer3d_vec4 out;
    out.x = (slayer3d_mat4_at(&m, 0, 0) * v.x) + (slayer3d_mat4_at(&m, 0, 1) * v.y) +
            (slayer3d_mat4_at(&m, 0, 2) * v.z) + (slayer3d_mat4_at(&m, 0, 3) * v.w);
    out.y = (slayer3d_mat4_at(&m, 1, 0) * v.x) + (slayer3d_mat4_at(&m, 1, 1) * v.y) +
            (slayer3d_mat4_at(&m, 1, 2) * v.z) + (slayer3d_mat4_at(&m, 1, 3) * v.w);
    out.z = (slayer3d_mat4_at(&m, 2, 0) * v.x) + (slayer3d_mat4_at(&m, 2, 1) * v.y) +
            (slayer3d_mat4_at(&m, 2, 2) * v.z) + (slayer3d_mat4_at(&m, 2, 3) * v.w);
    out.w = (slayer3d_mat4_at(&m, 3, 0) * v.x) + (slayer3d_mat4_at(&m, 3, 1) * v.y) +
            (slayer3d_mat4_at(&m, 3, 2) * v.z) + (slayer3d_mat4_at(&m, 3, 3) * v.w);
    return out;
}

slayer3d_mat4 slayer3d_mat4_translate(slayer3d_vec3 translation)
{
    slayer3d_mat4 m = slayer3d_mat4_identity();
    slayer3d_mat4_set(&m, 0, 3, translation.x);
    slayer3d_mat4_set(&m, 1, 3, translation.y);
    slayer3d_mat4_set(&m, 2, 3, translation.z);
    return m;
}

slayer3d_mat4 slayer3d_mat4_scale(slayer3d_vec3 scale)
{
    slayer3d_mat4 m = slayer3d_mat4_identity();
    slayer3d_mat4_set(&m, 0, 0, scale.x);
    slayer3d_mat4_set(&m, 1, 1, scale.y);
    slayer3d_mat4_set(&m, 2, 2, scale.z);
    return m;
}

slayer3d_mat4 slayer3d_mat4_rotate(slayer3d_vec3 axis, float angle_radians)
{
    /* Rodrigues rotation formula, right-handed. */
    const slayer3d_vec3 n = slayer3d_vec3_normalize(axis);
    const float c = SDL_cosf(angle_radians);
    const float s = SDL_sinf(angle_radians);
    const float one_minus_c = 1.0f - c;

    slayer3d_mat4 m = slayer3d_mat4_identity();
    slayer3d_mat4_set(&m, 0, 0, c + (n.x * n.x * one_minus_c));
    slayer3d_mat4_set(&m, 0, 1, (n.x * n.y * one_minus_c) - (n.z * s));
    slayer3d_mat4_set(&m, 0, 2, (n.x * n.z * one_minus_c) + (n.y * s));

    slayer3d_mat4_set(&m, 1, 0, (n.y * n.x * one_minus_c) + (n.z * s));
    slayer3d_mat4_set(&m, 1, 1, c + (n.y * n.y * one_minus_c));
    slayer3d_mat4_set(&m, 1, 2, (n.y * n.z * one_minus_c) - (n.x * s));

    slayer3d_mat4_set(&m, 2, 0, (n.z * n.x * one_minus_c) - (n.y * s));
    slayer3d_mat4_set(&m, 2, 1, (n.z * n.y * one_minus_c) + (n.x * s));
    slayer3d_mat4_set(&m, 2, 2, c + (n.z * n.z * one_minus_c));
    return m;
}

bool slayer3d_mat4_perspective(float fovy_radians, float aspect, float near_plane, float far_plane,
                               slayer3d_mat4 *out_matrix)
{
    if (out_matrix == NULL)
    {
        return SDL_InvalidParamError("out_matrix");
    }

    if (!(aspect > 0.0f))
    {
        return SDL_SetError("Perspective aspect must be positive.");
    }

    if (!(fovy_radians > 0.0f) || !(fovy_radians < SLAYER3D_PI))
    {
        return SDL_SetError("Perspective fovy_radians must lie in (0, PI).");
    }

    if (!(near_plane > 0.0f) || !(far_plane > near_plane))
    {
        return SDL_SetError("Perspective requires 0 < near_plane < far_plane.");
    }

    const float f = 1.0f / SDL_tanf(fovy_radians * 0.5f);
    const float depth_range = near_plane - far_plane; /* negative */

    slayer3d_mat4 m;
    for (int i = 0; i < 16; ++i)
    {
        m.m[i] = 0.0f;
    }

    slayer3d_mat4_set(&m, 0, 0, f / aspect);
    slayer3d_mat4_set(&m, 1, 1, f);
    slayer3d_mat4_set(&m, 2, 2, (far_plane + near_plane) / depth_range);
    slayer3d_mat4_set(&m, 2, 3, (2.0f * far_plane * near_plane) / depth_range);
    slayer3d_mat4_set(&m, 3, 2, -1.0f);

    *out_matrix = m;
    return true;
}

bool slayer3d_mat4_orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane,
                                slayer3d_mat4 *out_matrix)
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

    slayer3d_mat4 m;
    for (int i = 0; i < 16; ++i)
    {
        m.m[i] = 0.0f;
    }

    slayer3d_mat4_set(&m, 0, 0, 2.0f / rl);
    slayer3d_mat4_set(&m, 1, 1, 2.0f / tb);
    slayer3d_mat4_set(&m, 2, 2, -2.0f / fn);
    slayer3d_mat4_set(&m, 0, 3, -(right + left) / rl);
    slayer3d_mat4_set(&m, 1, 3, -(top + bottom) / tb);
    slayer3d_mat4_set(&m, 2, 3, -(far_plane + near_plane) / fn);
    slayer3d_mat4_set(&m, 3, 3, 1.0f);

    *out_matrix = m;
    return true;
}

bool slayer3d_mat4_look_at(slayer3d_vec3 eye, slayer3d_vec3 target, slayer3d_vec3 up, slayer3d_mat4 *out_matrix)
{
    if (out_matrix == NULL)
    {
        return SDL_InvalidParamError("out_matrix");
    }

    const slayer3d_vec3 forward = slayer3d_vec3_sub(target, eye);
    if (slayer3d_vec3_length_squared(forward) <= 0.0f)
    {
        return SDL_SetError("look_at: eye and target must differ.");
    }

    const slayer3d_vec3 f = slayer3d_vec3_normalize(forward);
    const slayer3d_vec3 s_raw = slayer3d_vec3_cross(f, up);
    if (slayer3d_vec3_length_squared(s_raw) <= 0.0f)
    {
        return SDL_SetError("look_at: up must not be parallel to (target - eye).");
    }

    const slayer3d_vec3 s = slayer3d_vec3_normalize(s_raw);
    const slayer3d_vec3 u = slayer3d_vec3_cross(s, f);

    slayer3d_mat4 m = slayer3d_mat4_identity();
    slayer3d_mat4_set(&m, 0, 0, s.x);
    slayer3d_mat4_set(&m, 0, 1, s.y);
    slayer3d_mat4_set(&m, 0, 2, s.z);
    slayer3d_mat4_set(&m, 1, 0, u.x);
    slayer3d_mat4_set(&m, 1, 1, u.y);
    slayer3d_mat4_set(&m, 1, 2, u.z);
    slayer3d_mat4_set(&m, 2, 0, -f.x);
    slayer3d_mat4_set(&m, 2, 1, -f.y);
    slayer3d_mat4_set(&m, 2, 2, -f.z);
    slayer3d_mat4_set(&m, 0, 3, -slayer3d_vec3_dot(s, eye));
    slayer3d_mat4_set(&m, 1, 3, -slayer3d_vec3_dot(u, eye));
    slayer3d_mat4_set(&m, 2, 3, slayer3d_vec3_dot(f, eye));
    *out_matrix = m;
    return true;
}
