#include <gtest/gtest.h>

extern "C"
{
#include "backend.h"
}

TEST(DoomBackendProfiles, NamesFollowKeyboardOrder)
{
    EXPECT_STREQ("Modern", backend_profile_name(DOOM_RENDER_PROFILE_MODERN));
    EXPECT_STREQ("PS1", backend_profile_name(DOOM_RENDER_PROFILE_PS1));
    EXPECT_STREQ("N64", backend_profile_name(DOOM_RENDER_PROFILE_N64));
    EXPECT_STREQ("DOS", backend_profile_name(DOOM_RENDER_PROFILE_DOS));
    EXPECT_STREQ("SNES Star Fox", backend_profile_name(DOOM_RENDER_PROFILE_SNES));
    EXPECT_STREQ("Modern", backend_profile_name(DOOM_RENDER_PROFILE_COUNT));
}

TEST(DoomBackendProfiles, DescriptorsMatchSDL3DPresets)
{
    sdl3d_render_profile modern = backend_profile_descriptor(DOOM_RENDER_PROFILE_MODERN);
    EXPECT_EQ(SDL3D_SHADING_PHONG, modern.shading);
    EXPECT_EQ(SDL3D_TEXTURE_FILTER_BILINEAR, modern.texture_filter);
    EXPECT_EQ(SDL3D_UV_PERSPECTIVE, modern.uv_mode);

    sdl3d_render_profile ps1 = backend_profile_descriptor(DOOM_RENDER_PROFILE_PS1);
    EXPECT_EQ(SDL3D_SHADING_GOURAUD, ps1.shading);
    EXPECT_EQ(SDL3D_TEXTURE_FILTER_NEAREST, ps1.texture_filter);
    EXPECT_EQ(SDL3D_UV_AFFINE, ps1.uv_mode);
    EXPECT_TRUE(ps1.vertex_snap);

    sdl3d_render_profile n64 = backend_profile_descriptor(DOOM_RENDER_PROFILE_N64);
    EXPECT_EQ(SDL3D_TEXTURE_FILTER_BILINEAR, n64.texture_filter);
    EXPECT_EQ(SDL3D_UV_PERSPECTIVE, n64.uv_mode);

    sdl3d_render_profile dos = backend_profile_descriptor(DOOM_RENDER_PROFILE_DOS);
    EXPECT_EQ(SDL3D_TEXTURE_FILTER_NEAREST, dos.texture_filter);
    EXPECT_EQ(SDL3D_UV_AFFINE, dos.uv_mode);
    EXPECT_TRUE(dos.color_quantize);
    EXPECT_EQ(6, dos.color_depth);

    sdl3d_render_profile snes = backend_profile_descriptor(DOOM_RENDER_PROFILE_SNES);
    EXPECT_EQ(SDL3D_SHADING_FLAT, snes.shading);
    EXPECT_EQ(SDL3D_TEXTURE_FILTER_NEAREST, snes.texture_filter);
    EXPECT_EQ(SDL3D_UV_AFFINE, snes.uv_mode);
}
