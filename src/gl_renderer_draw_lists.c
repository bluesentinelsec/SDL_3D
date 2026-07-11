/*
 * Deferred renderer draw-list and overlay submission helpers.
 */

#include "gl_renderer_internal.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

void slayer3d_gl_free_draw_list(slayer3d_gl_context *ctx)
{
    for (int i = 0; i < ctx->draw_count; i++)
    {
        slayer3d_draw_entry *e = &ctx->draw_list[i];
        if (e->owns_arrays)
        {
            SDL_free((void *)e->positions);
            SDL_free((void *)e->normals);
            SDL_free((void *)e->uvs);
            SDL_free((void *)e->lightmap_uvs);
            SDL_free((void *)e->colors);
            SDL_free((void *)e->indices);
        }
        SDL_free(e->joint_matrices);
    }
    ctx->draw_count = 0;
}

slayer3d_draw_entry *slayer3d_gl_append_draw_entry(slayer3d_gl_context *ctx)
{
    if (ctx->draw_count == ctx->draw_capacity)
    {
        const int cap = ctx->draw_capacity != 0 ? ctx->draw_capacity * 2 : 64;
        slayer3d_draw_entry *buf = SDL_realloc(ctx->draw_list, (size_t)cap * sizeof(slayer3d_draw_entry));
        if (buf == NULL)
            return NULL;
        ctx->draw_list = buf;
        ctx->draw_capacity = cap;
    }

    slayer3d_draw_entry *e = &ctx->draw_list[ctx->draw_count++];
    SDL_memset(e, 0, sizeof(*e));
    return e;
}

void slayer3d_gl_free_overlay_list(slayer3d_gl_context *ctx)
{
    for (int i = 0; i < ctx->overlay_count; i++)
    {
        slayer3d_overlay_entry *e = &ctx->overlay_list[i];
        SDL_free(e->positions);
        SDL_free(e->uvs);
    }
    ctx->overlay_count = 0;

    /* Retired atlases (see find_or_create_overlay_atlas) stay alive until
     * the entries recorded against them have replayed; reclaim them now. */
    slayer3d_gl_funcs *gl = &ctx->gl;
    int kept = 0;
    for (int i = 0; i < ctx->overlay_atlas_count; i++)
    {
        slayer3d_overlay_atlas *a = &ctx->overlay_atlases[i];
        if (a->source == NULL)
        {
            gl->DeleteTextures(1, &a->gl_tex);
            continue;
        }
        ctx->overlay_atlases[kept++] = *a;
    }
    ctx->overlay_atlas_count = kept;
}

static slayer3d_overlay_entry *append_overlay_entry(slayer3d_gl_context *ctx)
{
    if (ctx->overlay_count == ctx->overlay_capacity)
    {
        const int cap = ctx->overlay_capacity != 0 ? ctx->overlay_capacity * 2 : 64;
        slayer3d_overlay_entry *buf = SDL_realloc(ctx->overlay_list, (size_t)cap * sizeof(slayer3d_overlay_entry));
        if (buf == NULL)
            return NULL;
        ctx->overlay_list = buf;
        ctx->overlay_capacity = cap;
    }

    slayer3d_overlay_entry *e = &ctx->overlay_list[ctx->overlay_count++];
    SDL_memset(e, 0, sizeof(*e));
    return e;
}

static bool ensure_overlay_atlas_capacity(slayer3d_gl_context *ctx)
{
    if (ctx->overlay_atlas_count < ctx->overlay_atlas_capacity)
        return true;

    const int cap = ctx->overlay_atlas_capacity != 0 ? ctx->overlay_atlas_capacity * 2 : 16;
    slayer3d_overlay_atlas *buf = SDL_realloc(ctx->overlay_atlases, (size_t)cap * sizeof(slayer3d_overlay_atlas));
    if (buf == NULL)
        return SDL_OutOfMemory();

    ctx->overlay_atlases = buf;
    ctx->overlay_atlas_capacity = cap;
    return true;
}

static int find_or_create_overlay_atlas(slayer3d_gl_context *ctx, const slayer3d_texture2d *texture)
{
    if (texture == NULL)
        return -1;

    slayer3d_gl_funcs *gl = &ctx->gl;
    for (int i = 0; i < ctx->overlay_atlas_count; i++)
    {
        slayer3d_overlay_atlas *a = &ctx->overlay_atlases[i];
        if (a->source != texture)
            continue;
        if (a->generation == texture->generation)
            return i;
        /* Same address, different generation: either an in-place texture
         * update or an unrelated texture reusing a freed address (caches
         * that store slayer3d_font/texture structs by value relocate on
         * growth). Never re-upload the existing GL texture — entries
         * recorded earlier this frame may still replay from it. Retire it
         * and fall through to a fresh atlas; the retired one is reclaimed
         * after replay in slayer3d_gl_free_overlay_list. */
        a->source = NULL;
        break;
    }

    if (!ensure_overlay_atlas_capacity(ctx))
        return -1;

    const int atlas_idx = ctx->overlay_atlas_count++;
    slayer3d_overlay_atlas *a = &ctx->overlay_atlases[atlas_idx];
    a->source = texture;
    a->generation = texture->generation;
    gl->GenTextures(1, &a->gl_tex);
    gl->BindTexture(GL_TEXTURE_2D, a->gl_tex);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   texture->pixels);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return atlas_idx;
}

bool slayer3d_gl_append_overlay(slayer3d_gl_context *ctx, const float *positions, const float *uvs, int vertex_count,
                                const float *mvp, const float *tint, const slayer3d_texture2d *texture,
                                bool scissor_enabled, const SDL_Rect *scissor_rect, slayer3d_overlay_effect effect,
                                float effect_progress, Uint32 effect_seed, const char *shader_vertex_source,
                                const char *shader_fragment_source)
{
    const int atlas_idx = find_or_create_overlay_atlas(ctx, texture);
    if (texture != NULL && atlas_idx < 0)
        return false;

    slayer3d_overlay_entry *e = append_overlay_entry(ctx);
    if (e == NULL)
        return SDL_OutOfMemory();

    const size_t pos_bytes = (size_t)vertex_count * 3 * sizeof(float);
    const size_t uv_bytes = (size_t)vertex_count * 2 * sizeof(float);
    e->positions = SDL_malloc(pos_bytes);
    e->uvs = SDL_malloc(uv_bytes);
    if (e->positions == NULL || e->uvs == NULL)
    {
        SDL_free(e->positions);
        SDL_free(e->uvs);
        ctx->overlay_count--;
        return SDL_OutOfMemory();
    }

    SDL_memcpy(e->positions, positions, pos_bytes);
    SDL_memcpy(e->uvs, uvs, uv_bytes);
    SDL_memcpy(e->mvp, mvp, 16 * sizeof(float));
    SDL_memcpy(e->tint, tint, 4 * sizeof(float));
    e->vertex_count = vertex_count;
    e->scissor_enabled = scissor_enabled;
    e->scissor_rect = scissor_rect != NULL ? *scissor_rect : (SDL_Rect){0, 0, 0, 0};
    e->atlas_index = atlas_idx;
    e->effect = effect;
    e->effect_progress = effect_progress;
    e->effect_seed = (float)effect_seed * (1.0f / 4294967295.0f);
    e->effect_columns = texture != NULL ? (float)SDL_max(24, texture->width / 16) : 1.0f;
    e->shader_vertex_source = shader_vertex_source;
    e->shader_fragment_source = shader_fragment_source;
    return true;
}
