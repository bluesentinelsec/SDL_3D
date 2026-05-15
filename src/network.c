#include "slayer3d/network.h"

#include <SDL3/SDL_endian.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>

#if SLAYER3D_NETWORKING_ENABLED
#include <SDL3_net/SDL_net.h>

#if (defined(__APPLE__) || defined(__linux__)) && !defined(__ANDROID__)
#include <arpa/inet.h>
#include <ifaddrs.h>
#if defined(__linux__)
#include <linux/if.h>
#else
#include <net/if.h>
#endif
#include <netinet/in.h>
#define SLAYER3D_NETWORK_CAN_ENUMERATE_BROADCAST_TARGETS 1
#else
#define SLAYER3D_NETWORK_CAN_ENUMERATE_BROADCAST_TARGETS 0
#endif
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
#define NET_Init() false
#define NET_Quit()                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (0)
#define NET_CreateDatagramSocket(interface, port, props) ((NET_DatagramSocket *)NULL)
#define NET_DestroyDatagramSocket(socket)                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (0)
#define NET_ResolveHostname(host) ((NET_Address *)NULL)
#define NET_GetAddressStatus(addr) NET_FAILURE
#define NET_RefAddress(addr) ((NET_Address *)(addr))
#define NET_UnrefAddress(addr)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (0)
#define NET_GetAddressString(addr) ((const char *)NULL)
#define NET_CompareAddresses(a, b) (((a) == (b)) ? 0 : 1)
#define NET_SendDatagram(socket, addr, port, buf, buflen) false
#define NET_WaitUntilInputAvailable(sockets, num_sockets, timeout) 0
#define NET_ReceiveDatagram(socket, out_datagram) false
#define NET_DestroyDatagram(dgram)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (0)
#endif

#define SLAYER3D_NETWORK_MAX_DISCOVERY_TARGETS 320
#define SLAYER3D_NETWORK_DISCOVERY_BATCH_SIZE 10
#define SLAYER3D_NETWORK_DISCOVERY_BATCH_INTERVAL 0.02f
#define SLAYER3D_NETWORK_GLOBAL_BROADCAST "255.255.255.255"

typedef enum slayer3d_network_packet_kind
{
    SLAYER3D_NETWORK_PACKET_HELLO = 1,
    SLAYER3D_NETWORK_PACKET_WELCOME = 2,
    SLAYER3D_NETWORK_PACKET_REJECT = 3,
    SLAYER3D_NETWORK_PACKET_KEEPALIVE = 4,
    SLAYER3D_NETWORK_PACKET_USER = 5,
    SLAYER3D_NETWORK_PACKET_DISCOVERY_QUERY = 6,
    SLAYER3D_NETWORK_PACKET_DISCOVERY_REPLY = 7,
} slayer3d_network_packet_kind;

typedef struct slayer3d_network_packet_entry
{
    Uint8 data[SLAYER3D_NETWORK_MAX_PACKET_SIZE];
    int size;
} slayer3d_network_packet_entry;

struct slayer3d_network_session
{
    slayer3d_network_session_desc desc;
    char host[SLAYER3D_NETWORK_MAX_HOST_LENGTH];
    char session_name[SLAYER3D_NETWORK_MAX_HOST_LENGTH];
    char status[SLAYER3D_NETWORK_MAX_STATUS_LENGTH];
    slayer3d_network_state state;
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
    slayer3d_network_packet_entry queue[SLAYER3D_NETWORK_MAX_QUEUE_SIZE];
    int queue_head;
    int queue_count;
};

struct slayer3d_network_discovery_session
{
    slayer3d_network_discovery_session_desc desc;
    char status[SLAYER3D_NETWORK_MAX_STATUS_LENGTH];
    NET_DatagramSocket *socket;
    NET_Address *target_addresses[SLAYER3D_NETWORK_MAX_DISCOVERY_TARGETS];
    char target_hosts[SLAYER3D_NETWORK_MAX_DISCOVERY_TARGETS][SLAYER3D_NETWORK_MAX_HOST_LENGTH];
    bool target_probe_sent[SLAYER3D_NETWORK_MAX_DISCOVERY_TARGETS];
    int target_count;
    int next_probe_index;
    Uint16 target_port;
    slayer3d_network_discovery_result results[SLAYER3D_NETWORK_MAX_DISCOVERY_RESULTS];
    int result_count;
    float elapsed;
    float refresh_elapsed;
    float probe_batch_elapsed;
    bool scanning;
};

#if SLAYER3D_NETWORKING_ENABLED
static int slayer3d_network_library_refs = 0;
#endif

#if SLAYER3D_NETWORKING_ENABLED
static NET_DatagramSocket *slayer3d_network_create_datagram_socket(Uint16 port, bool allow_broadcast)
{
    SDL_PropertiesID props = 0;
    NET_DatagramSocket *socket = NULL;
    if (allow_broadcast)
    {
        props = SDL_CreateProperties();
        if (props != 0 && SDL_SetBooleanProperty(props, NET_PROP_DATAGRAM_SOCKET_ALLOW_BROADCAST_BOOLEAN, true))
        {
            socket = NET_CreateDatagramSocket(NULL, port, props);
            if (socket == NULL)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "SLAYER3D broadcast socket create failed, retrying without broadcast: %s", SDL_GetError());
            }
        }
        if (props != 0)
            SDL_DestroyProperties(props);
    }

    return socket != NULL ? socket : NET_CreateDatagramSocket(NULL, port, 0);
}
#endif

#if SLAYER3D_NETWORKING_ENABLED
static void slayer3d_network_set_status(slayer3d_network_session *session, slayer3d_network_state state,
                                        const char *status)
{
    if (session == NULL)
    {
        return;
    }

    session->state = state;
    SDL_snprintf(session->status, sizeof(session->status), "%s", status != NULL ? status : "");
}

static void slayer3d_network_clear_peer(slayer3d_network_session *session)
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

static void slayer3d_network_destroy_socket(slayer3d_network_session *session)
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
#endif

#if SLAYER3D_NETWORKING_ENABLED
static void slayer3d_network_discovery_clear_results(slayer3d_network_discovery_session *session);
static void slayer3d_network_discovery_destroy_socket(slayer3d_network_discovery_session *session);
static void slayer3d_network_discovery_destroy_target_address(slayer3d_network_discovery_session *session);
static bool slayer3d_network_discovery_send_probe(slayer3d_network_discovery_session *session);

static void slayer3d_network_destroy_remote_address(slayer3d_network_session *session)
{
    if (session == NULL)
    {
        return;
    }

    if (session->remote_address != NULL)
    {
        (void)NET_WaitUntilResolved(session->remote_address, -1);
        NET_UnrefAddress(session->remote_address);
        session->remote_address = NULL;
    }
}

