/**
 * @file game_presentation_primitives.c
 * @brief Procedural primitive drawing helpers for JSON-authored presentation.
 */

#include "game_presentation_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/drawing3d.h"
#include "slayer3d/math.h"
#include "slayer3d/shapes.h"

#include "game_data_internal.h"
#include "render_context_internal.h"

static const float SLAYER3D_GAME_PRESENTATION_PI = 3.14159265358979323846f;

bool slayer3d_game_data_draw_sphere_batch(slayer3d_render_context *renderer,
                                          const slayer3d_game_data_render_primitive *primitive)
{
    if (renderer == NULL || primitive == NULL || primitive->instances == NULL || primitive->instance_count <= 0)
        return true;

    const int rings = SDL_max(primitive->rings, 3);
    const int slices = SDL_max(primitive->slices, 3);
    const int verts_per_sphere = (rings + 1) * (slices + 1);
    const int indices_per_sphere = rings * slices * 6;
    const int vertex_count = verts_per_sphere * primitive->instance_count;
    const int index_count = indices_per_sphere * primitive->instance_count;
    float *positions = (float *)SDL_malloc((size_t)vertex_count * 3U * sizeof(*positions));
    float *normals = (float *)SDL_malloc((size_t)vertex_count * 3U * sizeof(*normals));
    float *uvs = (float *)SDL_malloc((size_t)vertex_count * 2U * sizeof(*uvs));
    unsigned int *indices = (unsigned int *)SDL_malloc((size_t)index_count * sizeof(*indices));
    if (positions == NULL || normals == NULL || uvs == NULL || indices == NULL)
    {
        SDL_free(positions);
        SDL_free(normals);
        SDL_free(uvs);
        SDL_free(indices);
        return false;
    }

    int vertex_offset = 0;
    int index_offset = 0;
    for (int instance = 0; instance < primitive->instance_count; ++instance)
    {
        const slayer3d_vec3 center = primitive->instances[instance];
        for (int ring = 0; ring <= rings; ++ring)
        {
            const float theta = SDL_PI_F * (float)ring / (float)rings;
            const float sin_t = SDL_sinf(theta);
            const float cos_t = SDL_cosf(theta);
            for (int slice = 0; slice <= slices; ++slice)
            {
                const float phi = 2.0f * SDL_PI_F * (float)slice / (float)slices;
                const float nx = sin_t * SDL_cosf(phi);
                const float ny = cos_t;
                const float nz = sin_t * SDL_sinf(phi);
                const int vertex = vertex_offset + ring * (slices + 1) + slice;
                positions[vertex * 3 + 0] = center.x + primitive->radius * nx;
                positions[vertex * 3 + 1] = center.y + primitive->radius * ny;
                positions[vertex * 3 + 2] = center.z + primitive->radius * nz;
                normals[vertex * 3 + 0] = nx;
                normals[vertex * 3 + 1] = ny;
                normals[vertex * 3 + 2] = nz;
                uvs[vertex * 2 + 0] = (float)slice / (float)slices;
                uvs[vertex * 2 + 1] = 1.0f - (float)ring / (float)rings;
            }
        }
        for (int ring = 0; ring < rings; ++ring)
        {
            for (int slice = 0; slice < slices; ++slice)
            {
                const unsigned int v00 = (unsigned int)(vertex_offset + ring * (slices + 1) + slice);
                const unsigned int v01 = v00 + 1U;
                const unsigned int v10 = (unsigned int)(vertex_offset + (ring + 1) * (slices + 1) + slice);
                const unsigned int v11 = v10 + 1U;
                indices[index_offset++] = v00;
                indices[index_offset++] = v01;
                indices[index_offset++] = v11;
                indices[index_offset++] = v00;
                indices[index_offset++] = v11;
                indices[index_offset++] = v10;
            }
        }
        vertex_offset += verts_per_sphere;
    }

    slayer3d_mesh mesh;
    SDL_zero(mesh);
    mesh.positions = positions;
    mesh.normals = normals;
    mesh.uvs = uvs;
    mesh.indices = indices;
    mesh.vertex_count = vertex_count;
    mesh.index_count = index_count;
    const bool ok = slayer3d_draw_mesh(renderer, &mesh, NULL, primitive->color);
    SDL_free(positions);
    SDL_free(normals);
    SDL_free(uvs);
    SDL_free(indices);
    return ok;
}

bool slayer3d_game_data_primitive_sphere_can_batch(const slayer3d_game_data_render_primitive *primitive)
{
    if (primitive == NULL || primitive->type != SLAYER3D_GAME_DATA_RENDER_SPHERE || primitive->texture_image != NULL)
        return false;
    return SDL_fabsf(primitive->rotation_angle) <= 0.0001f && SDL_fabsf(primitive->rotation_axis.x) <= 0.0001f &&
           SDL_fabsf(primitive->rotation_axis.y) <= 0.0001f && SDL_fabsf(primitive->rotation_axis.z) <= 0.0001f;
}

static float primitive_lod_bounding_radius(const slayer3d_game_data_render_primitive *primitive)
{
    if (primitive == NULL)
        return 0.0f;
    switch (primitive->type)
    {
    case SLAYER3D_GAME_DATA_RENDER_SPHERE:
    case SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH:
        return SDL_max(primitive->radius, 0.0f);
    case SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE:
        switch (primitive->mesh_primitive)
        {
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
            return SDL_max(primitive->radius, 0.0f);
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
            return SDL_sqrtf(primitive->radius * primitive->radius +
                             (primitive->height * 0.5f) * (primitive->height * 0.5f));
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
            return SDL_max(primitive->major_radius + primitive->minor_radius, 0.0f);
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
            return 0.5f * SDL_sqrtf(primitive->size.x * primitive->size.x + primitive->size.y * primitive->size.y +
                                    primitive->size.z * primitive->size.z);
        case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
        default:
            return 0.0f;
        }
    case SLAYER3D_GAME_DATA_RENDER_CUBE:
    case SLAYER3D_GAME_DATA_RENDER_SPRITE:
    case SLAYER3D_GAME_DATA_RENDER_MODEL:
    default:
        return 0.0f;
    }
}

static float projected_primitive_pixels(const primitive_draw_context *context,
                                        const slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || context->camera == NULL || primitive == NULL)
        return 0.0f;
    const float radius = primitive_lod_bounding_radius(primitive);
    if (radius <= 0.0f)
        return 0.0f;

    slayer3d_vec3 center = primitive->position;
    if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH && primitive->instances != NULL &&
        primitive->instance_count > 0)
    {
        float best = 0.0f;
        slayer3d_game_data_render_primitive sample = *primitive;
        sample.type = SLAYER3D_GAME_DATA_RENDER_SPHERE;
        sample.instances = NULL;
        sample.instance_count = 0;
        for (int i = 0; i < primitive->instance_count; ++i)
        {
            sample.position = primitive->instances[i];
            best = SDL_max(best, projected_primitive_pixels(context, &sample));
        }
        return best;
    }

    const slayer3d_vec3 forward =
        slayer3d_vec3_normalize(slayer3d_vec3_sub(context->camera->target, context->camera->position));
    if (slayer3d_vec3_length_squared(forward) <= 0.000001f)
        return 0.0f;
    const float distance = slayer3d_vec3_dot(slayer3d_vec3_sub(center, context->camera->position), forward);
    if (distance <= 0.01f)
        return context->render_settings.procedural_lod_near_pixels;

    const int axis_pixels = context->camera->fov_axis == SLAYER3D_CAMERA_FOV_HORIZONTAL
                                ? slayer3d_get_render_context_width(context->renderer)
                                : slayer3d_get_render_context_height(context->renderer);
    const float fov = SDL_clamp(context->camera->fovy, 1.0f, 175.0f) * SLAYER3D_GAME_PRESENTATION_PI / 180.0f;
    const float denominator = 2.0f * distance * SDL_tanf(fov * 0.5f);
    if (axis_pixels <= 0 || denominator <= 0.0001f)
        return 0.0f;
    return ((radius * 2.0f) * (float)axis_pixels / denominator) * SDL_max(primitive->lod_bias, 0.001f);
}

