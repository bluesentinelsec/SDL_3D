#include "slayer3d/effects.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include "slayer3d/drawing3d.h"
#include "slayer3d/math.h"

#include "render_context_internal.h"

#include "gl_renderer.h"
#include "texture_internal.h"

/* ------------------------------------------------------------------ */
/* Particle system                                                     */
/* ------------------------------------------------------------------ */

typedef struct slayer3d_particle
{
    slayer3d_vec3 position;
    slayer3d_vec3 velocity;
    float lifetime;
    float max_lifetime;
    bool alive;
} slayer3d_particle;

struct slayer3d_particle_emitter
{
    slayer3d_particle_config config;
    slayer3d_particle *particles;
    int count;
    float emit_accumulator;
    Uint32 rng_state;
    bool deterministic_rng;
};

static slayer3d_vec3 slayer3d_effects_camera_right(const slayer3d_render_context *context);
static slayer3d_vec3 slayer3d_effects_camera_up(const slayer3d_render_context *context);
static slayer3d_vec3 slayer3d_effects_camera_forward(const slayer3d_render_context *context);

static const char k_soft_smoke_particle_frag[] =
    "in vec2 vTexCoord;\n"
    "in vec4 vColor;\n"
    "uniform vec4 uTint;\n"
    "out vec4 fragColor;\n"
    "float hash(vec2 p) {\n"
    "    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);\n"
    "}\n"
    "float noise(vec2 p) {\n"
    "    vec2 i = floor(p);\n"
    "    vec2 f = fract(p);\n"
    "    vec2 u = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(hash(i + vec2(0.0, 0.0)), hash(i + vec2(1.0, 0.0)), u.x),\n"
    "               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);\n"
    "}\n"
    "void main() {\n"
    "    vec2 centered = vTexCoord * 2.0 - 1.0;\n"
    "    float radius = length(centered);\n"
    "    if (radius >= 1.0) discard;\n"
    "    float radial = 1.0 - smoothstep(0.20, 1.0, radius);\n"
    "    float edge = 1.0 - smoothstep(0.62, 1.0, radius);\n"
    "    float n1 = noise(vTexCoord * 4.0 + vec2(vColor.r * 3.1, vColor.g * 2.7));\n"
    "    float n2 = noise(vTexCoord * 9.0 + vec2(vColor.b * 5.3, vColor.a * 1.7));\n"
    "    float wisps = smoothstep(0.20, 0.95, n1 * 0.65 + n2 * 0.35);\n"
    "    float alpha = vColor.a * uTint.a * radial * edge * mix(0.55, 1.0, wisps);\n"
    "    if (alpha <= 0.01) discard;\n"
    "    vec3 color = vColor.rgb * uTint.rgb * mix(0.72, 1.08, n1);\n"
    "    fragColor = vec4(color, alpha);\n"
    "}\n";

static float slayer3d_effects_absf(float value)
{
    return value < 0.0f ? -value : value;
}

static float slayer3d_effects_maxf(float a, float b)
{
    return a > b ? a : b;
}

static float slayer3d_effects_clampf(float value, float lo, float hi)
{
    if (value < lo)
    {
        return lo;
    }
    if (value > hi)
    {
        return hi;
    }
    return value;
}

static float slayer3d_randf(void)
{
    return (float)SDL_rand(1000000) / 1000000.0f;
}

static Uint32 slayer3d_particle_next_random_u32(slayer3d_particle_emitter *emitter)
{
    if (emitter == NULL || !emitter->deterministic_rng)
    {
        return (Uint32)SDL_rand_bits();
    }

    emitter->rng_state = emitter->rng_state * 1664525u + 1013904223u;
    return emitter->rng_state;
}

static float slayer3d_particle_randf(slayer3d_particle_emitter *emitter)
{
    if (emitter == NULL || !emitter->deterministic_rng)
    {
        return slayer3d_randf();
    }

    return (float)(slayer3d_particle_next_random_u32(emitter) >> 8) * (1.0f / 16777216.0f);
}

static float slayer3d_randf_range(slayer3d_particle_emitter *emitter, float lo, float hi)
{
    return lo + slayer3d_particle_randf(emitter) * (hi - lo);
}