static void slayer3d_network_discovery_destroy_target_address(slayer3d_network_discovery_session *session)
{
    if (session == NULL)
    {
        return;
    }

    for (int i = 0; i < session->target_count; ++i)
    {
        if (session->target_addresses[i] != NULL)
        {
            (void)NET_WaitUntilResolved(session->target_addresses[i], -1);
            NET_UnrefAddress(session->target_addresses[i]);
            session->target_addresses[i] = NULL;
        }
        SDL_zeroa(session->target_hosts[i]);
        session->target_probe_sent[i] = false;
    }
    session->target_count = 0;
    session->next_probe_index = 0;
}

#if SLAYER3D_NETWORKING_ENABLED
static int slayer3d_network_encode_packet(Uint8 *buffer, int buffer_size, slayer3d_network_packet_kind kind,
                                          const void *payload, int payload_size);
static bool slayer3d_network_decode_packet(const Uint8 *buffer, int size, slayer3d_network_packet_kind *out_kind,
                                           const Uint8 **out_payload, int *out_payload_size);
static void slayer3d_network_write_u16(Uint8 *dst, Uint16 value);
static Uint16 slayer3d_network_read_u16(const Uint8 *src);
static bool slayer3d_network_send_packet_to(slayer3d_network_session *session, NET_Address *address, Uint16 port,
                                            slayer3d_network_packet_kind kind, const void *payload, int payload_size);

static void slayer3d_network_queue_packet(slayer3d_network_session *session, const Uint8 *data, int size)
{
    if (session == NULL || data == NULL || size <= 0 || size > SLAYER3D_NETWORK_MAX_PACKET_SIZE ||
        session->queue_count >= SLAYER3D_NETWORK_MAX_QUEUE_SIZE)
    {
        return;
    }

    const int slot = (session->queue_head + session->queue_count) % SLAYER3D_NETWORK_MAX_QUEUE_SIZE;
    SDL_memcpy(session->queue[slot].data, data, (size_t)size);
    session->queue[slot].size = size;
    session->queue_count++;
}

static bool slayer3d_network_library_acquire(void)
{
    if (slayer3d_network_library_refs == 0 && !NET_Init())
    {
        return false;
    }
    slayer3d_network_library_refs++;
    return true;
}

static void slayer3d_network_library_release(void)
{
    if (slayer3d_network_library_refs > 0)
    {
        slayer3d_network_library_refs--;
        if (slayer3d_network_library_refs == 0)
        {
            NET_Quit();
        }
    }
}

static const char *slayer3d_network_session_advertised_name(const slayer3d_network_session *session)
{
    if (session == NULL || session->session_name[0] == '\0')
    {
        return "SLAYER3D Session";
    }
    return session->session_name;
}

