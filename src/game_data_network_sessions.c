/* Runtime-owned network direct-connect, host, and discovery session helpers. */

#include "game_data_internal.h"

static const char *game_data_network_state_name(slayer3d_network_state state)
{
    switch (state)
    {
    case SLAYER3D_NETWORK_STATE_DISCONNECTED:
        return "disconnected";
    case SLAYER3D_NETWORK_STATE_CONNECTING:
        return "connecting";
    case SLAYER3D_NETWORK_STATE_WAITING:
        return "waiting";
    case SLAYER3D_NETWORK_STATE_CONNECTED:
        return "connected";
    case SLAYER3D_NETWORK_STATE_REJECTED:
        return "rejected";
    case SLAYER3D_NETWORK_STATE_TIMED_OUT:
        return "timed_out";
    case SLAYER3D_NETWORK_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static runtime_direct_connect_session *find_direct_connect_session(slayer3d_game_data_runtime *runtime,
                                                                   const char *session_name)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->direct_connect_session_count; ++i)
    {
        if (SDL_strcmp(runtime->direct_connect_sessions[i].name, session_name) == 0)
            return &runtime->direct_connect_sessions[i];
    }
    return NULL;
}

static runtime_direct_connect_session *get_or_create_direct_connect_session(slayer3d_game_data_runtime *runtime,
                                                                            const char *session_name)
{
    runtime_direct_connect_session *existing = find_direct_connect_session(runtime, session_name);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;

    if (runtime->direct_connect_session_count >= runtime->direct_connect_session_capacity)
    {
        const int next_capacity =
            runtime->direct_connect_session_capacity > 0 ? runtime->direct_connect_session_capacity * 2 : 2;
        runtime_direct_connect_session *next = (runtime_direct_connect_session *)SDL_realloc(
            runtime->direct_connect_sessions, (size_t)next_capacity * sizeof(*runtime->direct_connect_sessions));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->direct_connect_session_capacity, 0,
                   (size_t)(next_capacity - runtime->direct_connect_session_capacity) *
                       sizeof(*runtime->direct_connect_sessions));
        runtime->direct_connect_sessions = next;
        runtime->direct_connect_session_capacity = next_capacity;
    }

    runtime_direct_connect_session *entry = &runtime->direct_connect_sessions[runtime->direct_connect_session_count];
    SDL_zero(*entry);
    entry->name = SDL_strdup(session_name);
    if (entry->name == NULL)
        return NULL;
    ++runtime->direct_connect_session_count;
    return entry;
}

static void direct_connect_publish_status_entry(slayer3d_game_data_runtime *runtime,
                                                const runtime_direct_connect_session *entry, const char *status_key,
                                                const char *state_key, const char *connected_key,
                                                const char *fallback_status)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const slayer3d_network_state state = entry != NULL && entry->session != NULL
                                             ? slayer3d_network_session_state(entry->session)
                                             : SLAYER3D_NETWORK_STATE_DISCONNECTED;
    const char *status =
        entry != NULL && entry->session != NULL ? slayer3d_network_session_status(entry->session) : NULL;
    if (status == NULL || status[0] == '\0')
        status = fallback_status != NULL ? fallback_status : game_data_network_state_name(state);

    if (status_key != NULL && status_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, status_key, status);
    if (state_key != NULL && state_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, state_key, game_data_network_state_name(state));
    if (connected_key != NULL && connected_key[0] != '\0')
        slayer3d_properties_set_bool(runtime->scene_state, connected_key, state == SLAYER3D_NETWORK_STATE_CONNECTED);
}

static void direct_connect_publish_manual_status(slayer3d_game_data_runtime *runtime, const char *status_key,
                                                 const char *state_key, const char *connected_key, const char *status,
                                                 const char *state, bool connected)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    if (status_key != NULL && status_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, status_key, status != NULL ? status : "");
    if (state_key != NULL && state_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, state_key, state != NULL ? state : "unknown");
    if (connected_key != NULL && connected_key[0] != '\0')
        slayer3d_properties_set_bool(runtime->scene_state, connected_key, connected);
}