static slayer3d_particle_config slayer3d_particle_config_normalized(const slayer3d_particle_config *config)
{
    slayer3d_particle_config normalized = *config;

    if (normalized.max_particles <= 0)
    {
        normalized.max_particles = 128;
    }
    if (normalized.speed_min > normalized.speed_max)
    {
        const float tmp = normalized.speed_min;
        normalized.speed_min = normalized.speed_max;
        normalized.speed_max = tmp;
    }
    if (normalized.lifetime_min > normalized.lifetime_max)
    {
        const float tmp = normalized.lifetime_min;
        normalized.lifetime_min = normalized.lifetime_max;
        normalized.lifetime_max = tmp;
    }
    normalized.lifetime_min = slayer3d_effects_maxf(normalized.lifetime_min, 0.001f);
    normalized.lifetime_max = slayer3d_effects_maxf(normalized.lifetime_max, normalized.lifetime_min);
    normalized.size_start = slayer3d_effects_maxf(normalized.size_start, 0.0f);
    normalized.size_end = slayer3d_effects_maxf(normalized.size_end, 0.0f);
    normalized.emit_rate = slayer3d_effects_maxf(normalized.emit_rate, 0.0f);
    normalized.spread = slayer3d_effects_clampf(normalized.spread, 0.0f, SDL_PI_F);
    normalized.extents.x = slayer3d_effects_absf(normalized.extents.x);
    normalized.extents.y = slayer3d_effects_absf(normalized.extents.y);
    normalized.extents.z = slayer3d_effects_absf(normalized.extents.z);
    normalized.radius = slayer3d_effects_absf(normalized.radius);
    normalized.emissive_intensity = slayer3d_effects_maxf(normalized.emissive_intensity, 0.0f);
    if (normalized.shape < SLAYER3D_PARTICLE_EMITTER_POINT || normalized.shape > SLAYER3D_PARTICLE_EMITTER_CIRCLE)
    {
        normalized.shape = SLAYER3D_PARTICLE_EMITTER_POINT;
    }
    if (normalized.render_style < SLAYER3D_PARTICLE_RENDER_DEFAULT ||
        normalized.render_style > SLAYER3D_PARTICLE_RENDER_SOFT_SMOKE)
    {
        normalized.render_style = SLAYER3D_PARTICLE_RENDER_DEFAULT;
    }
    return normalized;
}

static bool slayer3d_particle_emitter_apply_config(slayer3d_particle_emitter *emitter,
                                                   const slayer3d_particle_config *config, bool preserve_particles)
{
    slayer3d_particle_config normalized;
    slayer3d_particle *particles = NULL;

    if (emitter == NULL)
    {
        return SDL_InvalidParamError("emitter");
    }
    if (config == NULL)
    {
        return SDL_InvalidParamError("config");
    }

    normalized = slayer3d_particle_config_normalized(config);
    if (normalized.max_particles != emitter->config.max_particles)
    {
        particles = (slayer3d_particle *)SDL_calloc((size_t)normalized.max_particles, sizeof(*particles));
        if (particles == NULL)
        {
            return SDL_OutOfMemory();
        }

        if (preserve_particles && emitter->particles != NULL)
        {
            const int copy_count =
                emitter->count < normalized.max_particles ? emitter->count : normalized.max_particles;
            SDL_memcpy(particles, emitter->particles, (size_t)copy_count * sizeof(*particles));
            emitter->count = copy_count;
        }
        else
        {
            emitter->count = 0;
        }

        SDL_free(emitter->particles);
        emitter->particles = particles;
    }
    else if (!preserve_particles)
    {
        SDL_memset(emitter->particles, 0, (size_t)normalized.max_particles * sizeof(*emitter->particles));
        emitter->count = 0;
    }

    {
        const bool keep_rng_state = preserve_particles && normalized.random_seed == emitter->config.random_seed;
        emitter->config = normalized;
        if (!keep_rng_state)
        {
            emitter->deterministic_rng = normalized.random_seed != 0;
            emitter->rng_state = normalized.random_seed != 0 ? normalized.random_seed : 0;
        }
    }
    if (!preserve_particles)
    {
        emitter->emit_accumulator = 0.0f;
    }
    return true;
}

