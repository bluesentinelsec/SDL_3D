#ifndef SDL3D_MODEL_INTERNAL_H
#define SDL3D_MODEL_INTERNAL_H

#include <stdbool.h>

#include "sdl3d/model.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Loader entry points implemented in dedicated translation units.
     * Each populates `out` on success and returns false with
     * SDL_SetError on failure (the model is cleared either way).
     */
    bool sdl3d_load_model_obj(const char *path, sdl3d_model *out);
    bool sdl3d_load_model_gltf(const char *path, sdl3d_model *out);
    bool sdl3d_load_model_fbx(const char *path, sdl3d_model *out);

#ifdef __cplusplus
}
#endif

#endif
