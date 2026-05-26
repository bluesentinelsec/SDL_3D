/**
 * @file gl_renderer_postprocess.c
 * @brief OpenGL post-process, transition, and presentation profile helpers.
 */

#include "gl_renderer_internal.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/transition.h"

bool slayer3d_gl_queue_transition(slayer3d_gl_context *ctx, const slayer3d_transition *transition)
{
    if (ctx == NULL)
    {
        return SDL_InvalidParamError("ctx");
    }
    if (transition == NULL)
    {
        return SDL_InvalidParamError("transition");
    }
    if (!transition->active)
    {
        return true;
    }

    ctx->pending_transition = *transition;
    ctx->transition_pending = true;
    return true;
}

void slayer3d_gl_apply_transition_pass(slayer3d_gl_context *ctx)
{
    if (ctx == NULL || !ctx->transition_pending || ctx->transition_program == 0)
    {
        return;
    }

    slayer3d_gl_funcs *gl = &ctx->gl;
    const slayer3d_transition *transition = &ctx->pending_transition;

    if (transition->type == SLAYER3D_TRANSITION_MELT && ctx->transition_melt_offsets_tex != 0)
    {
        gl->ActiveTexture(GL_TEXTURE0 + 1);
        gl->BindTexture(GL_TEXTURE_2D, ctx->transition_melt_offsets_tex);
        gl->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SLAYER3D_TRANSITION_MELT_COLUMNS, 1, GL_RED, GL_FLOAT,
                          transition->melt_offsets);
    }

    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->pp_fbo_a);
    gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
    gl->Clear(GL_COLOR_BUFFER_BIT);
    gl->UseProgram(ctx->transition_program);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, ctx->fbo_color);
    gl->Uniform1i(ctx->transition_scene_loc, 0);
    gl->ActiveTexture(GL_TEXTURE0 + 1);
    gl->BindTexture(GL_TEXTURE_2D, ctx->transition_melt_offsets_tex);
    gl->Uniform1i(ctx->transition_melt_offsets_loc, 1);
    gl->Uniform1i(ctx->transition_type_loc, (int)transition->type);
    gl->Uniform1i(ctx->transition_direction_loc, (int)transition->direction);
    gl->Uniform1f(ctx->transition_progress_loc, slayer3d_transition_progress(transition));
    gl->Uniform4f(ctx->transition_color_loc, (float)transition->color.r / 255.0f, (float)transition->color.g / 255.0f,
                  (float)transition->color.b / 255.0f, (float)transition->color.a / 255.0f);
    gl->Uniform2f(ctx->transition_resolution_loc, (float)ctx->world_w, (float)ctx->world_h);
    gl->BindVertexArray(ctx->fullscreen_vao);
    gl->DrawArrays(GL_TRIANGLES, 0, 3);

    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    gl->Viewport(0, 0, ctx->world_w, ctx->world_h);
    gl->UseProgram(ctx->copy_program);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, ctx->pp_tex_a);
    gl->Uniform1i(ctx->copy_texture_loc, 0);
    gl->DrawArrays(GL_TRIANGLES, 0, 3);
    gl->ActiveTexture(GL_TEXTURE0);

    ctx->transition_pending = false;
}

void slayer3d_gl_post_process(slayer3d_gl_context *ctx, int effects, float bloom_threshold, float bloom_intensity,
                              float vignette_intensity, float contrast, float brightness, float saturation)
{
    (void)ctx;
    (void)effects;
    (void)bloom_threshold;
    (void)bloom_intensity;
    (void)vignette_intensity;
    (void)contrast;
    (void)brightness;
    (void)saturation;
}

int slayer3d_gl_active_retro_profile(const slayer3d_gl_context *ctx)
{
    return ctx != NULL ? ctx->active_retro_profile : (int)SLAYER3D_DISPLAY_PROFILE_MODERN;
}

void slayer3d_gl_active_retro_virtual_resolution(const slayer3d_gl_context *ctx, int *out_width, int *out_height)
{
    if (out_width != NULL)
        *out_width = ctx != NULL ? ctx->active_retro_virtual_w : 0;
    if (out_height != NULL)
        *out_height = ctx != NULL ? ctx->active_retro_virtual_h : 0;
}

int slayer3d_gl_active_retro_filter(const slayer3d_gl_context *ctx)
{
    return ctx != NULL ? ctx->active_retro_filter : (int)SLAYER3D_DISPLAY_FILTER_LINEAR;
}
