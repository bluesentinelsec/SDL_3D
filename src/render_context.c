#include "sdl3d/render_context.h"

#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "rasterizer.h"
#include "render_context_internal.h"

static const char *const SDL3D_BACKEND_ENV = "SDL3D_BACKEND";
static const float SDL3D_DEFAULT_NEAR_PLANE = 0.01f;
static const float SDL3D_DEFAULT_FAR_PLANE = 1000.0f;

static void sdl3d_try_create_parallel_rasterizer(sdl3d_render_context *context)
{
    const int logical_cores = SDL_GetNumLogicalCPUCores();

    if (context == NULL || logical_cores <= 1)
    {
        return;
    }

    sdl3d_parallel_rasterizer *parallel_rasterizer = NULL;
    if (sdl3d_parallel_rasterizer_create(logical_cores - 1, &parallel_rasterizer))
    {
        context->parallel_rasterizer = parallel_rasterizer;
        return;
    }

    SDL_ClearError();
}

static bool sdl3d_parse_backend_name(const char *name, sdl3d_backend *backend)
{
    if (name == NULL || backend == NULL)
    {
        return SDL_InvalidParamError(name == NULL ? "name" : "backend");
    }

    if (SDL_strcasecmp(name, "auto") == 0)
    {
        *backend = SDL3D_BACKEND_AUTO;
        return true;
    }

    if (SDL_strcasecmp(name, "software") == 0)
    {
        *backend = SDL3D_BACKEND_SOFTWARE;
        return true;
    }

    if (SDL_strcasecmp(name, "sdlgpu") == 0 || SDL_strcasecmp(name, "gpu") == 0)
    {
        *backend = SDL3D_BACKEND_SDLGPU;
        return true;
    }

    return SDL_SetError("Unsupported SDL3D backend override: %s", name);
}

static bool sdl3d_resolve_backend(sdl3d_backend requested_backend, bool allow_backend_fallback,
                                  sdl3d_backend *resolved_backend)
{
    if (resolved_backend == NULL)
    {
        return SDL_InvalidParamError("resolved_backend");
    }

    switch (requested_backend)
    {
    case SDL3D_BACKEND_AUTO:
    case SDL3D_BACKEND_SOFTWARE:
        *resolved_backend = SDL3D_BACKEND_SOFTWARE;
        return true;
    case SDL3D_BACKEND_SDLGPU:
        if (allow_backend_fallback)
        {
            *resolved_backend = SDL3D_BACKEND_SOFTWARE;
            return true;
        }

        return SDL_SetError("The SDL GPU backend is not implemented yet.");
    default:
        return SDL_SetError("Unknown SDL3D backend value: %d", (int)requested_backend);
    }
}

void sdl3d_init_render_context_config(sdl3d_render_context_config *config)
{
    if (config == NULL)
    {
        SDL_InvalidParamError("config");
        return;
    }

    config->backend = SDL3D_BACKEND_AUTO;
    config->allow_backend_fallback = true;
    config->logical_width = 0;
    config->logical_height = 0;
    config->logical_presentation = SDL_LOGICAL_PRESENTATION_STRETCH;
}

const char *sdl3d_get_backend_name(sdl3d_backend backend)
{
    switch (backend)
    {
    case SDL3D_BACKEND_AUTO:
        return "auto";
    case SDL3D_BACKEND_SOFTWARE:
        return "software";
    case SDL3D_BACKEND_SDLGPU:
        return "sdlgpu";
    default:
        return "unknown";
    }
}

bool sdl3d_get_backend_override_from_environment(sdl3d_backend *backend)
{
    const char *value = SDL_getenv(SDL3D_BACKEND_ENV);

    if (value == NULL || *value == '\0')
    {
        return false;
    }

    return sdl3d_parse_backend_name(value, backend);
}

