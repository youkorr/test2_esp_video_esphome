/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include <string.h>
#include "esp_log.h"
#include "esp_ipa.h"

// Version par défaut si non définie
#ifndef ESP_IPA_VER_MAJOR
#define ESP_IPA_VER_MAJOR 1
#endif
#ifndef ESP_IPA_VER_MINOR
#define ESP_IPA_VER_MINOR 0
#endif
#ifndef ESP_IPA_VER_PATCH
#define ESP_IPA_VER_PATCH 0
#endif

static const char *TAG = "esp_ipa";

/**
 * @brief Print esp-ipa version string.
 *
 * @return None
 */
void esp_ipa_print_version(void)
{
    ESP_LOGI(TAG, "ESP-IPA version: %d.%d.%d", ESP_IPA_VER_MAJOR, ESP_IPA_VER_MINOR, ESP_IPA_VER_PATCH);
}

/**
 * @brief Get IPA configuration for a specific camera device.
 *
 * @param cam_name  Camera device name
 *
 * @return
 *      - Pointer to IPA configuration if found
 *      - NULL if not found
 */
const esp_ipa_config_t *esp_ipa_pipeline_get_config(const char *cam_name)
{
    /* IPA pipeline for SC202CS sensor */
    static const char *sc202cs_ipa_names[] = {
        "awb_gray_world",           /* Auto White Balance */
        "agc_threshold",            /* Auto Gain Control (brightness) */
        "denoising_gain_feedback",  /* Noise reduction */
        "sharpen_freq_feedback",    /* Sharpening */
        "gamma_lumma_feedback",     /* Gamma correction */
        "cc_linear",                /* Color Correction */
    };

    static const esp_ipa_config_t sc202cs_ipa_config = {
        .ipa_nums = 6,
        .ipa_names = sc202cs_ipa_names,
    };

    /* Check if this is the SC202CS sensor */
    if (cam_name && strcmp(cam_name, "SC202CS") == 0) {
        ESP_LOGI(TAG, "📸 IPA config found for %s - enabling AE/AWB pipeline", cam_name);
        return &sc202cs_ipa_config;
    }

    ESP_LOGW(TAG, "No IPA config found for camera: %s", cam_name ? cam_name : "NULL");
    return NULL;
}