slayer3d_particle_emitter *slayer3d_create_particle_emitter(const slayer3d_particle_config *config)
{
    slayer3d_particle_emitter *em;

    if (config == NULL)
    {
        SDL_InvalidParamError("config");
        return NULL;
    }

    em = (slayer3d_particle_emitter *)SDL_calloc(1, sizeof(*em));
    if (em == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

    if (!slayer3d_particle_emitter_apply_config(em, config, false))
    {
        SDL_free(em);
        return NULL;
    }

    return em;
}

void slayer3d_destroy_particle_emitter(slayer3d_particle_emitter *emitter)
{
    if (emitter == NULL)
    {
        return;
    }
    SDL_free(emitter->particles);
    SDL_free(emitter);
}

void slayer3d_particle_emitter_set_position(slayer3d_particle_emitter *emitter, slayer3d_vec3 position)
{
    if (emitter != NULL)
    {
        emitter->config.position = position;
    }
}

bool slayer3d_particle_emitter_set_config(slayer3d_particle_emitter *emitter, const slayer3d_particle_config *config)
{
    return slayer3d_particle_emitter_apply_config(emitter, config, true);
}

const slayer3d_particle_config *slayer3d_particle_emitter_get_config(const slayer3d_particle_emitter *emitter)
{
    return emitter != NULL ? &emitter->config : NULL;
}

void slayer3d_particle_emitter_clear(slayer3d_particle_emitter *emitter)
{
    if (emitter == NULL)
    {
        return;
    }

    SDL_memset(emitter->particles, 0, (size_t)emitter->config.max_particles * sizeof(*emitter->particles));
    emitter->count = 0;
    emitter->emit_accumulator = 0.0f;
}

static slayer3d_vec3 slayer3d_particle_spawn_position(slayer3d_particle_emitter *em)
{
    slayer3d_vec3 position = em->config.position;

    switch (em->config.shape)
    {
    case SLAYER3D_PARTICLE_EMITTER_BOX:
        position.x += slayer3d_randf_range(em, -em->config.extents.x, em->config.extents.x);
        position.y += slayer3d_randf_range(em, -em->config.extents.y, em->config.extents.y);
        position.z += slayer3d_randf_range(em, -em->config.extents.z, em->config.extents.z);
        break;
    case SLAYER3D_PARTICLE_EMITTER_CIRCLE: {
        const float angle = slayer3d_particle_randf(em) * SDL_PI_F * 2.0f;
        const float radius = SDL_sqrtf(slayer3d_particle_randf(em)) * em->config.radius;
        position.x += SDL_cosf(angle) * radius;
        position.z += SDL_sinf(angle) * radius;
        break;
    }
    case SLAYER3D_PARTICLE_EMITTER_POINT:
    default:
        break;
    }

    return position;
}

static void slayer3d_emit_one(slayer3d_particle_emitter *em)
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
        slayer3d_particle *p = &em->particles[i];
        float speed = slayer3d_randf_range(em, em->config.speed_min, em->config.speed_max);
        float theta = slayer3d_particle_randf(em) * SDL_PI_F * 2.0f;
        float phi = slayer3d_particle_randf(em) * em->config.spread;
        float sp = SDL_sinf(phi);

        slayer3d_vec3 dir = slayer3d_vec3_normalize(em->config.direction);
        if (slayer3d_vec3_length_squared(dir) <= 0.000001f)
        {
            dir = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
        }
        /* Build a random direction within the cone. */
        slayer3d_vec3 up = (SDL_fabsf(dir.y) < 0.99f) ? slayer3d_vec3_make(0, 1, 0) : slayer3d_vec3_make(1, 0, 0);
        slayer3d_vec3 right = slayer3d_vec3_normalize(slayer3d_vec3_cross(up, dir));
        slayer3d_vec3 fwd = slayer3d_vec3_cross(dir, right);

        slayer3d_vec3 offset;
        offset.x = dir.x * SDL_cosf(phi) + right.x * sp * SDL_cosf(theta) + fwd.x * sp * SDL_sinf(theta);
        offset.y = dir.y * SDL_cosf(phi) + right.y * sp * SDL_cosf(theta) + fwd.y * sp * SDL_sinf(theta);
        offset.z = dir.z * SDL_cosf(phi) + right.z * sp * SDL_cosf(theta) + fwd.z * sp * SDL_sinf(theta);

        p->position = slayer3d_particle_spawn_position(em);
        p->velocity = slayer3d_vec3_scale(offset, speed);
        p->lifetime = 0.0f;
        p->max_lifetime = slayer3d_randf_range(em, em->config.lifetime_min, em->config.lifetime_max);
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

void slayer3d_particle_emitter_emit(slayer3d_particle_emitter *emitter, int count)
{
    if (emitter == NULL)
    {
        return;
    }
    for (int i = 0; i < count; ++i)
    {
        slayer3d_emit_one(emitter);
    }
}

void slayer3d_particle_emitter_update(slayer3d_particle_emitter *emitter, float delta_time)
{
    if (emitter == NULL || delta_time <= 0.0f)
    {
        return;
    }

    /* Update existing particles. */
    for (int i = 0; i < emitter->count; ++i)
    {
        slayer3d_particle *p = &emitter->particles[i];
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
            slayer3d_emit_one(emitter);
            emitter->emit_accumulator -= 1.0f;
        }
    }
}

int slayer3d_particle_emitter_get_count(const slayer3d_particle_emitter *emitter)
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

int slayer3d_particle_emitter_snapshot(const slayer3d_particle_emitter *emitter,
                                       slayer3d_particle_snapshot *out_particles, int max_particles)
{
    int live = 0;
    int copied = 0;

    if (emitter == NULL)
    {
        return 0;
    }

    for (int i = 0; i < emitter->count; ++i)
    {
        const slayer3d_particle *p = &emitter->particles[i];
        if (!p->alive)
        {
            continue;
        }

        if (out_particles != NULL && copied < max_particles)
        {
            out_particles[copied].position = p->position;
            out_particles[copied].velocity = p->velocity;
            out_particles[copied].lifetime = p->lifetime;
            out_particles[copied].max_lifetime = p->max_lifetime;
            out_particles[copied].alive = p->alive;
            ++copied;
        }
        ++live;
    }

    return live;
}

