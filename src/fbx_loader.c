/*
 * ufbx-backed FBX loader.
 *
 * Loads meshes and materials from .fbx files (binary and ASCII).
 * Each ufbx_mesh material_part becomes one sdl3d_mesh; each
 * ufbx_material becomes one sdl3d_material. Faces are triangulated
 * via ufbx_triangulate_face.
 */

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <ufbx.h>

#include "model_internal.h"

/* ------------------------------------------------------------------ */
/* String helpers                                                      */
/* ------------------------------------------------------------------ */

static char *sdl3d_fbx_strdup(const char *s)
{
    size_t len;
    char *copy;
    if (s == NULL || s[0] == '\0')
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

static char *sdl3d_fbx_strdup_ufbx(ufbx_string s)
{
    if (s.length == 0 || s.data == NULL)
    {
        return NULL;
    }
    return sdl3d_fbx_strdup(s.data);
}

/* ------------------------------------------------------------------ */
/* Material conversion                                                 */
/* ------------------------------------------------------------------ */

static void sdl3d_fbx_material_init(sdl3d_material *mat)
{
    SDL_zerop(mat);
    mat->albedo[0] = 1.0f;
    mat->albedo[1] = 1.0f;
    mat->albedo[2] = 1.0f;
    mat->albedo[3] = 1.0f;
    mat->metallic = 0.0f;
    mat->roughness = 1.0f;
}

static char *sdl3d_fbx_texture_path(const ufbx_material_map *map)
{
    if (map->texture == NULL)
    {
        return NULL;
    }
    /* Prefer the relative filename resolved by ufbx. */
    if (map->texture->relative_filename.length > 0)
    {
        return sdl3d_fbx_strdup_ufbx(map->texture->relative_filename);
    }
    if (map->texture->filename.length > 0)
    {
        return sdl3d_fbx_strdup_ufbx(map->texture->filename);
    }
    return NULL;
}

static void sdl3d_fbx_convert_material(const ufbx_material *src, sdl3d_material *dst)
{
    sdl3d_fbx_material_init(dst);
    dst->name = sdl3d_fbx_strdup_ufbx(src->name);

    /* Use PBR base_color when it looks populated, otherwise fall back to
     * FBX classic diffuse_color × diffuse_factor. */
    {
        float r = (float)src->pbr.base_color.value_vec4.x;
        float g = (float)src->pbr.base_color.value_vec4.y;
        float b = (float)src->pbr.base_color.value_vec4.z;
        if (r > 0.0f || g > 0.0f || b > 0.0f)
        {
            dst->albedo[0] = r;
            dst->albedo[1] = g;
            dst->albedo[2] = b;
        }
        else
        {
            float df = (float)src->fbx.diffuse_factor.value_real;
            dst->albedo[0] = (float)src->fbx.diffuse_color.value_vec3.x * df;
            dst->albedo[1] = (float)src->fbx.diffuse_color.value_vec3.y * df;
            dst->albedo[2] = (float)src->fbx.diffuse_color.value_vec3.z * df;
        }
    }
    dst->albedo[3] = 1.0f - (float)src->fbx.transparency_factor.value_real;
    dst->metallic = (float)src->pbr.metalness.value_real;
    dst->roughness = (float)src->pbr.roughness.value_real;

    /* Clamp roughness: ufbx may report 0 for classic FBX materials that
     * have no roughness concept; default to 1 (fully rough) in that case. */
    if (dst->roughness <= 0.0f && dst->metallic <= 0.0f)
    {
        dst->roughness = 1.0f;
    }

    dst->emissive[0] = (float)src->fbx.emission_color.value_vec3.x * (float)src->fbx.emission_factor.value_real;
    dst->emissive[1] = (float)src->fbx.emission_color.value_vec3.y * (float)src->fbx.emission_factor.value_real;
    dst->emissive[2] = (float)src->fbx.emission_color.value_vec3.z * (float)src->fbx.emission_factor.value_real;

    /* Texture paths. */
    dst->albedo_map = sdl3d_fbx_texture_path(&src->pbr.base_color);
    if (dst->albedo_map == NULL)
    {
        dst->albedo_map = sdl3d_fbx_texture_path(&src->fbx.diffuse_color);
    }
    dst->normal_map = sdl3d_fbx_texture_path(&src->fbx.normal_map);
    dst->emissive_map = sdl3d_fbx_texture_path(&src->fbx.emission_color);
}

/* ------------------------------------------------------------------ */
/* Mesh conversion                                                     */
/* ------------------------------------------------------------------ */

static int sdl3d_fbx_find_material_index(const ufbx_scene *scene, const ufbx_material *mat)
{
    if (mat == NULL)
    {
        return -1;
    }
    for (size_t i = 0; i < scene->materials.count; ++i)
    {
        if (scene->materials.data[i] == mat)
        {
            return (int)i;
        }
    }
    return -1;
}

static bool sdl3d_fbx_convert_mesh_part(const ufbx_scene *scene, const ufbx_mesh *mesh, const ufbx_mesh_part *part,
                                        int part_index, sdl3d_mesh *dst)
{
    size_t num_triangles = part->num_triangles;
    size_t vertex_count = num_triangles * 3;
    size_t max_tri_indices = mesh->max_face_triangles * 3;
    uint32_t *tri_indices = NULL;
    size_t out_vertex = 0;

    SDL_zerop(dst);

    if (num_triangles == 0)
    {
        return SDL_SetError("FBX mesh part has no triangles.");
    }

    /* Name. */
    {
        char buf[256];
        if (mesh->name.length > 0)
        {
            SDL_snprintf(buf, sizeof(buf), "%s/%d", mesh->name.data, part_index);
        }
        else
        {
            SDL_snprintf(buf, sizeof(buf), "mesh_%d/%d", (int)mesh->typed_id, part_index);
        }
        dst->name = sdl3d_fbx_strdup(buf);
    }

    /* Material index. */
    if (part_index >= 0 && (size_t)part_index < mesh->materials.count)
    {
        dst->material_index = sdl3d_fbx_find_material_index(scene, mesh->materials.data[part_index]);
    }
    else
    {
        dst->material_index = -1;
    }

    /* Allocate output arrays. */
    dst->vertex_count = (int)vertex_count;
    dst->positions = (float *)SDL_malloc(vertex_count * 3 * sizeof(float));
    if (dst->positions == NULL)
    {
        return SDL_OutOfMemory();
    }

    if (mesh->vertex_normal.exists)
    {
        dst->normals = (float *)SDL_malloc(vertex_count * 3 * sizeof(float));
        if (dst->normals == NULL)
        {
            return SDL_OutOfMemory();
        }
    }

    if (mesh->vertex_uv.exists)
    {
        dst->uvs = (float *)SDL_malloc(vertex_count * 2 * sizeof(float));
        if (dst->uvs == NULL)
        {
            return SDL_OutOfMemory();
        }
    }

    if (mesh->vertex_color.exists)
    {
        dst->colors = (float *)SDL_malloc(vertex_count * 4 * sizeof(float));
        if (dst->colors == NULL)
        {
            return SDL_OutOfMemory();
        }
    }

    /* Triangulation scratch buffer. */
    tri_indices = (uint32_t *)SDL_malloc(max_tri_indices * sizeof(uint32_t));
    if (tri_indices == NULL)
    {
        return SDL_OutOfMemory();
    }

    /* Triangulate each face in this part. */
    for (size_t fi = 0; fi < part->face_indices.count; ++fi)
    {
        uint32_t face_ix = part->face_indices.data[fi];
        ufbx_face face = mesh->faces.data[face_ix];
        uint32_t num_tris = ufbx_triangulate_face(tri_indices, max_tri_indices, mesh, face);

        for (uint32_t ti = 0; ti < num_tris * 3; ++ti)
        {
            uint32_t idx = tri_indices[ti];

            /* Position: use vertex_position which is indexed per-index. */
            {
                ufbx_vec3 p = mesh->vertex_position.values.data[mesh->vertex_position.indices.data[idx]];
                dst->positions[out_vertex * 3 + 0] = (float)p.x;
                dst->positions[out_vertex * 3 + 1] = (float)p.y;
                dst->positions[out_vertex * 3 + 2] = (float)p.z;
            }

            if (dst->normals != NULL)
            {
                ufbx_vec3 n = mesh->vertex_normal.values.data[mesh->vertex_normal.indices.data[idx]];
                dst->normals[out_vertex * 3 + 0] = (float)n.x;
                dst->normals[out_vertex * 3 + 1] = (float)n.y;
                dst->normals[out_vertex * 3 + 2] = (float)n.z;
            }

            if (dst->uvs != NULL)
            {
                ufbx_vec2 uv = mesh->vertex_uv.values.data[mesh->vertex_uv.indices.data[idx]];
                dst->uvs[out_vertex * 2 + 0] = (float)uv.x;
                dst->uvs[out_vertex * 2 + 1] = (float)uv.y;
            }

            if (dst->colors != NULL)
            {
                ufbx_vec4 c = mesh->vertex_color.values.data[mesh->vertex_color.indices.data[idx]];
                dst->colors[out_vertex * 4 + 0] = (float)c.x;
                dst->colors[out_vertex * 4 + 1] = (float)c.y;
                dst->colors[out_vertex * 4 + 2] = (float)c.z;
                dst->colors[out_vertex * 4 + 3] = (float)c.w;
            }

            ++out_vertex;
        }
    }

    SDL_free(tri_indices);

    /* Generate identity index buffer. */
    dst->vertex_count = (int)out_vertex;
    dst->index_count = (int)out_vertex;
    dst->indices = (unsigned int *)SDL_malloc(out_vertex * sizeof(unsigned int));
    if (dst->indices == NULL)
    {
        return SDL_OutOfMemory();
    }
    for (size_t i = 0; i < out_vertex; ++i)
    {
        dst->indices[i] = (unsigned int)i;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Count total mesh parts with triangles.                              */
/* ------------------------------------------------------------------ */

static int sdl3d_fbx_count_mesh_parts(const ufbx_scene *scene)
{
    int count = 0;
    for (size_t m = 0; m < scene->meshes.count; ++m)
    {
        const ufbx_mesh *mesh = scene->meshes.data[m];
        for (size_t p = 0; p < mesh->material_parts.count; ++p)
        {
            if (mesh->material_parts.data[p].num_triangles > 0)
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

bool sdl3d_load_model_fbx(const char *path, sdl3d_model *out)
{
    ufbx_load_opts opts;
    ufbx_error error;
    ufbx_scene *scene = NULL;
    int total_parts = 0;
    int mesh_index = 0;

    SDL_zerop(out);
    SDL_zerop(&opts);
    SDL_zerop(&error);

    opts.generate_missing_normals = true;

    scene = ufbx_load_file(path, &opts, &error);
    if (scene == NULL)
    {
        return SDL_SetError("ufbx_load_file failed for '%s': %s", path, error.description.data);
    }

    /* Store source path. */
    {
        size_t len = SDL_strlen(path);
        out->source_path = (char *)SDL_malloc(len + 1);
        if (out->source_path == NULL)
        {
            ufbx_free_scene(scene);
            return SDL_OutOfMemory();
        }
        SDL_memcpy(out->source_path, path, len + 1);
    }

    /* Convert materials. */
    if (scene->materials.count > 0)
    {
        out->material_count = (int)scene->materials.count;
        out->materials = (sdl3d_material *)SDL_calloc(scene->materials.count, sizeof(sdl3d_material));
        if (out->materials == NULL)
        {
            ufbx_free_scene(scene);
            sdl3d_free_model(out);
            return SDL_OutOfMemory();
        }
        for (size_t i = 0; i < scene->materials.count; ++i)
        {
            sdl3d_fbx_convert_material(scene->materials.data[i], &out->materials[i]);
        }
    }

    /* Count mesh parts. */
    total_parts = sdl3d_fbx_count_mesh_parts(scene);
    if (total_parts == 0)
    {
        ufbx_free_scene(scene);
        return SDL_SetError("FBX file '%s' contains no triangle geometry.", path);
    }

    out->mesh_count = total_parts;
    out->meshes = (sdl3d_mesh *)SDL_calloc((size_t)total_parts, sizeof(sdl3d_mesh));
    if (out->meshes == NULL)
    {
        ufbx_free_scene(scene);
        sdl3d_free_model(out);
        return SDL_OutOfMemory();
    }

    /* Convert each mesh part. */
    for (size_t m = 0; m < scene->meshes.count; ++m)
    {
        const ufbx_mesh *mesh = scene->meshes.data[m];
        for (size_t p = 0; p < mesh->material_parts.count; ++p)
        {
            const ufbx_mesh_part *part = &mesh->material_parts.data[p];
            if (part->num_triangles == 0)
            {
                continue;
            }
            if (!sdl3d_fbx_convert_mesh_part(scene, mesh, part, (int)p, &out->meshes[mesh_index]))
            {
                ufbx_free_scene(scene);
                sdl3d_free_model(out);
                return false;
            }
            ++mesh_index;
        }
    }

    ufbx_free_scene(scene);
    return true;
}