slayer3d_network_session *slayer3d_game_data_get_network_direct_connect_session(slayer3d_game_data_runtime *runtime,
                                                                                const char *session_name)
{
    runtime_direct_connect_session *entry = find_direct_connect_session(runtime, session_name);
    return entry != NULL ? entry->session : NULL;
}

bool slayer3d_game_data_network_direct_connect_publish_status(slayer3d_game_data_runtime *runtime,
                                                              const char *session_name, const char *status_key,
                                                              const char *state_key, const char *connected_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    direct_connect_publish_status_entry(runtime, find_direct_connect_session(runtime, session_name), status_key,
                                        state_key, connected_key, "Disconnected");
    return true;
}

bool slayer3d_game_data_network_direct_connect_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                      const char *status_key, const char *state_key,
                                                      const char *connected_key, const char *status)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_direct_connect_session *entry = find_direct_connect_session(runtime, session_name);
    if (entry != NULL && entry->session != NULL)
    {
        slayer3d_network_session_destroy(entry->session);
        entry->session = NULL;
    }
    direct_connect_publish_status_entry(runtime, entry, status_key, state_key, connected_key,
                                        status != NULL ? status : "Disconnected");
    return true;
}

bool slayer3d_game_data_network_direct_connect_start(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                     const char *host, int port, const char *status_key,
                                                     const char *state_key, const char *connected_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_direct_connect_session *entry = get_or_create_direct_connect_session(runtime, session_name);
    if (entry == NULL)
        return false;

    if (host == NULL || host[0] == '\0')
    {
        direct_connect_publish_manual_status(runtime, status_key, state_key, connected_key, "Invalid host", "error",
                                             false);
        return false;
    }

    if (port <= 0 || port > 65535)
    {
        direct_connect_publish_manual_status(runtime, status_key, state_key, connected_key, "Invalid port", "error",
                                             false);
        return false;
    }

    if (entry->session != NULL)
    {
        slayer3d_network_session_destroy(entry->session);
        entry->session = NULL;
    }

    slayer3d_network_session_desc desc;
    slayer3d_network_session_desc_init(&desc);
    desc.role = SLAYER3D_NETWORK_ROLE_CLIENT;
    desc.host = host;
    desc.port = (Uint16)port;
    desc.local_port = 0;
    desc.handshake_timeout = 5.0f;
    desc.idle_timeout = 10.0f;

    if (!slayer3d_network_session_create(&desc, &entry->session))
    {
        direct_connect_publish_manual_status(runtime, status_key, state_key, connected_key, SDL_GetError(), "error",
                                             false);
        return false;
    }

    direct_connect_publish_status_entry(runtime, entry, status_key, state_key, connected_key, "Connecting");
    return true;
}

static runtime_host_session *find_host_session(slayer3d_game_data_runtime *runtime, const char *session_name)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->host_session_count; ++i)
    {
        if (SDL_strcmp(runtime->host_sessions[i].name, session_name) == 0)
            return &runtime->host_sessions[i];
    }
    return NULL;
}

static runtime_host_session *get_or_create_host_session(slayer3d_game_data_runtime *runtime, const char *session_name)
{
    runtime_host_session *existing = find_host_session(runtime, session_name);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;

    if (runtime->host_session_count >= runtime->host_session_capacity)
    {
        const int next_capacity = runtime->host_session_capacity > 0 ? runtime->host_session_capacity * 2 : 2;
        runtime_host_session *next =
            (runtime_host_session *)SDL_realloc(runtime->host_sessions, (size_t)next_capacity * sizeof(*next));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->host_session_capacity, 0,
                   (size_t)(next_capacity - runtime->host_session_capacity) * sizeof(*next));
        runtime->host_sessions = next;
        runtime->host_session_capacity = next_capacity;
    }

    runtime_host_session *entry = &runtime->host_sessions[runtime->host_session_count];
    SDL_zero(*entry);
    entry->name = SDL_strdup(session_name);
    if (entry->name == NULL)
        return NULL;
    ++runtime->host_session_count;
    return entry;
}

