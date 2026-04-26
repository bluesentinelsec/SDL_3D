/* Surveillance camera interaction for the doom_level demo. */
#include "surveillance.h"

#include <SDL3/SDL_stdinc.h>

void doom_surveillance_init(doom_surveillance_camera *surveillance, sdl3d_bounding_box button_bounds,
                            sdl3d_camera3d camera)
{
    if (surveillance == NULL)
    {
        return;
    }

    SDL_zerop(surveillance);
    surveillance->button_bounds = button_bounds;
    sdl3d_logic_contact_sensor_init(&surveillance->button_sensor, 0, button_bounds, 0, SDL3D_TRIGGER_LEVEL);
    surveillance->camera = camera;
    surveillance->enabled = true;
}

bool doom_surveillance_update(doom_surveillance_camera *surveillance, sdl3d_vec3 sample_position)
{
    if (surveillance == NULL)
    {
        return false;
    }

    surveillance->button_sensor.enabled = surveillance->enabled;
    surveillance->active =
        sdl3d_logic_contact_sensor_update(&surveillance->button_sensor, NULL, sample_position).active;
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