static bool primitive_mesh_uses_procedural_lod(slayer3d_game_data_mesh_primitive_kind primitive)
{
    switch (primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        return true;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        return false;
    }
}

static int lod_segment_count(int authored, int minimum, float projected_pixels, float near_pixels, float far_pixels)
{
    authored = SDL_max(authored, 3);
    minimum = SDL_clamp(minimum, 3, authored);
    if (authored <= minimum || near_pixels <= far_pixels + 0.001f)
        return authored;
    if (projected_pixels >= near_pixels)
        return authored;
    if (projected_pixels <= far_pixels)
        return minimum;
    const float t = (projected_pixels - far_pixels) / (near_pixels - far_pixels);
    const float multiplier = t >= 0.66f ? 0.75f : (t >= 0.33f ? 0.5f : 0.0f);
    const int resolved = multiplier > 0.0f ? (int)SDL_floorf((float)authored * multiplier + 0.5f) : minimum;
    return SDL_clamp(resolved, minimum, authored);
}

static Uint64 procedural_lod_triangle_count(const slayer3d_game_data_render_primitive *primitive)
{
    if (primitive == NULL)
        return 0u;

    const int slices = SDL_max(primitive->slices, 3);
    const int rings = SDL_max(primitive->rings, 3);
    const int tube_segments = SDL_max(primitive->tube_segments, 3);
    const int instance_count = SDL_max(primitive->instance_count, 1);
    Uint64 triangles = 0u;

    if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE ||
        primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH)
    {
        triangles = (Uint64)rings * (Uint64)slices * 2u;
        return triangles * (Uint64)instance_count;
    }

    if (primitive->type != SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE)
        return 0u;

    switch (primitive->mesh_primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
        return (Uint64)rings * (Uint64)slices * 2u;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE:
        return (Uint64)slices * (Uint64)(SDL_max(rings, 1) * 2 + 1) * 2u;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
        return (Uint64)slices * 4u;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        return (Uint64)slices * (Uint64)tube_segments * 2u;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        return (Uint64)slices;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
        return (Uint64)rings * (Uint64)slices * 2u + (Uint64)slices;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        return (Uint64)SDL_max(rings, 1) * 72u;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        return 0u;
    }
}

static bool model_lod_accumulate_bounds(const slayer3d_mesh *mesh, slayer3d_vec3 scale, float *radius,
                                        Uint64 *triangles)
{
    if (mesh == NULL || radius == NULL || triangles == NULL)
        return false;

    *triangles += (Uint64)((mesh->index_count > 0 ? mesh->index_count : mesh->vertex_count) / 3);

    if (mesh->has_local_bounds)
    {
        const slayer3d_vec3 corners[] = {
            slayer3d_vec3_make(mesh->local_bounds.min.x, mesh->local_bounds.min.y, mesh->local_bounds.min.z),
            slayer3d_vec3_make(mesh->local_bounds.min.x, mesh->local_bounds.min.y, mesh->local_bounds.max.z),
            slayer3d_vec3_make(mesh->local_bounds.min.x, mesh->local_bounds.max.y, mesh->local_bounds.min.z),
            slayer3d_vec3_make(mesh->local_bounds.min.x, mesh->local_bounds.max.y, mesh->local_bounds.max.z),
            slayer3d_vec3_make(mesh->local_bounds.max.x, mesh->local_bounds.min.y, mesh->local_bounds.min.z),
            slayer3d_vec3_make(mesh->local_bounds.max.x, mesh->local_bounds.min.y, mesh->local_bounds.max.z),
            slayer3d_vec3_make(mesh->local_bounds.max.x, mesh->local_bounds.max.y, mesh->local_bounds.min.z),
            slayer3d_vec3_make(mesh->local_bounds.max.x, mesh->local_bounds.max.y, mesh->local_bounds.max.z),
        };
        for (size_t i = 0; i < SDL_arraysize(corners); ++i)
        {
            const slayer3d_vec3 scaled =
                slayer3d_vec3_make(corners[i].x * scale.x, corners[i].y * scale.y, corners[i].z * scale.z);
            *radius = SDL_max(*radius, slayer3d_vec3_length(scaled));
        }
        return true;
    }

    if (mesh->positions == NULL || mesh->vertex_count <= 0)
        return true;

    for (int i = 0; i < mesh->vertex_count; ++i)
    {
        const slayer3d_vec3 scaled =
            slayer3d_vec3_make(mesh->positions[i * 3 + 0] * scale.x, mesh->positions[i * 3 + 1] * scale.y,
                               mesh->positions[i * 3 + 2] * scale.z);
        *radius = SDL_max(*radius, slayer3d_vec3_length(scaled));
    }
    return true;
}

static bool model_lod_projected_pixels(const primitive_draw_context *context, const slayer3d_model *model,
                                       const slayer3d_game_data_render_primitive *primitive,
                                       float *out_projected_pixels, Uint64 *out_triangles)
{
    if (context == NULL || context->camera == NULL || model == NULL || primitive == NULL ||
        out_projected_pixels == NULL || out_triangles == NULL || model->meshes == NULL || model->mesh_count <= 0)
    {
        return false;
    }

    float radius = 0.0f;
    Uint64 triangles = 0u;
    const slayer3d_vec3 scale = slayer3d_vec3_make(
        SDL_fabsf(primitive->model_scale.x), SDL_fabsf(primitive->model_scale.y), SDL_fabsf(primitive->model_scale.z));
    for (int i = 0; i < model->mesh_count; ++i)
    {
        if (!model_lod_accumulate_bounds(&model->meshes[i], scale, &radius, &triangles))
            return false;
    }
    if (radius <= 0.0f || triangles == 0u)
        return false;

    const slayer3d_vec3 forward =
        slayer3d_vec3_normalize(slayer3d_vec3_sub(context->camera->target, context->camera->position));
    if (slayer3d_vec3_length_squared(forward) <= 0.000001f)
        return false;
    const float distance =
        slayer3d_vec3_dot(slayer3d_vec3_sub(primitive->position, context->camera->position), forward);
    if (distance <= 0.01f)
    {
        *out_projected_pixels = context->render_settings.procedural_lod_near_pixels;
        *out_triangles = triangles;
        return true;
    }

    const int axis_pixels = context->camera->fov_axis == SLAYER3D_CAMERA_FOV_HORIZONTAL
                                ? slayer3d_get_render_context_width(context->renderer)
                                : slayer3d_get_render_context_height(context->renderer);
    const float fov = SDL_clamp(context->camera->fovy, 1.0f, 175.0f) * SLAYER3D_GAME_PRESENTATION_PI / 180.0f;
    const float denominator = 2.0f * distance * SDL_tanf(fov * 0.5f);
    if (axis_pixels <= 0 || denominator <= 0.0001f)
        return false;

    *out_projected_pixels = ((radius * 2.0f) * (float)axis_pixels / denominator) * SDL_max(primitive->lod_bias, 0.001f);
    *out_triangles = triangles;
    return true;
}

