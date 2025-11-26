/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// H.264 hardware configuration for ESP32-P4
// This file provides basic configuration constants for the H.264 encoder/decoder

// Maximum number of H.264 channels supported
#ifndef CONFIG_H264_MAX_CHANNELS
#define CONFIG_H264_MAX_CHANNELS 2
#endif

// H.264 DMA buffer alignment
#ifndef CONFIG_H264_DMA_BUFFER_ALIGN
#define CONFIG_H264_DMA_BUFFER_ALIGN 64
#endif

// H.264 interrupt priority
#ifndef CONFIG_H264_INTR_PRIORITY
#define CONFIG_H264_INTR_PRIORITY 1
#endif

#ifdef __cplusplus
}
#endif
