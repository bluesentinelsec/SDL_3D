/**
 * @file game_data_defaults.h
 * @brief Public defaults for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_DEFAULTS_H
#define SLAYER3D_GAME_DATA_DEFAULTS_H

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Maximum bytes stored for one dynamic menu row label or value, including room for terminator. */
#define SLAYER3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY 256U

    /** @brief Default vertical field-of-view for authored perspective, chase, and FPS cameras. */
#define SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES 90.0f

    /** @brief Default world unit label. SLAYER3D convention is one authored world unit equals one meter. */
#define SLAYER3D_GAME_DATA_DEFAULT_WORLD_UNITS "meters"

    /** @brief Default scale from authored world units to real-world meters. */
#define SLAYER3D_GAME_DATA_DEFAULT_METERS_PER_UNIT 1.0f

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_GAME_DATA_DEFAULTS_H */
