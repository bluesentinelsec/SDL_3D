#include "slayer3d/render_context.h"

#include <limits.h>
#include <stdint.h>

#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "gl_renderer.h"
#include "rasterizer.h"
#include "render_context_internal.h"
#include "slayer3d/image.h"
#include "texture_internal.h"

static const char *const SLAYER3D_BACKEND_ENV = "SLAYER3D_BACKEND";
static const char *const SLAYER3D_DISABLE_PARALLEL_RASTERIZER_ENV = "SLAYER3D_DISABLE_PARALLEL_RASTERIZER";
static const float SLAYER3D_DEFAULT_NEAR_PLANE = 0.01f;
static const float SLAYER3D_DEFAULT_FAR_PLANE = 1000.0f;
static const float SLAYER3D_DYNAMIC_SCALE_SLOW_FRAME_RATIO = 1.10f;
static const float SLAYER3D_DYNAMIC_SCALE_FAST_FRAME_RATIO = 0.82f;
static const float SLAYER3D_DYNAMIC_SCALE_DOWN_STEP = 0.05f;
static const float SLAYER3D_DYNAMIC_SCALE_UP_STEP = 0.025f;
static const int SLAYER3D_DYNAMIC_SCALE_SLOW_FRAME_COUNT = 8;
static const int SLAYER3D_DYNAMIC_SCALE_FAST_FRAME_COUNT = 60;

static bool slayer3d_parallel_rasterizer_disabled_by_environment(void)
{
    const char *value = SDL_getenv(SLAYER3D_DISABLE_PARALLEL_RASTERIZER_ENV);

    if (value == NULL || *value == '\0')
    {
        return false;
    }

    return SDL_strcasecmp(value, "0") != 0 && SDL_strcasecmp(value, "false") != 0 && SDL_strcasecmp(value, "no") != 0 &&
           SDL_strcasecmp(value, "off") != 0;
}

static void slayer3d_try_create_parallel_rasterizer(slayer3d_render_context *context)
{
    const int logical_cores = SDL_GetNumLogicalCPUCores();

    if (context == NULL || logical_cores <= 1 || slayer3d_parallel_rasterizer_disabled_by_environment())
    {
        return;
    }

    slayer3d_parallel_rasterizer *parallel_rasterizer = NULL;
    if (slayer3d_parallel_rasterizer_create(logical_cores - 1, &parallel_rasterizer))
    {
        context->parallel_rasterizer = parallel_rasterizer;
        return;
    }

    SDL_ClearError();
}

static bool slayer3d_parse_backend_name(const char *name, slayer3d_backend *backend)
{
    if (name == NULL || backend == NULL)
    {
        return SDL_InvalidParamError(name == NULL ? "name" : "backend");
    }

    if (SDL_strcasecmp(name, "auto") == 0)
    {
        *backend = SLAYER3D_BACKEND_AUTO;
        return true;
    }

    if (SDL_strcasecmp(name, "software") == 0)
    {
        *backend = SLAYER3D_BACKEND_SOFTWARE;
        return true;
    }

    if (SDL_strcasecmp(name, "opengl") == 0 || SDL_strcasecmp(name, "gl") == 0 || SDL_strcasecmp(name, "gpu") == 0)
    {
        *backend = SLAYER3D_BACKEND_OPENGL;
        return true;
    }

    return SDL_SetError("Unsupported SLAYER3D backend override: %s", name);
}

static bool slayer3d_resolve_backend(slayer3d_backend requested_backend, bool allow_backend_fallback,
                                     slayer3d_backend *resolved_backend)
{
    if (resolved_backend == NULL)
    {
        return SDL_InvalidParamError("resolved_backend");
    }

    switch (requested_backend)
    {
    case SLAYER3D_BACKEND_AUTO:
    case SLAYER3D_BACKEND_SOFTWARE:
        *resolved_backend = SLAYER3D_BACKEND_SOFTWARE;
        return true;
    case SLAYER3D_BACKEND_OPENGL:
        (void)allow_backend_fallback;
        *resolved_backend = SLAYER3D_BACKEND_OPENGL;
        return true;
    default:
        return SDL_SetError("Unknown SLAYER3D backend value: %d", (int)requested_backend);
    }
}

void slayer3d_init_render_context_config(slayer3d_render_context_config *config)
{
    if (config == NULL)
    {
        SDL_InvalidParamError("config");
        return;
    }

    config->backend = SLAYER3D_BACKEND_AUTO;
    config->allow_backend_fallback = true;
    config->logical_width = 0;
    config->logical_height = 0;
    config->logical_size_policy = SLAYER3D_LOGICAL_SIZE_FIXED;
    config->logical_presentation = SDL_LOGICAL_PRESENTATION_STRETCH;
}

bool slayer3d_resolve_logical_size(int authored_width, int authored_height, int output_width, int output_height,
                                   slayer3d_logical_size_policy policy, int *out_width, int *out_height)
{
    if (out_width == NULL || out_height == NULL)
        return SDL_InvalidParamError("out_width/out_height");
    if (authored_width <= 0 || authored_height <= 0 || output_width <= 0 || output_height <= 0)
        return SDL_SetError("Logical and output dimensions must be positive.");
    if (policy != SLAYER3D_LOGICAL_SIZE_FIXED && policy != SLAYER3D_LOGICAL_SIZE_EXPAND)
        return SDL_SetError("Unknown logical size policy: %d", (int)policy);

    *out_width = authored_width;
    *out_height = authored_height;
    if (policy == SLAYER3D_LOGICAL_SIZE_FIXED)
        return true;

    const Sint64 authored_aspect_cross = (Sint64)authored_width * (Sint64)output_height;
    const Sint64 output_aspect_cross = (Sint64)output_width * (Sint64)authored_height;
    if (output_aspect_cross > authored_aspect_cross)
    {
        const Sint64 expanded_width =
            ((Sint64)authored_height * (Sint64)output_width + (Sint64)output_height - 1) / (Sint64)output_height;
        if (expanded_width > INT_MAX)
            return SDL_SetError("Expanded logical width exceeds the supported range.");
        *out_width = (int)expanded_width;
    }
    else if (output_aspect_cross < authored_aspect_cross)
    {
        const Sint64 expanded_height =
            ((Sint64)authored_width * (Sint64)output_height + (Sint64)output_width - 1) / (Sint64)output_width;
        if (expanded_height > INT_MAX)
            return SDL_SetError("Expanded logical height exceeds the supported range.");
        *out_height = (int)expanded_height;
    }
    return true;
}

bool slayer3d_resolve_aspect_fit_viewport(int logical_width, int logical_height, int output_width, int output_height,
                                          SDL_FRect *out_viewport)
{
    if (out_viewport == NULL)
        return SDL_InvalidParamError("out_viewport");
    if (logical_width <= 0 || logical_height <= 0 || output_width <= 0 || output_height <= 0)
        return SDL_SetError("Logical and output dimensions must be positive.");

    const float scale_x = (float)output_width / (float)logical_width;
    const float scale_y = (float)output_height / (float)logical_height;
    const float scale = SDL_min(scale_x, scale_y);
    out_viewport->w = (float)logical_width * scale;
    out_viewport->h = (float)logical_height * scale;
    out_viewport->x = ((float)output_width - out_viewport->w) * 0.5f;
    out_viewport->y = ((float)output_height - out_viewport->h) * 0.5f;
    return true;
}

