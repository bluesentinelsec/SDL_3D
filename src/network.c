#include "sdl3d/network.h"

#include <SDL3/SDL_endian.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#if SDL3D_NETWORKING_ENABLED
#include <SDL3_net/SDL_net.h>
#else
typedef struct NET_Address
{
    int unused;
} NET_Address;
typedef struct NET_Datagram
{
    const Uint8 *buf;
    int buflen;
    NET_Address *addr;
    Uint16 port;
} NET_Datagram;
typedef struct NET_DatagramSocket
{
    int unused;
} NET_DatagramSocket;
typedef int NET_Status;
#define NET_SUCCESS 0
#define NET_FAILURE (-1)

static bool NET_Init(void)
{
    return false;
}

static void NET_Quit(void)
{
}

static NET_DatagramSocket *NET_CreateDatagramSocket(const char *interface, Uint16 port)
{
    (void)interface;
    (void)port;
    return NULL;
}

static void NET_DestroyDatagramSocket(NET_DatagramSocket *socket)
{
    (void)socket;
}

static NET_Address *NET_ResolveHostname(const char *host)
{
    (void)host;
    return NULL;
}

static int NET_GetAddressStatus(const NET_Address *addr)
{
    (void)addr;
    return NET_FAILURE;
}

static NET_Address *NET_RefAddress(const NET_Address *addr)
{
    return (NET_Address *)addr;
}

static void NET_UnrefAddress(NET_Address *addr)
{
    (void)addr;
}

static const char *NET_GetAddressString(const NET_Address *addr)
{
    (void)addr;
    return NULL;
}

static int NET_CompareAddresses(const NET_Address *a, const NET_Address *b)
{
    return a == b ? 0 : 1;
}

static bool NET_SendDatagram(NET_DatagramSocket *socket, const NET_Address *addr, Uint16 port, const void *buf,
                             int buflen)
{
    (void)socket;
    (void)addr;
    (void)port;
    (void)buf;
    (void)buflen;
    return false;
}

static int NET_WaitUntilInputAvailable(void **sockets, int num_sockets, int timeout)
{
    (void)sockets;
    (void)num_sockets;
    (void)timeout;
    return 0;
}

static bool NET_ReceiveDatagram(NET_DatagramSocket *socket, NET_Datagram **out_datagram)
{
    (void)socket;
    if (out_datagram != NULL)
    {
        *out_datagram = NULL;
    }
    return true;
}

static void NET_DestroyDatagram(NET_Datagram *dgram)
{
    (void)dgram;
}
#endif

typedef enum sdl3d_network_packet_kind
{
    SDL3D_NETWORK_PACKET_HELLO = 1,
    SDL3D_NETWORK_PACKET_WELCOME = 2,
    SDL3D_NETWORK_PACKET_REJECT = 3,
    SDL3D_NETWORK_PACKET_KEEPALIVE = 4,
    SDL3D_NETWORK_PACKET_USER = 5,
} sdl3d_network_packet_kind;

typedef struct sdl3d_network_packet_entry
{
    Uint8 data[SDL3D_NETWORK_MAX_PACKET_SIZE];
    int size;
} sdl3d_network_packet_entry;

struct sdl3d_network_session
{
    sdl3d_network_session_desc desc;
    char host[SDL3D_NETWORK_MAX_HOST_LENGTH];
    char status[SDL3D_NETWORK_MAX_STATUS_LENGTH];
    sdl3d_network_state state;
    float handshake_elapsed;
    float handshake_send_elapsed;
    float idle_elapsed;
    float keepalive_elapsed;
    bool hello_sent;
    bool welcome_sent;
    Uint16 local_bound_port;
    Uint16 peer_port;
    NET_DatagramSocket *socket;
    NET_Address *remote_address;
    NET_Address *peer_address;
    sdl3d_network_packet_entry queue[SDL3D_NETWORK_MAX_QUEUE_SIZE];
    int queue_head;
    int queue_count;
};

#if SDL3D_NETWORKING_ENABLED
static int sdl3d_network_library_refs = 0;
#endif

static void sdl3d_network_set_status(sdl3d_network_session *session, sdl3d_network_state state, const char *status)
{
    if (session == NULL)
    {
        return;
    }

    session->state = state;
    SDL_snprintf(session->status, sizeof(session->status), "%s", status != NULL ? status : "");
}

