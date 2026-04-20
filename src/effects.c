#include "sdl3d/effects.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include <math.h>

#include "sdl3d/drawing3d.h"
#include "sdl3d/math.h"
#include "sdl3d/shapes.h"

#include "render_context_internal.h"

/* ------------------------------------------------------------------ */
/* Particle system                                                     */
/* ------------------------------------------------------------------ */

typedef struct sdl3d_particle
{
    sdl3d_vec3 position;
    sdl3d_vec3 velocity;
    float lifetime;
    float max_lifetime;
    bool alive;
} sdl3d_particle;

struct sdl3d_particle_emitter
{
    sdl3d_particle_config config;
    sdl3d_particle *particles;
    int count;
    float emit_accumulator;
};

static float sdl3d_randf(void)
{
    return (float)SDL_rand(1000000) / 1000000.0f;
}

static float sdl3d_randf_range(float lo, float hi)
{
    return lo + sdl3d_randf() * (hi - lo);
}

sdl3d_particle_emitter *sdl3d_create_particle_emitter(const sdl3d_particle_config *config)
{
    sdl3d_particle_emitter *em;
    int max;

    if (config == NULL)
    {
        SDL_InvalidParamError("config");
        return NULL;
    }

    em = (sdl3d_particle_emitter *)SDL_calloc(1, sizeof(*em));
    if (em == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

    em->config = *config;
    max = config->max_particles > 0 ? config->max_particles : 128;
    em->particles = (sdl3d_particle *)SDL_calloc((size_t)max, sizeof(sdl3d_particle));
    if (em->particles == NULL)
    {
        SDL_free(em);
        SDL_OutOfMemory();
        return NULL;
    }

    em->config.max_particles = max;
    return em;
}

void sdl3d_destroy_particle_emitter(sdl3d_particle_emitter *emitter)
{
    if (emitter == NULL)
    {
        return;
    }
    SDL_free(emitter->particles);
    SDL_free(emitter);
}

void sdl3d_particle_emitter_set_position(sdl3d_particle_emitter *emitter, sdl3d_vec3 position)
{
    if (emitter != NULL)
    {
        emitter->config.position = position;
    }
}

static void sdl3d_emit_one(sdl3d_particle_emitter *em)
{
    int i;
    for (i = 0; i < em->config.max_particles; ++i)
    {
        if (!em->particles[i].alive)
        {
            break;
        }
    }
    if (i >= em->config.max_particles)
    {
        return;
    }

    {
        sdl3d_particle *p = &em->particles[i];
        float speed = sdl3d_randf_range(em->config.speed_min, em->config.speed_max);
        float theta = sdl3d_randf() * 6.28318f;
        float phi = sdl3d_randf() * em->config.spread;
        float sp = sinf(phi);

        sdl3d_vec3 dir = sdl3d_vec3_normalize(em->config.direction);
        /* Build a random direction within the cone. */
        sdl3d_vec3 up = (fabsf(dir.y) < 0.99f) ? sdl3d_vec3_make(0, 1, 0) : sdl3d_vec3_make(1, 0, 0);
        sdl3d_vec3 right = sdl3d_vec3_normalize(sdl3d_vec3_cross(up, dir));
        sdl3d_vec3 fwd = sdl3d_vec3_cross(dir, right);

        sdl3d_vec3 offset;
        offset.x = dir.x * cosf(phi) + right.x * sp * cosf(theta) + fwd.x * sp * sinf(theta);
        offset.y = dir.y * cosf(phi) + right.y * sp * cosf(theta) + fwd.y * sp * sinf(theta);
        offset.z = dir.z * cosf(phi) + right.z * sp * cosf(theta) + fwd.z * sp * sinf(theta);

        p->position = em->config.position;
        p->velocity = sdl3d_vec3_scale(offset, speed);
        p->lifetime = 0.0f;
        p->max_lifetime = sdl3d_randf_range(em->config.lifetime_min, em->config.lifetime_max);
        if (p->max_lifetime < 0.001f)
        {
            p->max_lifetime = 0.001f;
        }
        p->alive = true;
        if (i >= em->count)
        {
            em->count = i + 1;
        }
    }
}

void sdl3d_particle_emitter_emit(sdl3d_particle_emitter *emitter, int count)
{
    if (emitter == NULL)
    {
        return;
    }
    for (int i = 0; i < count; ++i)
    {
        sdl3d_emit_one(emitter);
    }
}

void sdl3d_particle_emitter_update(sdl3d_particle_emitter *emitter, float delta_time)
{
    if (emitter == NULL || delta_time <= 0.0f)
    {
        return;
    }

    /* Update existing particles. */
    for (int i = 0; i < emitter->count; ++i)
    {
        sdl3d_particle *p = &emitter->particles[i];
        if (!p->alive)
        {
            continue;
        }
        p->lifetime += delta_time;
        if (p->lifetime >= p->max_lifetime)
        {
            p->alive = false;
            continue;
        }
        p->velocity.y -= emitter->config.gravity * delta_time;
        p->position.x += p->velocity.x * delta_time;
        p->position.y += p->velocity.y * delta_time;
        p->position.z += p->velocity.z * delta_time;
    }

    /* Auto-emit based on rate. */
    if (emitter->config.emit_rate > 0.0f)
    {
        emitter->emit_accumulator += delta_time * emitter->config.emit_rate;
        while (emitter->emit_accumulator >= 1.0f)
        {
            sdl3d_emit_one(emitter);
            emitter->emit_accumulator -= 1.0f;
        }
    }
}

int sdl3d_particle_emitter_get_count(const sdl3d_particle_emitter *emitter)
{
    int live = 0;
    if (emitter == NULL)
    {
        return 0;
    }
    for (int i = 0; i < emitter->count; ++i)
    {
        if (emitter->particles[i].alive)
        {
            ++live;
        }
    }
    return live;
}

bool sdl3d_draw_particles(sdl3d_render_context *context, const sdl3d_particle_emitter *emitter)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (emitter == NULL)
    {
        return true;
    }

    for (int i = 0; i < emitter->count; ++i)
    {
        const sdl3d_particle *p = &emitter->particles[i];
        float t, size;
        sdl3d_color color;

        if (!p->alive)
        {
            continue;
        }

        t = p->lifetime / p->max_lifetime;
        size = emitter->config.size_start + (emitter->config.size_end - emitter->config.size_start) * t;

        color.r = (Uint8)((float)emitter->config.color_start.r +
                          (float)(emitter->config.color_end.r - emitter->config.color_start.r) * t);
        color.g = (Uint8)((float)emitter->config.color_start.g +
                          (float)(emitter->config.color_end.g - emitter->config.color_start.g) * t);
        color.b = (Uint8)((float)emitter->config.color_start.b +
                          (float)(emitter->config.color_end.b - emitter->config.color_start.b) * t);
        color.a = (Uint8)((float)emitter->config.color_start.a +
                          (float)(emitter->config.color_end.a - emitter->config.color_start.a) * t);

        /* Draw as a small cube billboard (cheap, visible from all angles). */
        sdl3d_draw_cube(context, p->position, sdl3d_vec3_make(size, size, size), color);
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Skybox                                                              */
/* ------------------------------------------------------------------ */

bool sdl3d_draw_skybox_gradient(sdl3d_render_context *context, sdl3d_color top_color, sdl3d_color bottom_color)
{
    const int rings = 8;
    const int slices = 12;
    const float radius = 500.0f;

    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }

    /* Draw a large sphere centered at the camera with interpolated colors. */
    for (int r = 0; r < rings; ++r)
    {
        float t0 = (float)r / (float)rings;
        float t1 = (float)(r + 1) / (float)rings;
        float phi0 = t0 * 3.14159265f;
        float phi1 = t1 * 3.14159265f;
        float y0 = cosf(phi0) * radius;
        float y1 = cosf(phi1) * radius;
        float r0 = sinf(phi0) * radius;
        float r1 = sinf(phi1) * radius;

        /* Interpolate color from top to bottom. */
        sdl3d_color c0, c1;
        c0.r = (Uint8)((float)top_color.r + (float)(bottom_color.r - top_color.r) * t0);
        c0.g = (Uint8)((float)top_color.g + (float)(bottom_color.g - top_color.g) * t0);
        c0.b = (Uint8)((float)top_color.b + (float)(bottom_color.b - top_color.b) * t0);
        c0.a = 255;
        c1.r = (Uint8)((float)top_color.r + (float)(bottom_color.r - top_color.r) * t1);
        c1.g = (Uint8)((float)top_color.g + (float)(bottom_color.g - top_color.g) * t1);
        c1.b = (Uint8)((float)top_color.b + (float)(bottom_color.b - top_color.b) * t1);
        c1.a = 255;

        /* Use the average color for this ring band. */
        sdl3d_color avg;
        avg.r = (Uint8)(((int)c0.r + (int)c1.r) / 2);
        avg.g = (Uint8)(((int)c0.g + (int)c1.g) / 2);
        avg.b = (Uint8)(((int)c0.b + (int)c1.b) / 2);
        avg.a = 255;

        for (int s = 0; s < slices; ++s)
        {
            float theta0 = (float)s / (float)slices * 6.28318f;
            float theta1 = (float)(s + 1) / (float)slices * 6.28318f;

            sdl3d_vec3 v00 = sdl3d_vec3_make(cosf(theta0) * r0, y0, sinf(theta0) * r0);
            sdl3d_vec3 v10 = sdl3d_vec3_make(cosf(theta1) * r0, y0, sinf(theta1) * r0);
            sdl3d_vec3 v01 = sdl3d_vec3_make(cosf(theta0) * r1, y1, sinf(theta0) * r1);
            sdl3d_vec3 v11 = sdl3d_vec3_make(cosf(theta1) * r1, y1, sinf(theta1) * r1);

            /* Inward-facing triangles (we're inside the sphere). */
            sdl3d_draw_triangle_3d(context, v00, v01, v10, avg);
            sdl3d_draw_triangle_3d(context, v10, v01, v11, avg);
        }
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Post-process effects                                                */
/* ------------------------------------------------------------------ */

static float sdl3d_luminance(Uint8 r, Uint8 g, Uint8 b)
{
    return 0.2126f * (float)r / 255.0f + 0.7152f * (float)g / 255.0f + 0.0722f * (float)b / 255.0f;
}

static Uint8 sdl3d_clamp_byte(float v)
{
    if (v < 0.0f)
    {
        return 0;
    }
    if (v > 255.0f)
    {
        return 255;
    }
    return (Uint8)(v + 0.5f);
}

bool sdl3d_apply_post_process(sdl3d_render_context *context, const sdl3d_post_process_config *config)
{
    int w, h, total;
    Uint8 *pixels;

    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (config == NULL)
    {
        return SDL_InvalidParamError("config");
    }
    if (config->effects == SDL3D_POST_NONE)
    {
        return true;
    }

    w = context->width;
    h = context->height;
    total = w * h;
    pixels = context->color_buffer;

    /* Bloom: simple box-blur of bright pixels added back. */
    if (config->effects & SDL3D_POST_BLOOM)
    {
        Uint8 *bloom_buf = (Uint8 *)SDL_calloc((size_t)total * 4, 1);
        if (bloom_buf != NULL)
        {
            /* Extract bright pixels. */
            for (int i = 0; i < total; ++i)
            {
                Uint8 *px = &pixels[i * 4];
                float lum = sdl3d_luminance(px[0], px[1], px[2]);
                if (lum > config->bloom_threshold)
                {
                    bloom_buf[i * 4 + 0] = px[0];
                    bloom_buf[i * 4 + 1] = px[1];
                    bloom_buf[i * 4 + 2] = px[2];
                }
            }

            /* Simple 5x5 box blur + add back. */
            for (int y = 2; y < h - 2; ++y)
            {
                for (int x = 2; x < w - 2; ++x)
                {
                    int sr = 0, sg = 0, sb = 0;
                    for (int dy = -2; dy <= 2; ++dy)
                    {
                        for (int dx = -2; dx <= 2; ++dx)
                        {
                            int idx = ((y + dy) * w + (x + dx)) * 4;
                            sr += bloom_buf[idx + 0];
                            sg += bloom_buf[idx + 1];
                            sb += bloom_buf[idx + 2];
                        }
                    }
                    {
                        int idx = (y * w + x) * 4;
                        float intensity = config->bloom_intensity;
                        pixels[idx + 0] = sdl3d_clamp_byte((float)pixels[idx + 0] + (float)sr / 25.0f * intensity);
                        pixels[idx + 1] = sdl3d_clamp_byte((float)pixels[idx + 1] + (float)sg / 25.0f * intensity);
                        pixels[idx + 2] = sdl3d_clamp_byte((float)pixels[idx + 2] + (float)sb / 25.0f * intensity);
                    }
                }
            }
            SDL_free(bloom_buf);
        }
    }

    /* Vignette: darken corners based on distance from center. */
    if (config->effects & SDL3D_POST_VIGNETTE)
    {
        float cx = (float)w * 0.5f;
        float cy = (float)h * 0.5f;
        float max_dist = sqrtf(cx * cx + cy * cy);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                float dx = (float)x - cx;
                float dy = (float)y - cy;
                float dist = sqrtf(dx * dx + dy * dy) / max_dist;
                float factor = 1.0f - dist * dist * config->vignette_intensity;
                if (factor < 0.0f)
                {
                    factor = 0.0f;
                }
                {
                    int idx = (y * w + x) * 4;
                    pixels[idx + 0] = (Uint8)((float)pixels[idx + 0] * factor);
                    pixels[idx + 1] = (Uint8)((float)pixels[idx + 1] * factor);
                    pixels[idx + 2] = (Uint8)((float)pixels[idx + 2] * factor);
                }
            }
        }
    }

    /* Color grading: contrast, brightness, saturation. */
    if (config->effects & SDL3D_POST_COLOR_GRADE)
    {
        for (int i = 0; i < total; ++i)
        {
            Uint8 *px = &pixels[i * 4];
            float r = (float)px[0] / 255.0f;
            float g = (float)px[1] / 255.0f;
            float b = (float)px[2] / 255.0f;

            /* Contrast. */
            r = (r - 0.5f) * config->color_grade_contrast + 0.5f;
            g = (g - 0.5f) * config->color_grade_contrast + 0.5f;
            b = (b - 0.5f) * config->color_grade_contrast + 0.5f;

            /* Brightness. */
            r += config->color_grade_brightness;
            g += config->color_grade_brightness;
            b += config->color_grade_brightness;

            /* Saturation. */
            {
                float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                r = lum + (r - lum) * config->color_grade_saturation;
                g = lum + (g - lum) * config->color_grade_saturation;
                b = lum + (b - lum) * config->color_grade_saturation;
            }

            px[0] = sdl3d_clamp_byte(r * 255.0f);
            px[1] = sdl3d_clamp_byte(g * 255.0f);
            px[2] = sdl3d_clamp_byte(b * 255.0f);
        }
    }

    return true;
}