static bool slayer3d_get_render_output_size(SDL_Window *window, SDL_Renderer *renderer, int *out_width, int *out_height)
{
    if (renderer != NULL)
    {
        if (SDL_GetRenderOutputSize(renderer, out_width, out_height) && *out_width > 0 && *out_height > 0)
            return true;
        SDL_ClearError();
    }
    return SDL_GetWindowSizeInPixels(window, out_width, out_height) && *out_width > 0 && *out_height > 0;
}

static bool slayer3d_render_buffer_sizes(int width, int height, size_t *out_color_size, size_t *out_depth_size)
{
    if (width <= 0 || height <= 0 || out_color_size == NULL || out_depth_size == NULL)
        return SDL_InvalidParamError("render buffer dimensions");
    if ((size_t)width > SIZE_MAX / (size_t)height)
        return SDL_SetError("Logical render dimensions overflow the addressable buffer size.");
    const size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > SIZE_MAX / 4U || pixel_count > SIZE_MAX / sizeof(float))
        return SDL_SetError("Logical render dimensions overflow the addressable buffer size.");
    *out_color_size = pixel_count * 4U;
    *out_depth_size = pixel_count * sizeof(float);
    return true;
}

const char *slayer3d_get_backend_name(slayer3d_backend backend)
{
    switch (backend)
    {
    case SLAYER3D_BACKEND_AUTO:
        return "auto";
    case SLAYER3D_BACKEND_SOFTWARE:
        return "software";
    case SLAYER3D_BACKEND_OPENGL:
        return "opengl";
    default:
        return "unknown";
    }
}

bool slayer3d_get_backend_override_from_environment(slayer3d_backend *backend)
{
    const char *value = SDL_getenv(SLAYER3D_BACKEND_ENV);

    if (value == NULL || *value == '\0')
    {
        return false;
    }

    return slayer3d_parse_backend_name(value, backend);
}

bool slayer3d_create_render_context(SDL_Window *window, SDL_Renderer *renderer,
                                    const slayer3d_render_context_config *config, slayer3d_render_context **out_context)
{
    slayer3d_render_context_config local_config;
    slayer3d_backend requested_backend;
    slayer3d_backend resolved_backend = SLAYER3D_BACKEND_SOFTWARE;
    slayer3d_render_context *context;
    const char *env_backend_name;
    int render_width;
    int render_height;
    size_t color_buffer_size;
    size_t depth_buffer_size;

    if (window == NULL)
    {
        return SDL_InvalidParamError("window");
    }

    if (out_context == NULL)
    {
        return SDL_InvalidParamError("out_context");
    }

    local_config = (config != NULL) ? *config : (slayer3d_render_context_config){0};
    if (config == NULL)
    {
        slayer3d_init_render_context_config(&local_config);
    }

    if ((local_config.logical_width < 0) || (local_config.logical_height < 0))
    {
        return SDL_SetError("Logical dimensions must be zero or positive.");
    }

    if ((local_config.logical_width == 0) != (local_config.logical_height == 0))
    {
        return SDL_SetError("Logical width and height must both be zero or both be non-zero.");
    }

    if (local_config.logical_size_policy != SLAYER3D_LOGICAL_SIZE_FIXED &&
        local_config.logical_size_policy != SLAYER3D_LOGICAL_SIZE_EXPAND)
    {
        return SDL_SetError("Unknown logical size policy: %d", (int)local_config.logical_size_policy);
    }
    if (local_config.logical_size_policy == SLAYER3D_LOGICAL_SIZE_EXPAND && local_config.logical_width == 0)
    {
        return SDL_SetError("Expanded logical sizing requires authored logical dimensions.");
    }

    requested_backend = local_config.backend;
    env_backend_name = SDL_getenv(SLAYER3D_BACKEND_ENV);
    if (env_backend_name != NULL && *env_backend_name != '\0')
    {
        if (!slayer3d_parse_backend_name(env_backend_name, &requested_backend))
        {
            return false;
        }
    }

    if (!slayer3d_resolve_backend(requested_backend, local_config.allow_backend_fallback, &resolved_backend))
    {
        return false;
    }
    if (resolved_backend == SLAYER3D_BACKEND_SOFTWARE && renderer == NULL)
    {
        return SDL_InvalidParamError("renderer");
    }

    if (local_config.logical_width > 0)
    {
        int output_width = 0;
        int output_height = 0;
        if (!slayer3d_get_render_output_size(window, renderer, &output_width, &output_height) ||
            !slayer3d_resolve_logical_size(local_config.logical_width, local_config.logical_height, output_width,
                                           output_height, local_config.logical_size_policy, &render_width,
                                           &render_height))
        {
            return false;
        }
        if (renderer != NULL)
        {
            const SDL_RendererLogicalPresentation presentation =
                local_config.logical_size_policy == SLAYER3D_LOGICAL_SIZE_EXPAND ? SDL_LOGICAL_PRESENTATION_LETTERBOX
                                                                                 : local_config.logical_presentation;
            if (!SDL_SetRenderLogicalPresentation(renderer, render_width, render_height, presentation))
            {
                return false;
            }
        }
    }
    else
    {
        if (renderer != NULL)
        {
            if (!SDL_GetCurrentRenderOutputSize(renderer, &render_width, &render_height))
            {
                return false;
            }
        }
        else
        {
            if (!SDL_GetWindowSizeInPixels(window, &render_width, &render_height))
            {
                return false;
            }
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

    if (resolved_backend == SLAYER3D_BACKEND_SOFTWARE)
    {
        if (!slayer3d_render_buffer_sizes(render_width, render_height, &color_buffer_size, &depth_buffer_size))
        {
            SDL_free(context);
            return false;
        }
        context->color_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                                   render_width, render_height);
        if (context->color_texture == NULL)
        {
            SDL_free(context);
            return false;
        }

        context->color_buffer = SDL_calloc(1, color_buffer_size);
        if (context->color_buffer == NULL)
        {
            SDL_DestroyTexture(context->color_texture);
            SDL_free(context);
            return SDL_OutOfMemory();
        }

        context->depth_buffer = SDL_malloc(depth_buffer_size);
        if (context->depth_buffer == NULL)
        {
            SDL_free(context->color_buffer);
            SDL_DestroyTexture(context->color_texture);
            SDL_free(context);
            return SDL_OutOfMemory();
        }

        {
            const size_t pixel_count = (size_t)render_width * (size_t)render_height;
            for (size_t i = 0; i < pixel_count; ++i)
            {
                context->depth_buffer[i] = 1.0f;
            }
        }
    }
    else
    {
        /* GL backend: create OpenGL context and compile shaders. */
        context->gl = slayer3d_gl_create(window, render_width, render_height);
        if (context->gl == NULL)
        {
            SDL_free(context);
            return false;
        }
    }

    context->window = window;
    context->renderer = renderer;
    context->texture_cache = NULL;
    context->parallel_rasterizer = NULL;
    context->backend = resolved_backend;

    /* Initialize the backend dispatch table. */
    if (resolved_backend == SLAYER3D_BACKEND_OPENGL)
    {
        slayer3d_gl_backend_init(&context->backend_iface);
    }
    else
    {
        slayer3d_sw_backend_init(&context->backend_iface);
    }

    context->width = render_width;
    context->height = render_height;
    context->authored_width = local_config.logical_width > 0 ? local_config.logical_width : render_width;
    context->authored_height = local_config.logical_height > 0 ? local_config.logical_height : render_height;
    context->logical_size_policy = local_config.logical_size_policy;
    context->logical_presentation = local_config.logical_presentation;
    context->world_render_scale = 1.0f;
    context->world_render_target_scale = 1.0f;
    context->world_render_width = render_width;
    context->world_render_height = render_height;
    context->dynamic_world_render_scale_enabled = false;
    context->dynamic_world_render_min_scale = 0.5f;
    context->dynamic_world_render_max_scale = 1.0f;
    context->dynamic_world_render_target_fps = 60.0f;
    context->dynamic_world_render_frame_ms_ema = 0.0f;
    context->dynamic_world_render_slow_frames = 0;
    context->dynamic_world_render_fast_frames = 0;
    context->near_plane = SLAYER3D_DEFAULT_NEAR_PLANE;
    context->far_plane = SLAYER3D_DEFAULT_FAR_PLANE;
    context->in_mode_3d = false;
    context->model_stack = NULL;
    context->model_stack_depth = 0;
    context->model_stack_capacity = 0;
    context->backface_culling_enabled = false;
    context->wireframe_enabled = false;
    context->viewport_enabled = false;
    context->viewport_rect = (SDL_Rect){0, 0, render_width, render_height};
    context->scissor_enabled = false;
    context->scissor_rect = (SDL_Rect){0, 0, render_width, render_height};
    context->model = slayer3d_mat4_identity();
    context->view = slayer3d_mat4_identity();
    context->projection = slayer3d_mat4_identity();
    context->view_projection = slayer3d_mat4_identity();
    context->model_view_projection = slayer3d_mat4_identity();
    context->shading_mode = SLAYER3D_SHADING_UNLIT;
    context->light_count = 0;
    context->ambient[0] = 0.03f;
    context->ambient[1] = 0.03f;
    context->ambient[2] = 0.03f;
    context->bloom_enabled = true;
    context->ssao_enabled = true;
    context->point_shadows_enabled = true;
    context->depth_prepass_enabled = false;
    context->render_sample_queries_enabled = false;
    context->per_object_light_selection_enabled = false;
    context->per_object_light_limit = SLAYER3D_MAX_SHADER_LIGHTS;
    context->fog.mode = SLAYER3D_FOG_NONE;
    context->tonemap_mode = SLAYER3D_TONEMAP_NONE;
    SDL_memset(context->shadow_depth, 0, sizeof(context->shadow_depth));
    SDL_memset(context->shadow_enabled, 0, sizeof(context->shadow_enabled));
    context->shadow_bias = 0.005f;
    context->uv_mode = SLAYER3D_UV_PERSPECTIVE;
    context->fog_eval = SLAYER3D_FOG_EVAL_FRAGMENT;
    context->vertex_snap = false;
    context->vertex_snap_precision = 1;
    context->color_quantize = false;
    context->color_depth = 0;
    context->display_profile = SLAYER3D_DISPLAY_PROFILE_MODERN;
    context->display_width = 0;
    context->display_height = 0;
    context->display_filter = SLAYER3D_DISPLAY_FILTER_LINEAR;
    if (context->backend == SLAYER3D_BACKEND_SOFTWARE)
    {
        slayer3d_try_create_parallel_rasterizer(context);
    }

    *out_context = context;
    return true;
}

void slayer3d_destroy_render_context(slayer3d_render_context *context)
{
    if (context == NULL)
    {
        return;
    }

    /* Backend-specific cleanup. */
    if (context->backend_iface.destroy != NULL)
    {
        context->backend_iface.destroy(context);
    }

    /* Shared cleanup. */
    slayer3d_texture_cache_destroy(context->texture_cache);
    SDL_free(context->model_stack);
    for (int i = 0; i < SLAYER3D_MAX_LIGHTS; ++i)
    {
        SDL_free(context->shadow_depth[i]);
    }
    SDL_free(context);
}

slayer3d_backend slayer3d_get_render_context_backend(const slayer3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return SLAYER3D_BACKEND_AUTO;
    }

    return context->backend;
}

