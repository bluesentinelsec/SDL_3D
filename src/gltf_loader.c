/*
 * cgltf-backed glTF 2.0 / GLB loader.
 *
 * Loads meshes, materials, and texture-path references from .gltf and
 * .glb files. Each cgltf_primitive becomes one sdl3d_mesh; each
 * cgltf_material becomes one sdl3d_material. Only triangle primitives
 * are imported; non-triangle primitives are silently skipped.
 */

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <cgltf.h>

#include "model_internal.h"

/* ------------------------------------------------------------------ */
/* String helpers                                                      */
/* ------------------------------------------------------------------ */

static char *sdl3d_gltf_strdup(const char *s)
{
    size_t len;
    char *copy;
    if (s == NULL)
    {
        return NULL;
    }
    len = SDL_strlen(s);
    copy = (char *)SDL_malloc(len + 1);
    if (copy == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }
    SDL_memcpy(copy, s, len + 1);
    return copy;
}

/* ------------------------------------------------------------------ */
/* Material conversion                                                 */
/* ------------------------------------------------------------------ */

static void sdl3d_gltf_material_init(sdl3d_material *mat)
{
    SDL_zerop(mat);
    mat->albedo[0] = 1.0f;
    mat->albedo[1] = 1.0f;
    mat->albedo[2] = 1.0f;
    mat->albedo[3] = 1.0f;
    mat->metallic = 0.0f;
    mat->roughness = 1.0f;
}

static char *sdl3d_gltf_texture_uri(const cgltf_texture_view *view)
{
    if (view->texture == NULL || view->texture->image == NULL)
    {
        return NULL;
    }
    if (view->texture->image->uri != NULL)
    {
        return sdl3d_gltf_strdup(view->texture->image->uri);
    }
    /* Embedded buffer_view textures have no URI — skip for now. */
    return NULL;
}

static bool sdl3d_gltf_convert_material(const cgltf_material *src, sdl3d_material *dst)
{
    sdl3d_gltf_material_init(dst);
    dst->name = sdl3d_gltf_strdup(src->name);

    if (src->has_pbr_metallic_roughness)
    {
        const cgltf_pbr_metallic_roughness *pbr = &src->pbr_metallic_roughness;
        dst->albedo[0] = pbr->base_color_factor[0];
        dst->albedo[1] = pbr->base_color_factor[1];
        dst->albedo[2] = pbr->base_color_factor[2];
        dst->albedo[3] = pbr->base_color_factor[3];
        dst->metallic = pbr->metallic_factor;
        dst->roughness = pbr->roughness_factor;
        dst->albedo_map = sdl3d_gltf_texture_uri(&pbr->base_color_texture);
        dst->metallic_roughness_map = sdl3d_gltf_texture_uri(&pbr->metallic_roughness_texture);
    }

    dst->emissive[0] = src->emissive_factor[0];
    dst->emissive[1] = src->emissive_factor[1];
    dst->emissive[2] = src->emissive_factor[2];

    if (src->has_emissive_strength)
    {
        dst->emissive[0] *= src->emissive_strength.emissive_strength;
        dst->emissive[1] *= src->emissive_strength.emissive_strength;
        dst->emissive[2] *= src->emissive_strength.emissive_strength;
    }

    dst->normal_map = sdl3d_gltf_texture_uri(&src->normal_texture);
    dst->emissive_map = sdl3d_gltf_texture_uri(&src->emissive_texture);

    return true;
}

/* ------------------------------------------------------------------ */
/* Mesh conversion                                                     */
/* ------------------------------------------------------------------ */

static int sdl3d_gltf_find_material_index(const cgltf_data *data, const cgltf_material *mat)
{
    if (mat == NULL)
    {
        return -1;
    }
    return (int)(mat - data->materials);
}

