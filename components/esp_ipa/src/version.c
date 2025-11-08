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
 * @brief Get IPA pipeline configuration for specified camera sensor
 *
 * Configuration IPA pour SC202CS - TOUS les IPAs incluant AEC
 * Pipeline: Capteur (RAW8) → ISP → IPA (6 algorithmes) → RGB565
 *
 * Algorithmes actifs:
 * - aec.simple: Auto Exposure Control (corrige surexposition) ← RÉACTIVÉ
 * - awb.gray: Auto White Balance (corrige blanc→vert)
 * - denoising.gain_feedback: Réduction bruit (image plus propre)
 * - sharpen.freq_feedback: Netteté (image plus claire)
 * - gamma.lumma_feedback: Gamma (luminosité optimale)
 * - cc.linear: Color Correction (couleurs correctes)
 *
 * Note: Utilisé "aec.simple" au lieu de "agc.threshold" pour un contrôle
 * d'exposition plus doux et éviter les flashes.
 *
 * @param cam_name Camera sensor name (e.g., "SC202CS", "OV5647", "OV02C10")
 * @return IPA configuration pointer for the camera, NULL if not found
 */
const esp_ipa_config_t *esp_ipa_pipeline_get_config(const char *cam_name)
{
    /* Configuration IPA pour TOUS les capteurs - 6 algorithmes avec AEC */
    static const char *ipa_names[] = {
        "aec.simple",              /* Auto Exposure Control - corrige surexposition */
        "awb.gray",                /* Auto White Balance - corrige blanc→vert */
        "denoising.gain_feedback", /* Réduction bruit - image plus propre */
        "sharpen.freq_feedback",   /* Netteté - image plus claire */
        "gamma.lumma_feedback",    /* Gamma - luminosité optimale */
        "cc.linear",               /* Color Correction - couleurs correctes */
    };

    static const esp_ipa_config_t ipa_config = {
        .ipa_nums = 6,     /* 6 IPAs actifs (incluant AEC) */
        .ipa_names = ipa_names,
    };

    if (cam_name) {
        ESP_LOGI(TAG, "📸 IPA config for %s: AEC+AWB+Denoise+Sharpen+Gamma+CC", cam_name);
        return &ipa_config;
    }

    ESP_LOGW(TAG, "No camera name provided - using default IPA config");
    return &ipa_config;
}
