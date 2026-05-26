#include "gl_renderer_internal.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

static bool mesh_cache_matches(const slayer3d_gl_mesh_cache_entry *entry, bool lit, GLenum primitive_mode,
                               const float *positions, const float *normals, const float *uvs,
                               const float *lightmap_uvs, const float *colors, const unsigned int *indices,
                               const unsigned short *joint_indices, const float *joint_weights, int vertex_count,
                               int index_count, bool has_lightmap_uvs, bool gpu_skinned)
{
    return entry->lit == lit && entry->primitive_mode == primitive_mode && entry->positions == positions &&
           entry->normals == normals && entry->uvs == uvs && entry->lightmap_uvs == lightmap_uvs &&
           entry->colors == colors && entry->indices == indices && entry->joint_indices == joint_indices &&
           entry->joint_weights == joint_weights && entry->vertex_count == vertex_count &&
           entry->index_count == index_count && entry->has_lightmap_uvs == has_lightmap_uvs &&
           entry->gpu_skinned == gpu_skinned;
}

static void mesh_cache_bind_float_attrib(slayer3d_gl_funcs *gl, GLuint *buffer, GLuint attrib, GLint components,
                                         const float *data, size_t count)
{
    gl->GenBuffers(1, buffer);
    gl->BindBuffer(GL_ARRAY_BUFFER, *buffer);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(float)), data, GL_STATIC_DRAW);
    gl->EnableVertexAttribArray(attrib);
    gl->VertexAttribPointer(attrib, components, GL_FLOAT, GL_FALSE, 0, NULL);
}

static void mesh_cache_bind_ushort_attrib(slayer3d_gl_funcs *gl, GLuint *buffer, GLuint attrib, GLint components,
                                          const unsigned short *data, size_t count)
{
    gl->GenBuffers(1, buffer);
    gl->BindBuffer(GL_ARRAY_BUFFER, *buffer);
    gl->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(unsigned short)), data, GL_STATIC_DRAW);
    gl->EnableVertexAttribArray(attrib);
    gl->VertexAttribPointer(attrib, components, GL_UNSIGNED_SHORT, GL_FALSE, 0, NULL);
}

