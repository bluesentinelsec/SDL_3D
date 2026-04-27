/* Surveillance camera interaction for the doom_level demo. */
#include "surveillance.h"

#include <SDL3/SDL_stdinc.h>

void doom_surveillance_init(doom_surveillance_camera *surveillance, sdl3d_bounding_box button_bounds,
                            sdl3d_camera3d camera, int enter_signal_id, int exit_signal_id)
{
    if (surveillance == NULL)
    {
        return;
    }

    SDL_zerop(surveillance);
    surveillance->button_bounds = button_bounds;
    sdl3d_logic_contact_sensor_init(&surveillance->enter_sensor, 0, button_bounds, enter_signal_id,
                                    SDL3D_TRIGGER_EDGE_ENTER);
    sdl3d_logic_contact_sensor_init(&surveillance->exit_sensor, 0, button_bounds, exit_signal_id,
                                    SDL3D_TRIGGER_EDGE_EXIT);
    surveillance->camera = camera;
    surveillance->enabled = true;
}

bool doom_surveillance_update(doom_surveillance_camera *surveillance, sdl3d_logic_world *world,
                              sdl3d_vec3 sample_position)
{
    if (surveillance == NULL)
    {
        return false;
    }

    surveillance->enter_sensor.enabled = surveillance->enabled;
    surveillance->exit_sensor.enabled = surveillance->enabled;
    sdl3d_logic_contact_sensor_update(&surveillance->enter_sensor, world, sample_position);
    sdl3d_logic_contact_sensor_update(&surveillance->exit_sensor, world, sample_position);
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
