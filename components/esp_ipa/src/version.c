/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include "esp_log.h"
#include "esp_ipa.h"

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
    /* Stub implementation - returns NULL indicating no config found */
    /* In a full implementation, this would look up the config in a registry */
    (void)cam_name;
    return NULL;
}