static bool slayer3d_network_discovery_target_exists(const slayer3d_network_discovery_session *session,
                                                     const char *host)
{
    if (session == NULL || host == NULL || host[0] == '\0')
    {
        return false;
    }

    for (int i = 0; i < session->target_count; ++i)
    {
        if (SDL_strcmp(session->target_hosts[i], host) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool slayer3d_network_discovery_add_target(slayer3d_network_discovery_session *session, const char *host)
{
    if (session == NULL || host == NULL || host[0] == '\0')
    {
        return false;
    }

    if (slayer3d_network_discovery_target_exists(session, host))
    {
        return true;
    }

    if (session->target_count >= SLAYER3D_NETWORK_MAX_DISCOVERY_TARGETS)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery target dropped: list full host=%s", host);
        return false;
    }

    NET_Address *address = NET_ResolveHostname(host);
    if (address == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SLAYER3D discovery target resolution create failed: host=%s error=%s", host, SDL_GetError());
        return false;
    }

    const int index = session->target_count++;
    session->target_addresses[index] = address;
    SDL_snprintf(session->target_hosts[index], sizeof(session->target_hosts[index]), "%s", host);
    session->target_probe_sent[index] = false;
    return true;
}

#if SLAYER3D_NETWORK_CAN_ENUMERATE_BROADCAST_TARGETS
static bool slayer3d_network_discovery_host_list_contains(char hosts[][SLAYER3D_NETWORK_MAX_HOST_LENGTH], int count,
                                                          const char *host)
{
    if (hosts == NULL || host == NULL || host[0] == '\0')
    {
        return false;
    }

    for (int i = 0; i < count; ++i)
    {
        if (SDL_strcmp(hosts[i], host) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool slayer3d_network_discovery_add_host_to_list(char hosts[][SLAYER3D_NETWORK_MAX_HOST_LENGTH], int *count,
                                                        int max, const char *host)
{
    if (hosts == NULL || count == NULL || max <= 0 || *count >= max || host == NULL || host[0] == '\0' ||
        slayer3d_network_discovery_host_list_contains(hosts, *count, host))
    {
        return false;
    }

    SDL_snprintf(hosts[*count], SLAYER3D_NETWORK_MAX_HOST_LENGTH, "%s", host);
    (*count)++;
    return true;
}

static int slayer3d_network_collect_directed_broadcast_hosts(char hosts[][SLAYER3D_NETWORK_MAX_HOST_LENGTH], int max)
{
    struct ifaddrs *interfaces = NULL;
    int count = 0;

    if (hosts == NULL || max <= 0)
    {
        return 0;
    }

    if (getifaddrs(&interfaces) != 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery interface enumeration failed");
        return 0;
    }

    for (const struct ifaddrs *iface = interfaces; iface != NULL && count < max; iface = iface->ifa_next)
    {
        if (iface->ifa_addr == NULL || iface->ifa_addr->sa_family != AF_INET || (iface->ifa_flags & IFF_UP) == 0 ||
            (iface->ifa_flags & IFF_LOOPBACK) != 0)
        {
            continue;
        }

        const struct sockaddr_in *addr = (const struct sockaddr_in *)iface->ifa_addr;
        Uint32 broadcast_ipv4 = 0;

        if ((iface->ifa_flags & IFF_BROADCAST) != 0 && iface->ifa_broadaddr != NULL &&
            iface->ifa_broadaddr->sa_family == AF_INET)
        {
            const struct sockaddr_in *broadcast = (const struct sockaddr_in *)iface->ifa_broadaddr;
            broadcast_ipv4 = ntohl(broadcast->sin_addr.s_addr);
        }
        else if (iface->ifa_netmask != NULL && iface->ifa_netmask->sa_family == AF_INET)
        {
            const struct sockaddr_in *netmask = (const struct sockaddr_in *)iface->ifa_netmask;
            const Uint32 address_ipv4 = ntohl(addr->sin_addr.s_addr);
            const Uint32 mask_ipv4 = ntohl(netmask->sin_addr.s_addr);
            broadcast_ipv4 = address_ipv4 | ~mask_ipv4;
        }

        if (broadcast_ipv4 == 0)
        {
            continue;
        }

        struct in_addr broadcast_addr;
        char host[INET_ADDRSTRLEN];
        broadcast_addr.s_addr = htonl(broadcast_ipv4);
        if (inet_ntop(AF_INET, &broadcast_addr, host, sizeof(host)) == NULL)
        {
            continue;
        }

        (void)slayer3d_network_discovery_add_host_to_list(hosts, &count, max, host);
    }

    freeifaddrs(interfaces);
    return count;
}

static int slayer3d_network_collect_lan_unicast_hosts(char hosts[][SLAYER3D_NETWORK_MAX_HOST_LENGTH], int max)
{
    struct ifaddrs *interfaces = NULL;
    int count = 0;

    if (hosts == NULL || max <= 0)
    {
        return 0;
    }

    if (getifaddrs(&interfaces) != 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery interface enumeration failed");
        return 0;
    }

    for (const struct ifaddrs *iface = interfaces; iface != NULL && count < max; iface = iface->ifa_next)
    {
        if (iface->ifa_addr == NULL || iface->ifa_addr->sa_family != AF_INET || (iface->ifa_flags & IFF_UP) == 0 ||
            (iface->ifa_flags & IFF_LOOPBACK) != 0)
        {
            continue;
        }

        const struct sockaddr_in *addr = (const struct sockaddr_in *)iface->ifa_addr;
        const Uint32 address_ipv4 = ntohl(addr->sin_addr.s_addr);
        const Uint32 network24 = address_ipv4 & 0xFFFFFF00u;

        for (Uint32 host_octet = 1; host_octet <= 254u && count < max; ++host_octet)
        {
            const Uint32 target_ipv4 = network24 | host_octet;
            struct in_addr target_addr;
            char host[INET_ADDRSTRLEN];

            if (target_ipv4 == address_ipv4)
            {
                continue;
            }

            target_addr.s_addr = htonl(target_ipv4);
            if (inet_ntop(AF_INET, &target_addr, host, sizeof(host)) == NULL)
            {
                continue;
            }

            (void)slayer3d_network_discovery_add_host_to_list(hosts, &count, max, host);
        }
    }

    freeifaddrs(interfaces);
    return count;
}
#else
static int slayer3d_network_collect_directed_broadcast_hosts(char hosts[][SLAYER3D_NETWORK_MAX_HOST_LENGTH], int max)
{
    (void)hosts;
    (void)max;
    return 0;
}

static int slayer3d_network_collect_lan_unicast_hosts(char hosts[][SLAYER3D_NETWORK_MAX_HOST_LENGTH], int max)
{
    (void)hosts;
    (void)max;
    return 0;
}
#endif

static void slayer3d_network_discovery_add_default_targets(slayer3d_network_discovery_session *session)
{
    char hosts[SLAYER3D_NETWORK_MAX_DISCOVERY_TARGETS][SLAYER3D_NETWORK_MAX_HOST_LENGTH];
    int host_count = 0;

    if (session == NULL)
    {
        return;
    }

    host_count = slayer3d_network_collect_directed_broadcast_hosts(hosts, SLAYER3D_NETWORK_MAX_DISCOVERY_TARGETS - 1);
    for (int i = 0; i < host_count; ++i)
    {
        (void)slayer3d_network_discovery_add_target(session, hosts[i]);
    }

    (void)slayer3d_network_discovery_add_target(session, SLAYER3D_NETWORK_GLOBAL_BROADCAST);

    host_count = slayer3d_network_collect_lan_unicast_hosts(hosts, SLAYER3D_NETWORK_MAX_DISCOVERY_TARGETS);
    for (int i = 0; i < host_count; ++i)
    {
        (void)slayer3d_network_discovery_add_target(session, hosts[i]);
    }
}

static void slayer3d_network_discovery_clear_results(slayer3d_network_discovery_session *session)
{
    if (session == NULL)
    {
        return;
    }

    SDL_zeroa(session->results);
    session->result_count = 0;
}

static bool slayer3d_network_discovery_add_result(slayer3d_network_discovery_session *session, const char *session_name,
                                                  const char *host, Uint16 port, const char *status)
{
    if (session == NULL || host == NULL || host[0] == '\0' || port == 0)
    {
        return false;
    }

    for (int i = 0; i < session->result_count; ++i)
    {
        slayer3d_network_discovery_result *result = &session->results[i];
        if (SDL_strcmp(result->host, host) == 0 && result->port == port)
        {
            SDL_snprintf(result->session_name, sizeof(result->session_name), "%s",
                         session_name != NULL && session_name[0] != '\0' ? session_name : "SLAYER3D Session");
            SDL_snprintf(result->status, sizeof(result->status), "%s", status != NULL ? status : "");
            result->last_seen_ms = SDL_GetTicks();
            return true;
        }
    }

    if (session->result_count >= SLAYER3D_NETWORK_MAX_DISCOVERY_RESULTS)
    {
        return false;
    }

    slayer3d_network_discovery_result *result = &session->results[session->result_count++];
    SDL_snprintf(result->session_name, sizeof(result->session_name), "%s",
                 session_name != NULL && session_name[0] != '\0' ? session_name : "SLAYER3D Session");
    SDL_snprintf(result->host, sizeof(result->host), "%s", host);
    SDL_snprintf(result->status, sizeof(result->status), "%s", status != NULL ? status : "");
    result->port = port;
    result->last_seen_ms = SDL_GetTicks();
    return true;
}

static void slayer3d_network_discovery_destroy_socket(slayer3d_network_discovery_session *session)
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

static bool slayer3d_network_discovery_send_packet_to(slayer3d_network_discovery_session *session, NET_Address *address,
                                                      Uint16 port, slayer3d_network_packet_kind kind,
                                                      const void *payload, int payload_size)
{
    Uint8 packet[SLAYER3D_NETWORK_MAX_PACKET_SIZE];
    const int size = slayer3d_network_encode_packet(packet, (int)sizeof(packet), kind, payload, payload_size);
    if (session == NULL || session->socket == NULL || address == NULL || port == 0 || size < 0)
    {
        return false;
    }
    return NET_SendDatagram(session->socket, address, port, packet, size);
}

static bool slayer3d_network_discovery_send_probe(slayer3d_network_discovery_session *session)
{
    bool any_sent = false;
    bool any_pending = false;
    int processed = 0;

    if (session == NULL || session->socket == NULL)
    {
        return false;
    }

    if (session->target_count <= 0)
    {
        return false;
    }

    SDL_snprintf(session->status, sizeof(session->status), "Scanning for local matches");
    for (int i = session->next_probe_index;
         i < session->target_count && processed < SLAYER3D_NETWORK_DISCOVERY_BATCH_SIZE; ++i)
    {
        NET_Status target_status;
        if (session->target_addresses[i] == NULL || session->target_probe_sent[i])
        {
            session->next_probe_index = i + 1;
            continue;
        }

        target_status = NET_GetAddressStatus(session->target_addresses[i]);
        if (target_status != NET_SUCCESS)
        {
            (void)NET_WaitUntilResolved(session->target_addresses[i], 0);
            target_status = NET_GetAddressStatus(session->target_addresses[i]);
        }
        if (target_status == NET_FAILURE)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery target resolution failed: target=%s error=%s",
                        session->target_hosts[i], SDL_GetError());
            session->target_probe_sent[i] = true;
            session->next_probe_index = i + 1;
            processed++;
            continue;
        }
        if (target_status != NET_SUCCESS)
        {
            any_pending = true;
            break;
        }

        if (session->target_count <= 16)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery probe send: target=%s port=%u",
                        session->target_hosts[i][0] != '\0' ? session->target_hosts[i] : "<unknown>",
                        (unsigned int)session->target_port);
        }
        if (slayer3d_network_discovery_send_packet_to(session, session->target_addresses[i], session->target_port,
                                                      SLAYER3D_NETWORK_PACKET_DISCOVERY_QUERY, NULL, 0))
        {
            any_sent = true;
        }
        else
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery probe send failed: target=%s error=%s",
                        session->target_hosts[i], SDL_GetError());
        }
        session->target_probe_sent[i] = true;
        session->next_probe_index = i + 1;
        processed++;
    }

    if (any_pending)
    {
        SDL_snprintf(session->status, sizeof(session->status), "Resolving discovery targets");
    }
    return any_sent || any_pending;
}

