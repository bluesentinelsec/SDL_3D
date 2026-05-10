/**
 * @file script.h
 * @brief Embedded Lua scripting runtime.
 *
 * SLAYER3D scripts are intended for game-specific behavior that should not require
 * recompiling the engine or demo executable. The engine owns the Lua state,
 * loads scripts from game data, and exposes a small, deterministic API for
 * manipulating authored entities and emitting gameplay effects.
 */

#ifndef SLAYER3D_SCRIPT_H
#define SLAYER3D_SCRIPT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque Lua script engine. */
    typedef struct slayer3d_script_engine slayer3d_script_engine;

    /**
     * @brief Opaque reference to a Lua value owned by a script engine.
     *
     * References remain valid until explicitly released with
     * slayer3d_script_engine_unref() or until the script engine is destroyed.
     */
    typedef int slayer3d_script_ref;

    /** @brief Invalid script reference value. */
#define SLAYER3D_SCRIPT_REF_INVALID 0

    /**
     * @brief Create a Lua script engine.
     *
     * The engine is isolated from other SLAYER3D systems until a caller registers
     * domain-specific APIs. Lua is statically linked into SLAYER3D.
     *
     * @return A new script engine, or NULL on allocation failure.
     */
    slayer3d_script_engine *slayer3d_script_engine_create(void);

    /**
     * @brief Destroy a script engine and its Lua state.
     *
     * Safe to call with NULL.
     */
    void slayer3d_script_engine_destroy(slayer3d_script_engine *engine);

    /**
     * @brief Load and execute a Lua source file.
     *
     * @param engine Script engine.
     * @param path File path to load.
     * @param error_buffer Optional human-readable error output.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the file loaded and executed successfully.
     */
    bool slayer3d_script_engine_load_file(slayer3d_script_engine *engine, const char *path, char *error_buffer,
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
    bool slayer3d_script_engine_load_module_file(slayer3d_script_engine *engine, const char *path,
                                                 const char *module_name, slayer3d_script_ref *out_module_ref,
                                                 char *error_buffer, int error_buffer_size);

    /**
     * @brief Load Lua source bytes as a named module table.
     *
     * This is equivalent to slayer3d_script_engine_load_module_file(), but the
     * caller supplies already-resolved bytes and a chunk name for diagnostics.
     * Use this when loading scripts through an asset resolver, packed archive,
     * or embedded resource.
     *
     * @param engine Script engine.
     * @param source Lua source bytes.
     * @param source_size Number of bytes in @p source.
     * @param chunk_name Human-readable source name for Lua error messages.
     * @param module_name Stable module name, such as "pong.rules".
     * @param out_module_ref Receives a registry reference to the returned table.
     * @param error_buffer Optional human-readable error output.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the source loaded, returned a table, and was registered.
     */
    bool slayer3d_script_engine_load_module_buffer(slayer3d_script_engine *engine, const void *source,
                                                   size_t source_size, const char *chunk_name, const char *module_name,
                                                   slayer3d_script_ref *out_module_ref, char *error_buffer,
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
     * slayer3d_script_engine_load_module_file().
     * @param function_name Function path inside the module table.
     * @param out_function_ref Receives a registry reference to the function.
     * @param error_buffer Optional human-readable error output.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the function was found and referenced.
     */
    bool slayer3d_script_engine_ref_module_function(slayer3d_script_engine *engine, slayer3d_script_ref module_ref,
                                                    const char *function_name, slayer3d_script_ref *out_function_ref,
                                                    char *error_buffer, int error_buffer_size);

    /**
     * @brief Release a script registry reference.
     *
     * Safe to call with SLAYER3D_SCRIPT_REF_INVALID.
     */
    void slayer3d_script_engine_unref(slayer3d_script_engine *engine, slayer3d_script_ref ref);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_SCRIPT_H */
