#ifndef SDL3D_TYPES_H
#define SDL3D_TYPES_H

#include <SDL3/SDL_stdinc.h>

typedef struct sdl3d_vec2
{
    float x;
    float y;
} sdl3d_vec2;

typedef struct sdl3d_vec3
{
    float x;
    float y;
    float z;
} sdl3d_vec3;

typedef struct sdl3d_color
{
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 a;
} sdl3d_color;

typedef struct sdl3d_mat4
{
    float m[16];
} sdl3d_mat4;

#endif