static void sdl3d_network_clear_peer(sdl3d_network_session *session)
{
    if (session == NULL)
    {
        return;
    }

    if (session->peer_address != NULL)
    {
        NET_UnrefAddress(session->peer_address);
        session->peer_address = NULL;
    }
    session->peer_port = 0;
    session->hello_sent = false;
    session->welcome_sent = false;
    session->handshake_elapsed = 0.0f;
    session->handshake_send_elapsed = 0.0f;
    session->idle_elapsed = 0.0f;
    session->keepalive_elapsed = 0.0f;
    session->queue_head = 0;
    session->queue_count = 0;
}

static void sdl3d_network_destroy_socket(sdl3d_network_session *session)
{
    if (session == NULL)
    {
        return;
    }

    if (session->socket != NULL)
    {
        NET_DestroyDatagramSocket(session->socket);
        session->socket = NULL;
    }
}

static void sdl3d_network_destroy_remote_address(sdl3d_network_session *session)
{
    if (session == NULL)
    {
        return;
    }

    if (session->remote_address != NULL)
    {
        NET_UnrefAddress(session->remote_address);
        session->remote_address = NULL;
    }
}

static void sdl3d_network_queue_packet(sdl3d_network_session *session, const Uint8 *data, int size)
{
    if (session == NULL || data == NULL || size <= 0 || size > SDL3D_NETWORK_MAX_PACKET_SIZE ||
        session->queue_count >= SDL3D_NETWORK_MAX_QUEUE_SIZE)
    {
        return;
    }

    const int slot = (session->queue_head + session->queue_count) % SDL3D_NETWORK_MAX_QUEUE_SIZE;
    SDL_memcpy(session->queue[slot].data, data, (size_t)size);
    session->queue[slot].size = size;
    session->queue_count++;
}

static bool sdl3d_network_library_acquire(void)
{
#if SDL3D_NETWORKING_ENABLED
    if (sdl3d_network_library_refs == 0 && !NET_Init())
    {
        return false;
    }
    sdl3d_network_library_refs++;
    return true;
#else
    SDL_SetError("SDL3D networking is disabled at build time.");
    return false;
#endif
}

static void sdl3d_network_library_release(void)
{
#if SDL3D_NETWORKING_ENABLED
    if (sdl3d_network_library_refs > 0)
    {
        sdl3d_network_library_refs--;
        if (sdl3d_network_library_refs == 0)
        {
            NET_Quit();
        }
    }
#endif
}

static void sdl3d_network_write_u16(Uint8 *dst, Uint16 value)
{
    Uint16 encoded = SDL_Swap16LE(value);
    SDL_memcpy(dst, &encoded, sizeof(encoded));
}

static void sdl3d_network_write_u32(Uint8 *dst, Uint32 value)
{
    Uint32 encoded = SDL_Swap32LE(value);
    SDL_memcpy(dst, &encoded, sizeof(encoded));
}

static Uint16 sdl3d_network_read_u16(const Uint8 *src)
{
    Uint16 value = 0;
    SDL_memcpy(&value, src, sizeof(value));
    return SDL_Swap16LE(value);
}

static Uint32 sdl3d_network_read_u32(const Uint8 *src)
{
    Uint32 value = 0;
    SDL_memcpy(&value, src, sizeof(value));
    return SDL_Swap32LE(value);
}

static int sdl3d_network_encode_packet(Uint8 *buffer, int buffer_size, sdl3d_network_packet_kind kind,
                                       const void *payload, int payload_size)
{
    const int header_size = 12;
    if (buffer == NULL || buffer_size < header_size || payload_size < 0 || payload_size > buffer_size - header_size)
    {
        return -1;
    }

    sdl3d_network_write_u32(buffer, 0x53444C33u);
    sdl3d_network_write_u16(buffer + 4, 1u);
    buffer[6] = (Uint8)kind;
    buffer[7] = 0u;
    sdl3d_network_write_u16(buffer + 8, (Uint16)payload_size);
    sdl3d_network_write_u16(buffer + 10, 0u);
    if (payload_size > 0 && payload != NULL)
    {
        SDL_memcpy(buffer + header_size, payload, (size_t)payload_size);
    }
    return header_size + payload_size;
}