int slayer3d_get_render_context_width(const slayer3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return 0;
    }

    return context->width;
}

int slayer3d_get_render_context_height(const slayer3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return 0;
    }

    return context->height;
}

static float slayer3d_map_window_axis_to_logical(float value, float viewport_origin, float viewport_extent,
                                                 float logical_extent)
{
    return viewport_extent > 0.0f ? (value - viewport_origin) * logical_extent / viewport_extent : 0.0f;
}

bool slayer3d_get_render_context_safe_area(const slayer3d_render_context *context, SDL_FRect *out_area)
{
    if (context == NULL || out_area == NULL)
        return SDL_InvalidParamError("context/out_area");

    *out_area = (SDL_FRect){0.0f, 0.0f, (float)context->width, (float)context->height};
    if (context->window == NULL || context->width <= 0 || context->height <= 0)
        return true;

    SDL_Rect safe;
    int window_width = 0;
    int window_height = 0;
    if (!SDL_GetWindowSafeArea(context->window, &safe) ||
        !SDL_GetWindowSize(context->window, &window_width, &window_height) || window_width <= 0 || window_height <= 0)
    {
        SDL_ClearError();
        return true;
    }

    if (context->renderer != NULL)
    {
        float logical_left = 0.0f;
        float logical_top = 0.0f;
        float logical_right = 0.0f;
        float logical_bottom = 0.0f;
        if (SDL_RenderCoordinatesFromWindow(context->renderer, (float)safe.x, (float)safe.y, &logical_left,
                                            &logical_top) &&
            SDL_RenderCoordinatesFromWindow(context->renderer, (float)(safe.x + safe.w), (float)(safe.y + safe.h),
                                            &logical_right, &logical_bottom))
        {
            out_area->x = SDL_clamp(logical_left, 0.0f, (float)context->width);
            out_area->y = SDL_clamp(logical_top, 0.0f, (float)context->height);
            out_area->w = SDL_max(SDL_clamp(logical_right, 0.0f, (float)context->width) - out_area->x, 0.0f);
            out_area->h = SDL_max(SDL_clamp(logical_bottom, 0.0f, (float)context->height) - out_area->y, 0.0f);
            if (out_area->w > 0.0f && out_area->h > 0.0f)
                return true;
        }
        SDL_ClearError();
        *out_area = (SDL_FRect){0.0f, 0.0f, (float)context->width, (float)context->height};
    }

    float viewport_x = 0.0f;
    float viewport_y = 0.0f;
    float viewport_width = (float)window_width;
    float viewport_height = (float)window_height;
    if (context->logical_size_policy == SLAYER3D_LOGICAL_SIZE_EXPAND ||
        (context->logical_presentation != SDL_LOGICAL_PRESENTATION_STRETCH &&
         context->logical_presentation != SDL_LOGICAL_PRESENTATION_DISABLED))
    {
        if (context->logical_size_policy == SLAYER3D_LOGICAL_SIZE_EXPAND ||
            context->logical_presentation == SDL_LOGICAL_PRESENTATION_LETTERBOX)
        {
            SDL_FRect viewport;
            if (!slayer3d_resolve_aspect_fit_viewport(context->width, context->height, window_width, window_height,
                                                      &viewport))
                return false;
            viewport_x = viewport.x;
            viewport_y = viewport.y;
            viewport_width = viewport.w;
            viewport_height = viewport.h;
        }
        else
        {
            const float scale_x = (float)window_width / (float)context->width;
            const float scale_y = (float)window_height / (float)context->height;
            float scale;
            if (context->logical_presentation == SDL_LOGICAL_PRESENTATION_OVERSCAN)
                scale = SDL_max(scale_x, scale_y);
            else
                scale = SDL_max(SDL_floorf(SDL_min(scale_x, scale_y)), 1.0f);
            viewport_width = (float)context->width * scale;
            viewport_height = (float)context->height * scale;
            viewport_x = ((float)window_width - viewport_width) * 0.5f;
            viewport_y = ((float)window_height - viewport_height) * 0.5f;
        }
    }

    const float safe_left = SDL_max((float)safe.x, viewport_x);
    const float safe_top = SDL_max((float)safe.y, viewport_y);
    const float safe_right = SDL_min((float)(safe.x + safe.w), viewport_x + viewport_width);
    const float safe_bottom = SDL_min((float)(safe.y + safe.h), viewport_y + viewport_height);
    if (safe_right <= safe_left || safe_bottom <= safe_top)
        return true;

    const float logical_left =
        slayer3d_map_window_axis_to_logical(safe_left, viewport_x, viewport_width, (float)context->width);
    const float logical_top =
        slayer3d_map_window_axis_to_logical(safe_top, viewport_y, viewport_height, (float)context->height);
    const float logical_right =
        slayer3d_map_window_axis_to_logical(safe_right, viewport_x, viewport_width, (float)context->width);
    const float logical_bottom =
        slayer3d_map_window_axis_to_logical(safe_bottom, viewport_y, viewport_height, (float)context->height);
    out_area->x = SDL_clamp(logical_left, 0.0f, (float)context->width);
    out_area->y = SDL_clamp(logical_top, 0.0f, (float)context->height);
    out_area->w = SDL_max(SDL_clamp(logical_right, 0.0f, (float)context->width) - out_area->x, 0.0f);
    out_area->h = SDL_max(SDL_clamp(logical_bottom, 0.0f, (float)context->height) - out_area->y, 0.0f);
    return true;
}

