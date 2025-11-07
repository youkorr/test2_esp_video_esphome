/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include "esp_ipa_types.h"

/**
 * @brief IPA detection array - ESPHome/PlatformIO implementation
 *
 * libesp_ipa.a iterates: for (p = &__esp_ipa_detect_array_start; p < &__esp_ipa_detect_array_end; ++p)
 * We provide a simple contiguous array.
 */

// Forward declarations from libesp_ipa.a
extern esp_ipa_t *__esp_ipa_detect_fn_awb_gray_world(void *config);
extern esp_ipa_t *__esp_ipa_detect_fn_agc_threshold(void *config);
extern esp_ipa_t *__esp_ipa_detect_fn_denoising_gain_feedback(void *config);
extern esp_ipa_t *__esp_ipa_detect_fn_sharpen_freq_feedback(void *config);
extern esp_ipa_t *__esp_ipa_detect_fn_gamma_lumma_feedback(void *config);
extern esp_ipa_t *__esp_ipa_detect_fn_cc_linear(void *config);

// Detection structure type
typedef struct esp_ipa_detect {
    const char *name;
    esp_ipa_t *(*detect)(void *);
} esp_ipa_detect_t;

/**
 * The IPA detection array - simple approach
 *
 * Create array with __esp_ipa_detect_array_start as its name,
 * and a separate __esp_ipa_detect_array_end symbol after it.
 */

esp_ipa_detect_t __esp_ipa_detect_array_start[7] = {
    { .name = "awb.gray",                .detect = __esp_ipa_detect_fn_awb_gray_world },
    { .name = "agc.threshold",           .detect = __esp_ipa_detect_fn_agc_threshold },
    { .name = "denoising.gain_feedback", .detect = __esp_ipa_detect_fn_denoising_gain_feedback },
    { .name = "sharpen.freq_feedback",   .detect = __esp_ipa_detect_fn_sharpen_freq_feedback },
    { .name = "gamma.lumma_feedback",    .detect = __esp_ipa_detect_fn_gamma_lumma_feedback },
    { .name = "cc.linear",               .detect = __esp_ipa_detect_fn_cc_linear },
    { .name = NULL,                      .detect = NULL },  // Sentinel
};

/**
 * End marker - separate symbol that MUST be placed right after the array
 *
 * Using a weak symbol that can be overridden by linker
 */
esp_ipa_detect_t __esp_ipa_detect_array_end __attribute__((weak)) = {
    .name = NULL,
    .detect = NULL,
};