static bool sdl3d_network_decode_packet(const Uint8 *buffer, int size, sdl3d_network_packet_kind *out_kind,
                                        const Uint8 **out_payload, int *out_payload_size)
{
    const int header_size = 12;
    if (buffer == NULL || size < header_size || out_kind == NULL || out_payload == NULL || out_payload_size == NULL)
    {
        return false;
    }

    if (sdl3d_network_read_u32(buffer) != 0x53444C33u)
    {
        return false;
    }

    const Uint16 version = sdl3d_network_read_u16(buffer + 4);
    if (version != 1u)
    {
        return false;
    }

    const int payload_size = (int)sdl3d_network_read_u16(buffer + 8);
    if (payload_size < 0 || payload_size > size - header_size)
    {
        return false;
    }

    *out_kind = (sdl3d_network_packet_kind)buffer[6];
    *out_payload = buffer + header_size;
    *out_payload_size = payload_size;
    return true;
}

static bool sdl3d_network_address_matches(const NET_Address *a, Uint16 port_a, const NET_Address *b, Uint16 port_b)
{
#if SDL3D_NETWORKING_ENABLED
    if (a == NULL || b == NULL)
    {
        return false;
    }
    return NET_CompareAddresses(a, b) == 0 && port_a == port_b;
#else
    (void)a;
    (void)port_a;
    (void)b;
    (void)port_b;
    return false;
#endif
}

static bool sdl3d_network_send_packet_to(sdl3d_network_session *session, NET_Address *address, Uint16 port,
                                         sdl3d_network_packet_kind kind, const void *payload, int payload_size)
{
#if SDL3D_NETWORKING_ENABLED
    if (session == NULL || session->socket == NULL || address == NULL || port == 0)
    {
        return false;
    }

    Uint8 packet[SDL3D_NETWORK_MAX_PACKET_SIZE];
    const int size = sdl3d_network_encode_packet(packet, (int)sizeof(packet), kind, payload, payload_size);
    if (size < 0)
    {
        return false;
    }
    return NET_SendDatagram(session->socket, address, port, packet, size);
#else
    (void)session;
    (void)address;
    (void)port;
    (void)kind;
    (void)payload;
    (void)payload_size;
    return false;
#endif
}

static bool sdl3d_network_send_control(sdl3d_network_session *session, sdl3d_network_packet_kind kind,
                                       const void *payload, int payload_size)
{
#if SDL3D_NETWORKING_ENABLED
    if (session == NULL || session->socket == NULL)
    {
        return false;
    }

    if (session->peer_address != NULL && session->peer_port != 0)
    {
        return sdl3d_network_send_packet_to(session, session->peer_address, session->peer_port, kind, payload,
                                            payload_size);
    }

    if (session->desc.role == SDL3D_NETWORK_ROLE_CLIENT && session->remote_address != NULL && session->desc.port != 0)
    {
        return sdl3d_network_send_packet_to(session, session->remote_address, session->desc.port, kind, payload,
                                            payload_size);
    }

    return false;
#else
    (void)session;
    (void)kind;
    (void)payload;
    (void)payload_size;
    return false;
#endif
}

static void sdl3d_network_update_connected_activity(sdl3d_network_session *session, float dt)
{
    if (session == NULL || session->state != SDL3D_NETWORK_STATE_CONNECTED)
    {
        return;
    }

    session->idle_elapsed += SDL_max(dt, 0.0f);
    session->keepalive_elapsed += SDL_max(dt, 0.0f);

    if (session->keepalive_elapsed >= 1.0f)
    {
        if (!sdl3d_network_send_control(session, SDL3D_NETWORK_PACKET_KEEPALIVE, NULL, 0))
        {
            sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_ERROR, "Failed to send keepalive");
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network keepalive send failed: %s", SDL_GetError());
            return;
        }
        session->keepalive_elapsed = 0.0f;
    }

    if (session->idle_elapsed >= session->desc.idle_timeout)
    {
        if (session->desc.role == SDL3D_NETWORK_ROLE_HOST)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network peer timed out; returning host to waiting state");
            sdl3d_network_clear_peer(session);
            sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_WAITING, "Awaiting client");
        }
        else
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network connection timed out");
            sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_TIMED_OUT, "Connection timed out");
        }
    }
}

