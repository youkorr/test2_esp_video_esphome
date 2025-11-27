// Minimal mbedTLS AES header stub for linking purposes
// This satisfies fbs_loader.cpp include but functions won't be called for unencrypted models

#ifndef MBEDTLS_AES_H
#define MBEDTLS_AES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief AES context structure (minimal stub version)
 */
typedef struct mbedtls_aes_context {
    uint8_t dummy[256]; // Placeholder
} mbedtls_aes_context;

/**
 * \brief Initialize AES context
 */
void mbedtls_aes_init(mbedtls_aes_context *ctx);

/**
 * \brief Set encryption key
 */
int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx,
                           const unsigned char *key,
                           unsigned int keybits);

/**
 * \brief AES-CTR encryption/decryption
 */
int mbedtls_aes_crypt_ctr(mbedtls_aes_context *ctx,
                          size_t length,
                          size_t *nc_off,
                          unsigned char nonce_counter[16],
                          unsigned char stream_block[16],
                          const unsigned char *input,
                          unsigned char *output);

/**
 * \brief Free AES context
 */
void mbedtls_aes_free(mbedtls_aes_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* MBEDTLS_AES_H */
