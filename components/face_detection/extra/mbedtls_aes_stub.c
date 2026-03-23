// Stub implementation of mbedTLS AES functions
// These satisfy the linker but won't be called since we use unencrypted models (mode == 0)
// If you need encrypted model support, link the real mbedTLS library instead

#include <stddef.h>
#include <stdint.h>

// Minimal AES context structure (opaque to users)
typedef struct {
    uint8_t dummy[256]; // Placeholder - not used
} mbedtls_aes_context;

void mbedtls_aes_init(mbedtls_aes_context *ctx)
{
    // Stub - should not be called for unencrypted models
}

int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx, const unsigned char *key, unsigned int keybits)
{
    // Stub - should not be called for unencrypted models
    return 0;
}

int mbedtls_aes_crypt_ctr(mbedtls_aes_context *ctx,
                          size_t length,
                          size_t *nc_off,
                          unsigned char nonce_counter[16],
                          unsigned char stream_block[16],
                          const unsigned char *input,
                          unsigned char *output)
{
    // Stub - should not be called for unencrypted models
    return 0;
}

void mbedtls_aes_free(mbedtls_aes_context *ctx)
{
    // Stub - should not be called for unencrypted models
}
