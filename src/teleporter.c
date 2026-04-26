#include "sdl3d/teleporter.h"

#include <SDL3/SDL_stdinc.h>

#include "sdl3d/math.h"

#define SDL3D_TELEPORT_DEFAULT_COOLDOWN 0.25f

static bool point_inside_bounds(sdl3d_bounding_box bounds, sdl3d_vec3 point)
{
    return point.x >= bounds.min.x && point.x <= bounds.max.x && point.y >= bounds.min.y && point.y <= bounds.max.y &&
           point.z >= bounds.min.z && point.z <= bounds.max.z;
}

static int teleporter_signal_id(const sdl3d_teleporter *teleporter)
{
    return teleporter->signal_id != 0 ? teleporter->signal_id : SDL3D_SIGNAL_TELEPORT;
}

static bool emit_teleport_signal(const sdl3d_teleporter *teleporter, sdl3d_signal_bus *bus)
{
    sdl3d_properties *payload;

    if (bus == NULL)
    {
        return false;
    }

    payload = sdl3d_properties_create();
    if (payload == NULL)
    {
        return false;
    }

    sdl3d_properties_set_int(payload, "teleporter_id", teleporter->teleporter_id);
    sdl3d_properties_set_vec3(payload, "destination", teleporter->destination.position);
    sdl3d_properties_set_float(payload, "destination_yaw", teleporter->destination.yaw);
    sdl3d_properties_set_float(payload, "destination_pitch", teleporter->destination.pitch);
    sdl3d_properties_set_bool(payload, "use_yaw", teleporter->destination.use_yaw);
    sdl3d_properties_set_bool(payload, "use_pitch", teleporter->destination.use_pitch);

    sdl3d_signal_emit(bus, teleporter_signal_id(teleporter), payload);
    sdl3d_properties_destroy(payload);
    return true;
}

void sdl3d_teleporter_init(sdl3d_teleporter *teleporter, int teleporter_id, sdl3d_bounding_box source_bounds,
                           sdl3d_teleport_destination destination)
{
    if (teleporter == NULL)
    {
        return;
    }

    SDL_zerop(teleporter);
    teleporter->source_bounds = source_bounds;
    teleporter->destination = destination;
    teleporter->teleporter_id = teleporter_id;
    teleporter->signal_id = SDL3D_SIGNAL_TELEPORT;
    teleporter->cooldown_seconds = SDL3D_TELEPORT_DEFAULT_COOLDOWN;
    teleporter->enabled = true;
}

bool sdl3d_teleporter_update(sdl3d_teleporter *teleporter, sdl3d_vec3 sample_position, float dt, sdl3d_signal_bus *bus)
{
    bool inside;
    bool emitted = false;

    if (teleporter == NULL)
    {
        return false;
    }

    if (dt > 0.0f && teleporter->cooldown_remaining > 0.0f)
    {
        teleporter->cooldown_remaining -= dt;
        if (teleporter->cooldown_remaining < 0.0f)
        {
            teleporter->cooldown_remaining = 0.0f;
        }
    }

    inside = teleporter->enabled && point_inside_bounds(teleporter->source_bounds, sample_position);
    if (inside && !teleporter->was_inside && teleporter->cooldown_remaining <= 0.0f)
    {
        emitted = emit_teleport_signal(teleporter, bus);
        if (emitted && teleporter->cooldown_seconds > 0.0f)
        {
            teleporter->cooldown_remaining = teleporter->cooldown_seconds;
        }
    }

    teleporter->was_inside = inside;
    return emitted;
}

void sdl3d_teleporter_reset(sdl3d_teleporter *teleporter)
{
    if (teleporter == NULL)
    {
        return;
    }

    teleporter->was_inside = false;
    teleporter->cooldown_remaining = 0.0f;
}

bool sdl3d_teleport_destination_from_payload(const sdl3d_properties *payload,
                                             sdl3d_teleport_destination *out_destination)
{
    if (payload == NULL || out_destination == NULL || !sdl3d_properties_has(payload, "destination"))
    {
        return false;
    }

    out_destination->position = sdl3d_properties_get_vec3(payload, "destination", sdl3d_vec3_make(0.0f, 0.0f, 0.0f));
    out_destination->yaw = sdl3d_properties_get_float(payload, "destination_yaw", 0.0f);
    out_destination->pitch = sdl3d_properties_get_float(payload, "destination_pitch", 0.0f);
    out_destination->use_yaw = sdl3d_properties_get_bool(payload, "use_yaw", false);
    out_destination->use_pitch = sdl3d_properties_get_bool(payload, "use_pitch", false);
    return true;
}