static void host_publish_manual_status(slayer3d_game_data_runtime *runtime, const char *status_key,
                                       const char *endpoint_key, const char *peer_key, const char *connected_key,
                                       const char *status, Uint16 port, const char *peer, bool connected)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    if (status_key != NULL && status_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, status_key, status != NULL ? status : "");
    if (endpoint_key != NULL && endpoint_key[0] != '\0')
    {
        char endpoint[32];
        SDL_snprintf(endpoint, sizeof(endpoint), "UDP %u", (unsigned int)port);
        slayer3d_properties_set_string(runtime->scene_state, endpoint_key, endpoint);
    }
    if (peer_key != NULL && peer_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, peer_key, peer != NULL ? peer : "Waiting for client");
    if (connected_key != NULL && connected_key[0] != '\0')
        slayer3d_properties_set_bool(runtime->scene_state, connected_key, connected);
}

static void host_publish_status_entry(slayer3d_game_data_runtime *runtime, const runtime_host_session *entry,
                                      const char *status_key, const char *endpoint_key, const char *peer_key,
                                      const char *connected_key, const char *fallback_status, Uint16 fallback_port)
{
    const slayer3d_network_state state = entry != NULL && entry->session != NULL
                                             ? slayer3d_network_session_state(entry->session)
                                             : SLAYER3D_NETWORK_STATE_DISCONNECTED;
    const char *status =
        entry != NULL && entry->session != NULL ? slayer3d_network_session_status(entry->session) : NULL;
    const Uint16 port =
        entry != NULL && entry->session != NULL ? slayer3d_network_session_port(entry->session) : fallback_port;
    char peer_label[SLAYER3D_NETWORK_MAX_HOST_LENGTH + 48];
    char peer_host[SLAYER3D_NETWORK_MAX_HOST_LENGTH];
    Uint16 peer_port = 0;
    const bool connected =
        entry != NULL && entry->session != NULL && slayer3d_network_session_is_connected(entry->session);

    if (status == NULL || status[0] == '\0')
        status = fallback_status != NULL ? fallback_status : game_data_network_state_name(state);
    SDL_snprintf(peer_label, sizeof(peer_label), "Waiting for client");
    SDL_zero(peer_host);
    if (connected &&
        slayer3d_network_session_get_peer_endpoint(entry->session, peer_host, (int)sizeof(peer_host), &peer_port))
        SDL_snprintf(peer_label, sizeof(peer_label), "Client 1 - %s:%u", peer_host, (unsigned int)peer_port);
    else if (connected)
        SDL_snprintf(peer_label, sizeof(peer_label), "Client 1 - Connected");

    host_publish_manual_status(runtime, status_key, endpoint_key, peer_key, connected_key, status, port, peer_label,
                               connected);
}

slayer3d_network_session *slayer3d_game_data_get_network_host_session(slayer3d_game_data_runtime *runtime,
                                                                      const char *session_name)
{
    runtime_host_session *entry = find_host_session(runtime, session_name);
    return entry != NULL ? entry->session : NULL;
}

bool slayer3d_game_data_network_host_publish_status(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                    const char *status_key, const char *endpoint_key,
                                                    const char *peer_key, const char *connected_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    host_publish_status_entry(runtime, find_host_session(runtime, session_name), status_key, endpoint_key, peer_key,
                              connected_key, "Not hosting", SLAYER3D_NETWORK_DEFAULT_PORT);
    return true;
}

bool slayer3d_game_data_network_host_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                            const char *status_key, const char *endpoint_key, const char *peer_key,
                                            const char *connected_key, const char *status)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_host_session *entry = find_host_session(runtime, session_name);
    Uint16 port = SLAYER3D_NETWORK_DEFAULT_PORT;
    if (entry != NULL && entry->session != NULL)
    {
        port = slayer3d_network_session_port(entry->session);
        slayer3d_network_session_destroy(entry->session);
        entry->session = NULL;
    }
    host_publish_manual_status(runtime, status_key, endpoint_key, peer_key, connected_key,
                               status != NULL ? status : "Not hosting", port, "Waiting for client", false);
    return true;
}

