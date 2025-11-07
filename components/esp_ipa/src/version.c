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
