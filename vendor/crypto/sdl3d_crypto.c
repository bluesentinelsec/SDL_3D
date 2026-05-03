/*
 * @file sdl3d_crypto.c
 * @brief BLAKE2s helpers for SDL3D pack obfuscation.
 */

#include "sdl3d_crypto.h"

#include <string.h>

static const uint32_t sdl3d_blake2s_iv[8] = {
    0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au, 0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u,
};

static const uint8_t sdl3d_blake2s_sigma[10][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4}, {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13}, {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11}, {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5}, {2, 1, 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
};

static uint32_t sdl3d_crypto_rotr32(uint32_t value, unsigned int shift)
{
    return (value >> shift) | (value << (32u - shift));
}

static uint32_t sdl3d_crypto_load_u32le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) | ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
}

static void sdl3d_crypto_store_u32le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8u) & 0xFFu);
    data[2] = (uint8_t)((value >> 16u) & 0xFFu);
    data[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void sdl3d_crypto_store_u64le(uint8_t *data, uint64_t value)
{
    sdl3d_crypto_store_u32le(data, (uint32_t)(value & UINT32_MAX));
    sdl3d_crypto_store_u32le(data + 4u, (uint32_t)(value >> 32u));
}

static void sdl3d_crypto_blake2s_increment_counter(sdl3d_crypto_hash32_state *state, uint32_t increment)
{
    state->t[0] += increment;
    if (state->t[0] < increment)
        state->t[1] += 1u;
}

static void sdl3d_crypto_blake2s_compress(sdl3d_crypto_hash32_state *state, const uint8_t block[64])
{
    uint32_t m[16];
    uint32_t v[16];

    for (size_t i = 0; i < 16u; ++i)
        m[i] = sdl3d_crypto_load_u32le(block + i * 4u);

    for (size_t i = 0; i < 8u; ++i)
        v[i] = state->h[i];
    for (size_t i = 0; i < 8u; ++i)
        v[i + 8u] = sdl3d_blake2s_iv[i];

    v[12] ^= state->t[0];
    v[13] ^= state->t[1];
    v[14] ^= state->f[0];
    v[15] ^= state->f[1];

#define SDL3D_BLAKE2S_G(r, i, a, b, c, d)                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        a = a + b + m[sdl3d_blake2s_sigma[(r)][2u * (i) + 0u]];                                                        \
        d = sdl3d_crypto_rotr32(d ^ a, 16u);                                                                           \
        c = c + d;                                                                                                     \
        b = sdl3d_crypto_rotr32(b ^ c, 12u);                                                                           \
        a = a + b + m[sdl3d_blake2s_sigma[(r)][2u * (i) + 1u]];                                                        \
        d = sdl3d_crypto_rotr32(d ^ a, 8u);                                                                            \
        c = c + d;                                                                                                     \
        b = sdl3d_crypto_rotr32(b ^ c, 7u);                                                                            \
    } while (0)

    for (size_t round = 0; round < 10u; ++round)
    {
        SDL3D_BLAKE2S_G(round, 0, v[0], v[4], v[8], v[12]);
        SDL3D_BLAKE2S_G(round, 1, v[1], v[5], v[9], v[13]);
        SDL3D_BLAKE2S_G(round, 2, v[2], v[6], v[10], v[14]);
        SDL3D_BLAKE2S_G(round, 3, v[3], v[7], v[11], v[15]);
        SDL3D_BLAKE2S_G(round, 4, v[0], v[5], v[10], v[15]);
        SDL3D_BLAKE2S_G(round, 5, v[1], v[6], v[11], v[12]);
        SDL3D_BLAKE2S_G(round, 6, v[2], v[7], v[8], v[13]);
        SDL3D_BLAKE2S_G(round, 7, v[3], v[4], v[9], v[14]);
    }

#undef SDL3D_BLAKE2S_G

    for (size_t i = 0; i < 8u; ++i)
        state->h[i] ^= v[i] ^ v[i + 8u];
}

void sdl3d_crypto_hash32_init(sdl3d_crypto_hash32_state *state)
{
    if (state == NULL)
        return;

    memset(state, 0, sizeof(*state));
    for (size_t i = 0; i < 8u; ++i)
        state->h[i] = sdl3d_blake2s_iv[i];
    state->h[0] ^= 0x01010020u;
    state->outlen = SDL3D_CRYPTO_HASH_SIZE;
}

