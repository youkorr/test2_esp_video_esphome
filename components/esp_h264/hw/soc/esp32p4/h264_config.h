/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 *  SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#if defined(__has_include)
#if __has_include("hal/config.h")
#include "hal/config.h"
#endif
#else
#include "hal/config.h"
#endif
#ifndef HAL_CONFIG
#define HAL_CONFIG(x) HAL_CONFIG_##x
#endif

// For ESPHome/PlatformIO: Set default ESP32-P4 revision
// Default to 300 (rev 3.0+) for modern boards
// Change to lower value if you have older revision boards
#ifndef CONFIG_ESP_REV_MIN_FULL
#define CONFIG_ESP_REV_MIN_FULL 300
#endif

#ifndef HAL_CONFIG_CHIP_SUPPORT_MIN_REV
#define HAL_CONFIG_CHIP_SUPPORT_MIN_REV CONFIG_ESP_REV_MIN_FULL
#endif
