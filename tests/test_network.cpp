#include <gtest/gtest.h>

#include <cstring>

extern "C"
{
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/network.h"
}

namespace
{
constexpr Uint16 kBasePort = 27183;
constexpr int kPumpLimit = 400;

struct NetworkPair
{
    sdl3d_network_session *host = nullptr;
    sdl3d_network_session *client = nullptr;
    Uint16 port = 0;
};

void destroy_pair(NetworkPair *pair)
{
    if (pair == nullptr)
    {
        return;
    }

    sdl3d_network_session_destroy(pair->client);
    sdl3d_network_session_destroy(pair->host);
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
        sdl3d_network_session_desc host_desc{};
        sdl3d_network_session_desc_init(&host_desc);
        host_desc.role = SDL3D_NETWORK_ROLE_HOST;
        host_desc.port = port;
        host_desc.handshake_timeout = 2.0f;
        host_desc.idle_timeout = 2.0f;

        if (!sdl3d_network_session_create(&host_desc, &pair->host))
        {
            continue;
        }

        sdl3d_network_session_desc client_desc{};
        sdl3d_network_session_desc_init(&client_desc);
        client_desc.role = SDL3D_NETWORK_ROLE_CLIENT;
        client_desc.host = "127.0.0.1";
        client_desc.port = port;
        client_desc.handshake_timeout = 2.0f;
        client_desc.idle_timeout = 2.0f;

        if (!sdl3d_network_session_create(&client_desc, &pair->client))
        {
            sdl3d_network_session_destroy(pair->host);
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
    for (int i = 0; i < kPumpLimit; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(pair->host, 0.016f));
        EXPECT_TRUE(sdl3d_network_session_update(pair->client, 0.016f));
        if (sdl3d_network_session_is_connected(pair->host) && sdl3d_network_session_is_connected(pair->client))
        {
            return true;
        }
        if (sdl3d_network_session_state(pair->client) == SDL3D_NETWORK_STATE_REJECTED ||
            sdl3d_network_session_state(pair->client) == SDL3D_NETWORK_STATE_TIMED_OUT ||
            sdl3d_network_session_state(pair->client) == SDL3D_NETWORK_STATE_ERROR)
        {
            return false;
        }
    }
    return false;
}
} // namespace

TEST(NetworkSession, HostAndClientCanHandshakeAndExchangePackets)
{
    NetworkPair pair{};
    ASSERT_TRUE(create_host_client_pair(&pair));

    ASSERT_TRUE(pump_until_connected(&pair));
    EXPECT_TRUE(sdl3d_network_session_is_connected(pair.host));
    EXPECT_TRUE(sdl3d_network_session_is_connected(pair.client));

    char endpoint_host[SDL3D_NETWORK_MAX_HOST_LENGTH]{};
    Uint16 endpoint_port = 0;
    EXPECT_TRUE(sdl3d_network_session_get_peer_endpoint(pair.client, endpoint_host, (int)sizeof(endpoint_host),
                                                        &endpoint_port));
    EXPECT_NE(endpoint_port, 0);
    EXPECT_STRNE(endpoint_host, "");

    const char payload[] = "hello-network";
    ASSERT_TRUE(sdl3d_network_session_send(pair.client, payload, (int)sizeof(payload)));

    for (int i = 0; i < 25; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(pair.host, 0.016f));
        EXPECT_TRUE(sdl3d_network_session_update(pair.client, 0.016f));
    }

    char recv_buffer[64]{};
    const int received = sdl3d_network_session_receive(pair.host, recv_buffer, (int)sizeof(recv_buffer));
    ASSERT_GT(received, 0);
    EXPECT_EQ(received, (int)sizeof(payload));
    EXPECT_EQ(std::memcmp(recv_buffer, payload, sizeof(payload)), 0);

    destroy_pair(&pair);
}

TEST(NetworkSession, HostRejectsSecondClient)
{
    NetworkPair pair{};
    ASSERT_TRUE(create_host_client_pair(&pair));
    ASSERT_TRUE(pump_until_connected(&pair));

    sdl3d_network_session_desc second_desc{};
    sdl3d_network_session_desc_init(&second_desc);
    second_desc.role = SDL3D_NETWORK_ROLE_CLIENT;
    second_desc.host = "127.0.0.1";
    second_desc.port = pair.port;
    second_desc.handshake_timeout = 1.0f;
    second_desc.idle_timeout = 1.0f;

    sdl3d_network_session *second_client = nullptr;
    ASSERT_TRUE(sdl3d_network_session_create(&second_desc, &second_client));

    bool rejected = false;
    for (int i = 0; i < kPumpLimit; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(pair.host, 0.016f));
        EXPECT_TRUE(sdl3d_network_session_update(pair.client, 0.016f));
        EXPECT_TRUE(sdl3d_network_session_update(second_client, 0.016f));
        if (sdl3d_network_session_state(second_client) == SDL3D_NETWORK_STATE_REJECTED)
        {
            rejected = true;
            break;
        }
    }

    EXPECT_TRUE(rejected);
    sdl3d_network_session_destroy(second_client);
    destroy_pair(&pair);
}

TEST(NetworkSession, ClientTimesOutWhenNoHostResponds)
{
    sdl3d_network_session_desc desc{};
    sdl3d_network_session_desc_init(&desc);
    desc.role = SDL3D_NETWORK_ROLE_CLIENT;
    desc.host = "127.0.0.1";
    desc.port = (Uint16)(kBasePort + 100);
    desc.handshake_timeout = 0.1f;
    desc.idle_timeout = 0.5f;

    sdl3d_network_session *client = nullptr;
    ASSERT_TRUE(sdl3d_network_session_create(&desc, &client));

    bool timed_out = false;
    for (int i = 0; i < kPumpLimit; ++i)
    {
        EXPECT_TRUE(sdl3d_network_session_update(client, 0.02f));
        if (sdl3d_network_session_state(client) == SDL3D_NETWORK_STATE_TIMED_OUT)
        {
            timed_out = true;
            break;
        }
    }

    EXPECT_TRUE(timed_out);
    sdl3d_network_session_destroy(client);
}