typedef struct slayer3d_network_discovery_reply_payload
{
    Uint8 port[2];
    char session_name[SLAYER3D_NETWORK_MAX_HOST_LENGTH];
    char status[SLAYER3D_NETWORK_MAX_STATUS_LENGTH];
} slayer3d_network_discovery_reply_payload;

static void slayer3d_network_discovery_process_datagram(slayer3d_network_discovery_session *session,
                                                        const NET_Datagram *dgram)
{
    if (session == NULL || dgram == NULL || dgram->buf == NULL || dgram->buflen <= 0)
    {
        return;
    }

    slayer3d_network_packet_kind kind;
    const Uint8 *payload = NULL;
    int payload_size = 0;
    if (!slayer3d_network_decode_packet(dgram->buf, dgram->buflen, &kind, &payload, &payload_size))
    {
        return;
    }

    if (kind != SLAYER3D_NETWORK_PACKET_DISCOVERY_REPLY ||
        payload_size != (int)sizeof(slayer3d_network_discovery_reply_payload))
    {
        return;
    }

    const slayer3d_network_discovery_reply_payload *reply = (const slayer3d_network_discovery_reply_payload *)payload;
    char host_string[SLAYER3D_NETWORK_MAX_HOST_LENGTH];
    const Uint16 announced_port = slayer3d_network_read_u16(reply->port);

    SDL_snprintf(host_string, sizeof(host_string), "%s",
                 NET_GetAddressString(dgram->addr) != NULL ? NET_GetAddressString(dgram->addr) : "");

    if (host_string[0] == '\0')
    {
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery reply received: host=%s port=%u", host_string,
                (unsigned int)announced_port);
    if (!slayer3d_network_discovery_add_result(session, reply->session_name, host_string, announced_port,
                                               reply->status))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery result dropped: list full");
    }
    else
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "SLAYER3D discovery result added: session=%s host=%s port=%u status=%s", reply->session_name,
                    host_string, (unsigned int)announced_port, reply->status);
    }
}

static void slayer3d_network_discovery_process_query(slayer3d_network_session *session, const NET_Datagram *dgram)
{
    if (session == NULL || dgram == NULL || session->state != SLAYER3D_NETWORK_STATE_WAITING || dgram->addr == NULL ||
        dgram->port == 0)
    {
        return;
    }

    slayer3d_network_discovery_reply_payload payload;
    const char *session_name = slayer3d_network_session_advertised_name(session);
    const char *status = session->status[0] != '\0' ? session->status : "Awaiting client";
    SDL_zero(payload);
    SDL_snprintf(payload.session_name, sizeof(payload.session_name), "%s", session_name);
    SDL_snprintf(payload.status, sizeof(payload.status), "%s", status);
    slayer3d_network_write_u16(payload.port,
                               session->local_bound_port != 0 ? session->local_bound_port : session->desc.port);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery query received: host=%s port=%u session=%s status=%s",
                NET_GetAddressString(dgram->addr) != NULL ? NET_GetAddressString(dgram->addr) : "<unknown>",
                (unsigned int)dgram->port, session_name, status);
    SDL_LogInfo(
        SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network discovery reply: session=%s host=%s port=%u status=%s",
        session_name, NET_GetAddressString(dgram->addr) != NULL ? NET_GetAddressString(dgram->addr) : "<unknown>",
        (unsigned int)(session->local_bound_port != 0 ? session->local_bound_port : session->desc.port), status);
    (void)slayer3d_network_send_packet_to(session, dgram->addr, dgram->port, SLAYER3D_NETWORK_PACKET_DISCOVERY_REPLY,
                                          &payload, (int)sizeof(payload));
}
#endif
#if SLAYER3D_NETWORKING_ENABLED
static void slayer3d_network_write_u16(Uint8 *dst, Uint16 value)
{
    Uint16 encoded = SDL_Swap16LE(value);
    SDL_memcpy(dst, &encoded, sizeof(encoded));
}

static void slayer3d_network_write_u32(Uint8 *dst, Uint32 value)
{
    Uint32 encoded = SDL_Swap32LE(value);
    SDL_memcpy(dst, &encoded, sizeof(encoded));
}

static Uint16 slayer3d_network_read_u16(const Uint8 *src)
{
    Uint16 value = 0;
    SDL_memcpy(&value, src, sizeof(value));
    return SDL_Swap16LE(value);
}

static Uint32 slayer3d_network_read_u32(const Uint8 *src)
{
    Uint32 value = 0;
    SDL_memcpy(&value, src, sizeof(value));
    return SDL_Swap32LE(value);
}
#endif

#if SLAYER3D_NETWORKING_ENABLED
static int slayer3d_network_encode_packet(Uint8 *buffer, int buffer_size, slayer3d_network_packet_kind kind,
                                          const void *payload, int payload_size)
{
    const int header_size = 12;
    if (buffer == NULL || buffer_size < header_size || payload_size < 0 || payload_size > buffer_size - header_size)
    {
        return -1;
    }

    slayer3d_network_write_u32(buffer, 0x53444C33u);
    slayer3d_network_write_u16(buffer + 4, 1u);
    buffer[6] = (Uint8)kind;
    buffer[7] = 0u;
    slayer3d_network_write_u16(buffer + 8, (Uint16)payload_size);
    slayer3d_network_write_u16(buffer + 10, 0u);
    if (payload_size > 0 && payload != NULL)
    {
        SDL_memcpy(buffer + header_size, payload, (size_t)payload_size);
    }
    return header_size + payload_size;
}

