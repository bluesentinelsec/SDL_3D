/**
 * @file game_data_network.h
 * @brief Public network types for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_NETWORK_H
#define SLAYER3D_GAME_DATA_NETWORK_H

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/network_replication.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Exact byte size of an authored network control message packet.
     *
     * Control messages carry only fixed metadata: magic, version, tick,
     * control-message index, and the schema hash.
     */
#define SLAYER3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE (16U + SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE)

    /** @brief Direction for an authored network replication or control message. */
    typedef enum slayer3d_game_data_network_direction
    {
        /** @brief Invalid or unknown network direction. */
        SLAYER3D_GAME_DATA_NETWORK_DIRECTION_INVALID = 0,
        /** @brief Message flows from the authoritative host to a client. */
        SLAYER3D_GAME_DATA_NETWORK_DIRECTION_HOST_TO_CLIENT = 1,
        /** @brief Message flows from a client to the authoritative host. */
        SLAYER3D_GAME_DATA_NETWORK_DIRECTION_CLIENT_TO_HOST = 2,
        /** @brief Message may flow in either direction. */
        SLAYER3D_GAME_DATA_NETWORK_DIRECTION_BIDIRECTIONAL = 3,
    } slayer3d_game_data_network_direction;

    /** @brief Decoded authored network control message descriptor. */
    typedef struct slayer3d_game_data_network_control
    {
        /** @brief Authored control message name, owned by the runtime. */
        const char *name;
        /** @brief Authored message direction. */
        slayer3d_game_data_network_direction direction;
        /** @brief Signal referenced by the control message, or -1. */
        int signal_id;
        /** @brief Tick carried by the control packet. */
        Uint32 tick;
    } slayer3d_game_data_network_control;

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_GAME_DATA_NETWORK_H */
