#ifndef SLAYER3D_GAME_DATA_NETWORK_TYPES_H
#define SLAYER3D_GAME_DATA_NETWORK_TYPES_H

#include <stdbool.h>

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/network.h"

typedef struct runtime_direct_connect_session
{
    char *name;
    slayer3d_network_session *session;
} runtime_direct_connect_session;

typedef struct runtime_host_session
{
    char *name;
    slayer3d_network_session *session;
} runtime_host_session;

typedef struct runtime_discovery_session
{
    char *name;
    slayer3d_network_discovery_session *session;
} runtime_discovery_session;

typedef struct network_diagnostic_runtime_state
{
    char *name;
    Uint64 last_log_ms;
    bool logged;
} network_diagnostic_runtime_state;

#endif