bool slayer3d_game_data_model_lod_should_cull(primitive_draw_context *context, const slayer3d_model *model,
                                              const slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || context->renderer == NULL || model == NULL || primitive == NULL ||
        !context->render_settings.model_lod_culling_enabled || !primitive->lod_enabled || primitive->view_space)
    {
        return false;
    }

    float projected_pixels = 0.0f;
    Uint64 triangles = 0u;
    if (!model_lod_projected_pixels(context, model, primitive, &projected_pixels, &triangles))
        return false;

    ++context->renderer->stats.model_lod_candidates;
    const float cull_pixels = primitive->lod_cull_pixels >= 0.0f ? primitive->lod_cull_pixels
                                                                 : context->render_settings.model_lod_cull_pixels;
    if (cull_pixels > 0.0f && projected_pixels <= cull_pixels)
    {
        ++context->renderer->stats.model_lod_culled;
        context->renderer->stats.model_lod_triangles_saved += triangles;
        return true;
    }
    return false;
}

static void record_procedural_lod_stats(primitive_draw_context *context,
                                        const slayer3d_game_data_render_primitive *authored,
                                        const slayer3d_game_data_render_primitive *resolved)
{
    if (context == NULL || context->renderer == NULL || authored == NULL || resolved == NULL)
        return;

    const Uint64 authored_triangles = procedural_lod_triangle_count(authored);
    const Uint64 resolved_triangles = procedural_lod_triangle_count(resolved);
    ++context->renderer->stats.procedural_lod_candidates;
    context->renderer->stats.procedural_lod_authored_triangles += authored_triangles;
    context->renderer->stats.procedural_lod_resolved_triangles += resolved_triangles;
    if (authored_triangles > resolved_triangles)
    {
        ++context->renderer->stats.procedural_lod_reduced;
        context->renderer->stats.procedural_lod_triangles_saved += authored_triangles - resolved_triangles;
    }
}

void slayer3d_game_data_apply_primitive_lod(primitive_draw_context *context,
                                            slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || primitive == NULL || !context->render_settings.procedural_lod_enabled ||
        !primitive->lod_enabled || primitive->view_space)
    {
        return;
    }
    if (primitive->type != SLAYER3D_GAME_DATA_RENDER_SPHERE &&
        primitive->type != SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH &&
        (primitive->type != SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE ||
         !primitive_mesh_uses_procedural_lod(primitive->mesh_primitive)))
    {
        return;
    }

    const float projected = projected_primitive_pixels(context, primitive);
    if (projected <= 0.0f)
        return;
    const slayer3d_game_data_render_primitive authored = *primitive;
    const int minimum = context->render_settings.procedural_lod_min_segments;
    const float near_pixels = context->render_settings.procedural_lod_near_pixels;
    const float far_pixels = context->render_settings.procedural_lod_far_pixels;
    if (primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE ||
        primitive->type == SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH)
    {
        primitive->slices = lod_segment_count(primitive->slices, minimum, projected, near_pixels, far_pixels);
        primitive->rings = lod_segment_count(primitive->rings, minimum, projected, near_pixels, far_pixels);
        record_procedural_lod_stats(context, &authored, primitive);
        return;
    }

    switch (primitive->mesh_primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
        primitive->slices = lod_segment_count(primitive->slices, minimum, projected, near_pixels, far_pixels);
        primitive->rings = lod_segment_count(primitive->rings, minimum, projected, near_pixels, far_pixels);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        primitive->slices = lod_segment_count(primitive->slices, minimum, projected, near_pixels, far_pixels);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        primitive->slices = lod_segment_count(primitive->slices, minimum, projected, near_pixels, far_pixels);
        primitive->tube_segments =
            lod_segment_count(primitive->tube_segments, minimum, projected, near_pixels, far_pixels);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        primitive->rings = lod_segment_count(primitive->rings, minimum, projected, near_pixels, far_pixels);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        break;
    }
    record_procedural_lod_stats(context, &authored, primitive);
}

static bool primitive_sphere_batch_matches(const slayer3d_game_data_render_primitive *batch,
                                           const slayer3d_game_data_render_primitive *primitive)
{
    if (batch == NULL || primitive == NULL)
        return false;
    return batch->type == SLAYER3D_GAME_DATA_RENDER_SPHERE &&
           slayer3d_game_data_primitive_sphere_can_batch(primitive) &&
           SDL_fabsf(batch->radius - primitive->radius) <= 0.0001f && batch->rings == primitive->rings &&
           batch->slices == primitive->slices && batch->lighting_enabled == primitive->lighting_enabled &&
           batch->emissive == primitive->emissive &&
           SDL_fabsf(batch->emissive_color.x - primitive->emissive_color.x) <= 0.0001f &&
           SDL_fabsf(batch->emissive_color.y - primitive->emissive_color.y) <= 0.0001f &&
           SDL_fabsf(batch->emissive_color.z - primitive->emissive_color.z) <= 0.0001f &&
           batch->color.r == primitive->color.r && batch->color.g == primitive->color.g &&
           batch->color.b == primitive->color.b && batch->color.a == primitive->color.a;
}

bool slayer3d_game_data_flush_sphere_draw_batch(primitive_draw_context *context)
{
    if (context == NULL || !context->sphere_batch_active || context->sphere_batch_count <= 0)
        return true;
    slayer3d_game_data_render_primitive primitive = context->sphere_batch;
    primitive.type = SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH;
    primitive.instances = context->sphere_batch_positions;
    primitive.instance_count = context->sphere_batch_count;
    const bool ok = slayer3d_game_data_draw_sphere_batch(context->renderer, &primitive);
    context->sphere_batch_active = false;
    context->sphere_batch_count = 0;
    return ok;
}

bool slayer3d_game_data_append_sphere_draw_batch(primitive_draw_context *context,
                                                 const slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || primitive == NULL)
        return false;
    if (!context->sphere_batch_active || !primitive_sphere_batch_matches(&context->sphere_batch, primitive))
    {
        if (!slayer3d_game_data_flush_sphere_draw_batch(context))
            return false;
        context->sphere_batch = *primitive;
        context->sphere_batch.instances = NULL;
        context->sphere_batch.instance_count = 0;
        context->sphere_batch_active = true;
    }
    if (context->sphere_batch_count >= context->sphere_batch_capacity)
    {
        const int next_capacity = context->sphere_batch_capacity > 0 ? context->sphere_batch_capacity * 2 : 16;
        slayer3d_vec3 *positions =
            (slayer3d_vec3 *)SDL_realloc(context->sphere_batch_positions, (size_t)next_capacity * sizeof(*positions));
        if (positions == NULL)
            return false;
        context->sphere_batch_positions = positions;
        context->sphere_batch_capacity = next_capacity;
    }
    context->sphere_batch_positions[context->sphere_batch_count++] = primitive->position;
    return true;
}

bool slayer3d_game_data_primitive_asset_ready(const primitive_draw_context *context,
                                              slayer3d_game_data_asset_warmup_kind kind, const char *id)
{
    if (context == NULL || id == NULL || id[0] == '\0')
        return true;

    slayer3d_game_data_asset_warmup_state state;
    if (slayer3d_game_data_asset_warmup_request_state(context->asset_warmup, kind, NULL, id, &state) &&
        state != SLAYER3D_GAME_DATA_ASSET_WARMUP_READY)
    {
        return false;
    }
    return true;
}

const slayer3d_texture2d *slayer3d_game_data_primitive_texture(primitive_draw_context *context,
                                                               const slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || primitive == NULL || primitive->texture_image == NULL || context->image_cache == NULL)
        return NULL;
    if (!slayer3d_game_data_primitive_asset_ready(context, SLAYER3D_GAME_DATA_ASSET_WARMUP_UI_IMAGE,
                                                  primitive->texture_image))
    {
        return NULL;
    }
    slayer3d_game_data_image_cache_entry *entry =
        slayer3d_game_data_find_or_load_image_entry(context->runtime, context->image_cache, primitive->texture_image);
    return entry != NULL ? &entry->texture : NULL;
}

