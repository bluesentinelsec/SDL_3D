/*
 * OpenGL texture cache for renderer-owned texture uploads.
 */

#include "gl_renderer_internal.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

static GLuint tex_cache_lookup(slayer3d_gl_context *ctx, slayer3d_gl_tex_entry **cache, const slayer3d_texture2d *tex)
{
    slayer3d_gl_tex_entry **prev = cache;
    for (slayer3d_gl_tex_entry *e = *cache; e != NULL; prev = &e->next, e = e->next)
    {
        if (e->key == tex)
        {
            if (e->generation != tex->generation)
            {
                ctx->gl.DeleteTextures(1, &e->gl_tex);
                *prev = e->next;
                SDL_free(e);
                return 0;
            }
            return e->gl_tex;
        }
    }
    return 0;
}

static GLuint tex_cache_upload(slayer3d_gl_context *ctx, slayer3d_gl_tex_entry **cache, const slayer3d_texture2d *tex,
                               GLint wrap_s, GLint wrap_t)
{
    slayer3d_gl_funcs *gl = &ctx->gl;
    GLuint id;
    gl->GenTextures(1, &id);
    gl->BindTexture(GL_TEXTURE_2D, id);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->width, tex->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex->pixels);
    gl->GenerateMipmap(GL_TEXTURE_2D);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);

    float aniso = 16.0f;
    gl->TexParameterfv(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY_EXT */, &aniso);

    slayer3d_gl_tex_entry *entry = SDL_calloc(1, sizeof(*entry));
    if (entry == NULL)
    {
        SDL_OutOfMemory();
        gl->DeleteTextures(1, &id);
        return 0;
    }

    entry->key = tex;
    entry->generation = tex->generation;
    entry->gl_tex = id;
    entry->next = *cache;
    *cache = entry;
    return id;
}

GLuint slayer3d_gl_resolve_texture(slayer3d_gl_context *ctx, const slayer3d_texture2d *tex)
{
    if (tex == NULL)
        return ctx->white_texture;

    const GLuint id = tex_cache_lookup(ctx, &ctx->tex_cache, tex);
    return id != 0 ? id : tex_cache_upload(ctx, &ctx->tex_cache, tex, GL_REPEAT, GL_REPEAT);
}

GLuint slayer3d_gl_resolve_skybox_texture(slayer3d_gl_context *ctx, const slayer3d_texture2d *tex)
{
    if (tex == NULL)
        return ctx->white_texture;

    const GLuint id = tex_cache_lookup(ctx, &ctx->skybox_tex_cache, tex);
    return id != 0 ? id : tex_cache_upload(ctx, &ctx->skybox_tex_cache, tex, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
}

static void tex_cache_free_list(slayer3d_gl_context *ctx, slayer3d_gl_tex_entry **cache)
{
    slayer3d_gl_funcs *gl = &ctx->gl;
    slayer3d_gl_tex_entry *e = *cache;
    while (e != NULL)
    {
        slayer3d_gl_tex_entry *next = e->next;
        gl->DeleteTextures(1, &e->gl_tex);
        SDL_free(e);
        e = next;
    }
    *cache = NULL;
}

void slayer3d_gl_tex_cache_free(slayer3d_gl_context *ctx)
{
    tex_cache_free_list(ctx, &ctx->tex_cache);
    tex_cache_free_list(ctx, &ctx->skybox_tex_cache);
    ctx->tex_cache = NULL;
}
