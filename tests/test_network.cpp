#include <gtest/gtest.h>

#include <cstring>

extern "C"
{
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include "slayer3d/network.h"
}

namespace
{
constexpr Uint16 kBasePort = 27183;
constexpr int kPumpLimit = 400;
constexpr float kConnectTimeoutSeconds = 8.0f;
constexpr Uint32 kPumpSleepMs = 2;

struct NetworkPair
{
    slayer3d_network_session *host = nullptr;
    slayer3d_network_session *client = nullptr;
    Uint16 port = 0;
};

struct DiscoveryPair
{
    slayer3d_network_session *host = nullptr;
    slayer3d_network_discovery_session *scanner = nullptr;
    Uint16 port = 0;
};

void destroy_pair(NetworkPair *pair)
{
    if (pair == nullptr)
    {
        return;
    }

    slayer3d_network_session_destroy(pair->client);
    slayer3d_network_session_destroy(pair->host);
    pair->client = nullptr;
    pair->host = nullptr;
    pair->port = 0;
}

bool create_host_client_pair(NetworkPair *pair)
{
    if (pair == nullptr)
    {
        return false;
    }

    for (Uint16 port = kBasePort; port < (Uint16)(kBasePort + 64); ++port)
    {
        slayer3d_network_session_desc host_desc{};
        slayer3d_network_session_desc_init(&host_desc);
        host_desc.role = SLAYER3D_NETWORK_ROLE_HOST;
        host_desc.port = port;
        host_desc.handshake_timeout = kConnectTimeoutSeconds;
        host_desc.idle_timeout = kConnectTimeoutSeconds;
        host_desc.session_name = "SLAYER3D Test Host";

        if (!slayer3d_network_session_create(&host_desc, &pair->host))
        {
            continue;
        }

        slayer3d_network_session_desc client_desc{};
        slayer3d_network_session_desc_init(&client_desc);
        client_desc.role = SLAYER3D_NETWORK_ROLE_CLIENT;
        client_desc.host = "127.0.0.1";
        client_desc.port = port;
        client_desc.handshake_timeout = kConnectTimeoutSeconds;
        client_desc.idle_timeout = kConnectTimeoutSeconds;

        if (!slayer3d_network_session_create(&client_desc, &pair->client))
        {
            slayer3d_network_session_destroy(pair->host);
            pair->host = nullptr;
            continue;
        }

        pair->port = port;
        return true;
    }

    return false;
}

bool pump_until_connected(NetworkPair *pair)
{
    if (pair == nullptr || pair->host == nullptr || pair->client == nullptr)
    {
        return false;
    }

    const Uint64 start = SDL_GetTicks();
    Uint64 last = start;
    while ((SDL_GetTicks() - start) < (Uint64)(kConnectTimeoutSeconds * 1000.0f))
    {
        const Uint64 now = SDL_GetTicks();
        float dt = (float)(now - last) / 1000.0f;
        if (dt <= 0.0f)
        {
            dt = 0.001f;
        }
        last = now;

        EXPECT_TRUE(slayer3d_network_session_update(pair->host, dt));
        EXPECT_TRUE(slayer3d_network_session_update(pair->client, dt));
        if (slayer3d_network_session_is_connected(pair->host) && slayer3d_network_session_is_connected(pair->client))
        {
            return true;
        }
        if (slayer3d_network_session_state(pair->client) == SLAYER3D_NETWORK_STATE_REJECTED ||
            slayer3d_network_session_state(pair->client) == SLAYER3D_NETWORK_STATE_TIMED_OUT ||
            slayer3d_network_session_state(pair->client) == SLAYER3D_NETWORK_STATE_ERROR)
        {
            return false;
        }
        SDL_Delay(kPumpSleepMs);
    }
    return false;
}

void destroy_discovery_pair(DiscoveryPair *pair)
{
    if (pair == nullptr)
    {
        return;
    }

    slayer3d_network_discovery_session_destroy(pair->scanner);
    slayer3d_network_session_destroy(pair->host);
    pair->scanner = nullptr;
    pair->host = nullptr;
    pair->port = 0;
}

bool create_discovery_pair(DiscoveryPair *pair)
{
    if (pair == nullptr)
    {
        return false;
    }

    for (Uint16 port = kBasePort; port < (Uint16)(kBasePort + 64); ++port)
    {
        slayer3d_network_session_desc host_desc{};
        slayer3d_network_session_desc_init(&host_desc);
        host_desc.role = SLAYER3D_NETWORK_ROLE_HOST;
        host_desc.port = port;
        host_desc.session_name = "SLAYER3D Discovery Host";
        host_desc.handshake_timeout = kConnectTimeoutSeconds;
        host_desc.idle_timeout = kConnectTimeoutSeconds;

        if (!slayer3d_network_session_create(&host_desc, &pair->host))
        {
            continue;
        }

        slayer3d_network_discovery_session_desc scanner_desc{};
        slayer3d_network_discovery_session_desc_init(&scanner_desc);
        scanner_desc.host = "127.0.0.1";
        scanner_desc.port = port;

        if (!slayer3d_network_discovery_session_create(&scanner_desc, &pair->scanner))
        {
            slayer3d_network_session_destroy(pair->host);
            pair->host = nullptr;
            continue;
        }

        pair->port = port;
        return true;
    }

    return false;
}

bool pump_until_discovered(DiscoveryPair *pair, slayer3d_network_discovery_result *out_result)
{
    if (pair == nullptr)
    {
        return false;
    }

    EXPECT_TRUE(slayer3d_network_discovery_session_refresh(pair->scanner));
    const Uint64 start = SDL_GetTicks();
    Uint64 last = start;
    while ((SDL_GetTicks() - start) < (Uint64)(kConnectTimeoutSeconds * 1000.0f))
    {
        const Uint64 now = SDL_GetTicks();
        float dt = (float)(now - last) / 1000.0f;
        if (dt <= 0.0f)
        {
            dt = 0.001f;
        }
        last = now;

        EXPECT_TRUE(slayer3d_network_session_update(pair->host, dt));
        EXPECT_TRUE(slayer3d_network_discovery_session_update(pair->scanner, dt));
        if (slayer3d_network_discovery_session_result_count(pair->scanner) > 0)
        {
            if (out_result != nullptr)
            {
                EXPECT_TRUE(slayer3d_network_discovery_session_get_result(pair->scanner, 0, out_result));
            }
            return true;
        }
        SDL_Delay(kPumpSleepMs);
    }
    return false;
}

bool pump_sessions_until_connected(slayer3d_network_session *host, slayer3d_network_session *client,
                                   float timeout_seconds)
{
    if (host == nullptr || client == nullptr || timeout_seconds <= 0.0f)
    {
        return false;
    }

    const Uint64 start = SDL_GetTicks();
    Uint64 last = start;
    while ((SDL_GetTicks() - start) < (Uint64)(timeout_seconds * 1000.0f))
    {
        const Uint64 now = SDL_GetTicks();
        float dt = (float)(now - last) / 1000.0f;
        if (dt <= 0.0f)
        {
            dt = 0.001f;
        }
        last = now;

        EXPECT_TRUE(slayer3d_network_session_update(host, dt));
        EXPECT_TRUE(slayer3d_network_session_update(client, dt));
        if (slayer3d_network_session_is_connected(host) && slayer3d_network_session_is_connected(client))
        {
            return true;
        }
        if (slayer3d_network_session_state(client) == SLAYER3D_NETWORK_STATE_REJECTED ||
            slayer3d_network_session_state(client) == SLAYER3D_NETWORK_STATE_TIMED_OUT ||
            slayer3d_network_session_state(client) == SLAYER3D_NETWORK_STATE_ERROR)
        {
            return false;
        }
        SDL_Delay(kPumpSleepMs);
    }
    return false;
}
} // namespace