static Uint8 slayer3d_particle_lerp_u8(Uint8 a, Uint8 b, float t)
{
    return (Uint8)((float)a + ((float)b - (float)a) * t + 0.5f);
}

static Uint8 slayer3d_particle_scale_u8(Uint8 value, float scale)
{
    const float scaled = (float)value * scale;
    if (scaled <= 0.0f)
    {
        return 0;
    }
    if (scaled >= 255.0f)
    {
        return 255;
    }
    return (Uint8)(scaled + 0.5f);
}

static bool slayer3d_draw_particle_quad(slayer3d_render_context *context, const slayer3d_particle_config *config,
                                        const slayer3d_particle *particle, float size, slayer3d_color color)
{
    slayer3d_vec3 right = slayer3d_effects_camera_right(context);
    slayer3d_vec3 up;
    const float half = size * 0.5f;
    float positions[12];
    float uvs[8] = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f};
    float colors[16];
    unsigned int indices[12] = {0, 1, 2, 2, 1, 3, 2, 1, 0, 3, 1, 2};
    slayer3d_mesh mesh;

    if (size <= 0.0f || color.a == 0)
    {
        return true;
    }

    if (config->render_style == SLAYER3D_PARTICLE_RENDER_SOFT_SMOKE)
    {
        const slayer3d_vec2 billboard_size = {size, size};
        const slayer3d_vec2 anchor = {0.5f, 0.5f};
        return slayer3d_draw_billboard_shader_ex(context, config->texture, particle->position, billboard_size, anchor,
                                                 config->camera_facing ? SLAYER3D_BILLBOARD_SPHERICAL
                                                                       : SLAYER3D_BILLBOARD_UPRIGHT,
                                                 color, false, NULL, k_soft_smoke_particle_frag);
    }

    if (config->camera_facing)
    {
        up = slayer3d_effects_camera_up(context);
    }
    else
    {
        const slayer3d_vec3 world_up = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
        slayer3d_vec3 forward = slayer3d_effects_camera_forward(context);
        forward.y = 0.0f;
        if (slayer3d_vec3_length_squared(forward) <= 0.000001f)
        {
            forward = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
        }
        else
        {
            forward = slayer3d_vec3_normalize(forward);
        }
        right = slayer3d_vec3_normalize(slayer3d_vec3_cross(forward, world_up));
        if (slayer3d_vec3_length_squared(right) <= 0.000001f)
        {
            right = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
        }
        up = world_up;
    }

    {
        const slayer3d_vec3 scaled_right = slayer3d_vec3_scale(right, half);
        const slayer3d_vec3 scaled_up = slayer3d_vec3_scale(up, half);
        const slayer3d_vec3 verts[4] = {
            slayer3d_vec3_sub(slayer3d_vec3_sub(particle->position, scaled_right), scaled_up),
            slayer3d_vec3_add(slayer3d_vec3_sub(particle->position, scaled_right), scaled_up),
            slayer3d_vec3_sub(slayer3d_vec3_add(particle->position, scaled_right), scaled_up),
            slayer3d_vec3_add(slayer3d_vec3_add(particle->position, scaled_right), scaled_up),
        };

        for (int i = 0; i < 4; ++i)
        {
            positions[i * 3 + 0] = verts[i].x;
            positions[i * 3 + 1] = verts[i].y;
            positions[i * 3 + 2] = verts[i].z;
            colors[i * 4 + 0] = (float)color.r / 255.0f;
            colors[i * 4 + 1] = (float)color.g / 255.0f;
            colors[i * 4 + 2] = (float)color.b / 255.0f;
            colors[i * 4 + 3] = (float)color.a / 255.0f;
        }
    }

    SDL_zero(mesh);
    mesh.positions = positions;
    mesh.uvs = uvs;
    mesh.colors = colors;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 12;

    return slayer3d_draw_mesh(context, &mesh, config->texture, (slayer3d_color){255, 255, 255, 255});
}

