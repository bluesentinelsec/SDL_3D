/* Surveillance camera interaction for the doom_level demo. */
#include "surveillance.h"

#include <SDL3/SDL_stdinc.h>

static bool point_inside_bounds(sdl3d_bounding_box bounds, sdl3d_vec3 point)
{
    return point.x >= bounds.min.x && point.x <= bounds.max.x && point.y >= bounds.min.y && point.y <= bounds.max.y &&
           point.z >= bounds.min.z && point.z <= bounds.max.z;
}

void doom_surveillance_init(doom_surveillance_camera *surveillance, sdl3d_bounding_box button_bounds,
                            sdl3d_camera3d camera)
{
    if (surveillance == NULL)
    {
        return;
    }

    SDL_zerop(surveillance);
    surveillance->button_bounds = button_bounds;
    surveillance->camera = camera;
    surveillance->enabled = true;
}

bool doom_surveillance_update(doom_surveillance_camera *surveillance, sdl3d_vec3 sample_position)
{
    if (surveillance == NULL)
    {
        return false;
    }

    surveillance->active = surveillance->enabled && point_inside_bounds(surveillance->button_bounds, sample_position);
    return surveillance->active;
}

bool doom_surveillance_is_active(const doom_surveillance_camera *surveillance)
{
    return surveillance != NULL && surveillance->active;
}

const sdl3d_camera3d *doom_surveillance_active_camera(const doom_surveillance_camera *surveillance)
{
    return doom_surveillance_is_active(surveillance) ? &surveillance->camera : NULL;
}