static bool sdl3d_gltf_convert_primitive(const cgltf_data *data, const cgltf_primitive *prim, const char *mesh_name,
                                         int prim_index, sdl3d_mesh *dst)
{
    const cgltf_accessor *pos_accessor = NULL;
    const cgltf_accessor *norm_accessor = NULL;
    const cgltf_accessor *uv_accessor = NULL;
    const cgltf_accessor *color_accessor = NULL;
    int vertex_count = 0;

    SDL_zerop(dst);

    /* Build a name: "meshname/prim_index" or just "prim_index". */
    {
        char buf[256];
        if (mesh_name != NULL && mesh_name[0] != '\0')
        {
            SDL_snprintf(buf, sizeof(buf), "%s/%d", mesh_name, prim_index);
        }
        else
        {
            SDL_snprintf(buf, sizeof(buf), "primitive_%d", prim_index);
        }
        dst->name = sdl3d_gltf_strdup(buf);
    }

    /* Find attribute accessors. */
    for (cgltf_size i = 0; i < prim->attributes_count; ++i)
    {
        const cgltf_attribute *attr = &prim->attributes[i];
        switch (attr->type)
        {
        case cgltf_attribute_type_position:
            pos_accessor = attr->data;
            break;
        case cgltf_attribute_type_normal:
            norm_accessor = attr->data;
            break;
        case cgltf_attribute_type_texcoord:
            if (attr->index == 0)
            {
                uv_accessor = attr->data;
            }
            break;
        case cgltf_attribute_type_color:
            if (attr->index == 0)
            {
                color_accessor = attr->data;
            }
            break;
        default:
            break;
        }
    }

    if (pos_accessor == NULL)
    {
        return SDL_SetError("glTF primitive has no POSITION attribute.");
    }

    vertex_count = (int)pos_accessor->count;
    dst->vertex_count = vertex_count;
    dst->material_index = sdl3d_gltf_find_material_index(data, prim->material);

    /* Unpack positions. */
    dst->positions = (float *)SDL_malloc((size_t)vertex_count * 3 * sizeof(float));
    if (dst->positions == NULL)
    {
        return SDL_OutOfMemory();
    }
    if (cgltf_accessor_unpack_floats(pos_accessor, dst->positions, (cgltf_size)vertex_count * 3) == 0)
    {
        return SDL_SetError("Failed to unpack glTF POSITION data.");
    }

    /* Unpack normals. */
    if (norm_accessor != NULL && (int)norm_accessor->count == vertex_count)
    {
        dst->normals = (float *)SDL_malloc((size_t)vertex_count * 3 * sizeof(float));
        if (dst->normals == NULL)
        {
            return SDL_OutOfMemory();
        }
        if (cgltf_accessor_unpack_floats(norm_accessor, dst->normals, (cgltf_size)vertex_count * 3) == 0)
        {
            return SDL_SetError("Failed to unpack glTF NORMAL data.");
        }
    }

    /* Unpack UVs. */
    if (uv_accessor != NULL && (int)uv_accessor->count == vertex_count)
    {
        dst->uvs = (float *)SDL_malloc((size_t)vertex_count * 2 * sizeof(float));
        if (dst->uvs == NULL)
        {
            return SDL_OutOfMemory();
        }
        if (cgltf_accessor_unpack_floats(uv_accessor, dst->uvs, (cgltf_size)vertex_count * 2) == 0)
        {
            return SDL_SetError("Failed to unpack glTF TEXCOORD_0 data.");
        }
    }

    /* Unpack vertex colors. */
    if (color_accessor != NULL && (int)color_accessor->count == vertex_count)
    {
        cgltf_size components = cgltf_num_components(color_accessor->type);
        dst->colors = (float *)SDL_calloc((size_t)vertex_count * 4, sizeof(float));
        if (dst->colors == NULL)
        {
            return SDL_OutOfMemory();
        }
        if (components == 4)
        {
            if (cgltf_accessor_unpack_floats(color_accessor, dst->colors, (cgltf_size)vertex_count * 4) == 0)
            {
                return SDL_SetError("Failed to unpack glTF COLOR_0 data.");
            }
        }
        else if (components == 3)
        {
            /* RGB → RGBA with alpha = 1. */
            float *tmp = (float *)SDL_malloc((size_t)vertex_count * 3 * sizeof(float));
            if (tmp == NULL)
            {
                return SDL_OutOfMemory();
            }
            if (cgltf_accessor_unpack_floats(color_accessor, tmp, (cgltf_size)vertex_count * 3) == 0)
            {
                SDL_free(tmp);
                return SDL_SetError("Failed to unpack glTF COLOR_0 data.");
            }
            for (int v = 0; v < vertex_count; ++v)
            {
                dst->colors[v * 4 + 0] = tmp[v * 3 + 0];
                dst->colors[v * 4 + 1] = tmp[v * 3 + 1];
                dst->colors[v * 4 + 2] = tmp[v * 3 + 2];
                dst->colors[v * 4 + 3] = 1.0f;
            }
            SDL_free(tmp);
        }
    }

    /* Unpack indices. */
    if (prim->indices != NULL)
    {
        const int index_count = (int)prim->indices->count;
        dst->index_count = index_count;
        dst->indices = (unsigned int *)SDL_malloc((size_t)index_count * sizeof(unsigned int));
        if (dst->indices == NULL)
        {
            return SDL_OutOfMemory();
        }
        if (cgltf_accessor_unpack_indices(prim->indices, dst->indices, sizeof(unsigned int), (cgltf_size)index_count) ==
            0)
        {
            return SDL_SetError("Failed to unpack glTF index data.");
        }
    }
    else
    {
        /* No index buffer: generate identity indices. */
        dst->index_count = vertex_count;
        dst->indices = (unsigned int *)SDL_malloc((size_t)vertex_count * sizeof(unsigned int));
        if (dst->indices == NULL)
        {
            return SDL_OutOfMemory();
        }
        for (int i = 0; i < vertex_count; ++i)
        {
            dst->indices[i] = (unsigned int)i;
        }
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Count total triangle primitives across all meshes.                  */
/* ------------------------------------------------------------------ */

static int sdl3d_gltf_count_triangle_primitives(const cgltf_data *data)
{
    int count = 0;
    for (cgltf_size m = 0; m < data->meshes_count; ++m)
    {
        const cgltf_mesh *mesh = &data->meshes[m];
        for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
        {
            if (mesh->primitives[p].type == cgltf_primitive_type_triangles)
            {
                ++count;
            }
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */

bool sdl3d_load_model_gltf(const char *path, sdl3d_model *out)
{
    cgltf_options options;
    cgltf_data *data = NULL;
    cgltf_result result;
    int total_prims = 0;
    int mesh_index = 0;

    SDL_zerop(out);
    SDL_zerop(&options);

    /* Parse the file. cgltf handles both .gltf (JSON) and .glb (binary). */
    result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success)
    {
        return SDL_SetError("cgltf_parse_file failed for '%s' (error %d).", path, (int)result);
    }

    /* Load external buffer data (for .gltf with separate .bin files). */
    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success)
    {
        cgltf_free(data);
        return SDL_SetError("cgltf_load_buffers failed for '%s' (error %d).", path, (int)result);
    }

    /* Validate. */
    result = cgltf_validate(data);
    if (result != cgltf_result_success)
    {
        cgltf_free(data);
        return SDL_SetError("cgltf_validate failed for '%s' (error %d).", path, (int)result);
    }

    /* Store source path. */
    out->source_path = sdl3d_gltf_strdup(path);
    if (out->source_path == NULL)
    {
        cgltf_free(data);
        return false;
    }

    /* Convert materials. */
    if (data->materials_count > 0)
    {
        out->material_count = (int)data->materials_count;
        out->materials = (sdl3d_material *)SDL_calloc(data->materials_count, sizeof(sdl3d_material));
        if (out->materials == NULL)
        {
            cgltf_free(data);
            sdl3d_free_model(out);
            return SDL_OutOfMemory();
        }
        for (cgltf_size i = 0; i < data->materials_count; ++i)
        {
            if (!sdl3d_gltf_convert_material(&data->materials[i], &out->materials[i]))
            {
                cgltf_free(data);
                sdl3d_free_model(out);
                return false;
            }
        }
    }

    /* Count triangle primitives to allocate mesh array. */
    total_prims = sdl3d_gltf_count_triangle_primitives(data);
    if (total_prims == 0)
    {
        cgltf_free(data);
        return SDL_SetError("glTF file '%s' contains no triangle primitives.", path);
    }

    out->mesh_count = total_prims;
    out->meshes = (sdl3d_mesh *)SDL_calloc((size_t)total_prims, sizeof(sdl3d_mesh));
    if (out->meshes == NULL)
    {
        cgltf_free(data);
        sdl3d_free_model(out);
        return SDL_OutOfMemory();
    }

    /* Convert each triangle primitive into an sdl3d_mesh. */
    for (cgltf_size m = 0; m < data->meshes_count; ++m)
    {
        const cgltf_mesh *mesh = &data->meshes[m];
        for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
        {
            const cgltf_primitive *prim = &mesh->primitives[p];
            if (prim->type != cgltf_primitive_type_triangles)
            {
                continue;
            }
            if (!sdl3d_gltf_convert_primitive(data, prim, mesh->name, (int)p, &out->meshes[mesh_index]))
            {
                cgltf_free(data);
                sdl3d_free_model(out);
                return false;
            }
            ++mesh_index;
        }
    }

    cgltf_free(data);
    return true;
}