bool slayer3d_draw_particles(slayer3d_render_context *context, const slayer3d_particle_emitter *emitter)
{
    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (emitter == NULL)
    {
        return true;
    }

    bool ok = true;
    for (int i = 0; i < emitter->count; ++i)
    {
        const slayer3d_particle *p = &emitter->particles[i];
        float t, size;
        slayer3d_color color;

        if (!p->alive)
        {
            continue;
        }

        t = p->lifetime / p->max_lifetime;
        t = slayer3d_effects_clampf(t, 0.0f, 1.0f);
        size = emitter->config.size_start + (emitter->config.size_end - emitter->config.size_start) * t;

        color.r = slayer3d_particle_lerp_u8(emitter->config.color_start.r, emitter->config.color_end.r, t);
        color.g = slayer3d_particle_lerp_u8(emitter->config.color_start.g, emitter->config.color_end.g, t);
        color.b = slayer3d_particle_lerp_u8(emitter->config.color_start.b, emitter->config.color_end.b, t);
        color.a = slayer3d_particle_lerp_u8(emitter->config.color_start.a, emitter->config.color_end.a, t);
        if (emitter->config.emissive_intensity > 0.0f)
        {
            const float scale = 1.0f + emitter->config.emissive_intensity;
            color.r = slayer3d_particle_scale_u8(color.r, scale);
            color.g = slayer3d_particle_scale_u8(color.g, scale);
            color.b = slayer3d_particle_scale_u8(color.b, scale);
        }

        ok = slayer3d_draw_particle_quad(context, &emitter->config, p, size, color) && ok;
    }

    return ok;
}

/* ------------------------------------------------------------------ */
/* Skybox                                                              */
/* ------------------------------------------------------------------ */

bool slayer3d_draw_skybox_gradient(slayer3d_render_context *context, slayer3d_color top_color,
                                   slayer3d_color bottom_color)
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
        float y0 = SDL_cosf(phi0) * radius;
        float y1 = SDL_cosf(phi1) * radius;
        float r0 = SDL_sinf(phi0) * radius;
        float r1 = SDL_sinf(phi1) * radius;

        /* Interpolate color from top to bottom. */
        slayer3d_color c0, c1;
        c0.r = (Uint8)((float)top_color.r + (float)(bottom_color.r - top_color.r) * t0);
        c0.g = (Uint8)((float)top_color.g + (float)(bottom_color.g - top_color.g) * t0);
        c0.b = (Uint8)((float)top_color.b + (float)(bottom_color.b - top_color.b) * t0);
        c0.a = 255;
        c1.r = (Uint8)((float)top_color.r + (float)(bottom_color.r - top_color.r) * t1);
        c1.g = (Uint8)((float)top_color.g + (float)(bottom_color.g - top_color.g) * t1);
        c1.b = (Uint8)((float)top_color.b + (float)(bottom_color.b - top_color.b) * t1);
        c1.a = 255;

        /* Use the average color for this ring band. */
        slayer3d_color avg;
        avg.r = (Uint8)(((int)c0.r + (int)c1.r) / 2);
        avg.g = (Uint8)(((int)c0.g + (int)c1.g) / 2);
        avg.b = (Uint8)(((int)c0.b + (int)c1.b) / 2);
        avg.a = 255;

        for (int s = 0; s < slices; ++s)
        {
            float theta0 = (float)s / (float)slices * 6.28318f;
            float theta1 = (float)(s + 1) / (float)slices * 6.28318f;

            slayer3d_vec3 v00 = slayer3d_vec3_make(SDL_cosf(theta0) * r0, y0, SDL_sinf(theta0) * r0);
            slayer3d_vec3 v10 = slayer3d_vec3_make(SDL_cosf(theta1) * r0, y0, SDL_sinf(theta1) * r0);
            slayer3d_vec3 v01 = slayer3d_vec3_make(SDL_cosf(theta0) * r1, y1, SDL_sinf(theta0) * r1);
            slayer3d_vec3 v11 = slayer3d_vec3_make(SDL_cosf(theta1) * r1, y1, SDL_sinf(theta1) * r1);

            /* Inward-facing triangles (we're inside the sphere). */
            slayer3d_draw_triangle_3d(context, v00, v01, v10, avg);
            slayer3d_draw_triangle_3d(context, v10, v01, v11, avg);
        }
    }

    return true;
}

static slayer3d_vec3 slayer3d_effects_camera_position(const slayer3d_render_context *context)
{
    const slayer3d_mat4 v = context->view;
    return slayer3d_vec3_make(-(v.m[0] * v.m[12] + v.m[1] * v.m[13] + v.m[2] * v.m[14]),
                              -(v.m[4] * v.m[12] + v.m[5] * v.m[13] + v.m[6] * v.m[14]),
                              -(v.m[8] * v.m[12] + v.m[9] * v.m[13] + v.m[10] * v.m[14]));
}

static slayer3d_vec3 slayer3d_effects_camera_right(const slayer3d_render_context *context)
{
    return slayer3d_vec3_normalize(slayer3d_vec3_make(context->view.m[0], context->view.m[4], context->view.m[8]));
}