static void mesh_primitive_free_mesh(slayer3d_mesh *mesh)
{
    if (mesh == NULL)
        return;
    SDL_free(mesh->positions);
    SDL_free(mesh->normals);
    SDL_free(mesh->uvs);
    SDL_free(mesh->indices);
    SDL_zero(*mesh);
}

static bool mesh_primitive_alloc_mesh(slayer3d_mesh *mesh, int vertex_count, int index_count, bool with_uvs)
{
    if (mesh == NULL || vertex_count <= 0 || index_count <= 0)
        return false;
    SDL_zero(*mesh);
    mesh->positions = (float *)SDL_calloc((size_t)vertex_count * 3U, sizeof(float));
    mesh->normals = (float *)SDL_calloc((size_t)vertex_count * 3U, sizeof(float));
    mesh->uvs = with_uvs ? (float *)SDL_calloc((size_t)vertex_count * 2U, sizeof(float)) : NULL;
    mesh->indices = (unsigned int *)SDL_calloc((size_t)index_count, sizeof(unsigned int));
    if (mesh->positions == NULL || mesh->normals == NULL || (with_uvs && mesh->uvs == NULL) || mesh->indices == NULL)
    {
        mesh_primitive_free_mesh(mesh);
        return SDL_OutOfMemory();
    }
    mesh->vertex_count = vertex_count;
    mesh->index_count = index_count;
    mesh->material_index = -1;
    return true;
}

static void mesh_primitive_set_vertex(slayer3d_mesh *mesh, int index, slayer3d_vec3 position, slayer3d_vec3 normal,
                                      float u, float v)
{
    mesh->positions[index * 3 + 0] = position.x;
    mesh->positions[index * 3 + 1] = position.y;
    mesh->positions[index * 3 + 2] = position.z;
    mesh->normals[index * 3 + 0] = normal.x;
    mesh->normals[index * 3 + 1] = normal.y;
    mesh->normals[index * 3 + 2] = normal.z;
    if (mesh->uvs != NULL)
    {
        mesh->uvs[index * 2 + 0] = u;
        mesh->uvs[index * 2 + 1] = v;
    }
}

static slayer3d_vec3 mesh_primitive_face_normal(slayer3d_vec3 a, slayer3d_vec3 b, slayer3d_vec3 c)
{
    return slayer3d_vec3_normalize(slayer3d_vec3_cross(slayer3d_vec3_sub(b, a), slayer3d_vec3_sub(c, a)));
}

static bool mesh_primitive_cache_entry_matches(const slayer3d_game_data_mesh_primitive_cache_entry *entry,
                                               const slayer3d_game_data_render_primitive *primitive)
{
    if (entry == NULL || primitive == NULL || !entry->loaded)
        return false;
    return entry->primitive == primitive->mesh_primitive && entry->size.x == primitive->size.x &&
           entry->size.y == primitive->size.y && entry->size.z == primitive->size.z &&
           entry->radius == primitive->radius && entry->radius_top == primitive->radius_top &&
           entry->radius_bottom == primitive->radius_bottom && entry->height == primitive->height &&
           entry->major_radius == primitive->major_radius && entry->minor_radius == primitive->minor_radius &&
           entry->bevel_radius == primitive->bevel_radius && entry->arc_angle == primitive->arc_angle &&
           entry->slices == primitive->slices && entry->rings == primitive->rings &&
           entry->tube_segments == primitive->tube_segments;
}

static void mesh_primitive_cache_entry_set_key(slayer3d_game_data_mesh_primitive_cache_entry *entry,
                                               const slayer3d_game_data_render_primitive *primitive)
{
    entry->primitive = primitive->mesh_primitive;
    entry->size = primitive->size;
    entry->radius = primitive->radius;
    entry->radius_top = primitive->radius_top;
    entry->radius_bottom = primitive->radius_bottom;
    entry->height = primitive->height;
    entry->major_radius = primitive->major_radius;
    entry->minor_radius = primitive->minor_radius;
    entry->bevel_radius = primitive->bevel_radius;
    entry->arc_angle = primitive->arc_angle;
    entry->slices = primitive->slices;
    entry->rings = primitive->rings;
    entry->tube_segments = primitive->tube_segments;
}

static Uint32 mesh_primitive_key_float_bits(float value)
{
    Uint32 bits = 0;
    SDL_memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool slayer3d_game_data_mesh_primitive_cacheable(const slayer3d_game_data_render_primitive *primitive)
{
    if (primitive == NULL || primitive->type != SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE)
        return false;
    switch (primitive->mesh_primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
        return true;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        return false;
    }
}

bool slayer3d_game_data_mesh_primitive_warmup_key(const slayer3d_game_data_render_primitive *primitive, char *buffer,
                                                  int buffer_size)
{
    if (!slayer3d_game_data_mesh_primitive_cacheable(primitive) || buffer == NULL || buffer_size <= 0)
        return false;

    const int written = SDL_snprintf(
        buffer, (size_t)buffer_size, "mesh:%d:%08x:%08x:%08x:%08x:%08x:%08x:%08x:%08x:%08x:%08x:%08x:%d:%d:%d",
        (int)primitive->mesh_primitive, (unsigned int)mesh_primitive_key_float_bits(primitive->size.x),
        (unsigned int)mesh_primitive_key_float_bits(primitive->size.y),
        (unsigned int)mesh_primitive_key_float_bits(primitive->size.z),
        (unsigned int)mesh_primitive_key_float_bits(primitive->radius),
        (unsigned int)mesh_primitive_key_float_bits(primitive->radius_top),
        (unsigned int)mesh_primitive_key_float_bits(primitive->radius_bottom),
        (unsigned int)mesh_primitive_key_float_bits(primitive->height),
        (unsigned int)mesh_primitive_key_float_bits(primitive->major_radius),
        (unsigned int)mesh_primitive_key_float_bits(primitive->minor_radius),
        (unsigned int)mesh_primitive_key_float_bits(primitive->bevel_radius),
        (unsigned int)mesh_primitive_key_float_bits(primitive->arc_angle), primitive->slices, primitive->rings,
        primitive->tube_segments);
    return written > 0 && written < buffer_size;
}

static bool build_cube_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    if (!mesh_primitive_alloc_mesh(mesh, 24, 36, true))
        return false;
    const float hx = primitive->size.x * 0.5f;
    const float hy = primitive->size.y * 0.5f;
    const float hz = primitive->size.z * 0.5f;
    const slayer3d_vec3 c[8] = {{-hx, -hy, -hz}, {hx, -hy, -hz}, {-hx, hy, -hz}, {hx, hy, -hz},
                                {-hx, -hy, hz},  {hx, -hy, hz},  {-hx, hy, hz},  {hx, hy, hz}};
    static const int faces[6][4] = {
        {4, 5, 7, 6}, {1, 0, 2, 3}, {5, 1, 3, 7}, {0, 4, 6, 2}, {6, 7, 3, 2}, {0, 1, 5, 4},
    };
    static const slayer3d_vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    static const float uvs[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int f = 0; f < 6; ++f)
    {
        const int base = f * 4;
        for (int v = 0; v < 4; ++v)
            mesh_primitive_set_vertex(mesh, base + v, c[faces[f][v]], normals[f], uvs[v][0], uvs[v][1]);
        const unsigned int b = (unsigned int)base;
        const int ii = f * 6;
        mesh->indices[ii + 0] = b;
        mesh->indices[ii + 1] = b + 1U;
        mesh->indices[ii + 2] = b + 2U;
        mesh->indices[ii + 3] = b;
        mesh->indices[ii + 4] = b + 2U;
        mesh->indices[ii + 5] = b + 3U;
    }
    return true;
}