static void sdl3d_network_process_datagram(sdl3d_network_session *session, const NET_Datagram *dgram)
{
    if (session == NULL || dgram == NULL || dgram->buf == NULL || dgram->buflen <= 0)
    {
        return;
    }

    sdl3d_network_packet_kind kind;
    const Uint8 *payload = NULL;
    int payload_size = 0;
    if (!sdl3d_network_decode_packet(dgram->buf, dgram->buflen, &kind, &payload, &payload_size))
    {
        return;
    }

    switch (kind)
    {
    case SDL3D_NETWORK_PACKET_HELLO:
        if (session->desc.role == SDL3D_NETWORK_ROLE_HOST && session->state == SDL3D_NETWORK_STATE_WAITING)
        {
            if (session->peer_address != NULL)
            {
                NET_UnrefAddress(session->peer_address);
            }
            session->peer_address = NET_RefAddress(dgram->addr);
            session->peer_port = dgram->port;
            session->idle_elapsed = 0.0f;
            sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_CONNECTED, "Client connected");
            if (!sdl3d_network_send_control(session, SDL3D_NETWORK_PACKET_WELCOME, NULL, 0))
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network welcome send failed: %s", SDL_GetError());
            }
            else
            {
                session->welcome_sent = true;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network host accepted peer on port %u",
                            (unsigned int)session->peer_port);
            }
        }
        else if (session->desc.role == SDL3D_NETWORK_ROLE_HOST && session->state == SDL3D_NETWORK_STATE_CONNECTED)
        {
            if (sdl3d_network_address_matches(session->peer_address, session->peer_port, dgram->addr, dgram->port))
            {
                (void)sdl3d_network_send_control(session, SDL3D_NETWORK_PACKET_WELCOME, NULL, 0);
            }
            else
            {
                (void)sdl3d_network_send_packet_to(session, dgram->addr, dgram->port, SDL3D_NETWORK_PACKET_REJECT, NULL,
                                                   0);
            }
        }
        break;
    case SDL3D_NETWORK_PACKET_WELCOME:
        if (session->desc.role == SDL3D_NETWORK_ROLE_CLIENT && session->state == SDL3D_NETWORK_STATE_CONNECTING)
        {
            if (session->peer_address != NULL)
            {
                NET_UnrefAddress(session->peer_address);
            }
            session->peer_address = NET_RefAddress(dgram->addr);
            session->peer_port = dgram->port;
            session->idle_elapsed = 0.0f;
            session->keepalive_elapsed = 0.0f;
            sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_CONNECTED, "Connected");
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network client connected to %s:%u",
                        NET_GetAddressString(session->peer_address) != NULL
                            ? NET_GetAddressString(session->peer_address)
                            : "<unknown>",
                        (unsigned int)session->peer_port);
        }
        break;
    case SDL3D_NETWORK_PACKET_REJECT:
        if (session->desc.role == SDL3D_NETWORK_ROLE_CLIENT && session->state == SDL3D_NETWORK_STATE_CONNECTING)
        {
            sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_REJECTED, "Connection rejected");
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network client rejected by host");
        }
        break;
    case SDL3D_NETWORK_PACKET_KEEPALIVE:
        if (session->state == SDL3D_NETWORK_STATE_CONNECTED &&
            sdl3d_network_address_matches(session->peer_address, session->peer_port, dgram->addr, dgram->port))
        {
            session->idle_elapsed = 0.0f;
        }
        break;
    case SDL3D_NETWORK_PACKET_USER:
        if (session->state == SDL3D_NETWORK_STATE_CONNECTED &&
            sdl3d_network_address_matches(session->peer_address, session->peer_port, dgram->addr, dgram->port))
        {
            sdl3d_network_queue_packet(session, payload, payload_size);
            session->idle_elapsed = 0.0f;
        }
        break;
    default:
        break;
    }
}

void sdl3d_network_session_desc_init(sdl3d_network_session_desc *desc)
{
    if (desc == NULL)
    {
        return;
    }

    SDL_zero(*desc);
    desc->role = SDL3D_NETWORK_ROLE_CLIENT;
    desc->port = SDL3D_NETWORK_DEFAULT_PORT;
    desc->local_port = 0;
    desc->handshake_timeout = 5.0f;
    desc->idle_timeout = 10.0f;
}

