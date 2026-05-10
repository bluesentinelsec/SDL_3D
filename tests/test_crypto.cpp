#include <gtest/gtest.h>

#include <cstring>

extern "C"
{
#include "slayer3d_crypto.h"
}

namespace
{
void derive_key(const char *password, const uint8_t salt[SLAYER3D_CRYPTO_SALT_SIZE],
                uint8_t key[SLAYER3D_CRYPTO_HASH_SIZE])
{
    slayer3d_crypto_hash32_state state;
    slayer3d_crypto_hash32_init(&state);
    if (password != nullptr && password[0] != '\0')
        slayer3d_crypto_hash32_update(&state, password, std::strlen(password));
    slayer3d_crypto_hash32_update(&state, salt, SLAYER3D_CRYPTO_SALT_SIZE);
    slayer3d_crypto_hash32_final(&state, key);
}

void derive_nonce(const uint8_t *data, size_t size, uint8_t nonce[SLAYER3D_CRYPTO_NONCE_SIZE])
{
    static const char label[] = "SLAYER3D pack nonce";
    slayer3d_crypto_hash32_state state;
    uint8_t digest[SLAYER3D_CRYPTO_HASH_SIZE];

    slayer3d_crypto_hash32_init(&state);
    slayer3d_crypto_hash32_update(&state, label, sizeof(label) - 1u);
    slayer3d_crypto_hash32_update(&state, data, size);
    slayer3d_crypto_hash32_final(&state, digest);
    std::memcpy(nonce, digest, SLAYER3D_CRYPTO_NONCE_SIZE);
}
} // namespace

TEST(CryptoHelpers, HashIsDeterministicAndSensitive)
{
    const char message[] = "SLAYER3D pack obfuscation";
    uint8_t first[SLAYER3D_CRYPTO_HASH_SIZE];
    uint8_t second[SLAYER3D_CRYPTO_HASH_SIZE];
    uint8_t different[SLAYER3D_CRYPTO_HASH_SIZE];

    slayer3d_crypto_hash32(message, sizeof(message), first);
    slayer3d_crypto_hash32(message, sizeof(message), second);
    EXPECT_EQ(std::memcmp(first, second, sizeof(first)), 0);

    const char altered[] = "SLAYER3D pack obfuscatioN";
    slayer3d_crypto_hash32(altered, sizeof(altered), different);
    EXPECT_NE(std::memcmp(first, different, sizeof(first)), 0);
}

TEST(CryptoHelpers, StreamCipherRoundTripsWithDerivedKey)
{
    const uint8_t plain[] = "A small blob of asset bytes used to test obfuscation.";
    uint8_t salt_digest[SLAYER3D_CRYPTO_HASH_SIZE];
    uint8_t salt[SLAYER3D_CRYPTO_SALT_SIZE];
    uint8_t nonce[SLAYER3D_CRYPTO_NONCE_SIZE];
    uint8_t key[SLAYER3D_CRYPTO_HASH_SIZE];
    uint8_t buffer[sizeof(plain)];

    slayer3d_crypto_hash32(plain, sizeof(plain), salt_digest);
    std::memcpy(salt, salt_digest, SLAYER3D_CRYPTO_SALT_SIZE);
    derive_nonce(plain, sizeof(plain), nonce);
    derive_key("test-password", salt, key);

    std::memcpy(buffer, plain, sizeof(plain));
    slayer3d_crypto_xor_stream(buffer, sizeof(buffer), key, nonce);
    EXPECT_NE(std::memcmp(buffer, plain, sizeof(plain)), 0);

    slayer3d_crypto_xor_stream(buffer, sizeof(buffer), key, nonce);
    EXPECT_EQ(std::memcmp(buffer, plain, sizeof(plain)), 0);
}

TEST(CryptoHelpers, KeyedTagChangesWhenPayloadChanges)
{
    const uint8_t salt[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const uint8_t header[] = {'S', '3', 'D', 'O', 'P', 'K', '1', '\0', 1, 0, 0, 0};
    const uint8_t payload[] = "payload";
    const uint8_t altered_payload[] = "payloae";
    uint8_t key[SLAYER3D_CRYPTO_HASH_SIZE];
    uint8_t tag_a[SLAYER3D_CRYPTO_HASH_SIZE];
    uint8_t tag_b[SLAYER3D_CRYPTO_HASH_SIZE];

    derive_key("tag-key", salt, key);
    slayer3d_crypto_hash32_state state;

    slayer3d_crypto_hash32_init_keyed(&state, key, sizeof(key));
    slayer3d_crypto_hash32_update(&state, header, sizeof(header));
    slayer3d_crypto_hash32_update(&state, payload, sizeof(payload));
    slayer3d_crypto_hash32_final(&state, tag_a);

    slayer3d_crypto_hash32_init_keyed(&state, key, sizeof(key));
    slayer3d_crypto_hash32_update(&state, header, sizeof(header));
    slayer3d_crypto_hash32_update(&state, altered_payload, sizeof(altered_payload));
    slayer3d_crypto_hash32_final(&state, tag_b);

    EXPECT_NE(std::memcmp(tag_a, tag_b, SLAYER3D_CRYPTO_TAG_SIZE), 0);
}