static slayer3d_vec3 slayer3d_effects_camera_up(const slayer3d_render_context *context)
{
    return slayer3d_vec3_normalize(slayer3d_vec3_make(context->view.m[1], context->view.m[5], context->view.m[9]));
}

static slayer3d_vec3 slayer3d_effects_camera_forward(const slayer3d_render_context *context)
{
    return slayer3d_vec3_normalize(slayer3d_vec3_make(-context->view.m[2], -context->view.m[6], -context->view.m[10]));
}

static Uint8 slayer3d_effects_float_to_byte(float value)
{
    if (value <= 0.0f)
    {
        return 0;
    }
    if (value >= 1.0f)
    {
        return 255;
    }
    return (Uint8)(value * 255.0f + 0.5f);
}

static void slayer3d_effects_sample_textured_skybox(const slayer3d_skybox_textured *skybox, slayer3d_vec3 direction,
                                                    float *out_r, float *out_g, float *out_b, float *out_a)
{
    const float abs_x = SDL_fabsf(direction.x);
    const float abs_y = SDL_fabsf(direction.y);
    const float abs_z = SDL_fabsf(direction.z);
    const slayer3d_texture2d *face = NULL;
    float major_axis = 1.0f;
    float u = 0.5f;
    float v = 0.5f;

    if (abs_z >= abs_x && abs_z >= abs_y)
    {
        major_axis = abs_z;
        if (direction.z >= 0.0f)
        {
            face = skybox->pos_x;
            u = (direction.x / major_axis + 1.0f) * 0.5f;
            v = (1.0f - direction.y / major_axis) * 0.5f;
        }
        else
        {
            face = skybox->neg_x;
            u = (1.0f - direction.x / major_axis) * 0.5f;
            v = (1.0f - direction.y / major_axis) * 0.5f;
        }
    }
    else if (abs_x >= abs_y)
    {
        major_axis = abs_x;
        if (direction.x >= 0.0f)
        {
            face = skybox->neg_z;
            u = (1.0f - direction.z / major_axis) * 0.5f;
            v = (1.0f - direction.y / major_axis) * 0.5f;
        }
        else
        {
            face = skybox->pos_z;
            u = (direction.z / major_axis + 1.0f) * 0.5f;
            v = (1.0f - direction.y / major_axis) * 0.5f;
        }
    }
    else
    {
        major_axis = abs_y;
        if (direction.y >= 0.0f)
        {
            face = skybox->pos_y;
            u = (direction.z / major_axis + 1.0f) * 0.5f;
            v = (1.0f - direction.x / major_axis) * 0.5f;
        }
        else
        {
            face = skybox->neg_y;
            u = (1.0f - direction.z / major_axis) * 0.5f;
            v = (1.0f - direction.x / major_axis) * 0.5f;
        }
    }

    slayer3d_texture_sample_rgba(face, u, v, 0.0f, out_r, out_g, out_b, out_a);
}

