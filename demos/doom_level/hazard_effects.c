/* Hazard particle effects for the doom_level demo. */
#include "hazard_effects.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/image.h"
#include "sdl3d/math.h"

#include "level_data.h"

#define SOFT_PARTICLE_SIZE 32
#define NUKAGE_VAPOR_MAX_PARTICLES 900
#define NUKAGE_MOTE_MAX_PARTICLES 220

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

            pixel[0] = (Uint8)(190.0f + core * 65.0f);
            pixel[1] = 255;
            pixel[2] = (Uint8)(35.0f + core * 95.0f);
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
    vapor.position = sdl3d_vec3_make(center_x, floor_y + 0.18f, center_z);
    vapor.direction = sdl3d_vec3_make(0.08f, 1.0f, -0.04f);
    vapor.spread = 1.25f;
    vapor.speed_min = 0.18f;
    vapor.speed_max = 0.75f;
    vapor.lifetime_min = 1.4f;
    vapor.lifetime_max = 3.0f;
    vapor.size_start = 0.10f;
    vapor.size_end = 0.32f;
    vapor.color_start = (sdl3d_color){120, 255, 40, 210};
    vapor.color_end = (sdl3d_color){25, 210, 70, 0};
    vapor.gravity = -0.08f;
    vapor.max_particles = NUKAGE_VAPOR_MAX_PARTICLES;
    vapor.emit_rate = 260.0f;
    vapor.shape = SDL3D_PARTICLE_EMITTER_BOX;
    vapor.extents = sdl3d_vec3_make(half_x * 0.88f, 0.05f, half_z * 0.88f);
    vapor.emissive_intensity = 1.1f;
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
    motes.position = sdl3d_vec3_make(center_x, floor_y + 0.28f, center_z);
    motes.direction = sdl3d_vec3_make(-0.05f, 1.0f, 0.1f);
    motes.spread = 1.8f;
    motes.speed_min = 0.45f;
    motes.speed_max = 1.8f;
    motes.lifetime_min = 0.45f;
    motes.lifetime_max = 1.2f;
    motes.size_start = 0.055f;
    motes.size_end = 0.015f;
    motes.color_start = (sdl3d_color){245, 255, 80, 255};
    motes.color_end = (sdl3d_color){80, 255, 50, 0};
    motes.gravity = 0.04f;
    motes.max_particles = NUKAGE_MOTE_MAX_PARTICLES;
    motes.emit_rate = 80.0f;
    motes.shape = SDL3D_PARTICLE_EMITTER_BOX;
    motes.extents = sdl3d_vec3_make(half_x * 0.82f, 0.05f, half_z * 0.82f);
    motes.emissive_intensity = 2.2f;
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

void doom_hazard_particles_update(doom_hazard_particles *particles, float dt)
{
    if (particles == NULL)
    {
        return;
    }

    sdl3d_particle_emitter_update(particles->nukage_vapor, dt);
    sdl3d_particle_emitter_update(particles->nukage_motes, dt);
}

bool doom_hazard_particles_draw(const doom_hazard_particles *particles, sdl3d_render_context *context)
{
    if (particles == NULL)
    {
        return true;
    }

    return sdl3d_draw_particles(context, particles->nukage_vapor) &&
           sdl3d_draw_particles(context, particles->nukage_motes);
}