bool sdl3d_network_session_create(const sdl3d_network_session_desc *desc, sdl3d_network_session **out_session)
{
    sdl3d_network_session_desc defaults;
    const sdl3d_network_session_desc *effective = desc;
    sdl3d_network_session *session = NULL;

    if (out_session == NULL)
    {
        return false;
    }
    *out_session = NULL;

    if (effective == NULL)
    {
        sdl3d_network_session_desc_init(&defaults);
        effective = &defaults;
    }

    if (effective->port == 0)
    {
        SDL_SetError("Network session requires a non-zero port.");
        return false;
    }

    session = (sdl3d_network_session *)SDL_calloc(1, sizeof(*session));
    if (session == NULL)
    {
        SDL_OutOfMemory();
        return false;
    }

    session->desc = *effective;
    SDL_zero(session->host);
    SDL_zero(session->status);
    if (effective->host != NULL)
    {
        SDL_snprintf(session->host, sizeof(session->host), "%s", effective->host);
    }

    if (!sdl3d_network_library_acquire())
    {
        sdl3d_network_session_destroy(session);
        return false;
    }

#if SDL3D_NETWORKING_ENABLED
    const Uint16 bound_port = effective->role == SDL3D_NETWORK_ROLE_HOST
                                  ? effective->port
                                  : (effective->local_port != 0 ? effective->local_port : 0);
    session->socket = NET_CreateDatagramSocket(NULL, bound_port);
    if (session->socket == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network socket create failed: %s", SDL_GetError());
        sdl3d_network_session_destroy(session);
        return false;
    }

    if (effective->role == SDL3D_NETWORK_ROLE_HOST)
    {
        session->local_bound_port = bound_port;
        sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_WAITING, "Awaiting client");
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network host listening on port %u",
                    (unsigned int)session->local_bound_port);
    }
    else
    {
        if (effective->host == NULL || effective->host[0] == '\0')
        {
            SDL_SetError("Client network session requires a remote host.");
            sdl3d_network_session_destroy(session);
            return false;
        }

        session->remote_address = NET_ResolveHostname(effective->host);
        if (session->remote_address == NULL)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network hostname resolution failed: %s", SDL_GetError());
            sdl3d_network_session_destroy(session);
            return false;
        }

        session->local_bound_port = bound_port;
        sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_CONNECTING, "Resolving host");
        if (NET_GetAddressStatus(session->remote_address) == NET_FAILURE)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network hostname resolution error: %s", SDL_GetError());
            sdl3d_network_session_destroy(session);
            return false;
        }
    }
#else
    (void)effective;
    sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_ERROR, "Networking disabled");
    sdl3d_network_session_destroy(session);
    return false;
#endif

    *out_session = session;
    return true;
}

void sdl3d_network_session_destroy(sdl3d_network_session *session)
{
    if (session == NULL)
    {
        return;
    }

    sdl3d_network_destroy_socket(session);
    sdl3d_network_destroy_remote_address(session);
    sdl3d_network_clear_peer(session);
    sdl3d_network_library_release();
    SDL_free(session);
}

bool sdl3d_network_session_update(sdl3d_network_session *session, float dt)
{
    if (session == NULL)
    {
        return false;
    }

    if (session->socket == NULL)
    {
        return true;
    }

#if SDL3D_NETWORKING_ENABLED
    if (session->desc.role == SDL3D_NETWORK_ROLE_CLIENT && session->state == SDL3D_NETWORK_STATE_CONNECTING)
    {
        session->handshake_elapsed += SDL_max(dt, 0.0f);

        if (session->remote_address != NULL)
        {
            const NET_Status address_status = NET_GetAddressStatus(session->remote_address);
            if (address_status == NET_SUCCESS)
            {
                if (!session->hello_sent || session->handshake_send_elapsed >= 0.5f)
                {
                    if (!sdl3d_network_send_control(session, SDL3D_NETWORK_PACKET_HELLO, NULL, 0))
                    {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network hello send failed: %s",
                                    SDL_GetError());
                    }
                    else
                    {
                        session->hello_sent = true;
                        session->handshake_send_elapsed = 0.0f;
                    }
                }
            }
            else if (address_status == NET_FAILURE)
            {
                sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_ERROR, "Host resolution failed");
                return true;
            }
        }

        session->handshake_send_elapsed += SDL_max(dt, 0.0f);
        if (session->handshake_elapsed >= session->desc.handshake_timeout)
        {
            sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_TIMED_OUT, "Connection timed out");
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network connection timed out while connecting");
            return true;
        }
    }

    const int input_count = NET_WaitUntilInputAvailable((void **)&session->socket, 1, 0);
    if (input_count < 0)
    {
        sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_ERROR, "Network poll failed");
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network poll failed: %s", SDL_GetError());
        return true;
    }

    if (input_count > 0)
    {
        for (;;)
        {
            NET_Datagram *dgram = NULL;
            if (!NET_ReceiveDatagram(session->socket, &dgram))
            {
                sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_ERROR, "Failed to receive datagram");
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL3D network receive failed: %s", SDL_GetError());
                return true;
            }

            if (dgram == NULL)
            {
                break;
            }

            sdl3d_network_process_datagram(session, dgram);
            NET_DestroyDatagram(dgram);
        }
    }

    sdl3d_network_update_connected_activity(session, dt);