bool slayer3d_game_data_network_host_start(slayer3d_game_data_runtime *runtime, const char *session_name, int port,
                                           const char *advertised_name, const char *status_key,
                                           const char *endpoint_key, const char *peer_key, const char *connected_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_host_session *entry = get_or_create_host_session(runtime, session_name);
    if (entry == NULL)
        return false;

    if (entry->session != NULL)
    {
        host_publish_status_entry(runtime, entry, status_key, endpoint_key, peer_key, connected_key, "Waiting",
                                  (Uint16)port);
        return true;
    }

    if (port <= 0 || port > 65535)
    {
        host_publish_manual_status(runtime, status_key, endpoint_key, peer_key, connected_key, "Invalid host port",
                                   SLAYER3D_NETWORK_DEFAULT_PORT, "Waiting for client", false);
        return false;
    }

    slayer3d_network_session_desc desc;
    slayer3d_network_session_desc_init(&desc);
    desc.role = SLAYER3D_NETWORK_ROLE_HOST;
    desc.host = NULL;
    desc.port = (Uint16)port;
    desc.local_port = 0;
    desc.handshake_timeout = 5.0f;
    desc.idle_timeout = 10.0f;
    desc.session_name = advertised_name != NULL && advertised_name[0] != '\0' ? advertised_name : "SLAYER3D Session";

    if (!slayer3d_network_session_create(&desc, &entry->session))
    {
        host_publish_manual_status(runtime, status_key, endpoint_key, peer_key, connected_key, SDL_GetError(),
                                   (Uint16)SDL_max(port, 0), "Waiting for client", false);
        return false;
    }

    host_publish_status_entry(runtime, entry, status_key, endpoint_key, peer_key, connected_key, "Waiting",
                              (Uint16)port);
    return true;
}

static runtime_discovery_session *find_discovery_session(slayer3d_game_data_runtime *runtime, const char *session_name)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->discovery_session_count; ++i)
    {
        if (SDL_strcmp(runtime->discovery_sessions[i].name, session_name) == 0)
            return &runtime->discovery_sessions[i];
    }
    return NULL;
}

static runtime_discovery_session *get_or_create_discovery_session(slayer3d_game_data_runtime *runtime,
                                                                  const char *session_name)
{
    runtime_discovery_session *existing = find_discovery_session(runtime, session_name);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return NULL;

    if (runtime->discovery_session_count >= runtime->discovery_session_capacity)
    {
        const int next_capacity = runtime->discovery_session_capacity > 0 ? runtime->discovery_session_capacity * 2 : 2;
        runtime_discovery_session *next = (runtime_discovery_session *)SDL_realloc(
            runtime->discovery_sessions, (size_t)next_capacity * sizeof(*runtime->discovery_sessions));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->discovery_session_capacity, 0,
                   (size_t)(next_capacity - runtime->discovery_session_capacity) *
                       sizeof(*runtime->discovery_sessions));
        runtime->discovery_sessions = next;
        runtime->discovery_session_capacity = next_capacity;
    }

    runtime_discovery_session *entry = &runtime->discovery_sessions[runtime->discovery_session_count];
    SDL_zero(*entry);
    entry->name = SDL_strdup(session_name);
    if (entry->name == NULL)
        return NULL;
    ++runtime->discovery_session_count;
    return entry;
}

static void discovery_publish_manual_status(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                            const char *status_key, const char *count_key, const char *status,
                                            int count)
{
    if (runtime == NULL)
        return;
    if (collection_name != NULL && collection_name[0] != '\0')
        (void)slayer3d_game_data_runtime_collection_clear(runtime, collection_name);
    if (runtime->scene_state != NULL && status_key != NULL && status_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, status_key, status != NULL ? status : "");
    if (runtime->scene_state != NULL && count_key != NULL && count_key[0] != '\0')
        slayer3d_properties_set_int(runtime->scene_state, count_key, count);
}