static bool slayer3d_draw_skybox_textured_software(slayer3d_render_context *context,
                                                   const slayer3d_skybox_textured *skybox)
{
    slayer3d_framebuffer framebuffer = slayer3d_framebuffer_from_context(context);
    const slayer3d_vec3 right = slayer3d_effects_camera_right(context);
    const slayer3d_vec3 up = slayer3d_effects_camera_up(context);
    const slayer3d_vec3 forward = slayer3d_effects_camera_forward(context);
    const int min_x = framebuffer.scissor_enabled ? framebuffer.scissor_rect.x : 0;
    const int min_y = framebuffer.scissor_enabled ? framebuffer.scissor_rect.y : 0;
    const int max_x =
        framebuffer.scissor_enabled ? (framebuffer.scissor_rect.x + framebuffer.scissor_rect.w) : framebuffer.width;
    const int max_y =
        framebuffer.scissor_enabled ? (framebuffer.scissor_rect.y + framebuffer.scissor_rect.h) : framebuffer.height;

    if (framebuffer.color_pixels == NULL || framebuffer.width <= 0 || framebuffer.height <= 0)
    {
        return SDL_SetError("Software skybox requires a valid framebuffer.");
    }

    if (context->projection.m[15] == 1.0f)
    {
        float r, g, b, a;
        slayer3d_effects_sample_textured_skybox(skybox, forward, &r, &g, &b, &a);
        for (int y = min_y; y < max_y; ++y)
        {
            for (int x = min_x; x < max_x; ++x)
            {
                const int index = (y * framebuffer.width) + x;
                Uint8 *pixel = &framebuffer.color_pixels[index * 4];
                if (framebuffer.depth_pixels != NULL && framebuffer.depth_pixels[index] < 1.0f)
                {
                    continue;
                }
                pixel[0] = slayer3d_effects_float_to_byte(r);
                pixel[1] = slayer3d_effects_float_to_byte(g);
                pixel[2] = slayer3d_effects_float_to_byte(b);
                pixel[3] = slayer3d_effects_float_to_byte(a);
                if (framebuffer.depth_pixels != NULL)
                {
                    framebuffer.depth_pixels[index] = 1.0f;
                }
            }
        }
        return true;
    }

    {
        const float inv_proj_x = 1.0f / context->projection.m[0];
        const float inv_proj_y = 1.0f / context->projection.m[5];
        const float ndc_x_start = ((2.0f * 0.5f) / (float)framebuffer.width) - 1.0f;
        const float ndc_x_step = 2.0f / (float)framebuffer.width;
        const slayer3d_vec3 x_step = slayer3d_vec3_scale(right, ndc_x_step * inv_proj_x);

        for (int y = min_y; y < max_y; ++y)
        {
            const float ndc_y = 1.0f - ((2.0f * ((float)y + 0.5f)) / (float)framebuffer.height);
            const slayer3d_vec3 row_base = slayer3d_vec3_add(forward, slayer3d_vec3_scale(up, ndc_y * inv_proj_y));
            slayer3d_vec3 ray = slayer3d_vec3_add(row_base, slayer3d_vec3_scale(right, ndc_x_start * inv_proj_x));

            for (int x = min_x; x < max_x; ++x)
            {
                const int index = (y * framebuffer.width) + x;
                float r, g, b, a;
                Uint8 *pixel = &framebuffer.color_pixels[index * 4];

                if (framebuffer.depth_pixels != NULL && framebuffer.depth_pixels[index] < 1.0f)
                {
                    ray = slayer3d_vec3_add(ray, x_step);
                    continue;
                }

                slayer3d_effects_sample_textured_skybox(skybox, slayer3d_vec3_normalize(ray), &r, &g, &b, &a);
                pixel[0] = slayer3d_effects_float_to_byte(r);
                pixel[1] = slayer3d_effects_float_to_byte(g);
                pixel[2] = slayer3d_effects_float_to_byte(b);
                pixel[3] = slayer3d_effects_float_to_byte(a);
                if (framebuffer.depth_pixels != NULL)
                {
                    framebuffer.depth_pixels[index] = 1.0f;
                }

                ray = slayer3d_vec3_add(ray, x_step);
            }
        }
    }

    return true;
}

static bool slayer3d_draw_skybox_face(slayer3d_render_context *context, const slayer3d_texture2d *texture,
                                      slayer3d_vec3 v0, slayer3d_vec3 v1, slayer3d_vec3 v2, slayer3d_vec3 v3)
{
    float positions[] = {v0.x, v0.y, v0.z, v1.x, v1.y, v1.z, v2.x, v2.y, v2.z, v3.x, v3.y, v3.z};
    float uvs[] = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f};
    unsigned int indices[] = {0, 1, 2, 2, 1, 3};
    slayer3d_mesh mesh;

    SDL_zero(mesh);
    mesh.positions = positions;
    mesh.uvs = uvs;
    mesh.vertex_count = 4;
    mesh.indices = indices;
    mesh.index_count = 6;
    return slayer3d_draw_mesh(context, &mesh, texture, (slayer3d_color){255, 255, 255, 255});
}

