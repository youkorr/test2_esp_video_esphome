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
    /* TEMPORAIREMENT DÉSACTIVÉ - problème avec detect array linker */
    /* IPA pipeline causait un crash au boot à cause de sections orphelines */
    /* TODO: Implémenter alternative pour AE/AWB (contrôles manuels exposition/gain) */

    ESP_LOGW(TAG, "⚠️  IPA pipeline DÉSACTIVÉ pour %s (temporaire)", cam_name ? cam_name : "NULL");
    ESP_LOGW(TAG, "   Image sera sombre - nécessite configuration manuelle exposition/gain");

    (void)cam_name;
    return NULL;
}
