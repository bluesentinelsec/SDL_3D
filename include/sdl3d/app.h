#ifndef SDL3D_APP_H
#define SDL3D_APP_H

#include "sdl3d/sdl3d.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Initialize SDL3D with a window and render context.
     * Returns true on success. The backend is chosen automatically
     * (OpenGL preferred, software fallback). */
    bool sdl3d_init(int width, int height, const char *title);

    /* Shut down SDL3D and free all resources. */
    void sdl3d_close(void);

    /* Returns true until the user closes the window or presses ESC. */
    bool sdl3d_should_close(void);

    /* Set the target frame rate. 0 = unlimited. */
    void sdl3d_set_target_fps(int fps);

    /* Get the time elapsed since the last frame in seconds. */
    float sdl3d_get_frame_time(void);

    /* Get the current FPS. */
    int sdl3d_get_fps(void);

    /* Get the render context (for users who need direct access). */
    sdl3d_render_context *sdl3d_get_context(void);

    /* Get the SDL window (for users who need direct access). */
    SDL_Window *sdl3d_get_window(void);

    /* Begin a new frame: polls events, clears the screen. */
    void sdl3d_begin_frame(sdl3d_color clear_color);

    /* End the frame: presents and enforces target FPS. */
    void sdl3d_end_frame(void);

    /* Check if a key is currently held down. Uses SDL_Scancode values. */
    bool sdl3d_is_key_down(int scancode);

    /* Check if a key was pressed this frame. */
    bool sdl3d_is_key_pressed(int scancode);

    /* Get mouse movement delta this frame. */
    void sdl3d_get_mouse_delta(float *dx, float *dy);

    /* Enable/disable mouse capture (FPS-style mouse look). */
    void sdl3d_set_mouse_captured(bool captured);

#ifdef __cplusplus
}
#endif

#endif