static bool slayer3d_network_decode_packet(const Uint8 *buffer, int size, slayer3d_network_packet_kind *out_kind,
                                           const Uint8 **out_payload, int *out_payload_size)
{
    const int header_size = 12;
    if (buffer == NULL || size < header_size || out_kind == NULL || out_payload == NULL || out_payload_size == NULL)
    {
        return false;
    }

    if (slayer3d_network_read_u32(buffer) != 0x53444C33u)
    {
        return false;
    }

    const Uint16 version = slayer3d_network_read_u16(buffer + 4);
    if (version != 1u)
    {
        return false;
    }

    const int payload_size = (int)slayer3d_network_read_u16(buffer + 8);
    if (payload_size < 0 || payload_size > size - header_size)
    {
        return false;
    }

    *out_kind = (slayer3d_network_packet_kind)buffer[6];
    *out_payload = buffer + header_size;
    *out_payload_size = payload_size;
    return true;
}

static bool slayer3d_network_address_matches(const NET_Address *a, Uint16 port_a, const NET_Address *b, Uint16 port_b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }
    return NET_CompareAddresses(a, b) == 0 && port_a == port_b;
}

static bool slayer3d_network_send_packet_to(slayer3d_network_session *session, NET_Address *address, Uint16 port,
                                            slayer3d_network_packet_kind kind, const void *payload, int payload_size)
{
    if (session == NULL || session->socket == NULL || address == NULL || port == 0)
    {
        return false;
    }

    Uint8 packet[SLAYER3D_NETWORK_MAX_PACKET_SIZE];
    const int size = slayer3d_network_encode_packet(packet, (int)sizeof(packet), kind, payload, payload_size);
    if (size < 0)
    {
        return false;
    }
    return NET_SendDatagram(session->socket, address, port, packet, size);
}

static bool slayer3d_network_send_control(slayer3d_network_session *session, slayer3d_network_packet_kind kind,
                                          const void *payload, int payload_size)
{
    if (session == NULL || session->socket == NULL)
    {
        return false;
    }

    if (session->peer_address != NULL && session->peer_port != 0)
    {
        return slayer3d_network_send_packet_to(session, session->peer_address, session->peer_port, kind, payload,
                                               payload_size);
    }

    if (session->desc.role == SLAYER3D_NETWORK_ROLE_CLIENT && session->remote_address != NULL &&
        session->desc.port != 0)
    {
        return slayer3d_network_send_packet_to(session, session->remote_address, session->desc.port, kind, payload,
                                               payload_size);
    }

    return false;
}

static void slayer3d_network_discovery_process_query(slayer3d_network_session *session, const NET_Datagram *dgram);

static void slayer3d_network_update_connected_activity(slayer3d_network_session *session, float dt)
{
    if (session == NULL || session->state != SLAYER3D_NETWORK_STATE_CONNECTED)
    {
        return;
    }

    session->idle_elapsed += SDL_max(dt, 0.0f);
    session->keepalive_elapsed += SDL_max(dt, 0.0f);

    if (session->keepalive_elapsed >= 1.0f)
    {
        if (!slayer3d_network_send_control(session, SLAYER3D_NETWORK_PACKET_KEEPALIVE, NULL, 0))
        {
            slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_ERROR, "Failed to send keepalive");
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network keepalive send failed: %s", SDL_GetError());
            return;
        }
        session->keepalive_elapsed = 0.0f;
    }

    if (session->idle_elapsed >= session->desc.idle_timeout)
    {
        if (session->desc.role == SLAYER3D_NETWORK_ROLE_HOST)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "SLAYER3D network peer timed out; returning host to waiting state");
            slayer3d_network_clear_peer(session);
            slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_WAITING, "Awaiting client");
        }
        else
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network connection timed out");
            slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_TIMED_OUT, "Connection timed out");
        }
    }
}

static void slayer3d_network_process_datagram(slayer3d_network_session *session, const NET_Datagram *dgram)
{
    if (session == NULL || dgram == NULL || dgram->buf == NULL || dgram->buflen <= 0)
    {
        return;
    }

    slayer3d_network_packet_kind kind;
    const Uint8 *payload = NULL;
    int payload_size = 0;
    if (!slayer3d_network_decode_packet(dgram->buf, dgram->buflen, &kind, &payload, &payload_size))
    {
        return;
    }

    switch (kind)
    {
    case SLAYER3D_NETWORK_PACKET_DISCOVERY_QUERY:
        if (session->desc.role == SLAYER3D_NETWORK_ROLE_HOST)
        {
            slayer3d_network_discovery_process_query(session, dgram);
        }
        break;
    case SLAYER3D_NETWORK_PACKET_DISCOVERY_REPLY:
        break;
    case SLAYER3D_NETWORK_PACKET_HELLO:
        if (session->desc.role == SLAYER3D_NETWORK_ROLE_HOST && session->state == SLAYER3D_NETWORK_STATE_WAITING)
        {
            if (session->peer_address != NULL)
            {
                NET_UnrefAddress(session->peer_address);
            }
            session->peer_address = NET_RefAddress(dgram->addr);
            session->peer_port = dgram->port;
            session->idle_elapsed = 0.0f;
            slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_CONNECTED, "Client connected");
            if (!slayer3d_network_send_control(session, SLAYER3D_NETWORK_PACKET_WELCOME, NULL, 0))
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network welcome send failed: %s", SDL_GetError());
            }
            else
            {
                session->welcome_sent = true;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network host accepted peer on port %u",
                            (unsigned int)session->peer_port);
            }
        }
        else if (session->desc.role == SLAYER3D_NETWORK_ROLE_HOST && session->state == SLAYER3D_NETWORK_STATE_CONNECTED)
        {
            if (slayer3d_network_address_matches(session->peer_address, session->peer_port, dgram->addr, dgram->port))
            {
                (void)slayer3d_network_send_control(session, SLAYER3D_NETWORK_PACKET_WELCOME, NULL, 0);
            }
            else
            {
                (void)slayer3d_network_send_packet_to(session, dgram->addr, dgram->port, SLAYER3D_NETWORK_PACKET_REJECT,
                                                      NULL, 0);
            }
        }
        break;
    case SLAYER3D_NETWORK_PACKET_WELCOME:
        if (session->desc.role == SLAYER3D_NETWORK_ROLE_CLIENT && session->state == SLAYER3D_NETWORK_STATE_CONNECTING)
        {
            if (session->peer_address != NULL)
            {
                NET_UnrefAddress(session->peer_address);
            }
            session->peer_address = NET_RefAddress(dgram->addr);
            session->peer_port = dgram->port;
            session->idle_elapsed = 0.0f;
            session->keepalive_elapsed = 0.0f;
            slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_CONNECTED, "Connected");
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network client connected to %s:%u",
                        NET_GetAddressString(session->peer_address) != NULL
                            ? NET_GetAddressString(session->peer_address)
                            : "<unknown>",
                        (unsigned int)session->peer_port);
        }
        break;
    case SLAYER3D_NETWORK_PACKET_REJECT:
        if (session->desc.role == SLAYER3D_NETWORK_ROLE_CLIENT && session->state == SLAYER3D_NETWORK_STATE_CONNECTING)
        {
            slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_REJECTED, "Connection rejected");
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network client rejected by host");
        }
        break;
    case SLAYER3D_NETWORK_PACKET_KEEPALIVE:
        if (session->state == SLAYER3D_NETWORK_STATE_CONNECTED &&
            slayer3d_network_address_matches(session->peer_address, session->peer_port, dgram->addr, dgram->port))
        {
            session->idle_elapsed = 0.0f;
        }
        break;
    case SLAYER3D_NETWORK_PACKET_USER:
        if (session->state == SLAYER3D_NETWORK_STATE_CONNECTED &&
            slayer3d_network_address_matches(session->peer_address, session->peer_port, dgram->addr, dgram->port))
        {
            slayer3d_network_queue_packet(session, payload, payload_size);
            session->idle_elapsed = 0.0f;
        }
        break;
    default:
        break;
    }
}
#endif