bool slayer3d_draw_skybox_textured(slayer3d_render_context *context, const slayer3d_skybox_textured *skybox)
{
    slayer3d_vec3 c;
    float s;

    if (context == NULL)
    {
        return SDL_InvalidParamError("context");
    }
    if (skybox == NULL)
    {
        return SDL_InvalidParamError("skybox");
    }
    if (skybox->pos_x == NULL || skybox->neg_x == NULL || skybox->pos_y == NULL || skybox->neg_y == NULL ||
        skybox->pos_z == NULL || skybox->neg_z == NULL)
    {
        return SDL_SetError("Textured skybox requires all six face textures.");
    }

    if (context->backend == SLAYER3D_BACKEND_SOFTWARE)
    {
        return slayer3d_draw_skybox_textured_software(context, skybox);
    }

    c = slayer3d_effects_camera_position(context);
    s = skybox->size > 1.0f ? skybox->size : 400.0f;

    /* Inward-facing cube centered on the camera. */
    return slayer3d_draw_skybox_face(context, skybox->pos_x, slayer3d_vec3_make(c.x - s, c.y - s, c.z + s),
                                     slayer3d_vec3_make(c.x - s, c.y + s, c.z + s),
                                     slayer3d_vec3_make(c.x + s, c.y - s, c.z + s),
                                     slayer3d_vec3_make(c.x + s, c.y + s, c.z + s)) &&
           slayer3d_draw_skybox_face(context, skybox->neg_x, slayer3d_vec3_make(c.x + s, c.y - s, c.z - s),
                                     slayer3d_vec3_make(c.x + s, c.y + s, c.z - s),
                                     slayer3d_vec3_make(c.x - s, c.y - s, c.z - s),
                                     slayer3d_vec3_make(c.x - s, c.y + s, c.z - s)) &&
           slayer3d_draw_skybox_face(context, skybox->neg_z, slayer3d_vec3_make(c.x + s, c.y - s, c.z + s),
                                     slayer3d_vec3_make(c.x + s, c.y + s, c.z + s),
                                     slayer3d_vec3_make(c.x + s, c.y - s, c.z - s),
                                     slayer3d_vec3_make(c.x + s, c.y + s, c.z - s)) &&
           slayer3d_draw_skybox_face(context, skybox->pos_z, slayer3d_vec3_make(c.x - s, c.y - s, c.z - s),
                                     slayer3d_vec3_make(c.x - s, c.y + s, c.z - s),
                                     slayer3d_vec3_make(c.x - s, c.y - s, c.z + s),
                                     slayer3d_vec3_make(c.x - s, c.y + s, c.z + s)) &&
           slayer3d_draw_skybox_face(context, skybox->pos_y, slayer3d_vec3_make(c.x - s, c.y + s, c.z - s),
                                     slayer3d_vec3_make(c.x + s, c.y + s, c.z - s),
                                     slayer3d_vec3_make(c.x - s, c.y + s, c.z + s),
                                     slayer3d_vec3_make(c.x + s, c.y + s, c.z + s)) &&
           slayer3d_draw_skybox_face(context, skybox->neg_y, slayer3d_vec3_make(c.x - s, c.y - s, c.z + s),
                                     slayer3d_vec3_make(c.x + s, c.y - s, c.z + s),
                                     slayer3d_vec3_make(c.x - s, c.y - s, c.z - s),
                                     slayer3d_vec3_make(c.x + s, c.y - s, c.z - s));
}

/* ------------------------------------------------------------------ */
/* Post-process effects                                                */
/* ------------------------------------------------------------------ */

static float slayer3d_luminance(Uint8 r, Uint8 g, Uint8 b)
{
    return 0.2126f * (float)r / 255.0f + 0.7152f * (float)g / 255.0f + 0.0722f * (float)b / 255.0f;
}

static Uint8 slayer3d_clamp_byte(float v)
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

bool slayer3d_apply_post_process(slayer3d_render_context *context, const slayer3d_post_process_config *config)
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
    if (config->effects == SLAYER3D_POST_NONE)
    {
        return true;
    }

    /* GL backend: apply post-processing as a fullscreen shader pass. */
    if (context->color_buffer == NULL)
    {
        if (context->gl != NULL)
        {
            slayer3d_gl_post_process(context->gl, config->effects, config->bloom_threshold, config->bloom_intensity,
                                     config->vignette_intensity, config->color_grade_contrast,
                                     config->color_grade_brightness, config->color_grade_saturation);
        }
        return true;
    }

    w = context->width;
    h = context->height;
    total = w * h;
    pixels = context->color_buffer;

    /* Bloom: simple box-blur of bright pixels added back. */
    if (config->effects & SLAYER3D_POST_BLOOM)
    {
        Uint8 *bloom_buf = (Uint8 *)SDL_calloc((size_t)total * 4, 1);
        if (bloom_buf != NULL)
        {
            /* Extract bright pixels. */
            for (int i = 0; i < total; ++i)
            {
                Uint8 *px = &pixels[i * 4];
                float lum = slayer3d_luminance(px[0], px[1], px[2]);
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
                        pixels[idx + 0] = slayer3d_clamp_byte((float)pixels[idx + 0] + (float)sr / 25.0f * intensity);
                        pixels[idx + 1] = slayer3d_clamp_byte((float)pixels[idx + 1] + (float)sg / 25.0f * intensity);
                        pixels[idx + 2] = slayer3d_clamp_byte((float)pixels[idx + 2] + (float)sb / 25.0f * intensity);
                    }
                }
            }
            SDL_free(bloom_buf);
        }
    }

    /* Vignette: darken corners based on distance from center. */
    if (config->effects & SLAYER3D_POST_VIGNETTE)
    {
        float cx = (float)w * 0.5f;
        float cy = (float)h * 0.5f;
        float max_dist = SDL_sqrtf(cx * cx + cy * cy);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                float dx = (float)x - cx;
                float dy = (float)y - cy;
                float dist = SDL_sqrtf(dx * dx + dy * dy) / max_dist;
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
    if (config->effects & SLAYER3D_POST_COLOR_GRADE)
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

            px[0] = slayer3d_clamp_byte(r * 255.0f);
            px[1] = slayer3d_clamp_byte(g * 255.0f);
            px[2] = slayer3d_clamp_byte(b * 255.0f);
        }
    }

    return true;
}
