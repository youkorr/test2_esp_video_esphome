// Stub implementation of mbedTLS AES functions used by ESP-DL when loading
// unencrypted models. The face_detection / yolo11_detection / pedestrian_detection
// components ship the same stub. When more than one of them is linked the
// duplicated strong symbols would break the build, so this copy uses
// __attribute__((weak)) so it only wins when no other component provides the
// stub (e.g. plain lvgl_camera_display without any detector).

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t dummy[256];
} mbedtls_aes_context;

__attribute__((weak)) void mbedtls_aes_init(mbedtls_aes_context *ctx)
{
    (void)ctx;
}

__attribute__((weak)) int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx,
                                                 const unsigned char *key,
                                                 unsigned int keybits)
{
    (void)ctx; (void)key; (void)keybits;
    return 0;
}

__attribute__((weak)) int mbedtls_aes_crypt_ctr(mbedtls_aes_context *ctx,
                                                size_t length,
                                                size_t *nc_off,
                                                unsigned char nonce_counter[16],
                                                unsigned char stream_block[16],
                                                const unsigned char *input,
                                                unsigned char *output)
{
    (void)ctx; (void)length; (void)nc_off; (void)nonce_counter;
    (void)stream_block; (void)input; (void)output;
    return 0;
}

__attribute__((weak)) void mbedtls_aes_free(mbedtls_aes_context *ctx)
{
    (void)ctx;
}