void slayer3d_network_session_desc_init(slayer3d_network_session_desc *desc)
{
    if (desc == NULL)
    {
        return;
    }

    SDL_zero(*desc);
    desc->role = SLAYER3D_NETWORK_ROLE_CLIENT;
    desc->port = SLAYER3D_NETWORK_DEFAULT_PORT;
    desc->local_port = 0;
    desc->handshake_timeout = 5.0f;
    desc->idle_timeout = 10.0f;
}

bool slayer3d_network_session_create(const slayer3d_network_session_desc *desc, slayer3d_network_session **out_session)
{
    slayer3d_network_session_desc defaults;
    const slayer3d_network_session_desc *effective = desc;
    slayer3d_network_session *session = NULL;

    if (out_session == NULL)
    {
        return false;
    }
    *out_session = NULL;

    if (effective == NULL)
    {
        slayer3d_network_session_desc_init(&defaults);
        effective = &defaults;
    }

    if (effective->port == 0)
    {
        SDL_SetError("Network session requires a non-zero port.");
        return false;
    }

    session = (slayer3d_network_session *)SDL_calloc(1, sizeof(*session));
    if (session == NULL)
    {
        SDL_OutOfMemory();
        return false;
    }

    session->desc = *effective;
    SDL_zero(session->host);
    SDL_zero(session->session_name);
    SDL_zero(session->status);
    if (effective->host != NULL)
    {
        SDL_snprintf(session->host, sizeof(session->host), "%s", effective->host);
    }
    if (effective->session_name != NULL)
    {
        SDL_snprintf(session->session_name, sizeof(session->session_name), "%s", effective->session_name);
    }

    if (!slayer3d_network_library_acquire())
    {
        slayer3d_network_session_destroy(session);
        return false;
    }

#if SLAYER3D_NETWORKING_ENABLED
    const Uint16 bound_port = effective->role == SLAYER3D_NETWORK_ROLE_HOST
                                  ? effective->port
                                  : (effective->local_port != 0 ? effective->local_port : 0);
    session->socket = slayer3d_network_create_datagram_socket(bound_port, false);
    if (session->socket == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network socket create failed: %s", SDL_GetError());
        slayer3d_network_session_destroy(session);
        return false;
    }

    if (effective->role == SLAYER3D_NETWORK_ROLE_HOST)
    {
        session->local_bound_port = bound_port;
        slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_WAITING, "Awaiting client");
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network host listening on port %u",
                    (unsigned int)session->local_bound_port);
    }
    else
    {
        if (effective->host == NULL || effective->host[0] == '\0')
        {
            SDL_SetError("Client network session requires a remote host.");
            slayer3d_network_session_destroy(session);
            return false;
        }

        session->remote_address = NET_ResolveHostname(effective->host);
        if (session->remote_address == NULL)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network hostname resolution failed: %s",
                        SDL_GetError());
            slayer3d_network_session_destroy(session);
            return false;
        }

        session->local_bound_port = bound_port;
        slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_CONNECTING, "Resolving host");
        if (NET_GetAddressStatus(session->remote_address) == NET_FAILURE)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network hostname resolution error: %s", SDL_GetError());
            slayer3d_network_session_destroy(session);
            return false;
        }
    }
#else
    (void)effective;
    slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_ERROR, "Networking disabled");
    slayer3d_network_session_destroy(session);
    return false;
#endif

    *out_session = session;
    return true;
}

void slayer3d_network_session_destroy(slayer3d_network_session *session)
{
    if (session == NULL)
    {
        return;
    }

    slayer3d_network_destroy_socket(session);
    slayer3d_network_destroy_remote_address(session);
    slayer3d_network_clear_peer(session);
    slayer3d_network_library_release();
    SDL_free(session);
}

bool slayer3d_network_session_update(slayer3d_network_session *session, float dt)
{
    if (session == NULL)
    {
        return false;
    }

    if (session->socket == NULL)
    {
        return true;
    }

#if SLAYER3D_NETWORKING_ENABLED
    if (session->desc.role == SLAYER3D_NETWORK_ROLE_CLIENT && session->state == SLAYER3D_NETWORK_STATE_CONNECTING)
    {
        session->handshake_elapsed += SDL_max(dt, 0.0f);

        if (session->remote_address != NULL)
        {
            const NET_Status address_status = NET_GetAddressStatus(session->remote_address);
            if (address_status == NET_SUCCESS)
            {
                if (!session->hello_sent || session->handshake_send_elapsed >= 0.5f)
                {
                    if (!slayer3d_network_send_control(session, SLAYER3D_NETWORK_PACKET_HELLO, NULL, 0))
                    {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network hello send failed: %s",
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
                slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_ERROR, "Host resolution failed");
                return true;
            }
        }

        session->handshake_send_elapsed += SDL_max(dt, 0.0f);
        if (session->handshake_elapsed >= session->desc.handshake_timeout)
        {
            slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_TIMED_OUT, "Connection timed out");
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network connection timed out while connecting");
            return true;
        }
    }

    const int input_count = NET_WaitUntilInputAvailable((void **)&session->socket, 1, 0);
    if (input_count < 0)
    {
        slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_ERROR, "Network poll failed");
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network poll failed: %s", SDL_GetError());
        return true;
    }

    if (input_count > 0)
    {
        for (;;)
        {
            NET_Datagram *dgram = NULL;
            if (!NET_ReceiveDatagram(session->socket, &dgram))
            {
                if (session->desc.role == SLAYER3D_NETWORK_ROLE_CLIENT &&
                    session->state == SLAYER3D_NETWORK_STATE_CONNECTING)
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "SLAYER3D network receive failed while connecting; continuing to wait: %s",
                                SDL_GetError());
                    break;
                }

                slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_ERROR, "Failed to receive datagram");
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D network receive failed: %s", SDL_GetError());
                return true;
            }

            if (dgram == NULL)
            {
                break;
            }

            slayer3d_network_process_datagram(session, dgram);
            NET_DestroyDatagram(dgram);
        }
    }

    slayer3d_network_update_connected_activity(session, dt);