bool slayer3d_get_render_context_authored_size(const slayer3d_render_context *context, int *out_width, int *out_height)
{
    if (context == NULL || out_width == NULL || out_height == NULL)
        return SDL_InvalidParamError("context/out_width/out_height");
    *out_width = context->authored_width;
    *out_height = context->authored_height;
    return true;
}

slayer3d_logical_size_policy slayer3d_get_render_context_logical_size_policy(const slayer3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return SLAYER3D_LOGICAL_SIZE_FIXED;
    }
    return context->logical_size_policy;
}

static SDL_Rect slayer3d_clamp_rect_to_logical_size(SDL_Rect rect, int width, int height)
{
    if (rect.x < 0)
    {
        rect.w += rect.x;
        rect.x = 0;
    }
    if (rect.y < 0)
    {
        rect.h += rect.y;
        rect.y = 0;
    }
    rect.x = SDL_min(rect.x, width);
    rect.y = SDL_min(rect.y, height);
    rect.w = SDL_max(0, SDL_min(rect.w, width - rect.x));
    rect.h = SDL_max(0, SDL_min(rect.h, height - rect.y));
    return rect;
}

static bool slayer3d_resize_render_context(slayer3d_render_context *context, int width, int height,
                                           slayer3d_logical_size_policy policy,
                                           SDL_RendererLogicalPresentation fixed_presentation)
{
    if (context == NULL)
        return SDL_InvalidParamError("context");
    if (width <= 0 || height <= 0)
        return SDL_SetError("Logical render dimensions must be positive.");
    if (context->width == width && context->height == height)
    {
        if (context->renderer != NULL)
        {
            const SDL_RendererLogicalPresentation presentation =
                policy == SLAYER3D_LOGICAL_SIZE_EXPAND ? SDL_LOGICAL_PRESENTATION_LETTERBOX : fixed_presentation;
            return SDL_SetRenderLogicalPresentation(context->renderer, width, height, presentation);
        }
        return true;
    }

    if (context->backend == SLAYER3D_BACKEND_SOFTWARE)
    {
        size_t color_size = 0;
        size_t depth_size = 0;
        if (!slayer3d_render_buffer_sizes(width, height, &color_size, &depth_size))
            return false;

        SDL_Texture *new_texture =
            SDL_CreateTexture(context->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
        Uint8 *new_color_buffer = (Uint8 *)SDL_calloc(1, color_size);
        float *new_depth_buffer = (float *)SDL_malloc(depth_size);
        if (new_texture == NULL || new_color_buffer == NULL || new_depth_buffer == NULL)
        {
            SDL_DestroyTexture(new_texture);
            SDL_free(new_color_buffer);
            SDL_free(new_depth_buffer);
            if (SDL_GetError()[0] == '\0')
                SDL_OutOfMemory();
            return false;
        }

        const size_t pixel_count = (size_t)width * (size_t)height;
        for (size_t i = 0; i < pixel_count; ++i)
            new_depth_buffer[i] = 1.0f;

        const SDL_RendererLogicalPresentation presentation =
            policy == SLAYER3D_LOGICAL_SIZE_EXPAND ? SDL_LOGICAL_PRESENTATION_LETTERBOX : fixed_presentation;
        if (!SDL_SetRenderLogicalPresentation(context->renderer, width, height, presentation))
        {
            SDL_DestroyTexture(new_texture);
            SDL_free(new_color_buffer);
            SDL_free(new_depth_buffer);
            return false;
        }

        SDL_DestroyTexture(context->color_texture);
        SDL_free(context->color_buffer);
        SDL_free(context->depth_buffer);
        context->color_texture = new_texture;
        context->color_buffer = new_color_buffer;
        context->depth_buffer = new_depth_buffer;
        context->world_render_width = width;
        context->world_render_height = height;
    }
    else if (!slayer3d_gl_sync_world_render_target(context->gl, width, height, context->world_render_scale,
                                                   &context->world_render_width, &context->world_render_height))
    {
        return false;
    }

    const int old_width = context->width;
    const int old_height = context->height;
    context->width = width;
    context->height = height;
    context->viewport_rect = context->viewport_enabled
                                 ? slayer3d_clamp_rect_to_logical_size(context->viewport_rect, width, height)
                                 : (SDL_Rect){0, 0, width, height};
    context->scissor_rect = context->scissor_enabled
                                ? slayer3d_clamp_rect_to_logical_size(context->scissor_rect, width, height)
                                : (SDL_Rect){0, 0, width, height};
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D logical canvas resized: %dx%d -> %dx%d", old_width, old_height,
                width, height);
    return true;
}