static bool build_quad_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    if (!mesh_primitive_alloc_mesh(mesh, 4, 6, true))
        return false;
    const float hx = primitive->size.x * 0.5f;
    const float hy = primitive->size.y * 0.5f;
    const slayer3d_vec3 n = {0.0f, 0.0f, 1.0f};
    mesh_primitive_set_vertex(mesh, 0, slayer3d_vec3_make(-hx, -hy, 0.0f), n, 0.0f, 1.0f);
    mesh_primitive_set_vertex(mesh, 1, slayer3d_vec3_make(hx, -hy, 0.0f), n, 1.0f, 1.0f);
    mesh_primitive_set_vertex(mesh, 2, slayer3d_vec3_make(hx, hy, 0.0f), n, 1.0f, 0.0f);
    mesh_primitive_set_vertex(mesh, 3, slayer3d_vec3_make(-hx, hy, 0.0f), n, 0.0f, 0.0f);
    const unsigned int indices[6] = {0, 1, 2, 0, 2, 3};
    SDL_memcpy(mesh->indices, indices, sizeof(indices));
    return true;
}

static bool build_disc_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int segments = primitive->slices;
    if (!mesh_primitive_alloc_mesh(mesh, segments + 1, segments * 3, true))
        return false;
    const slayer3d_vec3 n = {0.0f, 0.0f, 1.0f};
    mesh_primitive_set_vertex(mesh, 0, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), n, 0.5f, 0.5f);
    for (int i = 0; i < segments; ++i)
    {
        const float a = (float)i / (float)segments * SLAYER3D_GAME_PRESENTATION_PI * 2.0f;
        const float x = SDL_cosf(a) * primitive->radius;
        const float y = SDL_sinf(a) * primitive->radius;
        mesh_primitive_set_vertex(mesh, i + 1, slayer3d_vec3_make(x, y, 0.0f), n,
                                  0.5f + x / SDL_max(primitive->radius * 2.0f, 0.0001f),
                                  0.5f + y / SDL_max(primitive->radius * 2.0f, 0.0001f));
    }
    int ii = 0;
    for (int i = 0; i < segments; ++i)
    {
        mesh->indices[ii++] = 0U;
        mesh->indices[ii++] = (unsigned int)(i + 1);
        mesh->indices[ii++] = (unsigned int)((i + 1) % segments + 1);
    }
    return true;
}

static bool build_sphere_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int rings = primitive->rings;
    const int slices = primitive->slices;
    if (!mesh_primitive_alloc_mesh(mesh, (rings + 1) * (slices + 1), rings * slices * 6, true))
        return false;
    for (int r = 0; r <= rings; ++r)
    {
        const float theta = SLAYER3D_GAME_PRESENTATION_PI * (float)r / (float)rings;
        for (int s = 0; s <= slices; ++s)
        {
            const float phi = SLAYER3D_GAME_PRESENTATION_PI * 2.0f * (float)s / (float)slices;
            const slayer3d_vec3 normal =
                slayer3d_vec3_make(SDL_sinf(theta) * SDL_cosf(phi), SDL_cosf(theta), SDL_sinf(theta) * SDL_sinf(phi));
            const slayer3d_vec3 position = slayer3d_vec3_scale(normal, primitive->radius);
            mesh_primitive_set_vertex(mesh, r * (slices + 1) + s, position, normal, (float)s / (float)slices,
                                      1.0f - (float)r / (float)rings);
        }
    }
    int ii = 0;
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < slices; ++s)
        {
            const unsigned int a = (unsigned int)(r * (slices + 1) + s);
            const unsigned int b = a + 1U;
            const unsigned int c = (unsigned int)((r + 1) * (slices + 1) + s);
            const unsigned int d = c + 1U;
            mesh->indices[ii++] = a;
            mesh->indices[ii++] = b;
            mesh->indices[ii++] = d;
            mesh->indices[ii++] = a;
            mesh->indices[ii++] = d;
            mesh->indices[ii++] = c;
        }
    }
    return true;
}

static bool build_cylinder_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int slices = primitive->slices;
    const int side_verts = 2 * (slices + 1);
    const int cap_verts = (slices + 2) * 2;
    if (!mesh_primitive_alloc_mesh(mesh, side_verts + cap_verts, slices * 12, false))
        return false;
    const float hh = primitive->height * 0.5f;
    for (int s = 0; s <= slices; ++s)
    {
        const float phi = SLAYER3D_GAME_PRESENTATION_PI * 2.0f * (float)s / (float)slices;
        const float c = SDL_cosf(phi);
        const float z = SDL_sinf(phi);
        const slayer3d_vec3 normal = slayer3d_vec3_make(c, 0.0f, z);
        mesh_primitive_set_vertex(mesh, s, slayer3d_vec3_make(primitive->radius_top * c, hh, primitive->radius_top * z),
                                  normal, 0.0f, 0.0f);
        mesh_primitive_set_vertex(mesh, slices + 1 + s,
                                  slayer3d_vec3_make(primitive->radius_bottom * c, -hh, primitive->radius_bottom * z),
                                  normal, 0.0f, 0.0f);
    }
    int ii = 0;
    for (int s = 0; s < slices; ++s)
    {
        const unsigned int t0 = (unsigned int)s;
        const unsigned int t1 = (unsigned int)(s + 1);
        const unsigned int b0 = (unsigned int)(slices + 1 + s);
        const unsigned int b1 = (unsigned int)(slices + 1 + s + 1);
        mesh->indices[ii++] = b0;
        mesh->indices[ii++] = t0;
        mesh->indices[ii++] = t1;
        mesh->indices[ii++] = b0;
        mesh->indices[ii++] = t1;
        mesh->indices[ii++] = b1;
    }
    int cap = side_verts;
    mesh_primitive_set_vertex(mesh, cap, slayer3d_vec3_make(0.0f, hh, 0.0f), slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0,
                              0);
    for (int s = 0; s <= slices; ++s)
    {
        const float phi = SLAYER3D_GAME_PRESENTATION_PI * 2.0f * (float)s / (float)slices;
        mesh_primitive_set_vertex(
            mesh, cap + 1 + s,
            slayer3d_vec3_make(primitive->radius_top * SDL_cosf(phi), hh, primitive->radius_top * SDL_sinf(phi)),
            slayer3d_vec3_make(0.0f, 1.0f, 0.0f), 0, 0);
    }
    for (int s = 0; s < slices; ++s)
    {
        mesh->indices[ii++] = (unsigned int)cap;
        mesh->indices[ii++] = (unsigned int)(cap + 1 + s + 1);
        mesh->indices[ii++] = (unsigned int)(cap + 1 + s);
    }
    cap += slices + 2;
    mesh_primitive_set_vertex(mesh, cap, slayer3d_vec3_make(0.0f, -hh, 0.0f), slayer3d_vec3_make(0.0f, -1.0f, 0.0f), 0,
                              0);
    for (int s = 0; s <= slices; ++s)
    {
        const float phi = SLAYER3D_GAME_PRESENTATION_PI * 2.0f * (float)s / (float)slices;
        mesh_primitive_set_vertex(
            mesh, cap + 1 + s,
            slayer3d_vec3_make(primitive->radius_bottom * SDL_cosf(phi), -hh, primitive->radius_bottom * SDL_sinf(phi)),
            slayer3d_vec3_make(0.0f, -1.0f, 0.0f), 0, 0);
    }
    for (int s = 0; s < slices; ++s)
    {
        mesh->indices[ii++] = (unsigned int)cap;
        mesh->indices[ii++] = (unsigned int)(cap + 1 + s);
        mesh->indices[ii++] = (unsigned int)(cap + 1 + s + 1);
    }
    return true;
}

