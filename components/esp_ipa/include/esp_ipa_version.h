/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#pragma once
/**
 * @brief ESP IPA component version definitions
 * These macros are used for version reporting in ESPHome/PlatformIO builds
 */
#define ESP_IPA_VER_MAJOR 1
#define ESP_IPA_VER_MINOR 3
#define ESP_IPA_VER_PATCH 1


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Print esp-ipa version string.
 *
 * @return None
 */
void esp_ipa_print_version(void);

#ifdef __cplusplus
}
#endif