static bool slayer3d_apply_logical_size_config(slayer3d_render_context *context, int authored_width,
                                               int authored_height, slayer3d_logical_size_policy policy,
                                               SDL_RendererLogicalPresentation fixed_presentation)
{
    int output_width = 0;
    int output_height = 0;
    int width = 0;
    int height = 0;
    if (context == NULL)
        return SDL_InvalidParamError("context");
    if (!slayer3d_get_render_output_size(context->window, context->renderer, &output_width, &output_height) ||
        !slayer3d_resolve_logical_size(authored_width, authored_height, output_width, output_height, policy, &width,
                                       &height) ||
        !slayer3d_resize_render_context(context, width, height, policy, fixed_presentation))
    {
        return false;
    }

    context->authored_width = authored_width;
    context->authored_height = authored_height;
    context->logical_size_policy = policy;
    context->logical_presentation = fixed_presentation;
    return true;
}

bool slayer3d_sync_render_context_logical_size(slayer3d_render_context *context)
{
    if (context == NULL)
        return SDL_InvalidParamError("context");
    if (context->logical_size_policy == SLAYER3D_LOGICAL_SIZE_FIXED)
        return true;
    return slayer3d_apply_logical_size_config(context, context->authored_width, context->authored_height,
                                              context->logical_size_policy, context->logical_presentation);
}

float slayer3d_get_render_context_display_scale(const slayer3d_render_context *context)
{
    if (context == NULL || context->window == NULL || context->width <= 0 || context->height <= 0)
        return 1.0f;

    int pixel_w = 0;
    int pixel_h = 0;
    if (!SDL_GetWindowSizeInPixels(context->window, &pixel_w, &pixel_h) || pixel_w <= 0 || pixel_h <= 0)
        return 1.0f;

    /* Presentation letterboxes to preserve aspect, so one logical unit maps
     * to the smaller of the two axis scales in native pixels. */
    const float scale_x = (float)pixel_w / (float)context->width;
    const float scale_y = (float)pixel_h / (float)context->height;
    return SDL_max(SDL_min(scale_x, scale_y), 0.0625f);
}

static bool apply_world_render_scale(slayer3d_render_context *context, float scale)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (!(scale >= 0.25f && scale <= 1.0f))
    {
        return SDL_SetError("World render scale must be between 0.25 and 1.0.");
    }

    int world_width = context->width;
    int world_height = context->height;
    if (context->backend == SLAYER3D_BACKEND_OPENGL)
    {
        if (!slayer3d_gl_set_world_render_scale(context->gl, context->width, context->height, scale, &world_width,
                                                &world_height))
        {
            return false;
        }
    }

    context->world_render_scale = scale;
    context->world_render_width = world_width;
    context->world_render_height = world_height;
    return true;
}

bool slayer3d_set_world_render_scale(slayer3d_render_context *context, float scale)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (!(scale >= 0.25f && scale <= 1.0f))
    {
        return SDL_SetError("World render scale must be between 0.25 and 1.0.");
    }

    context->world_render_target_scale = scale;
    if (context->dynamic_world_render_scale_enabled)
    {
        float dynamic_scale = SDL_clamp(context->world_render_scale, context->dynamic_world_render_min_scale, scale);
        if (!(dynamic_scale >= context->dynamic_world_render_min_scale))
            dynamic_scale = scale;
        return apply_world_render_scale(context, dynamic_scale);
    }
    return apply_world_render_scale(context, scale);
}

float slayer3d_get_world_render_scale(const slayer3d_render_context *context)
{
    return context != NULL ? context->world_render_scale : 1.0f;
}

bool slayer3d_configure_dynamic_world_render_scale(slayer3d_render_context *context, bool enabled, float min_scale,
                                                   float max_scale, float target_fps)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (!(min_scale >= 0.25f && min_scale <= 1.0f && max_scale >= 0.25f && max_scale <= 1.0f && min_scale <= max_scale))
    {
        return SDL_SetError("Dynamic world render scale bounds must be between 0.25 and 1.0.");
    }
    if (!(target_fps >= 15.0f && target_fps <= 500.0f))
    {
        return SDL_SetError("Dynamic world render scale target FPS must be between 15 and 500.");
    }

    const bool settings_changed = context->dynamic_world_render_scale_enabled != enabled ||
                                  context->dynamic_world_render_min_scale != min_scale ||
                                  context->dynamic_world_render_max_scale != max_scale ||
                                  context->dynamic_world_render_target_fps != target_fps;
    context->dynamic_world_render_scale_enabled = enabled;
    context->dynamic_world_render_min_scale = min_scale;
    context->dynamic_world_render_max_scale = max_scale;
    context->dynamic_world_render_target_fps = target_fps;
    context->world_render_target_scale = SDL_clamp(context->world_render_target_scale, min_scale, max_scale);
    if (settings_changed)
    {
        context->dynamic_world_render_slow_frames = 0;
        context->dynamic_world_render_fast_frames = 0;
        context->dynamic_world_render_frame_ms_ema = 0.0f;
    }
    if (!enabled)
    {
        return apply_world_render_scale(context, context->world_render_target_scale);
    }
    if (context->world_render_scale > context->world_render_target_scale ||
        context->world_render_scale < context->dynamic_world_render_min_scale)
    {
        return apply_world_render_scale(context,
                                        SDL_clamp(context->world_render_scale, context->dynamic_world_render_min_scale,
                                                  context->world_render_target_scale));
    }
    return true;
}

bool slayer3d_update_dynamic_world_render_scale(slayer3d_render_context *context, float frame_ms)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (!context->dynamic_world_render_scale_enabled)
    {
        return true;
    }
    if (!(frame_ms > 0.0f))
    {
        return SDL_SetError("Dynamic world render scale frame time must be positive.");
    }

    if (context->dynamic_world_render_frame_ms_ema <= 0.0f)
        context->dynamic_world_render_frame_ms_ema = frame_ms;
    else
        context->dynamic_world_render_frame_ms_ema =
            context->dynamic_world_render_frame_ms_ema * 0.9f + frame_ms * 0.1f;

    const float target_ms = 1000.0f / context->dynamic_world_render_target_fps;
    float next_scale = context->world_render_scale;
    if (context->dynamic_world_render_frame_ms_ema > target_ms * SLAYER3D_DYNAMIC_SCALE_SLOW_FRAME_RATIO)
    {
        context->dynamic_world_render_slow_frames++;
        context->dynamic_world_render_fast_frames = 0;
        if (context->dynamic_world_render_slow_frames >= SLAYER3D_DYNAMIC_SCALE_SLOW_FRAME_COUNT)
        {
            next_scale = SDL_max(context->dynamic_world_render_min_scale,
                                 context->world_render_scale - SLAYER3D_DYNAMIC_SCALE_DOWN_STEP);
            context->dynamic_world_render_slow_frames = 0;
        }
    }
    else if (context->dynamic_world_render_frame_ms_ema < target_ms * SLAYER3D_DYNAMIC_SCALE_FAST_FRAME_RATIO)
    {
        context->dynamic_world_render_fast_frames++;
        context->dynamic_world_render_slow_frames = 0;
        if (context->dynamic_world_render_fast_frames >= SLAYER3D_DYNAMIC_SCALE_FAST_FRAME_COUNT)
        {
            next_scale = SDL_min(context->world_render_target_scale,
                                 context->world_render_scale + SLAYER3D_DYNAMIC_SCALE_UP_STEP);
            context->dynamic_world_render_fast_frames = 0;
        }
    }
    else
    {
        context->dynamic_world_render_slow_frames = 0;
        context->dynamic_world_render_fast_frames = 0;
    }

    if (next_scale != context->world_render_scale)
    {
        return apply_world_render_scale(context, next_scale);
    }
    return true;
}