static bool build_capsule_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int slices = primitive->slices;
    const int rings = primitive->rings;
    const int total_rings = 2 * rings + 2;
    const int verts_per_ring = slices + 1;
    if (!mesh_primitive_alloc_mesh(mesh, total_rings * verts_per_ring, (total_rings - 1) * slices * 6, false))
        return false;

    const float cylinder_span = SDL_max(primitive->height - primitive->radius * 2.0f, 0.0f);
    const float start_y = -cylinder_span * 0.5f;
    const float end_y = cylinder_span * 0.5f;
    for (int k = 0; k < total_rings; ++k)
    {
        float ring_radius = 0.0f;
        float center_y = 0.0f;
        float normal_y = 0.0f;
        if (k <= rings)
        {
            const float t = (float)k / (float)rings;
            const float theta = SLAYER3D_GAME_PRESENTATION_PI * 0.5f * t;
            ring_radius = primitive->radius * SDL_sinf(theta);
            center_y = end_y + primitive->radius * SDL_cosf(theta);
            normal_y = SDL_cosf(theta);
        }
        else
        {
            const int lower_k = k - (rings + 1);
            const float t = (float)lower_k / (float)rings;
            const float theta = SLAYER3D_GAME_PRESENTATION_PI * 0.5f * t;
            ring_radius = primitive->radius * SDL_cosf(theta);
            center_y = start_y - primitive->radius * SDL_sinf(theta);
            normal_y = -SDL_sinf(theta);
        }
        for (int s = 0; s <= slices; ++s)
        {
            const float phi = SLAYER3D_GAME_PRESENTATION_PI * 2.0f * (float)s / (float)slices;
            const float x = ring_radius * SDL_cosf(phi);
            const float z = ring_radius * SDL_sinf(phi);
            const slayer3d_vec3 normal = slayer3d_vec3_normalize(slayer3d_vec3_make(
                SDL_cosf(phi) * ring_radius, normal_y * primitive->radius, SDL_sinf(phi) * ring_radius));
            mesh_primitive_set_vertex(mesh, k * verts_per_ring + s, slayer3d_vec3_make(x, center_y, z), normal, 0, 0);
        }
    }

    int ii = 0;
    for (int k = 0; k + 1 < total_rings; ++k)
    {
        for (int s = 0; s < slices; ++s)
        {
            const unsigned int a = (unsigned int)(k * verts_per_ring + s);
            const unsigned int b = a + 1U;
            const unsigned int c = (unsigned int)((k + 1) * verts_per_ring + s);
            const unsigned int d = c + 1U;
            mesh->indices[ii++] = a;
            mesh->indices[ii++] = c;
            mesh->indices[ii++] = d;
            mesh->indices[ii++] = a;
            mesh->indices[ii++] = d;
            mesh->indices[ii++] = b;
        }
    }
    return true;
}

static bool build_torus_like_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh,
                                  bool full_torus)
{
    const int segments = primitive->slices;
    const int tube_segments = primitive->tube_segments;
    const int arc_vertices = segments + 1;
    const int tube_vertices = tube_segments + 1;
    if (!mesh_primitive_alloc_mesh(mesh, arc_vertices * tube_vertices, segments * tube_segments * 6, false))
        return false;
    const float arc_angle = full_torus ? SLAYER3D_GAME_PRESENTATION_PI * 2.0f : primitive->arc_angle;
    const float start_angle = full_torus ? 0.0f : -arc_angle * 0.5f;
    for (int a = 0; a <= segments; ++a)
    {
        const float u = start_angle + (float)a / (float)segments * arc_angle;
        const slayer3d_vec3 radial = slayer3d_vec3_make(SDL_cosf(u), 0.0f, SDL_sinf(u));
        const slayer3d_vec3 center = slayer3d_vec3_scale(radial, primitive->major_radius);
        for (int t = 0; t <= tube_segments; ++t)
        {
            const float v = (float)t / (float)tube_segments * SLAYER3D_GAME_PRESENTATION_PI * 2.0f;
            const slayer3d_vec3 normal = slayer3d_vec3_normalize(
                slayer3d_vec3_make(radial.x * SDL_cosf(v), SDL_sinf(v), radial.z * SDL_cosf(v)));
            mesh_primitive_set_vertex(mesh, a * tube_vertices + t,
                                      slayer3d_vec3_add(center, slayer3d_vec3_scale(normal, primitive->minor_radius)),
                                      normal, 0, 0);
        }
    }
    int ii = 0;
    for (int a = 0; a < segments; ++a)
    {
        for (int t = 0; t < tube_segments; ++t)
        {
            const unsigned int p00 = (unsigned int)(a * tube_vertices + t);
            const unsigned int p01 = p00 + 1U;
            const unsigned int p10 = (unsigned int)((a + 1) * tube_vertices + t);
            const unsigned int p11 = p10 + 1U;
            mesh->indices[ii++] = p00;
            mesh->indices[ii++] = p10;
            mesh->indices[ii++] = p01;
            mesh->indices[ii++] = p01;
            mesh->indices[ii++] = p10;
            mesh->indices[ii++] = p11;
        }
    }
    return true;
}

static bool build_pyramid_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    if (!mesh_primitive_alloc_mesh(mesh, 18, 18, false))
        return false;
    const float hx = primitive->size.x * 0.5f;
    const float hy = primitive->size.y * 0.5f;
    const float hz = primitive->size.z * 0.5f;
    const slayer3d_vec3 apex = slayer3d_vec3_make(0.0f, hy, 0.0f);
    const slayer3d_vec3 bl = slayer3d_vec3_make(-hx, -hy, -hz);
    const slayer3d_vec3 br = slayer3d_vec3_make(hx, -hy, -hz);
    const slayer3d_vec3 tr = slayer3d_vec3_make(hx, -hy, hz);
    const slayer3d_vec3 tl = slayer3d_vec3_make(-hx, -hy, hz);
    const slayer3d_vec3 faces[6][3] = {{bl, apex, br}, {br, apex, tr}, {tr, apex, tl},
                                       {tl, apex, bl}, {bl, br, tr},   {bl, tr, tl}};
    for (int f = 0; f < 6; ++f)
    {
        const slayer3d_vec3 normal = mesh_primitive_face_normal(faces[f][0], faces[f][1], faces[f][2]);
        for (int v = 0; v < 3; ++v)
        {
            const int vi = f * 3 + v;
            mesh_primitive_set_vertex(mesh, vi, faces[f][v], normal, 0, 0);
            mesh->indices[vi] = (unsigned int)vi;
        }
    }
    return true;
}

static bool build_wedge_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    if (!mesh_primitive_alloc_mesh(mesh, 24, 24, false))
        return false;
    const float hx = primitive->size.x * 0.5f;
    const float hy = primitive->size.y * 0.5f;
    const float hz = primitive->size.z * 0.5f;
    const slayer3d_vec3 p[6] = {slayer3d_vec3_make(-hx, -hy, -hz), slayer3d_vec3_make(hx, -hy, -hz),
                                slayer3d_vec3_make(-hx, -hy, hz),  slayer3d_vec3_make(hx, -hy, hz),
                                slayer3d_vec3_make(-hx, hy, hz),   slayer3d_vec3_make(hx, hy, hz)};
    const slayer3d_vec3 triangles[8][3] = {
        {p[0], p[3], p[1]}, {p[0], p[2], p[3]}, {p[2], p[5], p[3]}, {p[2], p[4], p[5]},
        {p[0], p[4], p[2]}, {p[0], p[1], p[5]}, {p[0], p[5], p[4]}, {p[1], p[3], p[5]},
    };
    for (int f = 0; f < 8; ++f)
    {
        const slayer3d_vec3 normal = mesh_primitive_face_normal(triangles[f][0], triangles[f][1], triangles[f][2]);
        for (int v = 0; v < 3; ++v)
        {
            const int vi = f * 3 + v;
            mesh_primitive_set_vertex(mesh, vi, triangles[f][v], normal, 0, 0);
            mesh->indices[vi] = (unsigned int)vi;
        }
    }
    return true;
}

