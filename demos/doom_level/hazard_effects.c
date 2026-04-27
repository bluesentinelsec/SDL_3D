/* Hazard particle effects for the doom_level demo. */
#include "hazard_effects.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/image.h"
#include "sdl3d/math.h"

#include "level_data.h"

#define SOFT_PARTICLE_SIZE 32
#define NUKAGE_VAPOR_MAX_PARTICLES 560
#define NUKAGE_MOTE_MAX_PARTICLES 120
#define NUKAGE_PULSE_HZ 3.2f

static float clamp01(float value)
{
    if (value < 0.0f)
    {
        return 0.0f;
    }
    if (value > 1.0f)
    {
        return 1.0f;
    }
    return value;
}

static bool create_soft_particle_texture(sdl3d_texture2d *out_texture)
{
    sdl3d_image image;
    SDL_zero(image);
    image.width = SOFT_PARTICLE_SIZE;
    image.height = SOFT_PARTICLE_SIZE;
    image.pixels = (Uint8 *)SDL_calloc((size_t)image.width * (size_t)image.height * 4u, 1);
    if (image.pixels == NULL)
    {
        return SDL_OutOfMemory();
    }

    for (int y = 0; y < image.height; ++y)
    {
        for (int x = 0; x < image.width; ++x)
        {
            const float fx = (((float)x + 0.5f) / (float)image.width) * 2.0f - 1.0f;
            const float fy = (((float)y + 0.5f) / (float)image.height) * 2.0f - 1.0f;
            const float dist = SDL_sqrtf(fx * fx + fy * fy);
            const float falloff = clamp01(1.0f - dist);
            const float core = falloff * falloff;
            Uint8 *pixel = &image.pixels[((y * image.width) + x) * 4];

            pixel[0] = (Uint8)(70.0f + core * 80.0f);
            pixel[1] = 255;
            pixel[2] = (Uint8)(35.0f + core * 65.0f);
            pixel[3] = dist <= 1.0f ? (Uint8)(core * 255.0f + 0.5f) : 0;
        }
    }

    const bool ok = sdl3d_create_texture_from_image(&image, out_texture);
    SDL_free(image.pixels);
    if (ok)
    {
        sdl3d_set_texture_filter(out_texture, SDL3D_TEXTURE_FILTER_BILINEAR);
        sdl3d_set_texture_wrap(out_texture, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP);
    }
    return ok;
}

static bool sector_bounds(const sdl3d_sector *sector, float *out_min_x, float *out_max_x, float *out_min_z,
                          float *out_max_z)
{
    if (sector == NULL || sector->num_points <= 0)
    {
        return false;
    }

    float min_x = sector->points[0][0];
    float max_x = sector->points[0][0];
    float min_z = sector->points[0][1];
    float max_z = sector->points[0][1];
    for (int i = 1; i < sector->num_points; ++i)
    {
        const float x = sector->points[i][0];
        const float z = sector->points[i][1];
        if (x < min_x)
            min_x = x;
        if (x > max_x)
            max_x = x;
        if (z < min_z)
            min_z = z;
        if (z > max_z)
            max_z = z;
    }

    *out_min_x = min_x;
    *out_max_x = max_x;
    *out_min_z = min_z;
    *out_max_z = max_z;
    return true;
}