static void discovery_publish_results(slayer3d_game_data_runtime *runtime, const runtime_discovery_session *entry,
                                      const char *collection_name, const char *status_key, const char *count_key)
{
    if (runtime == NULL)
        return;

    const int result_count =
        entry != NULL && entry->session != NULL ? slayer3d_network_discovery_session_result_count(entry->session) : 0;
    const char *status =
        entry != NULL && entry->session != NULL ? slayer3d_network_discovery_session_status(entry->session) : "Idle";
    if (status == NULL || status[0] == '\0')
        status = result_count > 0 ? "Session found" : "Scanning";

    if (collection_name != NULL && collection_name[0] != '\0')
    {
        (void)slayer3d_game_data_runtime_collection_clear(runtime, collection_name);
        for (int i = 0; i < result_count; ++i)
        {
            slayer3d_network_discovery_result result;
            char label[SLAYER3D_NETWORK_MAX_STATUS_LENGTH + SLAYER3D_NETWORK_MAX_HOST_LENGTH + 32];
            char endpoint[SLAYER3D_NETWORK_MAX_HOST_LENGTH + 16];
            SDL_zero(result);
            if (!slayer3d_network_discovery_session_get_result(entry->session, i, &result))
                continue;

            SDL_snprintf(endpoint, sizeof(endpoint), "%s:%u", result.host, (unsigned int)result.port);
            SDL_snprintf(label, sizeof(label), "%s  %s%s%s",
                         result.session_name[0] != '\0' ? result.session_name : "SLAYER3D Session", endpoint,
                         result.status[0] != '\0' ? "  " : "", result.status[0] != '\0' ? result.status : "");
            (void)slayer3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "label", label);
            (void)slayer3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "name",
                                                                   result.session_name[0] != '\0' ? result.session_name
                                                                                                  : "SLAYER3D Session");
            (void)slayer3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "host", result.host);
            (void)slayer3d_game_data_runtime_collection_set_int(runtime, collection_name, i, "port", (int)result.port);
            (void)slayer3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "status",
                                                                   result.status);
            (void)slayer3d_game_data_runtime_collection_set_string(runtime, collection_name, i, "endpoint", endpoint);
        }
    }

    if (runtime->scene_state != NULL && status_key != NULL && status_key[0] != '\0')
        slayer3d_properties_set_string(runtime->scene_state, status_key, status);
    if (runtime->scene_state != NULL && count_key != NULL && count_key[0] != '\0')
        slayer3d_properties_set_int(runtime->scene_state, count_key, result_count);
}

bool slayer3d_game_data_network_discovery_start(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                const char *host, int port, int local_port, const char *collection_name,
                                                const char *status_key, const char *count_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_discovery_session *entry = get_or_create_discovery_session(runtime, session_name);
    if (entry == NULL)
        return false;

    if (port <= 0 || port > 65535 || local_port < 0 || local_port > 65535)
    {
        discovery_publish_manual_status(runtime, collection_name, status_key, count_key, "Invalid discovery port", 0);
        return false;
    }

    if (entry->session == NULL)
    {
        slayer3d_network_discovery_session_desc desc;
        slayer3d_network_discovery_session_desc_init(&desc);
        desc.host = host != NULL && host[0] != '\0' ? host : NULL;
        desc.port = (Uint16)port;
        desc.local_port = (Uint16)local_port;
        if (!slayer3d_network_discovery_session_create(&desc, &entry->session))
        {
            discovery_publish_manual_status(runtime, collection_name, status_key, count_key, SDL_GetError(), 0);
            return false;
        }
    }

    if (!slayer3d_network_discovery_session_refresh(entry->session))
    {
        discovery_publish_manual_status(runtime, collection_name, status_key, count_key, SDL_GetError(), 0);
        return false;
    }

    discovery_publish_results(runtime, entry, collection_name, status_key, count_key);
    return true;
}