bool slayer3d_get_world_render_size(const slayer3d_render_context *context, int *out_width, int *out_height)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (out_width != NULL)
    {
        *out_width = context->world_render_width;
    }
    if (out_height != NULL)
    {
        *out_height = context->world_render_height;
    }
    return true;
}

bool slayer3d_get_render_stats(const slayer3d_render_context *context, slayer3d_render_stats *out_stats)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (out_stats == NULL)
    {
        return SDL_InvalidParamError("out_stats");
    }

    *out_stats = context->stats;
    return true;
}

void slayer3d_reset_render_stats(slayer3d_render_context *context)
{
    if (context == NULL)
    {
        return;
    }
    SDL_zero(context->stats);
}

bool slayer3d_set_depth_prepass_enabled(slayer3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    context->depth_prepass_enabled = enabled;
    return true;
}

bool slayer3d_is_depth_prepass_enabled(const slayer3d_render_context *context)
{
    return context != NULL && context->depth_prepass_enabled;
}

bool slayer3d_set_render_sample_queries_enabled(slayer3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    context->render_sample_queries_enabled = enabled;
    return true;
}

bool slayer3d_render_sample_queries_enabled(const slayer3d_render_context *context)
{
    return context != NULL && context->render_sample_queries_enabled;
}

bool slayer3d_set_per_object_light_selection_enabled(slayer3d_render_context *context, bool enabled)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    context->per_object_light_selection_enabled = enabled;
    return true;
}

bool slayer3d_per_object_light_selection_enabled(const slayer3d_render_context *context)
{
    return context != NULL && context->per_object_light_selection_enabled;
}

bool slayer3d_set_per_object_light_limit(slayer3d_render_context *context, int limit)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    context->per_object_light_limit = SDL_clamp(limit, 0, SLAYER3D_MAX_SHADER_LIGHTS);
    return true;
}

int slayer3d_per_object_light_limit(const slayer3d_render_context *context)
{
    return context != NULL ? context->per_object_light_limit : 0;
}

bool slayer3d_clear_render_context(slayer3d_render_context *context, slayer3d_color color)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    return context->backend_iface.clear(context, color);
}

bool slayer3d_clear_render_context_rect(slayer3d_render_context *context, const SDL_Rect *rect, slayer3d_color color)
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

    slayer3d_framebuffer framebuffer = slayer3d_framebuffer_from_context(context);
    slayer3d_framebuffer_clear_rect(&framebuffer, rect, color, 1.0f);
    return true;
}

bool slayer3d_set_render_viewport(slayer3d_render_context *context, const SDL_Rect *rect)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (rect == NULL)
    {
        context->viewport_enabled = false;
        context->viewport_rect = (SDL_Rect){0, 0, context->width, context->height};
        return true;
    }

    if (rect->w <= 0 || rect->h <= 0)
    {
        return SDL_SetError("Render viewport dimensions must be positive.");
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
        x = context->width;
    if (y > context->height)
        y = context->height;
    if (w > context->width - x)
        w = context->width - x;
    if (h > context->height - y)
        h = context->height - y;
    if (w <= 0 || h <= 0)
    {
        return SDL_SetError("Render viewport lies outside the logical render area.");
    }

    context->viewport_enabled = true;
    context->viewport_rect = (SDL_Rect){(int)x, (int)y, (int)w, (int)h};
    return true;
}

bool slayer3d_is_render_viewport_enabled(const slayer3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return false;
    }
    return context->viewport_enabled;
}

bool slayer3d_get_render_viewport(const slayer3d_render_context *context, SDL_Rect *out_rect)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (out_rect == NULL)
    {
        return SDL_InvalidParamError("out_rect");
    }
    *out_rect = context->viewport_enabled ? context->viewport_rect : (SDL_Rect){0, 0, context->width, context->height};
    return true;
}

bool slayer3d_set_scissor_rect(slayer3d_render_context *context, const SDL_Rect *rect)
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

bool slayer3d_is_scissor_enabled(const slayer3d_render_context *context)
{
    if (context == NULL)
    {
        SDL_InvalidParamError("context");
        return false;
    }

    return context->scissor_enabled;
}

bool slayer3d_get_scissor_rect(const slayer3d_render_context *context, SDL_Rect *out_rect)
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

bool slayer3d_present_render_context(slayer3d_render_context *context)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    if (!slayer3d_sync_render_context_logical_size(context))
        return false;
    return context->backend_iface.present(context);
}

/* ------------------------------------------------------------------ */
/* High-level window/context API                                       */
/* ------------------------------------------------------------------ */

void slayer3d_init_window_config(slayer3d_window_config *config)
{
    if (config == NULL)
    {
        return;
    }
    SDL_zerop(config);
    config->width = 1280;
    config->height = 720;
    config->logical_width = 1280;
    config->logical_height = 720;
    config->logical_size_policy = SLAYER3D_LOGICAL_SIZE_FIXED;
    config->title = "SLAYER3D";
    config->icon_path = NULL;
    config->backend = SLAYER3D_BACKEND_OPENGL;
    config->allow_backend_fallback = true;
    config->display_mode = SLAYER3D_WINDOW_MODE_WINDOWED;
    config->vsync = true;
    config->maximized = false;
    config->resizable = true;
    config->high_pixel_density = true;
}

static void slayer3d_apply_window_icon(SDL_Window *window, const char *icon_path)
{
    if (window == NULL || icon_path == NULL || icon_path[0] == '\0')
        return;

    slayer3d_image icon;
    SDL_zero(icon);
    if (!slayer3d_load_image_from_file(icon_path, &icon))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D window icon load failed: %s", SDL_GetError());
        SDL_ClearError();
        return;
    }

    SDL_Surface *surface =
        SDL_CreateSurfaceFrom(icon.width, icon.height, SDL_PIXELFORMAT_RGBA32, icon.pixels, icon.width * 4);
    if (surface != NULL)
    {
        SDL_SetWindowIcon(window, surface);
        SDL_DestroySurface(surface);
    }
    else
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D window icon surface creation failed: %s", SDL_GetError());
        SDL_ClearError();
    }
    slayer3d_free_image(&icon);
}