bool doom_hazard_particles_init(doom_hazard_particles *particles, const sdl3d_level *level, const sdl3d_sector *sectors)
{
    if (particles == NULL || level == NULL || sectors == NULL)
    {
        return SDL_InvalidParamError("particles");
    }

    SDL_zerop(particles);
    particles->enabled = true;
    if (DOOM_NUKAGE_BASIN_SECTOR < 0 || DOOM_NUKAGE_BASIN_SECTOR >= level->sector_count)
    {
        return SDL_SetError("Nukage basin sector is out of range.");
    }

    const sdl3d_sector *sector = &sectors[DOOM_NUKAGE_BASIN_SECTOR];
    float min_x, max_x, min_z, max_z;
    if (!sector_bounds(sector, &min_x, &max_x, &min_z, &max_z))
    {
        return SDL_SetError("Nukage basin sector has no valid bounds.");
    }

    particles->has_soft_particle_tex = create_soft_particle_texture(&particles->soft_particle_tex);
    if (!particles->has_soft_particle_tex)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Soft particle texture creation failed: %s", SDL_GetError());
        SDL_ClearError();
    }

    const float center_x = (min_x + max_x) * 0.5f;
    const float center_z = (min_z + max_z) * 0.5f;
    const float half_x = (max_x - min_x) * 0.5f;
    const float half_z = (max_z - min_z) * 0.5f;
    const float floor_y = sdl3d_sector_floor_at(sector, center_x, center_z);
    const sdl3d_texture2d *texture = particles->has_soft_particle_tex ? &particles->soft_particle_tex : NULL;

    sdl3d_particle_config vapor;
    SDL_zero(vapor);
    vapor.position = sdl3d_vec3_make(center_x, floor_y + 0.14f, center_z);
    vapor.direction = sdl3d_vec3_make(0.04f, 1.0f, -0.03f);
    vapor.spread = 1.15f;
    vapor.speed_min = 0.35f;
    vapor.speed_max = 1.05f;
    vapor.lifetime_min = 2.4f;
    vapor.lifetime_max = 4.6f;
    vapor.size_start = 0.045f;
    vapor.size_end = 0.16f;
    vapor.color_start = (sdl3d_color){35, 245, 45, 155};
    vapor.color_end = (sdl3d_color){0, 185, 55, 0};
    vapor.gravity = -0.16f;
    vapor.max_particles = NUKAGE_VAPOR_MAX_PARTICLES;
    vapor.emit_rate = 150.0f;
    vapor.shape = SDL3D_PARTICLE_EMITTER_BOX;
    vapor.extents = sdl3d_vec3_make(half_x * 0.88f, 0.05f, half_z * 0.88f);
    vapor.camera_facing = true;
    vapor.depth_test = true;
    vapor.additive_blend = true;
    vapor.texture = texture;
    vapor.random_seed = 0x51D3D00Du;
    particles->nukage_vapor = sdl3d_create_particle_emitter(&vapor);
    if (particles->nukage_vapor == NULL)
    {
        doom_hazard_particles_free(particles);
        return false;
    }

    sdl3d_particle_config motes;
    SDL_zero(motes);
    motes.position = sdl3d_vec3_make(center_x, floor_y + 0.22f, center_z);
    motes.direction = sdl3d_vec3_make(-0.04f, 1.0f, 0.08f);
    motes.spread = 1.65f;
    motes.speed_min = 0.8f;
    motes.speed_max = 2.2f;
    motes.lifetime_min = 0.9f;
    motes.lifetime_max = 1.8f;
    motes.size_start = 0.026f;
    motes.size_end = 0.008f;
    motes.color_start = (sdl3d_color){95, 255, 55, 230};
    motes.color_end = (sdl3d_color){10, 230, 45, 0};
    motes.gravity = -0.08f;
    motes.max_particles = NUKAGE_MOTE_MAX_PARTICLES;
    motes.emit_rate = 38.0f;
    motes.shape = SDL3D_PARTICLE_EMITTER_BOX;
    motes.extents = sdl3d_vec3_make(half_x * 0.82f, 0.05f, half_z * 0.82f);
    motes.camera_facing = true;
    motes.depth_test = true;
    motes.additive_blend = true;
    motes.texture = texture;
    motes.random_seed = 0xA11CE5u;
    particles->nukage_motes = sdl3d_create_particle_emitter(&motes);
    if (particles->nukage_motes == NULL)
    {
        doom_hazard_particles_free(particles);
        return false;
    }

    return true;
}

static void pulse_nukage_emitter(sdl3d_particle_emitter *emitter, float pulse, bool mote)
{
    const sdl3d_particle_config *current = sdl3d_particle_emitter_get_config(emitter);
    if (current == NULL)
    {
        return;
    }

    const float opacity = pulse * pulse;
    sdl3d_particle_config next = *current;
    if (mote)
    {
        const Uint8 alpha = (Uint8)(35.0f + opacity * 210.0f);
        next.color_start = (sdl3d_color){(Uint8)(55.0f + pulse * 55.0f), 255, (Uint8)(45.0f + pulse * 45.0f), alpha};
        next.color_end = (sdl3d_color){0, (Uint8)(180.0f + pulse * 55.0f), 45, 0};
    }
    else
    {
        const Uint8 alpha = (Uint8)(18.0f + opacity * 205.0f);
        next.color_start = (sdl3d_color){(Uint8)(15.0f + pulse * 40.0f), (Uint8)(215.0f + pulse * 40.0f),
                                         (Uint8)(35.0f + pulse * 45.0f), alpha};
        next.color_end = (sdl3d_color){0, (Uint8)(150.0f + pulse * 55.0f), 45, 0};
    }

    if (!sdl3d_particle_emitter_set_config(emitter, &next))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Nukage particle pulse update failed: %s", SDL_GetError());
        SDL_ClearError();
    }
}

void doom_hazard_particles_free(doom_hazard_particles *particles)
{
    if (particles == NULL)
    {
        return;
    }

    sdl3d_destroy_particle_emitter(particles->nukage_vapor);
    sdl3d_destroy_particle_emitter(particles->nukage_motes);
    sdl3d_free_texture(&particles->soft_particle_tex);
    SDL_zerop(particles);
}

void doom_hazard_particles_set_enabled(doom_hazard_particles *particles, bool enabled)
{
    if (particles != NULL)
    {
        particles->enabled = enabled;
    }
}

bool doom_hazard_particles_enabled(const doom_hazard_particles *particles)
{
    return particles != NULL && particles->enabled;
}

void doom_hazard_particles_update(doom_hazard_particles *particles, float dt)
{
    if (particles == NULL || !particles->enabled)
    {
        return;
    }

    particles->pulse_timer += dt * NUKAGE_PULSE_HZ;
    while (particles->pulse_timer >= 1.0f)
    {
        particles->pulse_timer -= 1.0f;
    }

    const float pulse = 0.5f + 0.5f * SDL_sinf(particles->pulse_timer * SDL_PI_F * 2.0f);
    pulse_nukage_emitter(particles->nukage_vapor, pulse, false);
    pulse_nukage_emitter(particles->nukage_motes, pulse, true);

    sdl3d_particle_emitter_update(particles->nukage_vapor, dt);
    sdl3d_particle_emitter_update(particles->nukage_motes, dt);
}

bool doom_hazard_particles_draw(const doom_hazard_particles *particles, sdl3d_render_context *context)
{
    if (particles == NULL || !particles->enabled)
    {
        return true;
    }

    return sdl3d_draw_particles(context, particles->nukage_vapor) &&
           sdl3d_draw_particles(context, particles->nukage_motes);
}