#else
    (void)dt;
#endif
    return true;
}

bool slayer3d_network_session_send(slayer3d_network_session *session, const void *data, int data_size)
{
    if (session == NULL || data == NULL || data_size <= 0 || data_size > SLAYER3D_NETWORK_MAX_PACKET_SIZE)
    {
        return false;
    }

#if SLAYER3D_NETWORKING_ENABLED
    if (session->state != SLAYER3D_NETWORK_STATE_CONNECTED)
    {
        SDL_SetError("Network session is not connected.");
        return false;
    }

    return slayer3d_network_send_control(session, SLAYER3D_NETWORK_PACKET_USER, data, data_size);
#else
    (void)data;
    (void)data_size;
    SDL_SetError("Networking disabled.");
    return false;
#endif
}

int slayer3d_network_session_receive(slayer3d_network_session *session, void *buffer, int buffer_size)
{
    if (session == NULL || buffer == NULL || buffer_size <= 0)
    {
        return -1;
    }

    if (session->queue_count <= 0)
    {
        return 0;
    }

    const slayer3d_network_packet_entry *entry = &session->queue[session->queue_head];
    if (entry->size > buffer_size)
    {
        SDL_SetError("Network receive buffer too small.");
        return -1;
    }

    SDL_memcpy(buffer, entry->data, (size_t)entry->size);
    const int size = entry->size;
    session->queue_head = (session->queue_head + 1) % SLAYER3D_NETWORK_MAX_QUEUE_SIZE;
    session->queue_count--;
    return size;
}

void slayer3d_network_session_disconnect(slayer3d_network_session *session)
{
    if (session == NULL)
    {
        return;
    }

    if (session->desc.role == SLAYER3D_NETWORK_ROLE_HOST)
    {
        slayer3d_network_clear_peer(session);
        slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_WAITING, "Awaiting client");
    }
    else
    {
        slayer3d_network_destroy_socket(session);
        slayer3d_network_destroy_remote_address(session);
        slayer3d_network_clear_peer(session);
        slayer3d_network_set_status(session, SLAYER3D_NETWORK_STATE_DISCONNECTED, "Disconnected");
        slayer3d_network_library_release();
    }
}

slayer3d_network_state slayer3d_network_session_state(const slayer3d_network_session *session)
{
    return session != NULL ? session->state : SLAYER3D_NETWORK_STATE_DISCONNECTED;
}

const char *slayer3d_network_session_status(const slayer3d_network_session *session)
{
    return session != NULL ? session->status : NULL;
}

bool slayer3d_network_session_is_connected(const slayer3d_network_session *session)
{
    return session != NULL && session->state == SLAYER3D_NETWORK_STATE_CONNECTED;
}

Uint16 slayer3d_network_session_port(const slayer3d_network_session *session)
{
    return session != NULL ? session->local_bound_port : 0u;
}

bool slayer3d_network_session_get_peer_endpoint(const slayer3d_network_session *session, char *host_buffer,
                                                int host_buffer_size, Uint16 *out_port)
{
    if (session == NULL || session->peer_address == NULL)
    {
        return false;
    }

#if SLAYER3D_NETWORKING_ENABLED
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

void slayer3d_network_discovery_session_desc_init(slayer3d_network_discovery_session_desc *desc)
{
    if (desc == NULL)
    {
        return;
    }

    SDL_zero(*desc);
    desc->port = SLAYER3D_NETWORK_DEFAULT_PORT;
    desc->local_port = 0;
}

bool slayer3d_network_discovery_session_create(const slayer3d_network_discovery_session_desc *desc,
                                               slayer3d_network_discovery_session **out_session)
{
    slayer3d_network_discovery_session_desc defaults;
    const slayer3d_network_discovery_session_desc *effective = desc;
    slayer3d_network_discovery_session *session = NULL;

    if (out_session == NULL)
    {
        return false;
    }
    *out_session = NULL;

    if (effective == NULL)
    {
        slayer3d_network_discovery_session_desc_init(&defaults);
        effective = &defaults;
    }

    if (effective->port == 0)
    {
        SDL_SetError("Discovery session requires a non-zero port.");
        return false;
    }

    session = (slayer3d_network_discovery_session *)SDL_calloc(1, sizeof(*session));
    if (session == NULL)
    {
        SDL_OutOfMemory();
        return false;
    }

    session->desc = *effective;
    session->target_port = effective->port;
    SDL_zero(session->status);
    session->target_count = 0;

    if (!slayer3d_network_library_acquire())
    {
        slayer3d_network_discovery_session_destroy(session);
        return false;
    }

#if SLAYER3D_NETWORKING_ENABLED
    const bool allow_broadcast = effective->host == NULL || effective->host[0] == '\0';
    session->socket = slayer3d_network_create_datagram_socket(effective->local_port != 0 ? effective->local_port : 0,
                                                              allow_broadcast);
    if (session->socket == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery socket create failed: %s", SDL_GetError());
        slayer3d_network_discovery_session_destroy(session);
        return false;
    }

    if (effective->host != NULL && effective->host[0] != '\0')
    {
        if (!slayer3d_network_discovery_add_target(session, effective->host))
        {
            slayer3d_network_discovery_session_destroy(session);
            return false;
        }
    }
    else
    {
        slayer3d_network_discovery_add_default_targets(session);
        if (session->target_count <= 0)
        {
            SDL_SetError("Discovery session has no usable probe targets.");
            slayer3d_network_discovery_session_destroy(session);
            return false;
        }
    }

    SDL_snprintf(session->status, sizeof(session->status), "Ready to scan");
#else
    slayer3d_network_discovery_session_destroy(session);
    SDL_SetError("Networking disabled.");
    return false;
#endif

    *out_session = session;
    return true;
}

void slayer3d_network_discovery_session_destroy(slayer3d_network_discovery_session *session)
{
    if (session == NULL)
    {
        return;
    }

    slayer3d_network_discovery_destroy_socket(session);
    slayer3d_network_discovery_destroy_target_address(session);
    slayer3d_network_library_release();
    SDL_free(session);
}

bool slayer3d_network_discovery_session_refresh(slayer3d_network_discovery_session *session)
{
    if (session == NULL || session->socket == NULL)
    {
        return false;
    }

    slayer3d_network_discovery_clear_results(session);
    session->elapsed = 0.0f;
    session->refresh_elapsed = 0.0f;
    session->probe_batch_elapsed = 0.0f;
    session->scanning = true;
    session->next_probe_index = 0;
    for (int i = 0; i < session->target_count; ++i)
    {
        session->target_probe_sent[i] = false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery refresh: targets=%d port=%u", session->target_count,
                (unsigned int)session->target_port);
    return slayer3d_network_discovery_send_probe(session);
}

bool slayer3d_network_discovery_session_update(slayer3d_network_discovery_session *session, float dt)
{
    if (session == NULL)
    {
        return false;
    }

    if (session->socket == NULL)
    {
        return true;
    }

#if SLAYER3D_NETWORKING_ENABLED
    session->elapsed += SDL_max(dt, 0.0f);
    session->refresh_elapsed += SDL_max(dt, 0.0f);

    if (session->scanning)
    {
        bool has_pending_probe = false;
        for (int i = session->next_probe_index; i < session->target_count; ++i)
        {
            if (!session->target_probe_sent[i])
            {
                has_pending_probe = true;
                break;
            }
        }
        if (has_pending_probe)
        {
            session->probe_batch_elapsed += SDL_max(dt, 0.0f);
            if (session->probe_batch_elapsed >= SLAYER3D_NETWORK_DISCOVERY_BATCH_INTERVAL)
            {
                session->probe_batch_elapsed = 0.0f;
                (void)slayer3d_network_discovery_send_probe(session);
            }
        }
    }

    const int input_count = NET_WaitUntilInputAvailable((void **)&session->socket, 1, 0);
    if (input_count < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery poll failed: %s", SDL_GetError());
        SDL_snprintf(session->status, sizeof(session->status), "Discovery poll failed");
        return true;
    }

    if (input_count > 0)
    {
        for (;;)
        {
            NET_Datagram *dgram = NULL;
            if (!NET_ReceiveDatagram(session->socket, &dgram))
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D discovery receive failed: %s", SDL_GetError());
                SDL_snprintf(session->status, sizeof(session->status), "Discovery receive failed");
                return true;
            }

            if (dgram == NULL)
            {
                break;
            }

            slayer3d_network_discovery_process_datagram(session, dgram);
            NET_DestroyDatagram(dgram);
        }
    }

    if (session->scanning && session->result_count == 0)
    {
        SDL_snprintf(session->status, sizeof(session->status), "Searching for local matches");
    }
    else if (session->result_count > 0)
    {
        SDL_snprintf(session->status, sizeof(session->status), "%d local match%s found", session->result_count,
                     session->result_count == 1 ? "" : "es");
        session->scanning = false;
    }

    return true;
#else
    (void)dt;
    return false;
#endif
}