TEST(NetworkSession, HostAndClientCanHandshakeAndExchangePackets)
{
    NetworkPair pair{};
    ASSERT_TRUE(create_host_client_pair(&pair));

    ASSERT_TRUE(pump_until_connected(&pair));
    EXPECT_TRUE(slayer3d_network_session_is_connected(pair.host));
    EXPECT_TRUE(slayer3d_network_session_is_connected(pair.client));

    char endpoint_host[SLAYER3D_NETWORK_MAX_HOST_LENGTH]{};
    Uint16 endpoint_port = 0;
    EXPECT_TRUE(slayer3d_network_session_get_peer_endpoint(pair.client, endpoint_host, (int)sizeof(endpoint_host),
                                                           &endpoint_port));
    EXPECT_NE(endpoint_port, 0);
    EXPECT_STRNE(endpoint_host, "");

    const char payload[] = "hello-network";
    ASSERT_TRUE(slayer3d_network_session_send(pair.client, payload, (int)sizeof(payload)));

    for (int i = 0; i < 25; ++i)
    {
        EXPECT_TRUE(slayer3d_network_session_update(pair.host, 0.016f));
        EXPECT_TRUE(slayer3d_network_session_update(pair.client, 0.016f));
    }

    char recv_buffer[64]{};
    const int received = slayer3d_network_session_receive(pair.host, recv_buffer, (int)sizeof(recv_buffer));
    ASSERT_GT(received, 0);
    EXPECT_EQ(received, (int)sizeof(payload));
    EXPECT_EQ(std::memcmp(recv_buffer, payload, sizeof(payload)), 0);

    destroy_pair(&pair);
}

