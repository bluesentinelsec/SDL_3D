/**
 * @file network.h
 * @brief UDP transport for direct-connect multiplayer sessions.
 *
 * SDL3D networking is intentionally small and opt-in. It provides a
 * host/client UDP session that can handshake, exchange user packets, and
 * detect disconnects or timeouts. The transport is suitable for LAN or
 * direct-connect play and can later be wrapped by discovery or relay layers.
 *
 * Local-only games can ignore this API entirely. If SDL3_net is unavailable
 * at build time, the functions compile but report networking as disabled.
 */

#ifndef SDL3D_NETWORK_H
#define SDL3D_NETWORK_H

#include <SDL3/SDL_stdinc.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SDL3D_NETWORK_MAX_HOST_LENGTH 256
#define SDL3D_NETWORK_MAX_STATUS_LENGTH 128
#define SDL3D_NETWORK_MAX_PACKET_SIZE 1024
#define SDL3D_NETWORK_MAX_QUEUE_SIZE 8
#define SDL3D_NETWORK_MAX_DISCOVERY_RESULTS 8
#define SDL3D_NETWORK_DEFAULT_PORT 27183

    /** @brief Operating role for a direct-connect network session. */
    typedef enum sdl3d_network_role
    {
        SDL3D_NETWORK_ROLE_HOST = 0,   /**< Listen for one remote client and accept it. */
        SDL3D_NETWORK_ROLE_CLIENT = 1, /**< Connect to a remote host by host / port. */
    } sdl3d_network_role;

    /** @brief Lifecycle state for a network session. */
    typedef enum sdl3d_network_state
    {
        SDL3D_NETWORK_STATE_DISCONNECTED = 0, /**< No socket or no active connection. */
        SDL3D_NETWORK_STATE_CONNECTING,       /**< Client is resolving or handshaking. */
        SDL3D_NETWORK_STATE_WAITING,          /**< Host is listening for a client. */
        SDL3D_NETWORK_STATE_CONNECTED,        /**< Peer handshake completed. */
        SDL3D_NETWORK_STATE_REJECTED,         /**< Remote host rejected the connection. */
        SDL3D_NETWORK_STATE_TIMED_OUT,        /**< Connection or peer activity timed out. */
        SDL3D_NETWORK_STATE_ERROR,            /**< A fatal transport error occurred. */
    } sdl3d_network_state;

    /**
     * @brief Creation descriptor for a UDP direct-connect session.
     *
     * Host sessions bind to @p port and wait for a single client.
     * Client sessions resolve @p host and connect to @p port.
     * @p local_port may be zero to request an ephemeral local bind.
     * Timeouts are expressed in seconds.
     */
    typedef struct sdl3d_network_session_desc
    {
        sdl3d_network_role role;  /**< Host or client. */
        const char *host;         /**< Remote host name or IP for client sessions. */
        Uint16 port;              /**< Host listen port or client remote port. */
        Uint16 local_port;        /**< Optional local bind port, or 0 for ephemeral. */
        float handshake_timeout;  /**< Client handshake timeout in seconds. */
        float idle_timeout;       /**< Connected-session inactivity timeout in seconds. */
        const char *session_name; /**< Optional host session name advertised to discovery clients. */
    } sdl3d_network_session_desc;

    /** @brief Session discovery announcement returned by LAN scans. */
    typedef struct sdl3d_network_discovery_result
    {
        /** @brief Advertised session name. */
        char session_name[SDL3D_NETWORK_MAX_HOST_LENGTH];
        /** @brief Advertised host or IP address. */
        char host[SDL3D_NETWORK_MAX_HOST_LENGTH];
        /** @brief Advertised UDP port. */
        Uint16 port;
        /** @brief Human-readable status string. */
        char status[SDL3D_NETWORK_MAX_STATUS_LENGTH];
        /** @brief Last time this session was seen, in SDL ticks. */
        Uint64 last_seen_ms;
    } sdl3d_network_discovery_result;

    /**
     * @brief Creation descriptor for a LAN discovery scanner.
     *
     * When @p host is NULL, discovery probes are sent to all enumerated
     * subnet-directed IPv4 broadcast addresses, then to 255.255.255.255 as a
     * fallback, then to same-/24 IPv4 unicast targets in rate-limited batches.
     * Otherwise the scanner probes the named host directly, which is useful
     * for tests or manual overrides.
     */
    typedef struct sdl3d_network_discovery_session_desc
    {
        /** @brief Optional probe host or broadcast address, or NULL. */
        const char *host;
        /** @brief Probe target port. */
        Uint16 port;
        /** @brief Optional local bind port, or 0 for ephemeral. */
        Uint16 local_port;
    } sdl3d_network_discovery_session_desc;

    /** @brief Opaque LAN discovery scanner. */
    typedef struct sdl3d_network_discovery_session sdl3d_network_discovery_session;

    /** @brief Opaque direct-connect UDP session. */
    typedef struct sdl3d_network_session sdl3d_network_session;

    /**
     * @brief Initialize a network session descriptor with sane defaults.
     *
     * Defaults:
     * - role: client
     * - host: NULL
     * - port: SDL3D_NETWORK_DEFAULT_PORT
     * - local_port: 0
     * - handshake_timeout: 5 seconds
     * - idle_timeout: 10 seconds
     */
    void sdl3d_network_session_desc_init(sdl3d_network_session_desc *desc);

    /**
     * @brief Initialize a discovery session descriptor with sane defaults.
     *
     * Defaults:
     * - host: NULL, meaning subnet broadcast discovery with global broadcast
     *   fallback and same-/24 unicast discovery sweep
     * - port: SDL3D_NETWORK_DEFAULT_PORT
     * - local_port: 0
     */
    void sdl3d_network_discovery_session_desc_init(sdl3d_network_discovery_session_desc *desc);

    /**
     * @brief Create a direct-connect network session.
     *
     * Host sessions listen for a single remote peer. Client sessions resolve
     * the remote host and perform a handshake. Networking must be enabled at
     * build time or the call fails with SDL_GetError set.
     *
     * @return true on success.
     */
    bool sdl3d_network_session_create(const sdl3d_network_session_desc *desc, sdl3d_network_session **out_session);

    /**
     * @brief Destroy a network session.
     *
     * Safe to call with NULL.
     */
    void sdl3d_network_session_destroy(sdl3d_network_session *session);

    /**
     * @brief Advance transport timers and process queued network traffic.
     *
     * Call once per frame or tick. This polls incoming datagrams, advances
     * handshake and idle timers, and performs keepalive / timeout handling.
     *
     * @return true on success.
     */
    bool sdl3d_network_session_update(sdl3d_network_session *session, float dt);

    /**
     * @brief Send a user payload to the connected peer.
     *
     * The payload is delivered as one UDP packet and is only valid once the
     * session is connected.
     *
     * @return true if the packet was queued for transmission.
     */
    bool sdl3d_network_session_send(sdl3d_network_session *session, const void *data, int data_size);

    /**
     * @brief Receive the next queued user payload from the connected peer.
     *
     * Returns the number of bytes copied into @p buffer, zero when no payload
     * is queued, or -1 on error. The payload queue only stores user messages;
     * handshake and keepalive packets are consumed internally.
     */
    int sdl3d_network_session_receive(sdl3d_network_session *session, void *buffer, int buffer_size);

    /** @brief Disconnect the current peer or close the session. */
    void sdl3d_network_session_disconnect(sdl3d_network_session *session);

    /** @brief Return the current session state. */
    sdl3d_network_state sdl3d_network_session_state(const sdl3d_network_session *session);

    /** @brief Return the current session status string, or NULL. */
    const char *sdl3d_network_session_status(const sdl3d_network_session *session);

    /** @brief Return true if the session has an active connected peer. */
    bool sdl3d_network_session_is_connected(const sdl3d_network_session *session);

    /**
     * @brief Return the port this session is bound to.
     *
     * For host sessions this is the listening port; for clients it is the
     * local ephemeral bind port if one was requested, or zero if unavailable.
     */
    Uint16 sdl3d_network_session_port(const sdl3d_network_session *session);

    /**
     * @brief Query the connected peer endpoint.
     *
     * @return true when a connected peer endpoint is available.
     */
    bool sdl3d_network_session_get_peer_endpoint(const sdl3d_network_session *session, char *host_buffer,
                                                 int host_buffer_size, Uint16 *out_port);

    /**
     * @brief Create a LAN discovery scanner.
     *
     * The scanner sends discovery probes and collects session announcements
     * from host sessions that are waiting for clients.
     */
    bool sdl3d_network_discovery_session_create(const sdl3d_network_discovery_session_desc *desc,
                                                sdl3d_network_discovery_session **out_session);

    /** @brief Destroy a discovery scanner. Safe to call with NULL. */
    void sdl3d_network_discovery_session_destroy(sdl3d_network_discovery_session *session);

    /**
     * @brief Send a discovery probe and clear stale results.
     *
     * Call this when a scan should begin or restart.
     */
    bool sdl3d_network_discovery_session_refresh(sdl3d_network_discovery_session *session);

    /**
     * @brief Advance discovery polling and collect new announcements.
     *
     * Call once per frame or tick while discovery is active.
     */
    bool sdl3d_network_discovery_session_update(sdl3d_network_discovery_session *session, float dt);

    /** @brief Return the number of discovered sessions. */
    int sdl3d_network_discovery_session_result_count(const sdl3d_network_discovery_session *session);

    /**
     * @brief Copy one discovered session into @p out_result.
     *
     * @return true when @p index refers to a valid discovered session.
     */
    bool sdl3d_network_discovery_session_get_result(const sdl3d_network_discovery_session *session, int index,
                                                    sdl3d_network_discovery_result *out_result);

    /** @brief Return the current discovery status string, or NULL. */
    const char *sdl3d_network_discovery_session_status(const sdl3d_network_discovery_session *session);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_NETWORK_H */