static const char *slayer3d_window_mode_name(slayer3d_window_mode mode)
{
    switch (mode)
    {
    case SLAYER3D_WINDOW_MODE_WINDOWED:
        return "windowed";
    case SLAYER3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE:
        return "fullscreen_exclusive";
    case SLAYER3D_WINDOW_MODE_FULLSCREEN_BORDERLESS:
        return "fullscreen_borderless";
    case SLAYER3D_WINDOW_MODE_DEFAULT:
    default:
        return "default";
    }
}

static bool slayer3d_apply_window_mode(SDL_Window *window, slayer3d_window_mode mode)
{
    if (window == NULL)
        return SDL_InvalidParamError("window");

    if (mode == SLAYER3D_WINDOW_MODE_FULLSCREEN_BORDERLESS)
    {
        if (!SDL_SetWindowFullscreenMode(window, NULL))
            return false;
        return SDL_SetWindowFullscreen(window, true);
    }
    else if (mode == SLAYER3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE)
    {
        SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        const SDL_DisplayMode *desktop = display != 0 ? SDL_GetDesktopDisplayMode(display) : NULL;
        if (desktop == NULL)
            return SDL_SetError("Unable to resolve desktop display mode for exclusive fullscreen");
        if (!SDL_SetWindowFullscreenMode(window, desktop))
            return false;
        return SDL_SetWindowFullscreen(window, true);
    }

    return SDL_SetWindowFullscreen(window, false);
}

static bool slayer3d_apply_context_vsync(slayer3d_render_context *context, bool vsync)
{
    if (context == NULL)
        return SDL_InvalidParamError("context");
    if (context->backend == SLAYER3D_BACKEND_OPENGL)
        return SDL_GL_SetSwapInterval(vsync ? 1 : 0);
    if (context->renderer != NULL)
        return SDL_SetRenderVSync(context->renderer, vsync ? 1 : 0);
    return true;
}

static bool slayer3d_get_context_vsync(const slayer3d_render_context *context, int *out_interval)
{
    if (context == NULL)
        return SDL_InvalidParamError("context");
    if (out_interval == NULL)
        return SDL_InvalidParamError("out_interval");
    if (context->backend == SLAYER3D_BACKEND_OPENGL)
        return SDL_GL_GetSwapInterval(out_interval);
    if (context->renderer != NULL)
        return SDL_GetRenderVSync(context->renderer, out_interval);

    *out_interval = 0;
    return true;
}

static const char *slayer3d_actual_window_mode_name(SDL_Window *window)
{
    if (window == NULL || (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0)
        return "windowed";
    return SDL_GetWindowFullscreenMode(window) == NULL ? "fullscreen_borderless" : "fullscreen_exclusive";
}

bool slayer3d_create_window(const slayer3d_window_config *config, SDL_Window **out_window,
                            slayer3d_render_context **out_context)
{
    slayer3d_window_config local;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    slayer3d_render_context *context = NULL;
    slayer3d_render_context_config rcfg;
    slayer3d_backend resolved = SLAYER3D_BACKEND_SOFTWARE;

    if (out_window == NULL || out_context == NULL)
    {
        return SDL_InvalidParamError("out_window/out_context");
    }
    *out_window = NULL;
    *out_context = NULL;

    if (config != NULL)
    {
        local = *config;
    }
    else
    {
        slayer3d_init_window_config(&local);
    }

    if (local.width <= 0)
        local.width = 1280;
    if (local.height <= 0)
        local.height = 720;
    if (local.logical_width <= 0)
        local.logical_width = local.width;
    if (local.logical_height <= 0)
        local.logical_height = local.height;
    if (local.title == NULL)
        local.title = "SLAYER3D";
    if (local.display_mode == SLAYER3D_WINDOW_MODE_DEFAULT)
        local.display_mode = SLAYER3D_WINDOW_MODE_WINDOWED;

    /* Resolve which backend we'll actually use. */
    if (!slayer3d_resolve_backend(local.backend, local.allow_backend_fallback, &resolved))
    {
        return false;
    }

retry_backend:
    window = NULL;
    renderer = NULL;
    context = NULL;

    /* Set up window flags and GL attributes based on resolved backend. */
    SDL_WindowFlags flags = 0;
    if (local.resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (local.maximized)
        flags |= SDL_WINDOW_MAXIMIZED;
    if (local.high_pixel_density)
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (resolved == SLAYER3D_BACKEND_OPENGL)
    {
#ifdef __EMSCRIPTEN__
        /* OpenGL ES 3.0 maps onto WebGL2 in the browser. */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        flags |= SDL_WINDOW_OPENGL;
    }

    window = SDL_CreateWindow(local.title, local.width, local.height, flags);
    if (window == NULL)
    {
        if (resolved == SLAYER3D_BACKEND_OPENGL && local.allow_backend_fallback)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SLAYER3D OpenGL window creation failed, falling back to software: %s", SDL_GetError());
            SDL_ClearError();
            resolved = SLAYER3D_BACKEND_SOFTWARE;
            local.allow_backend_fallback = false;
            goto retry_backend;
        }
        return false;
    }
    slayer3d_apply_window_icon(window, local.icon_path);
    if (!slayer3d_apply_window_mode(window, local.display_mode))
    {
        SDL_DestroyWindow(window);
        return false;
    }

    /* Software backend needs an SDL_Renderer; GL does not. */
    if (resolved != SLAYER3D_BACKEND_OPENGL)
    {
        renderer = SDL_CreateRenderer(window, NULL);
        if (renderer == NULL)
        {
            SDL_DestroyWindow(window);
            return false;
        }
        if (!SDL_SetRenderVSync(renderer, local.vsync ? 1 : 0))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D renderer vsync request failed: %s", SDL_GetError());
            SDL_ClearError();
        }
    }

    slayer3d_init_render_context_config(&rcfg);
    rcfg.backend = resolved;
    rcfg.allow_backend_fallback = false; /* already resolved */
    rcfg.logical_width = local.logical_width;
    rcfg.logical_height = local.logical_height;
    rcfg.logical_size_policy = local.logical_size_policy;
    rcfg.logical_presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;

    if (!slayer3d_create_render_context(window, renderer, &rcfg, &context))
    {
        if (resolved == SLAYER3D_BACKEND_OPENGL && local.allow_backend_fallback)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SLAYER3D OpenGL context creation failed, falling back to software: %s", SDL_GetError());
            SDL_ClearError();
            if (renderer != NULL)
                SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            resolved = SLAYER3D_BACKEND_SOFTWARE;
            local.allow_backend_fallback = false;
            goto retry_backend;
        }
        if (renderer != NULL)
            SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return false;
    }

    if (resolved == SLAYER3D_BACKEND_OPENGL && !SDL_GL_SetSwapInterval(local.vsync ? 1 : 0))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D GL vsync request failed: %s", SDL_GetError());
        SDL_ClearError();
    }

    int actual_vsync = 0;
    bool have_actual_vsync = slayer3d_get_context_vsync(context, &actual_vsync);
    if (!have_actual_vsync)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D vsync query failed: %s", SDL_GetError());
        SDL_ClearError();
    }

    *out_window = window;
    *out_context = context;
    int pixel_width = 0;
    int pixel_height = 0;
    SDL_FRect safe_area = {0.0f, 0.0f, (float)context->width, (float)context->height};
    (void)SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
    (void)slayer3d_get_render_context_safe_area(context, &safe_area);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SLAYER3D window created: mode=%s backend=%s requested_vsync=%s actual_vsync=%d size=%dx%d "
                "pixels=%dx%d logical=%dx%d safe_area=%.1f,%.1f %.1fx%.1f logical_policy=%s density=%.2f "
                "high_dpi=%s",
                slayer3d_window_mode_name(local.display_mode), slayer3d_get_backend_name(resolved),
                local.vsync ? "on" : "off", have_actual_vsync ? actual_vsync : -999, local.width, local.height,
                pixel_width, pixel_height, context->width, context->height, safe_area.x, safe_area.y, safe_area.w,
                safe_area.h, local.logical_size_policy == SLAYER3D_LOGICAL_SIZE_EXPAND ? "expand" : "fixed",
                SDL_GetWindowPixelDensity(window), local.high_pixel_density ? "on" : "off");
    return true;
}