slayer3d_gl_mesh_cache_entry *slayer3d_gl_mesh_cache_lookup_or_create(
    slayer3d_gl_context *ctx, bool lit, GLenum primitive_mode, const float *positions, const float *normals,
    const float *uvs, const float *lightmap_uvs, const float *colors, const unsigned int *indices,
    const unsigned short *joint_indices, const float *joint_weights, int vertex_count, int index_count,
    bool has_lightmap_uvs, bool gpu_skinned)
{
    for (slayer3d_gl_mesh_cache_entry *entry = ctx->mesh_cache; entry != NULL; entry = entry->next)
    {
        if (mesh_cache_matches(entry, lit, primitive_mode, positions, normals, uvs, lightmap_uvs, colors, indices,
                               joint_indices, joint_weights, vertex_count, index_count, has_lightmap_uvs, gpu_skinned))
        {
            return entry;
        }
    }

    slayer3d_gl_mesh_cache_entry *entry = (slayer3d_gl_mesh_cache_entry *)SDL_calloc(1, sizeof(*entry));
    if (entry == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

    slayer3d_gl_funcs *gl = &ctx->gl;
    entry->lit = lit;
    entry->primitive_mode = primitive_mode;
    entry->positions = positions;
    entry->normals = normals;
    entry->uvs = uvs;
    entry->lightmap_uvs = lightmap_uvs;
    entry->colors = colors;
    entry->indices = indices;
    entry->joint_indices = joint_indices;
    entry->joint_weights = joint_weights;
    entry->vertex_count = vertex_count;
    entry->index_count = index_count;
    entry->has_lightmap_uvs = has_lightmap_uvs;
    entry->gpu_skinned = gpu_skinned;

    gl->GenVertexArrays(1, &entry->vao);
    gl->BindVertexArray(entry->vao);
    mesh_cache_bind_float_attrib(gl, &entry->position_vbo, 0, 3, positions, (size_t)vertex_count * 3);
    if (lit)
    {
        mesh_cache_bind_float_attrib(gl, &entry->normal_vbo, 1, 3, normals, (size_t)vertex_count * 3);
        mesh_cache_bind_float_attrib(gl, &entry->uv_vbo, 2, 2, uvs, (size_t)vertex_count * 2);
        mesh_cache_bind_float_attrib(gl, &entry->lightmap_uv_vbo, 4, 2, has_lightmap_uvs ? lightmap_uvs : uvs,
                                     (size_t)vertex_count * 2);
        mesh_cache_bind_float_attrib(gl, &entry->color_vbo, 3, 4, colors, (size_t)vertex_count * 4);
        if (gpu_skinned)
        {
            mesh_cache_bind_ushort_attrib(gl, &entry->joint_index_vbo, 12, 4, joint_indices, (size_t)vertex_count * 4);
            mesh_cache_bind_float_attrib(gl, &entry->joint_weight_vbo, 13, 4, joint_weights, (size_t)vertex_count * 4);
        }
    }
    else
    {
        mesh_cache_bind_float_attrib(gl, &entry->uv_vbo, 1, 2, uvs, (size_t)vertex_count * 2);
        mesh_cache_bind_float_attrib(gl, &entry->color_vbo, 2, 4, colors, (size_t)vertex_count * 4);
    }
    if (indices != NULL && index_count > 0)
    {
        gl->GenBuffers(1, &entry->ebo);
        gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry->ebo);
        gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)index_count * sizeof(unsigned int)), indices,
                       GL_STATIC_DRAW);
    }

    gl->GenVertexArrays(1, &entry->shadow_vao);
    gl->BindVertexArray(entry->shadow_vao);
    mesh_cache_bind_float_attrib(gl, &entry->shadow_position_vbo, 0, 3, positions, (size_t)vertex_count * 3);
    if (gpu_skinned)
    {
        mesh_cache_bind_ushort_attrib(gl, &entry->shadow_joint_index_vbo, 12, 4, joint_indices,
                                      (size_t)vertex_count * 4);
        mesh_cache_bind_float_attrib(gl, &entry->shadow_joint_weight_vbo, 13, 4, joint_weights,
                                     (size_t)vertex_count * 4);
    }
    if (indices != NULL && index_count > 0)
    {
        gl->GenBuffers(1, &entry->shadow_ebo);
        gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry->shadow_ebo);
        gl->BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)index_count * sizeof(unsigned int)), indices,
                       GL_STATIC_DRAW);
    }
    gl->BindVertexArray(0);

    entry->next = ctx->mesh_cache;
    ctx->mesh_cache = entry;
    return entry;
}

void slayer3d_gl_mesh_cache_free(slayer3d_gl_context *ctx)
{
    slayer3d_gl_funcs *gl = &ctx->gl;
    slayer3d_gl_mesh_cache_entry *entry = ctx->mesh_cache;
    while (entry != NULL)
    {
        slayer3d_gl_mesh_cache_entry *next = entry->next;
        GLuint buffers[] = {entry->position_vbo,
                            entry->normal_vbo,
                            entry->uv_vbo,
                            entry->lightmap_uv_vbo,
                            entry->color_vbo,
                            entry->joint_index_vbo,
                            entry->joint_weight_vbo,
                            entry->ebo,
                            entry->shadow_position_vbo,
                            entry->shadow_joint_index_vbo,
                            entry->shadow_joint_weight_vbo,
                            entry->shadow_ebo};
        gl->DeleteBuffers(12, buffers);
        if (entry->vao)
            gl->DeleteVertexArrays(1, &entry->vao);
        if (entry->shadow_vao)
            gl->DeleteVertexArrays(1, &entry->shadow_vao);
        SDL_free(entry);
        entry = next;
    }
    ctx->mesh_cache = NULL;
}
