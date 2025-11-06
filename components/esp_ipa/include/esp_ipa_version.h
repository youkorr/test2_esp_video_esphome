/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESP IPA version macros
 */
#define ESP_IPA_VER_MAJOR    1
#define ESP_IPA_VER_MINOR    0
#define ESP_IPA_VER_PATCH    0

/**
 * @brief ESP IPA version string
 */
#define ESP_IPA_VERSION      "1.0.0"

/**
 * @brief Print esp-ipa version string.
 *
 * @return None
 */
void esp_ipa_print_version(void);

#ifdef __cplusplus
}
#endif
