#include <gtest/gtest.h>

#include <cstring>

extern "C"
{
#include "sdl3d_crypto.h"
}

namespace
{
void derive_key(const char *password, const uint8_t salt[SDL3D_CRYPTO_SALT_SIZE], uint8_t key[SDL3D_CRYPTO_HASH_SIZE])
{
    sdl3d_crypto_hash32_state state;
    sdl3d_crypto_hash32_init(&state);
    if (password != nullptr && password[0] != '\0')
        sdl3d_crypto_hash32_update(&state, password, std::strlen(password));
    sdl3d_crypto_hash32_update(&state, salt, SDL3D_CRYPTO_SALT_SIZE);
    sdl3d_crypto_hash32_final(&state, key);
}

void derive_nonce(const uint8_t *data, size_t size, uint8_t nonce[SDL3D_CRYPTO_NONCE_SIZE])
{
    static const char label[] = "SDL3D pack nonce";
    sdl3d_crypto_hash32_state state;
    uint8_t digest[SDL3D_CRYPTO_HASH_SIZE];

    sdl3d_crypto_hash32_init(&state);
    sdl3d_crypto_hash32_update(&state, label, sizeof(label) - 1u);
    sdl3d_crypto_hash32_update(&state, data, size);
    sdl3d_crypto_hash32_final(&state, digest);
    std::memcpy(nonce, digest, SDL3D_CRYPTO_NONCE_SIZE);
}
} // namespace

TEST(CryptoHelpers, HashIsDeterministicAndSensitive)
{
    const char message[] = "SDL3D pack obfuscation";
    uint8_t first[SDL3D_CRYPTO_HASH_SIZE];
    uint8_t second[SDL3D_CRYPTO_HASH_SIZE];
    uint8_t different[SDL3D_CRYPTO_HASH_SIZE];

    sdl3d_crypto_hash32(message, sizeof(message), first);
    sdl3d_crypto_hash32(message, sizeof(message), second);
    EXPECT_EQ(std::memcmp(first, second, sizeof(first)), 0);

    const char altered[] = "SDL3D pack obfuscatioN";
    sdl3d_crypto_hash32(altered, sizeof(altered), different);
    EXPECT_NE(std::memcmp(first, different, sizeof(first)), 0);
}

TEST(CryptoHelpers, StreamCipherRoundTripsWithDerivedKey)
{
    const uint8_t plain[] = "A small blob of asset bytes used to test obfuscation.";
    uint8_t salt_digest[SDL3D_CRYPTO_HASH_SIZE];
    uint8_t salt[SDL3D_CRYPTO_SALT_SIZE];
    uint8_t nonce[SDL3D_CRYPTO_NONCE_SIZE];
    uint8_t key[SDL3D_CRYPTO_HASH_SIZE];
    uint8_t buffer[sizeof(plain)];

    sdl3d_crypto_hash32(plain, sizeof(plain), salt_digest);
    std::memcpy(salt, salt_digest, SDL3D_CRYPTO_SALT_SIZE);
    derive_nonce(plain, sizeof(plain), nonce);
    derive_key("test-password", salt, key);

    std::memcpy(buffer, plain, sizeof(plain));
    sdl3d_crypto_xor_stream(buffer, sizeof(buffer), key, nonce);
    EXPECT_NE(std::memcmp(buffer, plain, sizeof(plain)), 0);

    sdl3d_crypto_xor_stream(buffer, sizeof(buffer), key, nonce);
    EXPECT_EQ(std::memcmp(buffer, plain, sizeof(plain)), 0);
}

TEST(CryptoHelpers, KeyedTagChangesWhenPayloadChanges)
{
    const uint8_t salt[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const uint8_t header[] = {'S', '3', 'D', 'O', 'P', 'K', '1', '\0', 1, 0, 0, 0};
    const uint8_t payload[] = "payload";
    const uint8_t altered_payload[] = "payloae";
    uint8_t key[SDL3D_CRYPTO_HASH_SIZE];
    uint8_t tag_a[SDL3D_CRYPTO_HASH_SIZE];
    uint8_t tag_b[SDL3D_CRYPTO_HASH_SIZE];

    derive_key("tag-key", salt, key);
    sdl3d_crypto_hash32_state state;

    sdl3d_crypto_hash32_init_keyed(&state, key, sizeof(key));
    sdl3d_crypto_hash32_update(&state, header, sizeof(header));
    sdl3d_crypto_hash32_update(&state, payload, sizeof(payload));
    sdl3d_crypto_hash32_final(&state, tag_a);

    sdl3d_crypto_hash32_init_keyed(&state, key, sizeof(key));
    sdl3d_crypto_hash32_update(&state, header, sizeof(header));
    sdl3d_crypto_hash32_update(&state, altered_payload, sizeof(altered_payload));
    sdl3d_crypto_hash32_final(&state, tag_b);

    EXPECT_NE(std::memcmp(tag_a, tag_b, SDL3D_CRYPTO_TAG_SIZE), 0);
}
