#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>

extern "C"
{
#include "slayer3d/slayer3d.h"
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

void ExpectVec3Near(slayer3d_vec3 a, slayer3d_vec3 b, float tol = kTol)
{
    EXPECT_PRED_FORMAT3(Near, a.x, b.x, tol);
    EXPECT_PRED_FORMAT3(Near, a.y, b.y, tol);
    EXPECT_PRED_FORMAT3(Near, a.z, b.z, tol);
}

void ExpectVec4Near(slayer3d_vec4 a, slayer3d_vec4 b, float tol = kTol)
{
    EXPECT_PRED_FORMAT3(Near, a.x, b.x, tol);
    EXPECT_PRED_FORMAT3(Near, a.y, b.y, tol);
    EXPECT_PRED_FORMAT3(Near, a.z, b.z, tol);
    EXPECT_PRED_FORMAT3(Near, a.w, b.w, tol);
}

slayer3d_vec4 TransformPoint(slayer3d_mat4 m, slayer3d_vec3 p)
{
    return slayer3d_mat4_transform_vec4(m, slayer3d_vec4_from_vec3(p, 1.0f));
}
} // namespace

/* --- Scalar helpers ------------------------------------------------------ */

TEST(SLAYER3DMath, DegreesRadiansRoundTrip)
{
    EXPECT_PRED_FORMAT3(Near, slayer3d_degrees_to_radians(180.0f), 3.14159265f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, slayer3d_radians_to_degrees(3.14159265f), 180.0f, 1e-4f);
}

/* --- Vec3 ---------------------------------------------------------------- */

TEST(SLAYER3DMathVec3, ArithmeticOps)
{
    const slayer3d_vec3 a = slayer3d_vec3_make(1.0f, 2.0f, 3.0f);
    const slayer3d_vec3 b = slayer3d_vec3_make(-4.0f, 5.0f, 6.0f);
    ExpectVec3Near(slayer3d_vec3_add(a, b), slayer3d_vec3_make(-3.0f, 7.0f, 9.0f));
    ExpectVec3Near(slayer3d_vec3_sub(a, b), slayer3d_vec3_make(5.0f, -3.0f, -3.0f));
    ExpectVec3Near(slayer3d_vec3_scale(a, 2.0f), slayer3d_vec3_make(2.0f, 4.0f, 6.0f));
    ExpectVec3Near(slayer3d_vec3_negate(a), slayer3d_vec3_make(-1.0f, -2.0f, -3.0f));
}

TEST(SLAYER3DMathVec3, DotProductStandardIdentities)
{
    const slayer3d_vec3 a = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    const slayer3d_vec3 b = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    EXPECT_PRED_FORMAT3(Near, slayer3d_vec3_dot(a, b), 0.0f, kTol);
    EXPECT_PRED_FORMAT3(Near, slayer3d_vec3_dot(a, a), 1.0f, kTol);
    EXPECT_PRED_FORMAT3(Near,
                        slayer3d_vec3_dot(slayer3d_vec3_make(2.0f, 3.0f, 4.0f), slayer3d_vec3_make(5.0f, 6.0f, 7.0f)),
                        10.0f + 18.0f + 28.0f, kTol);
}