static bool build_hemisphere_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int rings = primitive->rings;
    const int slices = primitive->slices;
    const int curved_vertices = (rings + 1) * (slices + 1);
    const int cap_center = curved_vertices;
    if (!mesh_primitive_alloc_mesh(mesh, curved_vertices + 1, rings * slices * 6 + slices * 3, false))
        return false;
    const float y_offset = primitive->radius * 0.5f;
    for (int r = 0; r <= rings; ++r)
    {
        const float theta = (float)r / (float)rings * SLAYER3D_GAME_PRESENTATION_PI * 0.5f;
        const float rr = SDL_sinf(theta) * primitive->radius;
        const float y = SDL_cosf(theta) * primitive->radius - y_offset;
        for (int s = 0; s <= slices; ++s)
        {
            const float phi = (float)s / (float)slices * SLAYER3D_GAME_PRESENTATION_PI * 2.0f;
            const slayer3d_vec3 position = slayer3d_vec3_make(SDL_cosf(phi) * rr, y, SDL_sinf(phi) * rr);
            const slayer3d_vec3 normal =
                slayer3d_vec3_normalize(slayer3d_vec3_make(position.x, y + y_offset, position.z));
            mesh_primitive_set_vertex(mesh, r * (slices + 1) + s, position, normal, 0, 0);
        }
    }
    int ii = 0;
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < slices; ++s)
        {
            const unsigned int a = (unsigned int)(r * (slices + 1) + s);
            const unsigned int b = a + 1U;
            const unsigned int c = (unsigned int)((r + 1) * (slices + 1) + s);
            const unsigned int d = c + 1U;
            mesh->indices[ii++] = a;
            mesh->indices[ii++] = c;
            mesh->indices[ii++] = b;
            mesh->indices[ii++] = b;
            mesh->indices[ii++] = c;
            mesh->indices[ii++] = d;
        }
    }
    mesh_primitive_set_vertex(mesh, cap_center, slayer3d_vec3_make(0.0f, -y_offset, 0.0f),
                              slayer3d_vec3_make(0.0f, -1.0f, 0.0f), 0, 0);
    const int base_row = rings * (slices + 1);
    for (int s = 0; s < slices; ++s)
    {
        mesh->indices[ii++] = (unsigned int)cap_center;
        mesh->indices[ii++] = (unsigned int)(base_row + s + 1);
        mesh->indices[ii++] = (unsigned int)(base_row + s);
    }
    return true;
}

static bool build_rounded_box_mesh(const slayer3d_game_data_render_primitive *primitive, slayer3d_mesh *mesh)
{
    const int grid = SDL_max(primitive->rings + 2, 3);
    const int face_vertices = grid * grid;
    if (!mesh_primitive_alloc_mesh(mesh, face_vertices * 6, (grid - 1) * (grid - 1) * 36, false))
        return false;
    const slayer3d_vec3 half = slayer3d_vec3_scale(primitive->size, 0.5f);
    const float radius = SDL_min(primitive->bevel_radius,
                                 SDL_min(primitive->size.x, SDL_min(primitive->size.y, primitive->size.z)) * 0.5f);
    const slayer3d_vec3 inner = slayer3d_vec3_make(SDL_max(half.x - radius, 0.0f), SDL_max(half.y - radius, 0.0f),
                                                   SDL_max(half.z - radius, 0.0f));
    int vi = 0;
    for (int face = 0; face < 6; ++face)
    {
        const int axis = face / 2;
        const float sign = (face % 2) == 0 ? -1.0f : 1.0f;
        for (int y = 0; y < grid; ++y)
        {
            const float v = -1.0f + 2.0f * (float)y / (float)(grid - 1);
            for (int x = 0; x < grid; ++x)
            {
                const float u = -1.0f + 2.0f * (float)x / (float)(grid - 1);
                slayer3d_vec3 local;
                if (axis == 0)
                    local = slayer3d_vec3_make(sign * half.x, u * half.y, v * half.z);
                else if (axis == 1)
                    local = slayer3d_vec3_make(u * half.x, sign * half.y, v * half.z);
                else
                    local = slayer3d_vec3_make(u * half.x, v * half.y, sign * half.z);
                const slayer3d_vec3 clamped =
                    slayer3d_vec3_make(SDL_clamp(local.x, -inner.x, inner.x), SDL_clamp(local.y, -inner.y, inner.y),
                                       SDL_clamp(local.z, -inner.z, inner.z));
                const slayer3d_vec3 delta = slayer3d_vec3_sub(local, clamped);
                slayer3d_vec3 face_normal = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
                if (axis == 0)
                    face_normal.x = sign;
                else if (axis == 1)
                    face_normal.y = sign;
                else
                    face_normal.z = sign;
                const slayer3d_vec3 normal =
                    slayer3d_vec3_length_squared(delta) > 0.000001f ? slayer3d_vec3_normalize(delta) : face_normal;
                mesh_primitive_set_vertex(mesh, vi++, slayer3d_vec3_add(clamped, slayer3d_vec3_scale(normal, radius)),
                                          normal, 0, 0);
            }
        }
    }
    int ii = 0;
    for (int face = 0; face < 6; ++face)
    {
        const int base = face * face_vertices;
        for (int y = 0; y < grid - 1; ++y)
        {
            for (int x = 0; x < grid - 1; ++x)
            {
                const unsigned int a = (unsigned int)(base + y * grid + x);
                const unsigned int b = a + 1U;
                const unsigned int c = (unsigned int)(base + (y + 1) * grid + x);
                const unsigned int d = c + 1U;
                mesh->indices[ii++] = a;
                mesh->indices[ii++] = c;
                mesh->indices[ii++] = b;
                mesh->indices[ii++] = b;
                mesh->indices[ii++] = c;
                mesh->indices[ii++] = d;
            }
        }
    }
    return true;
}

static bool build_mesh_primitive_cache_entry(slayer3d_game_data_mesh_primitive_cache_entry *entry,
                                             const slayer3d_game_data_render_primitive *primitive)
{
    bool ok = false;
    mesh_primitive_cache_entry_set_key(entry, primitive);
    switch (primitive->mesh_primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
        ok = build_cube_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
        ok = build_sphere_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE:
        ok = build_capsule_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
        ok = build_cylinder_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
        ok = build_torus_like_mesh(primitive, &entry->mesh, true);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        ok = build_torus_like_mesh(primitive, &entry->mesh, false);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
        ok = build_pyramid_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
        ok = build_wedge_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
        ok = build_quad_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        ok = build_disc_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
        ok = build_hemisphere_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        ok = build_rounded_box_mesh(primitive, &entry->mesh);
        break;
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        ok = false;
        break;
    }
    entry->loaded = ok;
    if (!ok)
        mesh_primitive_free_mesh(&entry->mesh);
    return ok;
}

const slayer3d_mesh *slayer3d_game_data_find_or_build_mesh_primitive(
    slayer3d_game_data_mesh_primitive_cache *cache, const slayer3d_game_data_render_primitive *primitive)
{
    if (cache == NULL || primitive == NULL)
        return NULL;
    for (int i = 0; i < cache->count; ++i)
    {
        if (mesh_primitive_cache_entry_matches(&cache->entries[i], primitive))
        {
            ++cache->hits;
            return &cache->entries[i].mesh;
        }
    }
    if (!slayer3d_game_data_ensure_mesh_primitive_cache_capacity(cache, cache->count + 1))
        return NULL;
    slayer3d_game_data_mesh_primitive_cache_entry *entry = &cache->entries[cache->count];
    SDL_zero(*entry);
    if (!build_mesh_primitive_cache_entry(entry, primitive))
        return NULL;
    ++cache->count;
    ++cache->misses;
    return &entry->mesh;
}

