#ifndef SLAYER3D_MODEL_INTERNAL_H
#define SLAYER3D_MODEL_INTERNAL_H

#include <stdbool.h>

#include "slayer3d/model.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Loader entry points implemented in dedicated translation units.
     * Each populates `out` on success and returns false with
     * SDL_SetError on failure (the model is cleared either way).
     */
    bool slayer3d_load_model_obj(const char *path, slayer3d_model *out);
    bool slayer3d_load_model_gltf(const char *path, slayer3d_model *out);
    bool slayer3d_load_model_fbx(const char *path, slayer3d_model *out);

#ifdef __cplusplus
}
#endif

#endif
