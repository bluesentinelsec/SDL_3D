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

#include "sdl3d/animation.h"

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

/* Store the cgltf_data pointer so we can resolve image indices. */
static char *sdl3d_gltf_texture_ref(const cgltf_texture_view *view, const cgltf_data *data)
{
    if (view->texture == NULL || view->texture->image == NULL)
    {
        return NULL;
    }
    if (view->texture->image->uri != NULL)
    {
        return sdl3d_gltf_strdup(view->texture->image->uri);
    }
    /* Embedded buffer_view texture: reference by image index "#N". */
    if (view->texture->image->buffer_view != NULL)
    {
        char buf[16];
        int idx = (int)cgltf_image_index(data, view->texture->image);
        SDL_snprintf(buf, sizeof(buf), "#%d", idx);
        return sdl3d_gltf_strdup(buf);
    }
    return NULL;
}

static bool sdl3d_gltf_convert_material(const cgltf_data *data, const cgltf_material *src, sdl3d_material *dst)
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
        dst->albedo_map = sdl3d_gltf_texture_ref(&pbr->base_color_texture, data);
        dst->metallic_roughness_map = sdl3d_gltf_texture_ref(&pbr->metallic_roughness_texture, data);
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

    dst->normal_map = sdl3d_gltf_texture_ref(&src->normal_texture, data);
    dst->emissive_map = sdl3d_gltf_texture_ref(&src->emissive_texture, data);

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
    const cgltf_accessor *joints_accessor = NULL;
    const cgltf_accessor *weights_accessor = NULL;
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
        case cgltf_attribute_type_joints:
            if (attr->index == 0)
            {
                joints_accessor = attr->data;
            }
            break;
        case cgltf_attribute_type_weights:
            if (attr->index == 0)
            {
                weights_accessor = attr->data;
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

    /* Unpack joint indices and weights for skinning. */
    if (joints_accessor != NULL && weights_accessor != NULL && (int)joints_accessor->count == vertex_count &&
        (int)weights_accessor->count == vertex_count)
    {
        dst->joint_indices = (unsigned short *)SDL_calloc((size_t)vertex_count * 4, sizeof(unsigned short));
        dst->joint_weights = (float *)SDL_calloc((size_t)vertex_count * 4, sizeof(float));
        if (dst->joint_indices == NULL || dst->joint_weights == NULL)
        {
            return SDL_OutOfMemory();
        }
        for (int v = 0; v < vertex_count; ++v)
        {
            cgltf_uint ji[4] = {0, 0, 0, 0};
            cgltf_float jw[4] = {0, 0, 0, 0};
            cgltf_accessor_read_uint(joints_accessor, (cgltf_size)v, ji, 4);
            cgltf_accessor_read_float(weights_accessor, (cgltf_size)v, jw, 4);
            dst->joint_indices[v * 4 + 0] = (unsigned short)ji[0];
            dst->joint_indices[v * 4 + 1] = (unsigned short)ji[1];
            dst->joint_indices[v * 4 + 2] = (unsigned short)ji[2];
            dst->joint_indices[v * 4 + 3] = (unsigned short)ji[3];
            dst->joint_weights[v * 4 + 0] = jw[0];
            dst->joint_weights[v * 4 + 1] = jw[1];
            dst->joint_weights[v * 4 + 2] = jw[2];
            dst->joint_weights[v * 4 + 3] = jw[3];
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
            if (!sdl3d_gltf_convert_material(data, &data->materials[i], &out->materials[i]))
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

    /* Decode embedded images (buffer_view textures in GLB files). */
    if (data->images_count > 0)
    {
        out->embedded_textures = (sdl3d_image *)SDL_calloc(data->images_count, sizeof(sdl3d_image));
        if (out->embedded_textures != NULL)
        {
            out->embedded_texture_count = (int)data->images_count;
            for (cgltf_size i = 0; i < data->images_count; ++i)
            {
                const cgltf_image *img = &data->images[i];
                if (img->buffer_view != NULL && img->buffer_view->buffer != NULL &&
                    img->buffer_view->buffer->data != NULL)
                {
                    const void *img_data = (const char *)img->buffer_view->buffer->data + img->buffer_view->offset;
                    sdl3d_load_image_from_memory(img_data, img->buffer_view->size, &out->embedded_textures[i]);
                }
            }
        }
    }

    /* Extract skeleton from the first skin. */
    if (data->skins_count > 0)
    {
        const cgltf_skin *skin = &data->skins[0];
        sdl3d_skeleton *skel = (sdl3d_skeleton *)SDL_calloc(1, sizeof(sdl3d_skeleton));
        if (skel != NULL && skin->joints_count > 0)
        {
            skel->joint_count = (int)skin->joints_count;
            skel->joints = (sdl3d_joint *)SDL_calloc(skin->joints_count, sizeof(sdl3d_joint));
            if (skel->joints != NULL)
            {
                for (cgltf_size j = 0; j < skin->joints_count; ++j)
                {
                    const cgltf_node *node = skin->joints[j];
                    sdl3d_joint *jt = &skel->joints[j];
                    jt->name = sdl3d_gltf_strdup(node->name);
                    jt->parent_index = -1;
                    if (node->parent != NULL)
                    {
                        for (cgltf_size k = 0; k < skin->joints_count; ++k)
                        {
                            if (skin->joints[k] == node->parent)
                            {
                                jt->parent_index = (int)k;
                                break;
                            }
                        }
                    }
                    if (skin->inverse_bind_matrices != NULL)
                    {
                        cgltf_accessor_read_float(skin->inverse_bind_matrices, j, jt->inverse_bind_matrix.m, 16);
                    }
                    else
                    {
                        jt->inverse_bind_matrix = sdl3d_mat4_identity();
                    }
                    if (node->has_translation)
                    {
                        jt->local_translation[0] = node->translation[0];
                        jt->local_translation[1] = node->translation[1];
                        jt->local_translation[2] = node->translation[2];
                    }
                    if (node->has_rotation)
                    {
                        jt->local_rotation[0] = node->rotation[0];
                        jt->local_rotation[1] = node->rotation[1];
                        jt->local_rotation[2] = node->rotation[2];
                        jt->local_rotation[3] = node->rotation[3];
                    }
                    else
                    {
                        jt->local_rotation[3] = 1.0f;
                    }
                    jt->local_scale[0] = node->has_scale ? node->scale[0] : 1.0f;
                    jt->local_scale[1] = node->has_scale ? node->scale[1] : 1.0f;
                    jt->local_scale[2] = node->has_scale ? node->scale[2] : 1.0f;
                }
                out->skeleton = skel;
            }
            else
            {
                SDL_free(skel);
            }
        }
        else
        {
            SDL_free(skel);
        }
    }

    /* Extract animations. */
    if (data->animations_count > 0 && out->skeleton != NULL)
    {
        out->animation_count = (int)data->animations_count;
        out->animations = (sdl3d_animation_clip *)SDL_calloc(data->animations_count, sizeof(sdl3d_animation_clip));
        if (out->animations != NULL)
        {
            for (cgltf_size ai = 0; ai < data->animations_count; ++ai)
            {
                const cgltf_animation *anim = &data->animations[ai];
                sdl3d_animation_clip *clip = &out->animations[ai];
                clip->name = sdl3d_gltf_strdup(anim->name);
                clip->channel_count = (int)anim->channels_count;
                clip->channels = (sdl3d_anim_channel *)SDL_calloc(anim->channels_count, sizeof(sdl3d_anim_channel));
                if (clip->channels == NULL)
                {
                    continue;
                }
                for (cgltf_size ci = 0; ci < anim->channels_count; ++ci)
                {
                    const cgltf_animation_channel *src_ch = &anim->channels[ci];
                    sdl3d_anim_channel *dst_ch = &clip->channels[ci];
                    const cgltf_animation_sampler *sampler = src_ch->sampler;
                    int kf_count;
                    dst_ch->joint_index = -1;
                    if (src_ch->target_node != NULL && data->skins_count > 0)
                    {
                        const cgltf_skin *skin = &data->skins[0];
                        for (cgltf_size k = 0; k < skin->joints_count; ++k)
                        {
                            if (skin->joints[k] == src_ch->target_node)
                            {
                                dst_ch->joint_index = (int)k;
                                break;
                            }
                        }
                    }
                    switch (src_ch->target_path)
                    {
                    case cgltf_animation_path_type_translation:
                        dst_ch->path = SDL3D_ANIM_TRANSLATION;
                        break;
                    case cgltf_animation_path_type_rotation:
                        dst_ch->path = SDL3D_ANIM_ROTATION;
                        break;
                    case cgltf_animation_path_type_scale:
                        dst_ch->path = SDL3D_ANIM_SCALE;
                        break;
                    default:
                        continue;
                    }
                    if (sampler == NULL || sampler->input == NULL || sampler->output == NULL)
                    {
                        continue;
                    }
                    kf_count = (int)sampler->input->count;
                    dst_ch->keyframe_count = kf_count;
                    dst_ch->keyframes = (sdl3d_keyframe *)SDL_calloc((size_t)kf_count, sizeof(sdl3d_keyframe));
                    if (dst_ch->keyframes == NULL)
                    {
                        continue;
                    }
                    for (int k = 0; k < kf_count; ++k)
                    {
                        cgltf_accessor_read_float(sampler->input, (cgltf_size)k, &dst_ch->keyframes[k].time, 1);
                        cgltf_accessor_read_float(sampler->output, (cgltf_size)k, dst_ch->keyframes[k].value, 4);
                        if (dst_ch->keyframes[k].time > clip->duration)
                        {
                            clip->duration = dst_ch->keyframes[k].time;
                        }
                    }
                }
            }
        }
    }

    /* ------------------------------------------------------------------ */
    /* Extract node hierarchy for transform propagation.                  */
    /* ------------------------------------------------------------------ */
    if (data->nodes_count > 0)
    {
        /* Build a mapping from cgltf mesh pointer → first sdl3d_mesh index.
         * Our mesh array is ordered: for each cgltf mesh, its triangle
         * primitives appear consecutively starting at mesh_start[m]. */
        int *mesh_start = (int *)SDL_calloc(data->meshes_count, sizeof(int));
        if (mesh_start != NULL)
        {
            int running = 0;
            for (cgltf_size m = 0; m < data->meshes_count; ++m)
            {
                mesh_start[m] = running;
                for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p)
                {
                    if (data->meshes[m].primitives[p].type == cgltf_primitive_type_triangles)
                    {
                        ++running;
                    }
                }
            }

            out->node_count = (int)data->nodes_count;
            out->nodes = (sdl3d_model_node *)SDL_calloc(data->nodes_count, sizeof(sdl3d_model_node));
            if (out->nodes != NULL)
            {
                for (cgltf_size n = 0; n < data->nodes_count; ++n)
                {
                    const cgltf_node *src = &data->nodes[n];
                    sdl3d_model_node *dst = &out->nodes[n];

                    /* Default TRS. */
                    dst->translation[0] = dst->translation[1] = dst->translation[2] = 0.0f;
                    dst->rotation[0] = dst->rotation[1] = dst->rotation[2] = 0.0f;
                    dst->rotation[3] = 1.0f;
                    dst->scale[0] = dst->scale[1] = dst->scale[2] = 1.0f;
                    dst->mesh_index = -1;

                    if (src->has_matrix)
                    {
                        /* cgltf decomposes matrix into TRS fields for us
                         * when has_matrix is set, but the has_translation/
                         * rotation/scale flags may not be set. Read the
                         * local transform via cgltf helper instead. */
                        float local[16];
                        cgltf_node_transform_local(src, local);
                        /* Extract translation from column 3. */
                        dst->translation[0] = local[12];
                        dst->translation[1] = local[13];
                        dst->translation[2] = local[14];
                        /* For scale + rotation, approximate: extract scale
                         * as column lengths, then we skip rotation for
                         * matrix nodes (rare in practice). */
                        float sx = SDL_sqrtf(local[0] * local[0] + local[1] * local[1] + local[2] * local[2]);
                        float sy = SDL_sqrtf(local[4] * local[4] + local[5] * local[5] + local[6] * local[6]);
                        float sz = SDL_sqrtf(local[8] * local[8] + local[9] * local[9] + local[10] * local[10]);
                        dst->scale[0] = sx;
                        dst->scale[1] = sy;
                        dst->scale[2] = sz;
                        /* TODO: extract rotation quaternion from the 3x3 */
                    }
                    if (src->has_translation)
                    {
                        dst->translation[0] = src->translation[0];
                        dst->translation[1] = src->translation[1];
                        dst->translation[2] = src->translation[2];
                    }
                    if (src->has_rotation)
                    {
                        dst->rotation[0] = src->rotation[0];
                        dst->rotation[1] = src->rotation[1];
                        dst->rotation[2] = src->rotation[2];
                        dst->rotation[3] = src->rotation[3];
                    }
                    if (src->has_scale)
                    {
                        dst->scale[0] = src->scale[0];
                        dst->scale[1] = src->scale[1];
                        dst->scale[2] = src->scale[2];
                    }

                    /* Map mesh reference. */
                    if (src->mesh != NULL)
                    {
                        cgltf_size mi = (cgltf_size)(src->mesh - data->meshes);
                        dst->mesh_index = mesh_start[mi];
                    }

                    /* Children. */
                    if (src->children_count > 0)
                    {
                        dst->child_count = (int)src->children_count;
                        dst->children = (int *)SDL_malloc(src->children_count * sizeof(int));
                        if (dst->children != NULL)
                        {
                            for (cgltf_size c = 0; c < src->children_count; ++c)
                            {
                                dst->children[c] = (int)(src->children[c] - data->nodes);
                            }
                        }
                    }
                }

                /* Identify root nodes from the default scene (or all parentless nodes). */
                if (data->scene != NULL && data->scene->nodes_count > 0)
                {
                    out->root_count = (int)data->scene->nodes_count;
                    out->root_nodes = (int *)SDL_malloc(data->scene->nodes_count * sizeof(int));
                    if (out->root_nodes != NULL)
                    {
                        for (cgltf_size r = 0; r < data->scene->nodes_count; ++r)
                        {
                            out->root_nodes[r] = (int)(data->scene->nodes[r] - data->nodes);
                        }
                    }
                }
                else
                {
                    /* Fallback: nodes with no parent are roots. */
                    int rc = 0;
                    for (cgltf_size n = 0; n < data->nodes_count; ++n)
                    {
                        if (data->nodes[n].parent == NULL)
                        {
                            ++rc;
                        }
                    }
                    out->root_nodes = (int *)SDL_malloc((size_t)rc * sizeof(int));
                    if (out->root_nodes != NULL)
                    {
                        out->root_count = rc;
                        int ri = 0;
                        for (cgltf_size n = 0; n < data->nodes_count; ++n)
                        {
                            if (data->nodes[n].parent == NULL)
                            {
                                out->root_nodes[ri++] = (int)n;
                            }
                        }
                    }
                }
            }
            SDL_free(mesh_start);
        }
    }

    cgltf_free(data);
    return true;
}