bool slayer3d_apply_window_config(SDL_Window **window, slayer3d_render_context **context,
                                  const slayer3d_window_config *config)
{
    slayer3d_window_config local;
    slayer3d_backend requested_backend = SLAYER3D_BACKEND_SOFTWARE;
    slayer3d_backend current_backend = SLAYER3D_BACKEND_SOFTWARE;

    if (window == NULL || context == NULL || *window == NULL || *context == NULL || config == NULL)
        return SDL_InvalidParamError("window/context/config");

    local = *config;
    if (local.display_mode == SLAYER3D_WINDOW_MODE_DEFAULT)
        local.display_mode = SLAYER3D_WINDOW_MODE_WINDOWED;
    if (local.title == NULL)
        local.title = SDL_GetWindowTitle(*window);
    if (local.width <= 0 || local.height <= 0)
        SDL_GetWindowSize(*window, &local.width, &local.height);
    if (local.logical_width <= 0 || local.logical_height <= 0)
    {
        if (!slayer3d_get_render_context_authored_size(*context, &local.logical_width, &local.logical_height))
            return false;
    }

    if (!slayer3d_resolve_backend(local.backend, local.allow_backend_fallback, &requested_backend))
        return false;
    current_backend = slayer3d_get_render_context_backend(*context);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D applying window settings: mode=%s backend=%s vsync=%s",
                slayer3d_window_mode_name(local.display_mode), slayer3d_get_backend_name(requested_backend),
                local.vsync ? "on" : "off");

    if (requested_backend != current_backend)
    {
        SDL_Window *new_window = NULL;
        slayer3d_render_context *new_context = NULL;
        local.backend = requested_backend;
        local.allow_backend_fallback = false;
        if (!slayer3d_create_window(&local, &new_window, &new_context))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D backend switch failed, keeping current window: %s",
                        SDL_GetError());
            return false;
        }

        slayer3d_destroy_window(*window, *context);
        *window = new_window;
        *context = new_context;
    }
    else
    {
        if (!slayer3d_apply_window_mode(*window, local.display_mode))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D display mode apply failed: %s", SDL_GetError());
            return false;
        }
        if (!slayer3d_apply_context_vsync(*context, local.vsync))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D vsync apply failed: %s", SDL_GetError());
            SDL_ClearError();
        }
        if (!slayer3d_apply_logical_size_config(*context, local.logical_width, local.logical_height,
                                                local.logical_size_policy, SDL_LOGICAL_PRESENTATION_LETTERBOX))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D logical canvas apply failed: %s", SDL_GetError());
            return false;
        }
    }

    int actual_vsync = 0;
    bool have_actual_vsync = slayer3d_get_context_vsync(*context, &actual_vsync);
    if (!have_actual_vsync)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D vsync query failed: %s", SDL_GetError());
        SDL_ClearError();
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SLAYER3D window settings applied: requested_mode=%s actual_mode=%s backend=%s requested_vsync=%s "
                "actual_vsync=%d",
                slayer3d_window_mode_name(local.display_mode), slayer3d_actual_window_mode_name(*window),
                slayer3d_get_backend_name(slayer3d_get_render_context_backend(*context)), local.vsync ? "on" : "off",
                have_actual_vsync ? actual_vsync : -999);
    return true;
}

void slayer3d_destroy_window(SDL_Window *window, slayer3d_render_context *context)
{
    SDL_Renderer *renderer = context != NULL ? context->renderer : NULL;

    slayer3d_destroy_render_context(context);

    if (renderer != NULL)
    {
        SDL_DestroyRenderer(renderer);
    }

    if (window != NULL)
    {
        SDL_DestroyWindow(window);
    }
}

bool slayer3d_switch_backend(SDL_Window **window, slayer3d_render_context **context, slayer3d_backend new_backend)
{
    int w, h;
    char title_buf[256];
    slayer3d_window_config wcfg;
    int logical_width = 1280;
    int logical_height = 720;
    slayer3d_logical_size_policy logical_size_policy = SLAYER3D_LOGICAL_SIZE_FIXED;

    if (window == NULL || context == NULL)
    {
        return SDL_InvalidParamError("window/context");
    }

    /* Capture current window properties before destroying. */
    w = 1280;
    h = 720;
    title_buf[0] = '\0';
    if (*window != NULL)
    {
        SDL_GetWindowSize(*window, &w, &h);
        const char *t = SDL_GetWindowTitle(*window);
        if (t != NULL)
            SDL_strlcpy(title_buf, t, sizeof(title_buf));
    }
    if (*context != NULL)
    {
        logical_width = (*context)->authored_width;
        logical_height = (*context)->authored_height;
        logical_size_policy = (*context)->logical_size_policy;
    }

    /* Tear down old context and window. */
    slayer3d_destroy_window(*window, *context);
    *window = NULL;
    *context = NULL;

    /* Create fresh window + context with the new backend. */
    slayer3d_init_window_config(&wcfg);
    wcfg.width = w;
    wcfg.height = h;
    wcfg.logical_width = logical_width;
    wcfg.logical_height = logical_height;
    wcfg.logical_size_policy = logical_size_policy;
    wcfg.title = title_buf[0] ? title_buf : "SLAYER3D";
    wcfg.backend = new_backend;
    wcfg.allow_backend_fallback = false;
    wcfg.resizable = true;

    return slayer3d_create_window(&wcfg, window, context);
}

/* ------------------------------------------------------------------ */
/* Feature queries                                                     */
/* ------------------------------------------------------------------ */

bool slayer3d_is_feature_available(const slayer3d_render_context *context, slayer3d_feature feature)
{
    if (context == NULL)
    {
        return false;
    }

    bool is_gl = (context->backend == SLAYER3D_BACKEND_OPENGL);

    switch (feature)
    {
    case SLAYER3D_FEATURE_BLOOM:
    case SLAYER3D_FEATURE_SSAO:
    case SLAYER3D_FEATURE_SHADOWS:
    case SLAYER3D_FEATURE_IBL:
    case SLAYER3D_FEATURE_POST_PROCESSING:
        return is_gl;
    default:
        return false;
    }
}
