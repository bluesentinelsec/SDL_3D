/* Surveillance camera interaction for the doom_level demo. */
#ifndef DOOM_SURVEILLANCE_H
#define DOOM_SURVEILLANCE_H

#include <stdbool.h>

#include "sdl3d/camera.h"
#include "sdl3d/logic.h"
#include "sdl3d/types.h"

typedef struct doom_surveillance_camera
{
    sdl3d_bounding_box button_bounds;
    sdl3d_logic_contact_sensor enter_sensor;
    sdl3d_logic_contact_sensor exit_sensor;
    sdl3d_camera3d camera;
    bool enabled;
    bool active;
} doom_surveillance_camera;

void doom_surveillance_init(doom_surveillance_camera *surveillance, sdl3d_bounding_box button_bounds,
                            sdl3d_camera3d camera, int enter_signal_id, int exit_signal_id);
bool doom_surveillance_update(doom_surveillance_camera *surveillance, sdl3d_logic_world *world,
                              sdl3d_vec3 sample_position);
bool doom_surveillance_is_active(const doom_surveillance_camera *surveillance);
const sdl3d_camera3d *doom_surveillance_active_camera(const doom_surveillance_camera *surveillance);

#endif
