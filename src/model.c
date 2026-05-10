#include "slayer3d/model.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/animation.h"

#include "model_internal.h"

void slayer3d_free_model(slayer3d_model *model)
{
    if (model == NULL)
    {
        return;
    }

    for (int i = 0; i < model->mesh_count; ++i)
    {
        slayer3d_mesh *mesh = &model->meshes[i];
        SDL_free(mesh->name);
        SDL_free(mesh->positions);
        SDL_free(mesh->normals);
        SDL_free(mesh->uvs);
        SDL_free(mesh->lightmap_uvs);
        SDL_free(mesh->colors);
        SDL_free(mesh->indices);
        SDL_free(mesh->joint_indices);
        SDL_free(mesh->joint_weights);
    }
    SDL_free(model->meshes);

    for (int i = 0; i < model->material_count; ++i)
    {
        slayer3d_material *mat = &model->materials[i];
        SDL_free(mat->name);
        SDL_free(mat->albedo_map);
        SDL_free(mat->normal_map);
        SDL_free(mat->metallic_roughness_map);
        SDL_free(mat->emissive_map);
    }
    SDL_free(model->materials);

    if (model->skeleton != NULL)
    {
        for (int i = 0; i < model->skeleton->joint_count; ++i)
        {
            SDL_free(model->skeleton->joints[i].name);
        }
        SDL_free(model->skeleton->joints);
        SDL_free(model->skeleton);
    }

    for (int i = 0; i < model->animation_count; ++i)
    {
        slayer3d_animation_clip *clip = &model->animations[i];
        SDL_free(clip->name);
        for (int c = 0; c < clip->channel_count; ++c)
        {
            SDL_free(clip->channels[c].keyframes);
        }
        SDL_free(clip->channels);
    }
    SDL_free(model->animations);

    for (int i = 0; i < model->embedded_texture_count; ++i)
    {
        slayer3d_free_image(&model->embedded_textures[i]);
    }
    SDL_free(model->embedded_textures);

    SDL_free(model->source_path);

    for (int i = 0; i < model->node_count; ++i)
    {
        SDL_free(model->nodes[i].children);
    }
    SDL_free(model->nodes);
    SDL_free(model->root_nodes);

    SDL_zerop(model);
}

static bool slayer3d_endswith_ci(const char *s, const char *suffix)
{
    const size_t ls = SDL_strlen(s);
    const size_t lt = SDL_strlen(suffix);
    if (lt > ls)
    {
        return false;
    }
    return SDL_strcasecmp(s + (ls - lt), suffix) == 0;
}

bool slayer3d_load_model_from_file(const char *path, slayer3d_model *out)
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

    if (slayer3d_endswith_ci(path, ".obj"))
    {
        return slayer3d_load_model_obj(path, out);
    }
    if (slayer3d_endswith_ci(path, ".gltf") || slayer3d_endswith_ci(path, ".glb"))
    {
        return slayer3d_load_model_gltf(path, out);
    }
    if (slayer3d_endswith_ci(path, ".fbx"))
    {
        return slayer3d_load_model_fbx(path, out);
    }
    return SDL_SetError("Unrecognized model file extension: '%s'.", path);
}
