/*
 * @file slayer3d_crypto.h
 * @brief Small BLAKE2s-based helpers for SLAYER3D pack obfuscation.
 *
 * This is intentionally minimal: a fast hash primitive and a keystream XOR
 * helper for pack wrapping. It is not exposed as a public SDK surface.
 */

#ifndef SLAYER3D_CRYPTO_H
#define SLAYER3D_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        SLAYER3D_CRYPTO_HASH_SIZE = 32,
        SLAYER3D_CRYPTO_SALT_SIZE = 16,
        SLAYER3D_CRYPTO_NONCE_SIZE = 16,
        SLAYER3D_CRYPTO_TAG_SIZE = 16,
        SLAYER3D_CRYPTO_STREAM_BLOCK_SIZE = 32,
    };

    typedef struct slayer3d_crypto_hash32_state
    {
        uint32_t h[8];
        uint32_t t[2];
        uint32_t f[2];
        uint8_t buf[64];
        size_t buflen;
        size_t outlen;
        size_t keylen;
    } slayer3d_crypto_hash32_state;

    void slayer3d_crypto_hash32_init(slayer3d_crypto_hash32_state *state);
    void slayer3d_crypto_hash32_init_keyed(slayer3d_crypto_hash32_state *state, const void *key, size_t key_size);
    void slayer3d_crypto_hash32_update(slayer3d_crypto_hash32_state *state, const void *data, size_t size);
    void slayer3d_crypto_hash32_final(slayer3d_crypto_hash32_state *state, uint8_t out[SLAYER3D_CRYPTO_HASH_SIZE]);

    void slayer3d_crypto_hash32(const void *data, size_t size, uint8_t out[SLAYER3D_CRYPTO_HASH_SIZE]);
    void slayer3d_crypto_hash32_keyed(const void *key, size_t key_size, const void *data, size_t size,
                                   uint8_t out[SLAYER3D_CRYPTO_HASH_SIZE]);

    void slayer3d_crypto_xor_stream(uint8_t *buffer, size_t size, const uint8_t key[SLAYER3D_CRYPTO_HASH_SIZE],
                                 const uint8_t nonce[SLAYER3D_CRYPTO_NONCE_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_CRYPTO_H */