bool sdl3d_create_render_context(SDL_Window *window, SDL_Renderer *renderer, const sdl3d_render_context_config *config,
                                 sdl3d_render_context **out_context)
{
    sdl3d_render_context_config local_config;
    sdl3d_backend requested_backend;
    sdl3d_backend resolved_backend;
    sdl3d_render_context *context;
    const char *env_backend_name;
    int render_width;
    int render_height;
    size_t color_buffer_size;
    size_t depth_buffer_size;

    if (window == NULL)
    {
        return SDL_InvalidParamError("window");
    }

    if (renderer == NULL)
    {
        return SDL_InvalidParamError("renderer");
    }

    if (out_context == NULL)
    {
        return SDL_InvalidParamError("out_context");
    }

    local_config = (config != NULL) ? *config : (sdl3d_render_context_config){0};
    if (config == NULL)
    {
        sdl3d_init_render_context_config(&local_config);
    }

    if ((local_config.logical_width < 0) || (local_config.logical_height < 0))
    {
        return SDL_SetError("Logical dimensions must be zero or positive.");
    }

    if ((local_config.logical_width == 0) != (local_config.logical_height == 0))
    {
        return SDL_SetError("Logical width and height must both be zero or both be non-zero.");
    }

    requested_backend = local_config.backend;
    env_backend_name = SDL_getenv(SDL3D_BACKEND_ENV);
    if (env_backend_name != NULL && *env_backend_name != '\0')
    {
        if (!sdl3d_parse_backend_name(env_backend_name, &requested_backend))
        {
            return false;
        }
    }

    if (!sdl3d_resolve_backend(requested_backend, local_config.allow_backend_fallback, &resolved_backend))
    {
        return false;
    }

    if (local_config.logical_width > 0)
    {
        if (!SDL_SetRenderLogicalPresentation(renderer, local_config.logical_width, local_config.logical_height,
                                              local_config.logical_presentation))
        {
            return false;
        }

        render_width = local_config.logical_width;
        render_height = local_config.logical_height;
    }
    else
    {
        if (!SDL_GetCurrentRenderOutputSize(renderer, &render_width, &render_height))
        {
            return false;
        }

        if (render_width <= 0 || render_height <= 0)
        {
            return SDL_SetError("The SDL renderer does not currently expose a valid output size.");
        }
    }

    context = SDL_calloc(1, sizeof(*context));
    if (context == NULL)
    {
        return SDL_OutOfMemory();
    }

    context->color_texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, render_width, render_height);
    if (context->color_texture == NULL)
    {
        SDL_free(context);
        return false;
    }

    color_buffer_size = (size_t)render_width * (size_t)render_height * 4U;
    context->color_buffer = SDL_calloc(1, color_buffer_size);
    if (context->color_buffer == NULL)
    {
        SDL_DestroyTexture(context->color_texture);
        SDL_free(context);
        return SDL_OutOfMemory();
    }

    depth_buffer_size = (size_t)render_width * (size_t)render_height * sizeof(float);
    context->depth_buffer = SDL_malloc(depth_buffer_size);
    if (context->depth_buffer == NULL)
    {
        SDL_free(context->color_buffer);
        SDL_DestroyTexture(context->color_texture);
        SDL_free(context);
        return SDL_OutOfMemory();
    }

    const size_t pixel_count = (size_t)render_width * (size_t)render_height;
    for (size_t i = 0; i < pixel_count; ++i)
    {
        context->depth_buffer[i] = 1.0f;
    }

    context->window = window;
    context->renderer = renderer;
    context->parallel_rasterizer = NULL;
    context->backend = resolved_backend;
    context->width = render_width;
    context->height = render_height;
    context->near_plane = SDL3D_DEFAULT_NEAR_PLANE;
    context->far_plane = SDL3D_DEFAULT_FAR_PLANE;
    context->in_mode_3d = false;
    context->model_stack = NULL;
    context->model_stack_depth = 0;
    context->model_stack_capacity = 0;
    context->backface_culling_enabled = false;
    context->wireframe_enabled = false;
    context->scissor_enabled = false;
    context->scissor_rect = (SDL_Rect){0, 0, render_width, render_height};
    context->model = sdl3d_mat4_identity();
    context->view = sdl3d_mat4_identity();
    context->projection = sdl3d_mat4_identity();
    context->view_projection = sdl3d_mat4_identity();
    context->model_view_projection = sdl3d_mat4_identity();
    sdl3d_try_create_parallel_rasterizer(context);

    *out_context = context;
    return true;
}

