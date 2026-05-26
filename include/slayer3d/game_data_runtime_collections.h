/**
 * @file game_data_runtime_collections.h
 * @brief Host-published runtime collections for data-authored menus and UI.
 */

#ifndef SLAYER3D_GAME_DATA_RUNTIME_COLLECTIONS_H
#define SLAYER3D_GAME_DATA_RUNTIME_COLLECTIONS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

    /**
     * @brief Remove every row from a runtime collection.
     *
     * Runtime collections are named, host-populated row sets that authored UI
     * can read through dynamic-list menu sources. Clearing an unknown
     * collection is a successful no-op.
     */
    bool slayer3d_game_data_runtime_collection_clear(slayer3d_game_data_runtime *runtime, const char *collection_name);

    /**
     * @brief Return the number of rows currently published in a runtime collection.
     *
     * Unknown collections have a count of zero.
     */
    int slayer3d_game_data_runtime_collection_count(const slayer3d_game_data_runtime *runtime,
                                                    const char *collection_name);

    /**
     * @brief Publish a string field on one runtime collection row.
     *
     * Rows are zero-based and created on demand. Host systems should publish
     * contiguous rows and then clear the collection before republishing a
     * shorter result set.
     */
    bool slayer3d_game_data_runtime_collection_set_string(slayer3d_game_data_runtime *runtime,
                                                          const char *collection_name, int row_index,
                                                          const char *field_name, const char *value);

    /** @brief Publish an integer field on one runtime collection row. */
    bool slayer3d_game_data_runtime_collection_set_int(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                                       int row_index, const char *field_name, int value);

    /** @brief Publish a floating-point field on one runtime collection row. */
    bool slayer3d_game_data_runtime_collection_set_float(slayer3d_game_data_runtime *runtime,
                                                         const char *collection_name, int row_index,
                                                         const char *field_name, float value);

    /** @brief Publish a boolean field on one runtime collection row. */
    bool slayer3d_game_data_runtime_collection_set_bool(slayer3d_game_data_runtime *runtime,
                                                        const char *collection_name, int row_index,
                                                        const char *field_name, bool value);

#ifdef __cplusplus
}
#endif

#endif
