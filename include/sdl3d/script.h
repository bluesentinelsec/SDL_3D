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
     * @brief Opaque reference to a Lua value owned by a script engine.
     *
     * References remain valid until explicitly released with
     * sdl3d_script_engine_unref() or until the script engine is destroyed.
     */
    typedef int sdl3d_script_ref;

    /** @brief Invalid script reference value. */
#define SDL3D_SCRIPT_REF_INVALID 0

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

    /**
     * @brief Load a Lua file as a named module table.
     *
     * The file is executed and must return a table. That table is stored in the
     * Lua registry and also published under @p module_name in the global
     * environment for optional interactive/debug access. Prefer this API for
     * game data scripts because it avoids large projects sharing one global
     * namespace.
     *
     * @param engine Script engine.
     * @param path Lua file path to load.
     * @param module_name Stable module name, such as "pong.rules".
     * @param out_module_ref Receives a registry reference to the returned table.
     * @param error_buffer Optional human-readable error output.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the file loaded, returned a table, and was registered.
     */
    bool sdl3d_script_engine_load_module_file(sdl3d_script_engine *engine, const char *path, const char *module_name,
                                              sdl3d_script_ref *out_module_ref, char *error_buffer,
                                              int error_buffer_size);

    /**
     * @brief Resolve a function from a loaded module table.
     *
     * @p function_name may name a direct table field, such as "serve", or a
     * dotted nested field, such as "rules.serve". The returned function
     * reference is pre-resolved and can be called repeatedly without string path
     * lookup.
     *
     * @param engine Script engine.
     * @param module_ref Registry reference returned by
     * sdl3d_script_engine_load_module_file().
     * @param function_name Function path inside the module table.
     * @param out_function_ref Receives a registry reference to the function.
     * @param error_buffer Optional human-readable error output.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the function was found and referenced.
     */
    bool sdl3d_script_engine_ref_module_function(sdl3d_script_engine *engine, sdl3d_script_ref module_ref,
                                                 const char *function_name, sdl3d_script_ref *out_function_ref,
                                                 char *error_buffer, int error_buffer_size);

    /**
     * @brief Release a script registry reference.
     *
     * Safe to call with SDL3D_SCRIPT_REF_INVALID.
     */
    void sdl3d_script_engine_unref(sdl3d_script_engine *engine, sdl3d_script_ref ref);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_SCRIPT_H */