TEST(SLAYER3DMathVec3, CrossIsRightHanded)
{
    // Right-handed: X cross Y = Z.
    const slayer3d_vec3 x = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    const slayer3d_vec3 y = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    ExpectVec3Near(slayer3d_vec3_cross(x, y), slayer3d_vec3_make(0.0f, 0.0f, 1.0f));
    // Anti-commutative: Y cross X = -Z.
    ExpectVec3Near(slayer3d_vec3_cross(y, x), slayer3d_vec3_make(0.0f, 0.0f, -1.0f));
    // Parallel vectors produce zero.
    ExpectVec3Near(slayer3d_vec3_cross(x, x), slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

TEST(SLAYER3DMathVec3, LengthAndNormalize)
{
    const slayer3d_vec3 v = slayer3d_vec3_make(3.0f, 4.0f, 0.0f);
    EXPECT_PRED_FORMAT3(Near, slayer3d_vec3_length(v), 5.0f, kTol);
    EXPECT_PRED_FORMAT3(Near, slayer3d_vec3_length_squared(v), 25.0f, kTol);
    ExpectVec3Near(slayer3d_vec3_normalize(v), slayer3d_vec3_make(0.6f, 0.8f, 0.0f));
}

TEST(SLAYER3DMathVec3, NormalizeZeroReturnsZero)
{
    ExpectVec3Near(slayer3d_vec3_normalize(slayer3d_vec3_make(0.0f, 0.0f, 0.0f)), slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

TEST(SLAYER3DMathVec3, LerpEndpoints)
{
    const slayer3d_vec3 a = slayer3d_vec3_make(1.0f, 2.0f, 3.0f);
    const slayer3d_vec3 b = slayer3d_vec3_make(5.0f, 7.0f, 9.0f);
    ExpectVec3Near(slayer3d_vec3_lerp(a, b, 0.0f), a);
    ExpectVec3Near(slayer3d_vec3_lerp(a, b, 1.0f), b);
    ExpectVec3Near(slayer3d_vec3_lerp(a, b, 0.5f), slayer3d_vec3_make(3.0f, 4.5f, 6.0f));
}

/* --- Vec4 ---------------------------------------------------------------- */

TEST(SLAYER3DMathVec4, FromVec3AndLerp)
{
    const slayer3d_vec4 v = slayer3d_vec4_from_vec3(slayer3d_vec3_make(2.0f, 3.0f, 4.0f), 5.0f);
    ExpectVec4Near(v, slayer3d_vec4_make(2.0f, 3.0f, 4.0f, 5.0f));

    const slayer3d_vec4 a = slayer3d_vec4_make(0.0f, 0.0f, 0.0f, 0.0f);
    const slayer3d_vec4 b = slayer3d_vec4_make(10.0f, 20.0f, 30.0f, 40.0f);
    ExpectVec4Near(slayer3d_vec4_lerp(a, b, 0.25f), slayer3d_vec4_make(2.5f, 5.0f, 7.5f, 10.0f));
}

/* --- Mat4: identity / multiply / transform ------------------------------- */

TEST(SLAYER3DMathMat4, IdentityPreservesVectors)
{
    const slayer3d_mat4 m = slayer3d_mat4_identity();
    const slayer3d_vec4 v = slayer3d_vec4_make(1.0f, 2.0f, 3.0f, 4.0f);
    ExpectVec4Near(slayer3d_mat4_transform_vec4(m, v), v);
}

TEST(SLAYER3DMathMat4, IdentityIsMultiplicativeIdentity)
{
    const slayer3d_mat4 id = slayer3d_mat4_identity();
    const slayer3d_mat4 t = slayer3d_mat4_translate(slayer3d_vec3_make(1.0f, 2.0f, 3.0f));
    const slayer3d_mat4 left = slayer3d_mat4_multiply(id, t);
    const slayer3d_mat4 right = slayer3d_mat4_multiply(t, id);
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_PRED_FORMAT3(Near, left.m[i], t.m[i], kTol);
        EXPECT_PRED_FORMAT3(Near, right.m[i], t.m[i], kTol);
    }
}

TEST(SLAYER3DMathMat4, CompositionAppliesRightToLeft)
{
    // M = T(1,0,0) * S(2,2,2). Applied to origin should give (1, 0, 0) in world.
    const slayer3d_mat4 t = slayer3d_mat4_translate(slayer3d_vec3_make(1.0f, 0.0f, 0.0f));
    const slayer3d_mat4 s = slayer3d_mat4_scale(slayer3d_vec3_make(2.0f, 2.0f, 2.0f));
    const slayer3d_mat4 ts = slayer3d_mat4_multiply(t, s);

    ExpectVec4Near(TransformPoint(ts, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)),
                   slayer3d_vec4_make(1.0f, 0.0f, 0.0f, 1.0f));
    // Applied to (1,0,0): first scale to (2,0,0) then translate to (3,0,0).
    ExpectVec4Near(TransformPoint(ts, slayer3d_vec3_make(1.0f, 0.0f, 0.0f)),
                   slayer3d_vec4_make(3.0f, 0.0f, 0.0f, 1.0f));
}

TEST(SLAYER3DMathMat4, TranslateMovesPoint)
{
    const slayer3d_mat4 t = slayer3d_mat4_translate(slayer3d_vec3_make(10.0f, 20.0f, 30.0f));
    ExpectVec4Near(TransformPoint(t, slayer3d_vec3_make(1.0f, 2.0f, 3.0f)),
                   slayer3d_vec4_make(11.0f, 22.0f, 33.0f, 1.0f));
}

TEST(SLAYER3DMathMat4, ScaleScalesPoint)
{
    const slayer3d_mat4 s = slayer3d_mat4_scale(slayer3d_vec3_make(2.0f, 3.0f, 4.0f));
    ExpectVec4Near(TransformPoint(s, slayer3d_vec3_make(1.0f, 1.0f, 1.0f)), slayer3d_vec4_make(2.0f, 3.0f, 4.0f, 1.0f));
}

TEST(SLAYER3DMathMat4, RotateAroundYAxis90DegreesMapsPlusXToMinusZ)
{
    const slayer3d_mat4 r =
        slayer3d_mat4_rotate(slayer3d_vec3_make(0.0f, 1.0f, 0.0f), slayer3d_degrees_to_radians(90.0f));
    ExpectVec4Near(TransformPoint(r, slayer3d_vec3_make(1.0f, 0.0f, 0.0f)), slayer3d_vec4_make(0.0f, 0.0f, -1.0f, 1.0f),
                   1e-4f);
}

TEST(SLAYER3DMathMat4, RotateAroundZAxis90DegreesMapsPlusXToPlusY)
{
    const slayer3d_mat4 r =
        slayer3d_mat4_rotate(slayer3d_vec3_make(0.0f, 0.0f, 1.0f), slayer3d_degrees_to_radians(90.0f));
    ExpectVec4Near(TransformPoint(r, slayer3d_vec3_make(1.0f, 0.0f, 0.0f)), slayer3d_vec4_make(0.0f, 1.0f, 0.0f, 1.0f),
                   1e-4f);
}

/* --- Perspective --------------------------------------------------------- */

TEST(SLAYER3DMathPerspective, PointOnNearPlaneMapsToMinusOneInNDC)
{
    slayer3d_mat4 p;
    ASSERT_TRUE(slayer3d_mat4_perspective(slayer3d_degrees_to_radians(60.0f), 1.0f, 1.0f, 100.0f, &p))
        << SDL_GetError();

    const slayer3d_vec4 clip = TransformPoint(p, slayer3d_vec3_make(0.0f, 0.0f, -1.0f));
    EXPECT_PRED_FORMAT3(Near, clip.z / clip.w, -1.0f, 1e-4f);
}

TEST(SLAYER3DMathPerspective, PointOnFarPlaneMapsToPlusOneInNDC)
{
    slayer3d_mat4 p;
    ASSERT_TRUE(slayer3d_mat4_perspective(slayer3d_degrees_to_radians(60.0f), 1.0f, 1.0f, 100.0f, &p))
        << SDL_GetError();

    const slayer3d_vec4 clip = TransformPoint(p, slayer3d_vec3_make(0.0f, 0.0f, -100.0f));
    EXPECT_PRED_FORMAT3(Near, clip.z / clip.w, 1.0f, 1e-4f);
}

TEST(SLAYER3DMathPerspective, CenterRayStaysOnAxis)
{
    slayer3d_mat4 p;
    ASSERT_TRUE(slayer3d_mat4_perspective(slayer3d_degrees_to_radians(60.0f), 16.0f / 9.0f, 1.0f, 100.0f, &p));

    const slayer3d_vec4 clip = TransformPoint(p, slayer3d_vec3_make(0.0f, 0.0f, -50.0f));
    EXPECT_PRED_FORMAT3(Near, clip.x / clip.w, 0.0f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, clip.y / clip.w, 0.0f, 1e-5f);
}

TEST(SLAYER3DMathPerspective, RejectsInvalidParameters)
{
    slayer3d_mat4 p;
    SDL_ClearError();
    EXPECT_FALSE(slayer3d_mat4_perspective(0.0f, 1.0f, 1.0f, 2.0f, &p));
    EXPECT_FALSE(slayer3d_mat4_perspective(1.0f, 0.0f, 1.0f, 2.0f, &p));
    EXPECT_FALSE(slayer3d_mat4_perspective(1.0f, 1.0f, 0.0f, 2.0f, &p));
    EXPECT_FALSE(slayer3d_mat4_perspective(1.0f, 1.0f, 2.0f, 1.0f, &p));
    EXPECT_FALSE(slayer3d_mat4_perspective(1.0f, 1.0f, 1.0f, 2.0f, nullptr));
}

/* --- Orthographic -------------------------------------------------------- */

TEST(SLAYER3DMathOrthographic, MapsCornersToNDC)
{
    slayer3d_mat4 p;
    ASSERT_TRUE(slayer3d_mat4_orthographic(-10.0f, 10.0f, -5.0f, 5.0f, 1.0f, 100.0f, &p)) << SDL_GetError();

    ExpectVec4Near(TransformPoint(p, slayer3d_vec3_make(-10.0f, -5.0f, -1.0f)),
                   slayer3d_vec4_make(-1.0f, -1.0f, -1.0f, 1.0f), 1e-5f);
    ExpectVec4Near(TransformPoint(p, slayer3d_vec3_make(10.0f, 5.0f, -100.0f)),
                   slayer3d_vec4_make(1.0f, 1.0f, 1.0f, 1.0f), 1e-5f);
}

TEST(SLAYER3DMathOrthographic, RejectsInvalidParameters)
{
    slayer3d_mat4 p;
    SDL_ClearError();
    EXPECT_FALSE(slayer3d_mat4_orthographic(1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, &p));
    EXPECT_FALSE(slayer3d_mat4_orthographic(0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, &p));
    EXPECT_FALSE(slayer3d_mat4_orthographic(0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, &p));
    EXPECT_FALSE(slayer3d_mat4_orthographic(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, nullptr));
}

/* --- LookAt -------------------------------------------------------------- */

TEST(SLAYER3DMathLookAt, EyeAtZ5TargetAtOriginMapsOriginToMinusZ5)
{
    slayer3d_mat4 v;
    ASSERT_TRUE(slayer3d_mat4_look_at(slayer3d_vec3_make(0.0f, 0.0f, 5.0f), slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                      slayer3d_vec3_make(0.0f, 1.0f, 0.0f), &v))
        << SDL_GetError();

    // World origin should land in front of the camera along -Z by 5 units.
    ExpectVec4Near(TransformPoint(v, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)), slayer3d_vec4_make(0.0f, 0.0f, -5.0f, 1.0f),
                   1e-5f);
    // The camera's own position maps to the view-space origin.
    ExpectVec4Near(TransformPoint(v, slayer3d_vec3_make(0.0f, 0.0f, 5.0f)), slayer3d_vec4_make(0.0f, 0.0f, 0.0f, 1.0f),
                   1e-5f);
}

TEST(SLAYER3DMathLookAt, RejectsDegenerateInputs)
{
    slayer3d_mat4 v;
    SDL_ClearError();
    // Eye == target.
    EXPECT_FALSE(slayer3d_mat4_look_at(slayer3d_vec3_make(1.0f, 2.0f, 3.0f), slayer3d_vec3_make(1.0f, 2.0f, 3.0f),
                                       slayer3d_vec3_make(0.0f, 1.0f, 0.0f), &v));
    // Up parallel to forward.
    EXPECT_FALSE(slayer3d_mat4_look_at(slayer3d_vec3_make(0.0f, 0.0f, 5.0f), slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                       slayer3d_vec3_make(0.0f, 0.0f, 1.0f), &v));
    // Null output.
    EXPECT_FALSE(slayer3d_mat4_look_at(slayer3d_vec3_make(0.0f, 0.0f, 5.0f), slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                       slayer3d_vec3_make(0.0f, 1.0f, 0.0f), nullptr));
}

/* --- Camera -------------------------------------------------------------- */

TEST(SLAYER3DCamera, PerspectiveMatrixDerivation)
{
    const slayer3d_camera3d camera = {
        {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 60.0f, SLAYER3D_CAMERA_PERSPECTIVE};
    slayer3d_mat4 view;
    slayer3d_mat4 projection;
    ASSERT_TRUE(slayer3d_camera3d_compute_matrices(&camera, 1600, 900, 1.0f, 100.0f, &view, &projection));

    // A point at the camera target should end up near NDC z = -something (inside frustum).
    const slayer3d_mat4 vp = slayer3d_mat4_multiply(projection, view);
    const slayer3d_vec4 clip = TransformPoint(vp, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    EXPECT_GT(clip.w, 0.0f);
    const float ndc_z = clip.z / clip.w;
    EXPECT_GT(ndc_z, -1.0f);
    EXPECT_LT(ndc_z, 1.0f);
}

TEST(SLAYER3DCamera, HorizontalFovDerivesVerticalProjectionFromAspect)
{
    slayer3d_camera3d camera = {
        {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 90.0f, SLAYER3D_CAMERA_PERSPECTIVE};
    camera.fov_axis = SLAYER3D_CAMERA_FOV_HORIZONTAL;

    slayer3d_mat4 view;
    slayer3d_mat4 projection;
    ASSERT_TRUE(slayer3d_camera3d_compute_matrices(&camera, 1600, 900, 1.0f, 100.0f, &view, &projection));

    EXPECT_PRED_FORMAT3(Near, projection.m[0], 1.0f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, projection.m[5], 16.0f / 9.0f, 1e-5f);
}

TEST(SLAYER3DCamera, OrthographicProducesUnitNDC)
{
    const slayer3d_camera3d camera = {
        {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 10.0f, SLAYER3D_CAMERA_ORTHOGRAPHIC};
    slayer3d_mat4 view;
    slayer3d_mat4 projection;
    ASSERT_TRUE(slayer3d_camera3d_compute_matrices(&camera, 100, 100, 1.0f, 100.0f, &view, &projection));

    const slayer3d_mat4 vp = slayer3d_mat4_multiply(projection, view);
    const slayer3d_vec4 clip_top = TransformPoint(vp, slayer3d_vec3_make(0.0f, 5.0f, 0.0f));
    EXPECT_PRED_FORMAT3(Near, clip_top.y / clip_top.w, 1.0f, 1e-4f);
    const slayer3d_vec4 clip_bot = TransformPoint(vp, slayer3d_vec3_make(0.0f, -5.0f, 0.0f));
    EXPECT_PRED_FORMAT3(Near, clip_bot.y / clip_bot.w, -1.0f, 1e-4f);
}

TEST(SLAYER3DCamera, ScreenRayMatchesPerspectiveCenterAndFovAxis)
{
    slayer3d_camera3d camera = {
        {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 90.0f, SLAYER3D_CAMERA_PERSPECTIVE};
    camera.fov_axis = SLAYER3D_CAMERA_FOV_HORIZONTAL;

    slayer3d_vec3 start{};
    slayer3d_vec3 end{};
    ASSERT_TRUE(slayer3d_camera3d_screen_ray(&camera, 1600.0f, 900.0f, 800.0f, 450.0f, 1.0f, 10.0f, &start, &end));
    EXPECT_PRED_FORMAT3(Near, start.x, 0.0f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, start.y, 0.0f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, start.z, 4.0f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, end.z, -5.0f, 1e-5f);

    ASSERT_TRUE(slayer3d_camera3d_screen_ray(&camera, 1600.0f, 900.0f, 1600.0f, 450.0f, 1.0f, 10.0f, &start, &end));
    const slayer3d_vec3 dir = slayer3d_vec3_normalize(slayer3d_vec3_sub(end, start));
    EXPECT_PRED_FORMAT3(Near, dir.x, 0.7071067f, 1e-4f);
    EXPECT_PRED_FORMAT3(Near, dir.y, 0.0f, 1e-4f);
    EXPECT_PRED_FORMAT3(Near, dir.z, -0.7071067f, 1e-4f);
}

TEST(SLAYER3DCamera, ScreenRayOffsetsOrthographicStart)
{
    const slayer3d_camera3d camera = {
        {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 10.0f, SLAYER3D_CAMERA_ORTHOGRAPHIC};

    slayer3d_vec3 start{};
    slayer3d_vec3 end{};
    ASSERT_TRUE(slayer3d_camera3d_screen_ray(&camera, 100.0f, 100.0f, 100.0f, 0.0f, 1.0f, 10.0f, &start, &end));
    EXPECT_PRED_FORMAT3(Near, start.x, 5.0f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, start.y, 5.0f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, start.z, 4.0f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, end.x, 5.0f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, end.y, 5.0f, 1e-5f);
    EXPECT_PRED_FORMAT3(Near, end.z, -5.0f, 1e-5f);
}

TEST(SLAYER3DCamera, RejectsInvalidArguments)
{
    const slayer3d_camera3d camera = {
        {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 60.0f, SLAYER3D_CAMERA_PERSPECTIVE};
    slayer3d_mat4 view;
    slayer3d_mat4 projection;

    SDL_ClearError();
    EXPECT_FALSE(slayer3d_camera3d_compute_matrices(nullptr, 100, 100, 1.0f, 100.0f, &view, &projection));
    EXPECT_FALSE(slayer3d_camera3d_compute_matrices(&camera, 0, 100, 1.0f, 100.0f, &view, &projection));
    EXPECT_FALSE(slayer3d_camera3d_compute_matrices(&camera, 100, 0, 1.0f, 100.0f, &view, &projection));
    EXPECT_FALSE(slayer3d_camera3d_compute_matrices(&camera, 100, 100, 1.0f, 100.0f, nullptr, &projection));
    EXPECT_FALSE(slayer3d_camera3d_compute_matrices(&camera, 100, 100, 1.0f, 100.0f, &view, nullptr));
}