static bool draw_mesh_primitive_solid(primitive_draw_context *context,
                                      const slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || primitive == NULL)
        return false;
    const slayer3d_texture2d *texture = slayer3d_game_data_primitive_texture(context, primitive);
    char warmup_key[256];
    slayer3d_game_data_asset_warmup_state warmup_state;
    if (slayer3d_game_data_mesh_primitive_warmup_key(primitive, warmup_key, (int)sizeof(warmup_key)) &&
        slayer3d_game_data_asset_warmup_request_state(
            context->asset_warmup, SLAYER3D_GAME_DATA_ASSET_WARMUP_MESH_PRIMITIVE, NULL, warmup_key, &warmup_state) &&
        (warmup_state == SLAYER3D_GAME_DATA_ASSET_WARMUP_QUEUED ||
         warmup_state == SLAYER3D_GAME_DATA_ASSET_WARMUP_LOADING ||
         warmup_state == SLAYER3D_GAME_DATA_ASSET_WARMUP_READY_FOR_FINALIZE))
    {
        return true;
    }
    const slayer3d_mesh *cached_mesh =
        slayer3d_game_data_find_or_build_mesh_primitive(context->mesh_primitive_cache, primitive);
    if (cached_mesh != NULL)
        return slayer3d_draw_static_mesh(context->renderer, cached_mesh, cached_mesh->uvs != NULL ? texture : NULL,
                                         primitive->color);
    switch (primitive->mesh_primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
        return slayer3d_draw_cube_textured(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                           slayer3d_vec3_make(0.0f, 0.0f, 0.0f), 0.0f, texture, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
        return slayer3d_draw_sphere_textured(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                             primitive->rings, primitive->slices, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                             0.0f, texture, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE: {
        const float cylinder_span = SDL_max(primitive->height - primitive->radius * 2.0f, 0.0f);
        const slayer3d_vec3 start = slayer3d_vec3_make(0.0f, -cylinder_span * 0.5f, 0.0f);
        const slayer3d_vec3 end = slayer3d_vec3_make(0.0f, cylinder_span * 0.5f, 0.0f);
        return slayer3d_draw_capsule(context->renderer, start, end, primitive->radius, primitive->slices,
                                     primitive->rings, primitive->color);
    }
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
        return slayer3d_draw_cylinder(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius_top,
                                      primitive->radius_bottom, primitive->height, primitive->slices, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
        return slayer3d_draw_cylinder(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius_top,
                                      primitive->radius_bottom, primitive->height, primitive->slices, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
        return slayer3d_draw_torus(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->major_radius,
                                   primitive->minor_radius, primitive->slices, primitive->tube_segments,
                                   primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
        return slayer3d_draw_pyramid(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                     primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
        return slayer3d_draw_wedge(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                   primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
        return slayer3d_draw_quad(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                  (slayer3d_vec2){primitive->size.x, primitive->size.y}, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        return slayer3d_draw_disc(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                  primitive->slices, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
        return slayer3d_draw_hemisphere(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                        primitive->rings, primitive->slices, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        return slayer3d_draw_rounded_box(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                         primitive->bevel_radius, primitive->rings, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        return slayer3d_draw_tube_segment(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                          primitive->major_radius, primitive->minor_radius, primitive->arc_angle,
                                          primitive->slices, primitive->tube_segments, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
        return slayer3d_draw_arrow(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                   primitive->height, primitive->slices, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
        return slayer3d_draw_quad(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                  (slayer3d_vec2){primitive->size.x, primitive->size.y}, primitive->color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        return false;
    }
}

static bool draw_mesh_primitive_wires(primitive_draw_context *context,
                                      const slayer3d_game_data_render_primitive *primitive)
{
    if (context == NULL || primitive == NULL)
        return false;
    switch (primitive->mesh_primitive)
    {
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE:
        return slayer3d_draw_cube_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                        primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE:
        return slayer3d_draw_sphere_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                          primitive->rings, primitive->slices, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE: {
        const float cylinder_span = SDL_max(primitive->height - primitive->radius * 2.0f, 0.0f);
        const slayer3d_vec3 start = slayer3d_vec3_make(0.0f, -cylinder_span * 0.5f, 0.0f);
        const slayer3d_vec3 end = slayer3d_vec3_make(0.0f, cylinder_span * 0.5f, 0.0f);
        return slayer3d_draw_capsule_wires(context->renderer, start, end, primitive->radius, primitive->slices,
                                           primitive->rings, primitive->wire_color);
    }
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER:
        return slayer3d_draw_cylinder_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                            primitive->radius_top, primitive->radius_bottom, primitive->height,
                                            primitive->slices, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE:
        return slayer3d_draw_cylinder_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                            primitive->radius_top, primitive->radius_bottom, primitive->height,
                                            primitive->slices, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS:
        return slayer3d_draw_torus_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                         primitive->major_radius, primitive->minor_radius, primitive->slices,
                                         primitive->tube_segments, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID:
        return slayer3d_draw_pyramid_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                           primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE:
        return slayer3d_draw_wedge_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                         primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE:
        return slayer3d_draw_quad_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                        (slayer3d_vec2){primitive->size.x, primitive->size.y}, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC:
        return slayer3d_draw_disc_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                        primitive->slices, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE:
        return slayer3d_draw_hemisphere_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                              primitive->radius, primitive->rings, primitive->slices,
                                              primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX:
        return slayer3d_draw_rounded_box_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->size,
                                               primitive->bevel_radius, primitive->rings, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT:
        return slayer3d_draw_tube_segment_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                                primitive->major_radius, primitive->minor_radius, primitive->arc_angle,
                                                primitive->slices, primitive->tube_segments, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW:
        return slayer3d_draw_arrow_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f), primitive->radius,
                                         primitive->height, primitive->slices, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE:
        return slayer3d_draw_quad_wires(context->renderer, slayer3d_vec3_make(0.0f, 0.0f, 0.0f),
                                        (slayer3d_vec2){primitive->size.x, primitive->size.y}, primitive->wire_color);
    case SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID:
    default:
        return false;
    }
}

bool slayer3d_game_data_draw_mesh_primitive(primitive_draw_context *context,
                                            const slayer3d_game_data_render_primitive *primitive)
{
    if (!slayer3d_push_matrix(context->renderer))
        return false;
    bool ok =
        slayer3d_translate(context->renderer, primitive->position.x, primitive->position.y, primitive->position.z);
    const float axis_length_sq = primitive->rotation_axis.x * primitive->rotation_axis.x +
                                 primitive->rotation_axis.y * primitive->rotation_axis.y +
                                 primitive->rotation_axis.z * primitive->rotation_axis.z;
    if (ok && axis_length_sq > 0.000001f && SDL_fabsf(primitive->rotation_angle) > 0.000001f)
        ok = slayer3d_rotate(context->renderer, primitive->rotation_axis, primitive->rotation_angle);
    if (ok && primitive->draw_mode != SLAYER3D_GAME_DATA_RENDER_DRAW_WIRE)
        ok = draw_mesh_primitive_solid(context, primitive);
    if (ok && primitive->draw_mode != SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID)
        ok = draw_mesh_primitive_wires(context, primitive);
    const bool pop_ok = slayer3d_pop_matrix(context->renderer);
    return ok && pop_ok;
}

void slayer3d_game_data_mesh_primitive_cache_init(slayer3d_game_data_mesh_primitive_cache *cache)
{
    if (cache == NULL)
        return;
    SDL_zero(*cache);
}

void slayer3d_game_data_mesh_primitive_cache_free(slayer3d_game_data_mesh_primitive_cache *cache)
{
    if (cache == NULL)
        return;
    for (int i = 0; i < cache->count; ++i)
        mesh_primitive_free_mesh(&cache->entries[i].mesh);
    SDL_free(cache->entries);
    SDL_zero(*cache);
}
