#include "sdl3d/model.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "model_internal.h"

void sdl3d_free_model(sdl3d_model *model)
{
    if (model == NULL)
    {
        return;
    }

    for (int i = 0; i < model->mesh_count; ++i)
    {
        sdl3d_mesh *mesh = &model->meshes[i];
        SDL_free(mesh->name);
        SDL_free(mesh->positions);
        SDL_free(mesh->normals);
        SDL_free(mesh->uvs);
        SDL_free(mesh->colors);
        SDL_free(mesh->indices);
    }
    SDL_free(model->meshes);

    for (int i = 0; i < model->material_count; ++i)
    {
        sdl3d_material *mat = &model->materials[i];
        SDL_free(mat->name);
        SDL_free(mat->albedo_map);
        SDL_free(mat->normal_map);
        SDL_free(mat->metallic_roughness_map);
        SDL_free(mat->emissive_map);
    }
    SDL_free(model->materials);

    SDL_free(model->source_path);

    SDL_zerop(model);
}

static bool sdl3d_endswith_ci(const char *s, const char *suffix)
{
    const size_t ls = SDL_strlen(s);
    const size_t lt = SDL_strlen(suffix);
    if (lt > ls)
    {
        return false;
    }
    return SDL_strcasecmp(s + (ls - lt), suffix) == 0;
}

bool sdl3d_load_model_from_file(const char *path, sdl3d_model *out)
{
    if (path == NULL)
    {
        return SDL_InvalidParamError("path");
    }
    if (out == NULL)
    {
        return SDL_InvalidParamError("out");
    }

    SDL_zerop(out);

    if (sdl3d_endswith_ci(path, ".obj"))
    {
        return sdl3d_load_model_obj(path, out);
    }
    if (sdl3d_endswith_ci(path, ".gltf") || sdl3d_endswith_ci(path, ".glb"))
    {
        return SDL_SetError("glTF loading is not implemented yet (path '%s').", path);
    }
    if (sdl3d_endswith_ci(path, ".fbx"))
    {
        return SDL_SetError("FBX loading is not implemented yet (path '%s').", path);
    }
    return SDL_SetError("Unrecognized model file extension: '%s'.", path);
}
