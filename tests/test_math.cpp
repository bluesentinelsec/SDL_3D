#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>

extern "C"
{
#include "sdl3d/sdl3d.h"
}

#include <cmath>
#include <string_view>

namespace
{
constexpr float kTol = 1e-5f;

::testing::AssertionResult Near(const char *ae, const char *be, const char *te, float a, float b, float tol)
{
    (void)te;
    if (std::fabs(a - b) <= tol)
    {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << ae << " (" << a << ") not within " << tol << " of " << be << " (" << b
                                         << ")";
}

void ExpectVec3Near(sdl3d_vec3 a, sdl3d_vec3 b, float tol = kTol)
{
    EXPECT_PRED_FORMAT3(Near, a.x, b.x, tol);
    EXPECT_PRED_FORMAT3(Near, a.y, b.y, tol);
    EXPECT_PRED_FORMAT3(Near, a.z, b.z, tol);
}

void ExpectVec4Near(sdl3d_vec4 a, sdl3d_vec4 b, float tol = kTol)
{
    EXPECT_PRED_FORMAT3(Near, a.x, b.x, tol);
    EXPECT_PRED_FORMAT3(Near, a.y, b.y, tol);
    EXPECT_PRED_FORMAT3(Near, a.z, b.z, tol);
    EXPECT_PRED_FORMAT3(Near, a.w, b.w, tol);
}

sdl3d_vec4 TransformPoint(sdl3d_mat4 m, sdl3d_vec3 p)
{
    return sdl3d_mat4_transform_vec4(m, sdl3d_vec4_from_vec3(p, 1.0f));
}
} // namespace

/* --- Scalar helpers ------------------------------------------------------ */

TEST(SDL3DMath, DegreesRadiansRoundTrip)
{
    EXPECT_PRED_FORMAT3(Near, sdl3d_degrees_to_radians(180.0f), 3.14159265f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, sdl3d_radians_to_degrees(3.14159265f), 180.0f, 1e-4f);
}

/* --- Vec3 ---------------------------------------------------------------- */

TEST(SDL3DMathVec3, ArithmeticOps)
{
    const sdl3d_vec3 a = sdl3d_vec3_make(1.0f, 2.0f, 3.0f);
    const sdl3d_vec3 b = sdl3d_vec3_make(-4.0f, 5.0f, 6.0f);
    ExpectVec3Near(sdl3d_vec3_add(a, b), sdl3d_vec3_make(-3.0f, 7.0f, 9.0f));
    ExpectVec3Near(sdl3d_vec3_sub(a, b), sdl3d_vec3_make(5.0f, -3.0f, -3.0f));
    ExpectVec3Near(sdl3d_vec3_scale(a, 2.0f), sdl3d_vec3_make(2.0f, 4.0f, 6.0f));
    ExpectVec3Near(sdl3d_vec3_negate(a), sdl3d_vec3_make(-1.0f, -2.0f, -3.0f));
}

TEST(SDL3DMathVec3, DotProductStandardIdentities)
{
    const sdl3d_vec3 a = sdl3d_vec3_make(1.0f, 0.0f, 0.0f);
    const sdl3d_vec3 b = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    EXPECT_PRED_FORMAT3(Near, sdl3d_vec3_dot(a, b), 0.0f, kTol);
    EXPECT_PRED_FORMAT3(Near, sdl3d_vec3_dot(a, a), 1.0f, kTol);
    EXPECT_PRED_FORMAT3(Near, sdl3d_vec3_dot(sdl3d_vec3_make(2.0f, 3.0f, 4.0f), sdl3d_vec3_make(5.0f, 6.0f, 7.0f)),
                        10.0f + 18.0f + 28.0f, kTol);
}

TEST(SDL3DMathVec3, CrossIsRightHanded)
{
    // Right-handed: X cross Y = Z.
    const sdl3d_vec3 x = sdl3d_vec3_make(1.0f, 0.0f, 0.0f);
    const sdl3d_vec3 y = sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
    ExpectVec3Near(sdl3d_vec3_cross(x, y), sdl3d_vec3_make(0.0f, 0.0f, 1.0f));
    // Anti-commutative: Y cross X = -Z.
    ExpectVec3Near(sdl3d_vec3_cross(y, x), sdl3d_vec3_make(0.0f, 0.0f, -1.0f));
    // Parallel vectors produce zero.
    ExpectVec3Near(sdl3d_vec3_cross(x, x), sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
}

TEST(SDL3DMathVec3, LengthAndNormalize)
{
    const sdl3d_vec3 v = sdl3d_vec3_make(3.0f, 4.0f, 0.0f);
    EXPECT_PRED_FORMAT3(Near, sdl3d_vec3_length(v), 5.0f, kTol);
    EXPECT_PRED_FORMAT3(Near, sdl3d_vec3_length_squared(v), 25.0f, kTol);
    ExpectVec3Near(sdl3d_vec3_normalize(v), sdl3d_vec3_make(0.6f, 0.8f, 0.0f));
}

TEST(SDL3DMathVec3, NormalizeZeroReturnsZero)
{
    ExpectVec3Near(sdl3d_vec3_normalize(sdl3d_vec3_make(0.0f, 0.0f, 0.0f)), sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
}

TEST(SDL3DMathVec3, LerpEndpoints)
{
    const sdl3d_vec3 a = sdl3d_vec3_make(1.0f, 2.0f, 3.0f);
    const sdl3d_vec3 b = sdl3d_vec3_make(5.0f, 7.0f, 9.0f);
    ExpectVec3Near(sdl3d_vec3_lerp(a, b, 0.0f), a);
    ExpectVec3Near(sdl3d_vec3_lerp(a, b, 1.0f), b);
    ExpectVec3Near(sdl3d_vec3_lerp(a, b, 0.5f), sdl3d_vec3_make(3.0f, 4.5f, 6.0f));
}

/* --- Vec4 ---------------------------------------------------------------- */

TEST(SDL3DMathVec4, FromVec3AndLerp)
{
    const sdl3d_vec4 v = sdl3d_vec4_from_vec3(sdl3d_vec3_make(2.0f, 3.0f, 4.0f), 5.0f);
    ExpectVec4Near(v, sdl3d_vec4_make(2.0f, 3.0f, 4.0f, 5.0f));

    const sdl3d_vec4 a = sdl3d_vec4_make(0.0f, 0.0f, 0.0f, 0.0f);
    const sdl3d_vec4 b = sdl3d_vec4_make(10.0f, 20.0f, 30.0f, 40.0f);
    ExpectVec4Near(sdl3d_vec4_lerp(a, b, 0.25f), sdl3d_vec4_make(2.5f, 5.0f, 7.5f, 10.0f));
}

/* --- Mat4: identity / multiply / transform ------------------------------- */

TEST(SDL3DMathMat4, IdentityPreservesVectors)
{
    const sdl3d_mat4 m = sdl3d_mat4_identity();
    const sdl3d_vec4 v = sdl3d_vec4_make(1.0f, 2.0f, 3.0f, 4.0f);
    ExpectVec4Near(sdl3d_mat4_transform_vec4(m, v), v);
}

TEST(SDL3DMathMat4, IdentityIsMultiplicativeIdentity)
{
    const sdl3d_mat4 id = sdl3d_mat4_identity();
    const sdl3d_mat4 t = sdl3d_mat4_translate(sdl3d_vec3_make(1.0f, 2.0f, 3.0f));
    const sdl3d_mat4 left = sdl3d_mat4_multiply(id, t);
    const sdl3d_mat4 right = sdl3d_mat4_multiply(t, id);
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_PRED_FORMAT3(Near, left.m[i], t.m[i], kTol);
        EXPECT_PRED_FORMAT3(Near, right.m[i], t.m[i], kTol);
    }
}

TEST(SDL3DMathMat4, CompositionAppliesRightToLeft)
{
    // M = T(1,0,0) * S(2,2,2). Applied to origin should give (1, 0, 0) in world.
    const sdl3d_mat4 t = sdl3d_mat4_translate(sdl3d_vec3_make(1.0f, 0.0f, 0.0f));
    const sdl3d_mat4 s = sdl3d_mat4_scale(sdl3d_vec3_make(2.0f, 2.0f, 2.0f));
    const sdl3d_mat4 ts = sdl3d_mat4_multiply(t, s);

    ExpectVec4Near(TransformPoint(ts, sdl3d_vec3_make(0.0f, 0.0f, 0.0f)), sdl3d_vec4_make(1.0f, 0.0f, 0.0f, 1.0f));
    // Applied to (1,0,0): first scale to (2,0,0) then translate to (3,0,0).
    ExpectVec4Near(TransformPoint(ts, sdl3d_vec3_make(1.0f, 0.0f, 0.0f)), sdl3d_vec4_make(3.0f, 0.0f, 0.0f, 1.0f));
}

TEST(SDL3DMathMat4, TranslateMovesPoint)
{
    const sdl3d_mat4 t = sdl3d_mat4_translate(sdl3d_vec3_make(10.0f, 20.0f, 30.0f));
    ExpectVec4Near(TransformPoint(t, sdl3d_vec3_make(1.0f, 2.0f, 3.0f)), sdl3d_vec4_make(11.0f, 22.0f, 33.0f, 1.0f));
}

TEST(SDL3DMathMat4, ScaleScalesPoint)
{
    const sdl3d_mat4 s = sdl3d_mat4_scale(sdl3d_vec3_make(2.0f, 3.0f, 4.0f));
    ExpectVec4Near(TransformPoint(s, sdl3d_vec3_make(1.0f, 1.0f, 1.0f)), sdl3d_vec4_make(2.0f, 3.0f, 4.0f, 1.0f));
}

TEST(SDL3DMathMat4, RotateAroundYAxis90DegreesMapsPlusXToMinusZ)
{
    const sdl3d_mat4 r = sdl3d_mat4_rotate(sdl3d_vec3_make(0.0f, 1.0f, 0.0f), sdl3d_degrees_to_radians(90.0f));
    ExpectVec4Near(TransformPoint(r, sdl3d_vec3_make(1.0f, 0.0f, 0.0f)), sdl3d_vec4_make(0.0f, 0.0f, -1.0f, 1.0f),
                   1e-4f);
}

TEST(SDL3DMathMat4, RotateAroundZAxis90DegreesMapsPlusXToPlusY)
{
    const sdl3d_mat4 r = sdl3d_mat4_rotate(sdl3d_vec3_make(0.0f, 0.0f, 1.0f), sdl3d_degrees_to_radians(90.0f));
    ExpectVec4Near(TransformPoint(r, sdl3d_vec3_make(1.0f, 0.0f, 0.0f)), sdl3d_vec4_make(0.0f, 1.0f, 0.0f, 1.0f),
                   1e-4f);
}

/* --- Perspective --------------------------------------------------------- */

TEST(SDL3DMathPerspective, PointOnNearPlaneMapsToMinusOneInNDC)
{
    sdl3d_mat4 p;
    ASSERT_TRUE(sdl3d_mat4_perspective(sdl3d_degrees_to_radians(60.0f), 1.0f, 1.0f, 100.0f, &p)) << SDL_GetError();

    const sdl3d_vec4 clip = TransformPoint(p, sdl3d_vec3_make(0.0f, 0.0f, -1.0f));
    EXPECT_PRED_FORMAT3(Near, clip.z / clip.w, -1.0f, 1e-4f);
}

TEST(SDL3DMathPerspective, PointOnFarPlaneMapsToPlusOneInNDC)
{
    sdl3d_mat4 p;
    ASSERT_TRUE(sdl3d_mat4_perspective(sdl3d_degrees_to_radians(60.0f), 1.0f, 1.0f, 100.0f, &p)) << SDL_GetError();

    const sdl3d_vec4 clip = TransformPoint(p, sdl3d_vec3_make(0.0f, 0.0f, -100.0f));
    EXPECT_PRED_FORMAT3(Near, clip.z / clip.w, 1.0f, 1e-4f);
}

TEST(SDL3DMathPerspective, CenterRayStaysOnAxis)
{
    sdl3d_mat4 p;
    ASSERT_TRUE(sdl3d_mat4_perspective(sdl3d_degrees_to_radians(60.0f), 16.0f / 9.0f, 1.0f, 100.0f, &p));

    const sdl3d_vec4 clip = TransformPoint(p, sdl3d_vec3_make(0.0f, 0.0f, -50.0f));
    EXPECT_PRED_FORMAT3(Near, clip.x / clip.w, 0.0f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, clip.y / clip.w, 0.0f, 1e-5f);
}

TEST(SDL3DMathPerspective, RejectsInvalidParameters)
{
    sdl3d_mat4 p;
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_mat4_perspective(0.0f, 1.0f, 1.0f, 2.0f, &p));
    EXPECT_FALSE(sdl3d_mat4_perspective(1.0f, 0.0f, 1.0f, 2.0f, &p));
    EXPECT_FALSE(sdl3d_mat4_perspective(1.0f, 1.0f, 0.0f, 2.0f, &p));
    EXPECT_FALSE(sdl3d_mat4_perspective(1.0f, 1.0f, 2.0f, 1.0f, &p));
    EXPECT_FALSE(sdl3d_mat4_perspective(1.0f, 1.0f, 1.0f, 2.0f, nullptr));
}

/* --- Orthographic -------------------------------------------------------- */

TEST(SDL3DMathOrthographic, MapsCornersToNDC)
{
    sdl3d_mat4 p;
    ASSERT_TRUE(sdl3d_mat4_orthographic(-10.0f, 10.0f, -5.0f, 5.0f, 1.0f, 100.0f, &p)) << SDL_GetError();

    ExpectVec4Near(TransformPoint(p, sdl3d_vec3_make(-10.0f, -5.0f, -1.0f)), sdl3d_vec4_make(-1.0f, -1.0f, -1.0f, 1.0f),
                   1e-5f);
    ExpectVec4Near(TransformPoint(p, sdl3d_vec3_make(10.0f, 5.0f, -100.0f)), sdl3d_vec4_make(1.0f, 1.0f, 1.0f, 1.0f),
                   1e-5f);
}

TEST(SDL3DMathOrthographic, RejectsInvalidParameters)
{
    sdl3d_mat4 p;
    SDL_ClearError();
    EXPECT_FALSE(sdl3d_mat4_orthographic(1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, &p));
    EXPECT_FALSE(sdl3d_mat4_orthographic(0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, &p));
    EXPECT_FALSE(sdl3d_mat4_orthographic(0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, &p));
    EXPECT_FALSE(sdl3d_mat4_orthographic(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, nullptr));
}

/* --- LookAt -------------------------------------------------------------- */

TEST(SDL3DMathLookAt, EyeAtZ5TargetAtOriginMapsOriginToMinusZ5)
{
    sdl3d_mat4 v;
    ASSERT_TRUE(sdl3d_mat4_look_at(sdl3d_vec3_make(0.0f, 0.0f, 5.0f), sdl3d_vec3_make(0.0f, 0.0f, 0.0f),
                                   sdl3d_vec3_make(0.0f, 1.0f, 0.0f), &v))
        << SDL_GetError();

    // World origin should land in front of the camera along -Z by 5 units.
    ExpectVec4Near(TransformPoint(v, sdl3d_vec3_make(0.0f, 0.0f, 0.0f)), sdl3d_vec4_make(0.0f, 0.0f, -5.0f, 1.0f),
                   1e-5f);
    // The camera's own position maps to the view-space origin.
    ExpectVec4Near(TransformPoint(v, sdl3d_vec3_make(0.0f, 0.0f, 5.0f)), sdl3d_vec4_make(0.0f, 0.0f, 0.0f, 1.0f),
                   1e-5f);
}

TEST(SDL3DMathLookAt, RejectsDegenerateInputs)
{
    sdl3d_mat4 v;
    SDL_ClearError();
    // Eye == target.
    EXPECT_FALSE(sdl3d_mat4_look_at(sdl3d_vec3_make(1.0f, 2.0f, 3.0f), sdl3d_vec3_make(1.0f, 2.0f, 3.0f),
                                    sdl3d_vec3_make(0.0f, 1.0f, 0.0f), &v));
    // Up parallel to forward.
    EXPECT_FALSE(sdl3d_mat4_look_at(sdl3d_vec3_make(0.0f, 0.0f, 5.0f), sdl3d_vec3_make(0.0f, 0.0f, 0.0f),
                                    sdl3d_vec3_make(0.0f, 0.0f, 1.0f), &v));
    // Null output.
    EXPECT_FALSE(sdl3d_mat4_look_at(sdl3d_vec3_make(0.0f, 0.0f, 5.0f), sdl3d_vec3_make(0.0f, 0.0f, 0.0f),
                                    sdl3d_vec3_make(0.0f, 1.0f, 0.0f), nullptr));
}

/* --- Camera -------------------------------------------------------------- */

TEST(SDL3DCamera, PerspectiveMatrixDerivation)
{
    const sdl3d_camera3d camera = {
        {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 60.0f, SDL3D_CAMERA_PERSPECTIVE};
    sdl3d_mat4 view;
    sdl3d_mat4 projection;
    ASSERT_TRUE(sdl3d_camera3d_compute_matrices(&camera, 1600, 900, 1.0f, 100.0f, &view, &projection));

    // A point at the camera target should end up near NDC z = -something (inside frustum).
    const sdl3d_mat4 vp = sdl3d_mat4_multiply(projection, view);
    const sdl3d_vec4 clip = TransformPoint(vp, sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_GT(clip.w, 0.0f);
    const float ndc_z = clip.z / clip.w;
    EXPECT_GT(ndc_z, -1.0f);
    EXPECT_LT(ndc_z, 1.0f);
}

TEST(SDL3DCamera, OrthographicProducesUnitNDC)
{
    const sdl3d_camera3d camera = {
        {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 10.0f, SDL3D_CAMERA_ORTHOGRAPHIC};
    sdl3d_mat4 view;
    sdl3d_mat4 projection;
    ASSERT_TRUE(sdl3d_camera3d_compute_matrices(&camera, 100, 100, 1.0f, 100.0f, &view, &projection));

    const sdl3d_mat4 vp = sdl3d_mat4_multiply(projection, view);
    const sdl3d_vec4 clip_top = TransformPoint(vp, sdl3d_vec3_make(0.0f, 5.0f, 0.0f));
    EXPECT_PRED_FORMAT3(Near, clip_top.y / clip_top.w, 1.0f, 1e-4f);
    const sdl3d_vec4 clip_bot = TransformPoint(vp, sdl3d_vec3_make(0.0f, -5.0f, 0.0f));
    EXPECT_PRED_FORMAT3(Near, clip_bot.y / clip_bot.w, -1.0f, 1e-4f);
}

TEST(SDL3DCamera, RejectsInvalidArguments)
{
    const sdl3d_camera3d camera = {
        {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 60.0f, SDL3D_CAMERA_PERSPECTIVE};
    sdl3d_mat4 view;
    sdl3d_mat4 projection;

    SDL_ClearError();
    EXPECT_FALSE(sdl3d_camera3d_compute_matrices(nullptr, 100, 100, 1.0f, 100.0f, &view, &projection));
    EXPECT_FALSE(sdl3d_camera3d_compute_matrices(&camera, 0, 100, 1.0f, 100.0f, &view, &projection));
    EXPECT_FALSE(sdl3d_camera3d_compute_matrices(&camera, 100, 0, 1.0f, 100.0f, &view, &projection));
    EXPECT_FALSE(sdl3d_camera3d_compute_matrices(&camera, 100, 100, 1.0f, 100.0f, nullptr, &projection));
    EXPECT_FALSE(sdl3d_camera3d_compute_matrices(&camera, 100, 100, 1.0f, 100.0f, &view, nullptr));
}