#else
    (void)dt;
#endif
    return true;
}

bool sdl3d_network_session_send(sdl3d_network_session *session, const void *data, int data_size)
{
    if (session == NULL || data == NULL || data_size <= 0 || data_size > SDL3D_NETWORK_MAX_PACKET_SIZE)
    {
        return false;
    }

#if SDL3D_NETWORKING_ENABLED
    if (session->state != SDL3D_NETWORK_STATE_CONNECTED)
    {
        SDL_SetError("Network session is not connected.");
        return false;
    }

    return sdl3d_network_send_control(session, SDL3D_NETWORK_PACKET_USER, data, data_size);
#else
    (void)data;
    (void)data_size;
    SDL_SetError("Networking disabled.");
    return false;
#endif
}

int sdl3d_network_session_receive(sdl3d_network_session *session, void *buffer, int buffer_size)
{
    if (session == NULL || buffer == NULL || buffer_size <= 0)
    {
        return -1;
    }

    if (session->queue_count <= 0)
    {
        return 0;
    }

    const sdl3d_network_packet_entry *entry = &session->queue[session->queue_head];
    if (entry->size > buffer_size)
    {
        SDL_SetError("Network receive buffer too small.");
        return -1;
    }

    SDL_memcpy(buffer, entry->data, (size_t)entry->size);
    const int size = entry->size;
    session->queue_head = (session->queue_head + 1) % SDL3D_NETWORK_MAX_QUEUE_SIZE;
    session->queue_count--;
    return size;
}

void sdl3d_network_session_disconnect(sdl3d_network_session *session)
{
    if (session == NULL)
    {
        return;
    }

    if (session->desc.role == SDL3D_NETWORK_ROLE_HOST)
    {
        sdl3d_network_clear_peer(session);
        sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_WAITING, "Awaiting client");
    }
    else
    {
        sdl3d_network_destroy_socket(session);
        sdl3d_network_destroy_remote_address(session);
        sdl3d_network_clear_peer(session);
        sdl3d_network_set_status(session, SDL3D_NETWORK_STATE_DISCONNECTED, "Disconnected");
        sdl3d_network_library_release();
    }
}

sdl3d_network_state sdl3d_network_session_state(const sdl3d_network_session *session)
{
    return session != NULL ? session->state : SDL3D_NETWORK_STATE_DISCONNECTED;
}

const char *sdl3d_network_session_status(const sdl3d_network_session *session)
{
    return session != NULL ? session->status : NULL;
}

bool sdl3d_network_session_is_connected(const sdl3d_network_session *session)
{
    return session != NULL && session->state == SDL3D_NETWORK_STATE_CONNECTED;
}

Uint16 sdl3d_network_session_port(const sdl3d_network_session *session)
{
    return session != NULL ? session->local_bound_port : 0u;
}

bool sdl3d_network_session_get_peer_endpoint(const sdl3d_network_session *session, char *host_buffer,
                                             int host_buffer_size, Uint16 *out_port)
{
    if (session == NULL || session->peer_address == NULL)
    {
        return false;
    }

#if SDL3D_NETWORKING_ENABLED
    if (host_buffer != NULL && host_buffer_size > 0)
    {
        const char *host = NET_GetAddressString(session->peer_address);
        SDL_snprintf(host_buffer, (size_t)host_buffer_size, "%s", host != NULL ? host : "<unknown>");
    }
    if (out_port != NULL)
    {
        *out_port = session->peer_port;
    }
    return true;
#else
    (void)host_buffer;
    (void)host_buffer_size;
    (void)out_port;
    return false;
#endif
}