void sdl3d_destroy_render_context(sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return;
    }

    sdl3d_parallel_rasterizer_destroy(context->parallel_rasterizer);
    SDL_free(context->model_stack);
    SDL_DestroyTexture(context->color_texture);
    SDL_free(context->color_buffer);
    SDL_free(context->depth_buffer);
    SDL_free(context);
}

sdl3d_backend sdl3d_get_render_context_backend(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return SDL3D_BACKEND_AUTO;
    }

    return context->backend;
}

int sdl3d_get_render_context_width(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return 0;
    }

    return context->width;
}

int sdl3d_get_render_context_height(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return 0;
    }

    return context->height;
}

bool sdl3d_clear_render_context(sdl3d_render_context *context, sdl3d_color color)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    sdl3d_framebuffer framebuffer = sdl3d_framebuffer_from_context(context);
    sdl3d_framebuffer_clear(&framebuffer, color, 1.0f);
    return true;
}

bool sdl3d_clear_render_context_rect(sdl3d_render_context *context, const SDL_Rect *rect, sdl3d_color color)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (rect == NULL)
    {
        return SDL_InvalidParamError("rect");
    }
    if (rect->w < 0 || rect->h < 0)
    {
        return SDL_SetError("Clear rect dimensions must be non-negative.");
    }

    sdl3d_framebuffer framebuffer = sdl3d_framebuffer_from_context(context);
    sdl3d_framebuffer_clear_rect(&framebuffer, rect, color, 1.0f);
    return true;
}

bool sdl3d_set_scissor_rect(sdl3d_render_context *context, const SDL_Rect *rect)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (rect == NULL)
    {
        context->scissor_enabled = false;
        context->scissor_rect = (SDL_Rect){0, 0, context->width, context->height};
        return true;
    }

    if (rect->w < 0 || rect->h < 0)
    {
        return SDL_SetError("Scissor rect dimensions must be non-negative.");
    }

    Sint64 x = rect->x;
    Sint64 y = rect->y;
    Sint64 w = rect->w;
    Sint64 h = rect->h;

    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }

    if (x > context->width)
    {
        x = context->width;
    }
    if (y > context->height)
    {
        y = context->height;
    }

    if (w > context->width - x)
    {
        w = context->width - x;
    }
    if (h > context->height - y)
    {
        h = context->height - y;
    }

    if (w < 0)
    {
        w = 0;
    }
    if (h < 0)
    {
        h = 0;
    }

    context->scissor_enabled = true;
    context->scissor_rect = (SDL_Rect){(int)x, (int)y, (int)w, (int)h};
    return true;
}

bool sdl3d_is_scissor_enabled(const sdl3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return false;
    }

    return context->scissor_enabled;
}

bool sdl3d_get_scissor_rect(const sdl3d_render_context *context, SDL_Rect *out_rect)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (out_rect == NULL)
    {
        return SDL_InvalidParamError("out_rect");
    }
    if (!context->scissor_enabled)
    {
        return SDL_SetError("Scissor is not enabled on this render context.");
    }

    *out_rect = context->scissor_rect;
    return true;
}

bool sdl3d_present_render_context(sdl3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (!SDL_UpdateTexture(context->color_texture, NULL, context->color_buffer, context->width * 4))
    {
        return false;
    }

    if (!SDL_RenderTexture(context->renderer, context->color_texture, NULL, NULL))
    {
        return false;
    }

    return SDL_RenderPresent(context->renderer);
}