void sdl3d_crypto_hash32_init_keyed(sdl3d_crypto_hash32_state *state, const void *key, size_t key_size)
{
    if (state == NULL)
        return;

    if (key_size > SDL3D_CRYPTO_HASH_SIZE)
        key_size = SDL3D_CRYPTO_HASH_SIZE;

    sdl3d_crypto_hash32_init(state);
    state->h[0] ^= (uint32_t)key_size << 8u;
    state->keylen = key_size;

    if (key_size > 0u && key != NULL)
    {
        uint8_t block[64] = {0};
        memcpy(block, key, key_size);
        sdl3d_crypto_hash32_update(state, block, sizeof(block));
    }
}

void sdl3d_crypto_hash32_update(sdl3d_crypto_hash32_state *state, const void *data, size_t size)
{
    const uint8_t *input = (const uint8_t *)data;
    if (state == NULL || input == NULL || size == 0u)
        return;

    while (size > 0u)
    {
        const size_t fill = 64u - state->buflen;
        const size_t take = size < fill ? size : fill;
        memcpy(state->buf + state->buflen, input, take);
        state->buflen += take;
        input += take;
        size -= take;

        if (state->buflen == 64u)
        {
            sdl3d_crypto_blake2s_increment_counter(state, 64u);
            sdl3d_crypto_blake2s_compress(state, state->buf);
            state->buflen = 0u;
        }
    }
}

void sdl3d_crypto_hash32_final(sdl3d_crypto_hash32_state *state, uint8_t out[SDL3D_CRYPTO_HASH_SIZE])
{
    if (state == NULL || out == NULL)
        return;

    sdl3d_crypto_blake2s_increment_counter(state, (uint32_t)state->buflen);
    state->f[0] = UINT32_MAX;
    memset(state->buf + state->buflen, 0, sizeof(state->buf) - state->buflen);
    sdl3d_crypto_blake2s_compress(state, state->buf);

    for (size_t i = 0; i < 8u; ++i)
        sdl3d_crypto_store_u32le(out + i * 4u, state->h[i]);

    memset(state, 0, sizeof(*state));
}

void sdl3d_crypto_hash32(const void *data, size_t size, uint8_t out[SDL3D_CRYPTO_HASH_SIZE])
{
    sdl3d_crypto_hash32_state state;
    sdl3d_crypto_hash32_init(&state);
    sdl3d_crypto_hash32_update(&state, data, size);
    sdl3d_crypto_hash32_final(&state, out);
}

void sdl3d_crypto_hash32_keyed(const void *key, size_t key_size, const void *data, size_t size,
                               uint8_t out[SDL3D_CRYPTO_HASH_SIZE])
{
    sdl3d_crypto_hash32_state state;
    sdl3d_crypto_hash32_init_keyed(&state, key, key_size);
    sdl3d_crypto_hash32_update(&state, data, size);
    sdl3d_crypto_hash32_final(&state, out);
}

void sdl3d_crypto_xor_stream(uint8_t *buffer, size_t size, const uint8_t key[SDL3D_CRYPTO_HASH_SIZE],
                             const uint8_t nonce[SDL3D_CRYPTO_NONCE_SIZE])
{
    if (buffer == NULL || key == NULL || nonce == NULL || size == 0u)
        return;

    uint8_t counter_block[SDL3D_CRYPTO_NONCE_SIZE + sizeof(uint64_t)];
    uint8_t keystream[SDL3D_CRYPTO_STREAM_BLOCK_SIZE];
    uint64_t counter = 0u;
    size_t offset = 0u;

    memcpy(counter_block, nonce, SDL3D_CRYPTO_NONCE_SIZE);
    while (offset < size)
    {
        sdl3d_crypto_store_u64le(counter_block + SDL3D_CRYPTO_NONCE_SIZE, counter++);
        sdl3d_crypto_hash32_keyed(key, SDL3D_CRYPTO_HASH_SIZE, counter_block, sizeof(counter_block), keystream);
        const size_t chunk =
            size - offset < SDL3D_CRYPTO_STREAM_BLOCK_SIZE ? size - offset : SDL3D_CRYPTO_STREAM_BLOCK_SIZE;
        for (size_t i = 0; i < chunk; ++i)
            buffer[offset + i] ^= keystream[i];
        offset += chunk;
    }
}
