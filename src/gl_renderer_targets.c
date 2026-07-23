/**
 * @file gl_renderer_targets.c
 * @brief OpenGL world render target lifecycle helpers.
 */

#include "gl_renderer_internal.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

/* ------------------------------------------------------------------ */
/* FBO helpers                                                         */
/* ------------------------------------------------------------------ */

static bool create_fbo(slayer3d_gl_context *ctx, int w, int h)
{
    slayer3d_gl_funcs *gl = &ctx->gl;

    gl->GenTextures(1, &ctx->fbo_color);
    gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl->GenTextures(1, &ctx->fbo_depth);
    gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_depth);
    /* ES 3.0 pairs DEPTH_COMPONENT24 with UNSIGNED_INT, never FLOAT. */
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    gl->GenFramebuffers(1, &ctx->fbo);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->fbo_color, 0);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->fbo_depth, 0);

    GLenum status = gl->CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL FBO incomplete: 0x%x", status);
        return false;
    }

    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    ctx->world_w = w;
    ctx->world_h = h;
    return true;
}

static bool create_post_process_fbos(slayer3d_gl_context *ctx, int w, int h)
{
    slayer3d_gl_funcs *gl = &ctx->gl;
    GLuint *fbos[2] = {&ctx->pp_fbo_a, &ctx->pp_fbo_b};
    GLuint *texs[2] = {&ctx->pp_tex_a, &ctx->pp_tex_b};

    for (int i = 0; i < 2; i++)
    {
        gl->GenTextures(1, texs[i]);
        gl->BindTexture(GL_TEXTURE_2D, *texs[i]);
        gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        gl->GenFramebuffers(1, fbos[i]);
        gl->BindFramebuffer(GL_FRAMEBUFFER, *fbos[i]);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texs[i], 0);
        GLenum status = gl->CheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL: post-process FBO %d incomplete: 0x%x", i, status);
            gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void slayer3d_gl_destroy_world_targets(slayer3d_gl_context *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    slayer3d_gl_funcs *gl = &ctx->gl;
    if (ctx->pp_fbo_a)
        gl->DeleteFramebuffers(1, &ctx->pp_fbo_a);
    if (ctx->pp_fbo_b)
        gl->DeleteFramebuffers(1, &ctx->pp_fbo_b);
    if (ctx->pp_tex_a)
        gl->DeleteTextures(1, &ctx->pp_tex_a);
    if (ctx->pp_tex_b)
        gl->DeleteTextures(1, &ctx->pp_tex_b);
    if (ctx->fbo)
        gl->DeleteFramebuffers(1, &ctx->fbo);
    if (ctx->fbo_color)
        gl->DeleteTextures(1, &ctx->fbo_color);
    if (ctx->fbo_depth)
        gl->DeleteTextures(1, &ctx->fbo_depth);

    ctx->pp_fbo_a = 0;
    ctx->pp_fbo_b = 0;
    ctx->pp_tex_a = 0;
    ctx->pp_tex_b = 0;
    ctx->fbo = 0;
    ctx->fbo_color = 0;
    ctx->fbo_depth = 0;
    ctx->world_w = 0;
    ctx->world_h = 0;
}

bool slayer3d_gl_create_world_targets(slayer3d_gl_context *ctx, int w, int h)
{
    return create_fbo(ctx, w, h) && create_post_process_fbos(ctx, w, h);
}

int slayer3d_gl_scaled_world_dimension(int logical_dimension, float scale)
{
    return SDL_max(1, (int)SDL_floorf((float)logical_dimension * scale + 0.5f));
}

static bool gl_presentation_viewport_size(slayer3d_gl_context *ctx, int logical_width, int logical_height,
                                          int *out_width, int *out_height)
{
    int output_width = 0;
    int output_height = 0;
    if (!SDL_GetWindowSizeInPixels(ctx->window, &output_width, &output_height))
    {
        return false;
    }
    if (output_width <= 0 || output_height <= 0)
    {
        return SDL_SetError("The GL window does not currently expose a valid pixel size.");
    }

    SDL_FRect viewport;
    if (!slayer3d_resolve_aspect_fit_viewport(logical_width, logical_height, output_width, output_height, &viewport))
        return false;
    *out_width = SDL_max(1, (int)SDL_floorf(viewport.w + 0.5f));
    *out_height = SDL_max(1, (int)SDL_floorf(viewport.h + 0.5f));
    return true;
}

bool slayer3d_gl_sync_world_render_target(slayer3d_gl_context *ctx, int logical_width, int logical_height, float scale,
                                          int *out_width, int *out_height)
{
    if (ctx == NULL)
    {
        return SDL_InvalidParamError("ctx");
    }
    if (logical_width <= 0 || logical_height <= 0)
    {
        return SDL_InvalidParamError("logical dimensions");
    }
    if (!(scale >= 0.25f && scale <= 1.0f))
    {
        return SDL_SetError("World render scale must be between 0.25 and 1.0.");
    }

    int presentation_width = logical_width;
    int presentation_height = logical_height;
    if (!gl_presentation_viewport_size(ctx, logical_width, logical_height, &presentation_width, &presentation_height))
    {
        return false;
    }

    const int new_width = slayer3d_gl_scaled_world_dimension(presentation_width, scale);
    const int new_height = slayer3d_gl_scaled_world_dimension(presentation_height, scale);
    if (ctx->logical_w == logical_width && ctx->logical_h == logical_height && ctx->world_w == new_width &&
        ctx->world_h == new_height)
    {
        ctx->world_render_scale = scale;
        if (out_width != NULL)
            *out_width = ctx->world_w;
        if (out_height != NULL)
            *out_height = ctx->world_h;
        return true;
    }

    const int old_width = ctx->world_w > 0 ? ctx->world_w : presentation_width;
    const int old_height = ctx->world_h > 0 ? ctx->world_h : presentation_height;
    const float old_scale = ctx->world_render_scale > 0.0f ? ctx->world_render_scale : 1.0f;

    SDL_GL_MakeCurrent(ctx->window, ctx->gl_context);
    slayer3d_gl_destroy_world_targets(ctx);
    if (!slayer3d_gl_create_world_targets(ctx, new_width, new_height))
    {
        slayer3d_gl_destroy_world_targets(ctx);
        (void)slayer3d_gl_create_world_targets(ctx, old_width, old_height);
        ctx->world_render_scale = old_scale;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL: failed to resize world render target to %dx%d",
                     new_width, new_height);
        return false;
    }

    ctx->logical_w = logical_width;
    ctx->logical_h = logical_height;
    ctx->world_render_scale = scale;
    if (out_width != NULL)
        *out_width = ctx->world_w;
    if (out_height != NULL)
        *out_height = ctx->world_h;
    return true;
}

bool slayer3d_gl_set_world_render_scale(slayer3d_gl_context *ctx, int logical_width, int logical_height, float scale,
                                        int *out_width, int *out_height)
{
    if (ctx == NULL)
    {
        return SDL_InvalidParamError("ctx");
    }
    if (logical_width <= 0 || logical_height <= 0)
    {
        return SDL_InvalidParamError("logical dimensions");
    }
    if (!(scale >= 0.25f && scale <= 1.0f))
    {
        return SDL_SetError("World render scale must be between 0.25 and 1.0.");
    }

    return slayer3d_gl_sync_world_render_target(ctx, logical_width, logical_height, scale, out_width, out_height);
}

float slayer3d_gl_world_render_scale(const slayer3d_gl_context *ctx)
{
    return ctx != NULL ? ctx->world_render_scale : 1.0f;
}

void slayer3d_gl_world_render_size(const slayer3d_gl_context *ctx, int *out_width, int *out_height)
{
    if (out_width != NULL)
        *out_width = ctx != NULL ? ctx->world_w : 0;
    if (out_height != NULL)
        *out_height = ctx != NULL ? ctx->world_h : 0;
}