int slayer3d_network_discovery_session_result_count(const slayer3d_network_discovery_session *session)
{
    return session != NULL ? session->result_count : 0;
}

bool slayer3d_network_discovery_session_get_result(const slayer3d_network_discovery_session *session, int index,
                                                   slayer3d_network_discovery_result *out_result)
{
    if (session == NULL || out_result == NULL || index < 0 || index >= session->result_count)
    {
        return false;
    }

    *out_result = session->results[index];
    return true;
}

const char *slayer3d_network_discovery_session_status(const slayer3d_network_discovery_session *session)
{
    return session != NULL ? session->status : NULL;
}
#else
void slayer3d_network_session_desc_init(slayer3d_network_session_desc *desc)
{
    if (desc == NULL)
    {
        return;
    }

    SDL_zero(*desc);
    desc->role = SLAYER3D_NETWORK_ROLE_CLIENT;
    desc->port = SLAYER3D_NETWORK_DEFAULT_PORT;
    desc->local_port = 0;
    desc->handshake_timeout = 5.0f;
    desc->idle_timeout = 10.0f;
}

bool slayer3d_network_session_create(const slayer3d_network_session_desc *desc, slayer3d_network_session **out_session)
{
    (void)desc;
    if (out_session != NULL)
    {
        *out_session = NULL;
    }
    SDL_SetError("SLAYER3D networking is disabled at build time.");
    return false;
}

void slayer3d_network_session_destroy(slayer3d_network_session *session)
{
    (void)session;
}

bool slayer3d_network_session_update(slayer3d_network_session *session, float dt)
{
    (void)session;
    (void)dt;
    return false;
}

bool slayer3d_network_session_send(slayer3d_network_session *session, const void *data, int data_size)
{
    (void)session;
    (void)data;
    (void)data_size;
    SDL_SetError("SLAYER3D networking is disabled at build time.");
    return false;
}

int slayer3d_network_session_receive(slayer3d_network_session *session, void *buffer, int buffer_size)
{
    (void)session;
    (void)buffer;
    (void)buffer_size;
    SDL_SetError("SLAYER3D networking is disabled at build time.");
    return -1;
}

void slayer3d_network_session_disconnect(slayer3d_network_session *session)
{
    (void)session;
}

slayer3d_network_state slayer3d_network_session_state(const slayer3d_network_session *session)
{
    (void)session;
    return SLAYER3D_NETWORK_STATE_DISCONNECTED;
}

const char *slayer3d_network_session_status(const slayer3d_network_session *session)
{
    (void)session;
    return "Networking disabled";
}

bool slayer3d_network_session_is_connected(const slayer3d_network_session *session)
{
    (void)session;
    return false;
}

Uint16 slayer3d_network_session_port(const slayer3d_network_session *session)
{
    (void)session;
    return 0;
}

bool slayer3d_network_session_get_peer_endpoint(const slayer3d_network_session *session, char *host_buffer,
                                                int host_buffer_size, Uint16 *out_port)
{
    (void)session;
    (void)host_buffer;
    (void)host_buffer_size;
    (void)out_port;
    return false;
}

void slayer3d_network_discovery_session_desc_init(slayer3d_network_discovery_session_desc *desc)
{
    if (desc == NULL)
    {
        return;
    }

    SDL_zero(*desc);
    desc->port = SLAYER3D_NETWORK_DEFAULT_PORT;
    desc->local_port = 0;
}

bool slayer3d_network_discovery_session_create(const slayer3d_network_discovery_session_desc *desc,
                                               slayer3d_network_discovery_session **out_session)
{
    (void)desc;
    if (out_session != NULL)
    {
        *out_session = NULL;
    }
    SDL_SetError("SLAYER3D networking is disabled at build time.");
    return false;
}

void slayer3d_network_discovery_session_destroy(slayer3d_network_discovery_session *session)
{
    (void)session;
}

bool slayer3d_network_discovery_session_refresh(slayer3d_network_discovery_session *session)
{
    (void)session;
    SDL_SetError("SLAYER3D networking is disabled at build time.");
    return false;
}

bool slayer3d_network_discovery_session_update(slayer3d_network_discovery_session *session, float dt)
{
    (void)session;
    (void)dt;
    return false;
}

int slayer3d_network_discovery_session_result_count(const slayer3d_network_discovery_session *session)
{
    (void)session;
    return 0;
}

bool slayer3d_network_discovery_session_get_result(const slayer3d_network_discovery_session *session, int index,
                                                   slayer3d_network_discovery_result *out_result)
{
    (void)session;
    (void)index;
    (void)out_result;
    return false;
}

const char *slayer3d_network_discovery_session_status(const slayer3d_network_discovery_session *session)
{
    (void)session;
    return "Networking disabled";
}
#endif