bool slayer3d_game_data_network_discovery_update(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                 float dt, const char *collection_name, const char *status_key,
                                                 const char *count_key)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_discovery_session *entry = find_discovery_session(runtime, session_name);
    if (entry == NULL || entry->session == NULL)
    {
        discovery_publish_manual_status(runtime, collection_name, status_key, count_key, "Idle", 0);
        return true;
    }
    if (dt < 0.0f)
        dt = 0.0f;
    if (!slayer3d_network_discovery_session_update(entry->session, dt))
    {
        discovery_publish_results(runtime, entry, collection_name, status_key, count_key);
        return false;
    }
    discovery_publish_results(runtime, entry, collection_name, status_key, count_key);
    return true;
}

bool slayer3d_game_data_network_discovery_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                 const char *collection_name, const char *status_key,
                                                 const char *count_key, const char *status)
{
    if (runtime == NULL || session_name == NULL || session_name[0] == '\0')
        return false;
    runtime_discovery_session *entry = find_discovery_session(runtime, session_name);
    if (entry != NULL && entry->session != NULL)
    {
        slayer3d_network_discovery_session_destroy(entry->session);
        entry->session = NULL;
    }
    discovery_publish_manual_status(runtime, collection_name, status_key, count_key,
                                    status != NULL ? status : "Discovery canceled", 0);
    return true;
}

bool slayer3d_game_data_network_discovery_connect_selected(slayer3d_game_data_runtime *runtime,
                                                           const char *discovery_name, const char *collection_name,
                                                           int selected_index, const char *direct_connect_name,
                                                           const char *host_key, const char *port_key,
                                                           const char *status_key, const char *state_key,
                                                           const char *connected_key, const char *connecting_status)
{
    if (runtime == NULL || collection_name == NULL || collection_name[0] == '\0' || selected_index < 0 ||
        direct_connect_name == NULL || direct_connect_name[0] == '\0')
    {
        return false;
    }

    const runtime_collection *collection = find_runtime_collection_const(runtime, collection_name);
    if (collection == NULL || selected_index >= collection->row_count || collection->rows[selected_index] == NULL)
    {
        direct_connect_publish_manual_status(runtime, status_key, state_key, connected_key, "No session selected",
                                             "error", false);
        return false;
    }

    const char *host = slayer3d_properties_get_string(collection->rows[selected_index], "host", NULL);
    char host_copy[SLAYER3D_NETWORK_MAX_HOST_LENGTH];
    const slayer3d_value *port_value = slayer3d_properties_get_value(collection->rows[selected_index], "port");
    const int port = port_value != NULL && port_value->type == SLAYER3D_VALUE_INT
                         ? port_value->as_int
                         : SDL_atoi(slayer3d_properties_get_string(collection->rows[selected_index], "port", "0"));
    SDL_strlcpy(host_copy, host != NULL ? host : "", sizeof(host_copy));
    if (runtime->scene_state != NULL)
    {
        if (host_key != NULL && host_key[0] != '\0')
            slayer3d_properties_set_string(runtime->scene_state, host_key, host_copy);
        if (port_key != NULL && port_key[0] != '\0')
        {
            char port_text[16];
            SDL_snprintf(port_text, sizeof(port_text), "%d", port);
            slayer3d_properties_set_string(runtime->scene_state, port_key, port_text);
        }
    }

    (void)slayer3d_game_data_network_discovery_cancel(runtime, discovery_name, collection_name, NULL, NULL,
                                                      "Discovery canceled");
    const bool ok = slayer3d_game_data_network_direct_connect_start(runtime, direct_connect_name, host_copy, port,
                                                                    status_key, state_key, connected_key);
    if (ok && connecting_status != NULL && connecting_status[0] != '\0' && runtime->scene_state != NULL &&
        status_key != NULL && status_key[0] != '\0')
    {
        slayer3d_properties_set_string(runtime->scene_state, status_key, connecting_status);
    }
    return ok;
}
