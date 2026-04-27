/**
 * @file script.h
 * @brief Embedded Lua scripting runtime.
 *
 * SDL3D scripts are intended for game-specific behavior that should not require
 * recompiling the engine or demo executable. The engine owns the Lua state,
 * loads scripts from game data, and exposes a small, deterministic API for
 * manipulating authored entities and emitting gameplay effects.
 */

#ifndef SDL3D_SCRIPT_H
#define SDL3D_SCRIPT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque Lua script engine. */
    typedef struct sdl3d_script_engine sdl3d_script_engine;

    /**
     * @brief Create a Lua script engine.
     *
     * The engine is isolated from other SDL3D systems until a caller registers
     * domain-specific APIs. Lua is statically linked into SDL3D.
     *
     * @return A new script engine, or NULL on allocation failure.
     */
    sdl3d_script_engine *sdl3d_script_engine_create(void);

    /**
     * @brief Destroy a script engine and its Lua state.
     *
     * Safe to call with NULL.
     */
    void sdl3d_script_engine_destroy(sdl3d_script_engine *engine);

    /**
     * @brief Load and execute a Lua source file.
     *
     * @param engine Script engine.
     * @param path File path to load.
     * @param error_buffer Optional human-readable error output.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the file loaded and executed successfully.
     */
    bool sdl3d_script_engine_load_file(sdl3d_script_engine *engine, const char *path, char *error_buffer,
                                       int error_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_SCRIPT_H */