TEST(NetworkSession, LanDiscoveryFindsWaitingHost)
{
    DiscoveryPair pair{};
    ASSERT_TRUE(create_discovery_pair(&pair));

    slayer3d_network_discovery_result result{};
    ASSERT_TRUE(pump_until_discovered(&pair, &result));
    EXPECT_STREQ(result.session_name, "SLAYER3D Discovery Host");
    EXPECT_EQ(result.port, pair.port);
    EXPECT_STREQ(result.status, "Awaiting client");
    EXPECT_STRNE(result.host, "");

    destroy_discovery_pair(&pair);
}

TEST(NetworkSession, ClientCanConnectToDiscoveredHost)
{
    DiscoveryPair pair{};
    ASSERT_TRUE(create_discovery_pair(&pair));

    slayer3d_network_discovery_result result{};
    ASSERT_TRUE(pump_until_discovered(&pair, &result));

    slayer3d_network_session_desc client_desc{};
    slayer3d_network_session_desc_init(&client_desc);
    client_desc.role = SLAYER3D_NETWORK_ROLE_CLIENT;
    client_desc.host = result.host;
    client_desc.port = result.port;
    client_desc.handshake_timeout = kConnectTimeoutSeconds;
    client_desc.idle_timeout = kConnectTimeoutSeconds;

    slayer3d_network_session *client = nullptr;
    ASSERT_TRUE(slayer3d_network_session_create(&client_desc, &client));

    const bool connected = pump_sessions_until_connected(pair.host, client, kConnectTimeoutSeconds);

    EXPECT_TRUE(connected) << "discovered_host=" << result.host << " port=" << result.port
                           << " host_status=" << slayer3d_network_session_status(pair.host)
                           << " client_status=" << slayer3d_network_session_status(client);
    EXPECT_TRUE(slayer3d_network_session_is_connected(pair.host));
    EXPECT_TRUE(slayer3d_network_session_is_connected(client));

    slayer3d_network_session_destroy(client);
    destroy_discovery_pair(&pair);
}

TEST(NetworkSession, HostRejectsSecondClient)
{
    NetworkPair pair{};
    ASSERT_TRUE(create_host_client_pair(&pair));
    ASSERT_TRUE(pump_until_connected(&pair));

    slayer3d_network_session_desc second_desc{};
    slayer3d_network_session_desc_init(&second_desc);
    second_desc.role = SLAYER3D_NETWORK_ROLE_CLIENT;
    second_desc.host = "127.0.0.1";
    second_desc.port = pair.port;
    second_desc.handshake_timeout = kConnectTimeoutSeconds;
    second_desc.idle_timeout = 1.0f;

    slayer3d_network_session *second_client = nullptr;
    ASSERT_TRUE(slayer3d_network_session_create(&second_desc, &second_client));

    bool rejected = false;
    const Uint64 start = SDL_GetTicks();
    Uint64 last = start;
    while ((SDL_GetTicks() - start) < (Uint64)(kConnectTimeoutSeconds * 1000.0f))
    {
        const Uint64 now = SDL_GetTicks();
        float dt = (float)(now - last) / 1000.0f;
        if (dt <= 0.0f)
        {
            dt = 0.001f;
        }
        last = now;

        EXPECT_TRUE(slayer3d_network_session_update(pair.host, dt));
        EXPECT_TRUE(slayer3d_network_session_update(pair.client, dt));
        EXPECT_TRUE(slayer3d_network_session_update(second_client, dt));
        if (slayer3d_network_session_state(second_client) == SLAYER3D_NETWORK_STATE_REJECTED)
        {
            rejected = true;
            break;
        }
        if (slayer3d_network_session_state(second_client) == SLAYER3D_NETWORK_STATE_TIMED_OUT ||
            slayer3d_network_session_state(second_client) == SLAYER3D_NETWORK_STATE_ERROR)
        {
            break;
        }
        SDL_Delay(kPumpSleepMs);
    }

    EXPECT_TRUE(rejected);
    slayer3d_network_session_destroy(second_client);
    destroy_pair(&pair);
}

TEST(NetworkSession, ClientTimesOutWhenNoHostResponds)
{
    slayer3d_network_session_desc desc{};
    slayer3d_network_session_desc_init(&desc);
    desc.role = SLAYER3D_NETWORK_ROLE_CLIENT;
    desc.host = "127.0.0.1";
    desc.port = (Uint16)(kBasePort + 100);
    desc.handshake_timeout = 0.1f;
    desc.idle_timeout = 0.5f;

    slayer3d_network_session *client = nullptr;
    ASSERT_TRUE(slayer3d_network_session_create(&desc, &client));

    bool timed_out = false;
    for (int i = 0; i < kPumpLimit; ++i)
    {
        EXPECT_TRUE(slayer3d_network_session_update(client, 0.02f));
        if (slayer3d_network_session_state(client) == SLAYER3D_NETWORK_STATE_TIMED_OUT)
        {
            timed_out = true;
            break;
        }
    }

    EXPECT_TRUE(timed_out);
    slayer3d_network_session_destroy(client);
}
