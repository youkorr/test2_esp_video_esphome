#pragma once

/**
 * Simplified GMF PPA implementation for simple_video_player
 * Based on esp-gmf/elements/gmf_video/esp_gmf_video_ppa.c
 *
 * Supports YUV420 RGB565 conversion using:
 * 1. 2D-DMA (best performance, if supported)
 * 2. PPA (fallback, if 2D-DMA not available)
 */

// Force ESP32-P4 target definition for PPA hardware
// This should be set by build script, but we force it here as backup
#ifndef CONFIG_IDF_TARGET_ESP32P4
  #define CONFIG_IDF_TARGET_ESP32P4 1
#endif

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include "esp_err.h"
#include <cstdint>

/**
 * @brief Convert YUV420 (I420) to RGB565 using GMF PPA method
 *
 * Automatically selects best method:
 * - 2D-DMA if supported (fastest)
 * - PPA if 2D-DMA not available
 *
 * @param yuv_in Input YUV420 buffer (planar: Y + U + V)
 * @param rgb_out Output RGB565 buffer
 * @param width Frame width (must be even for YUV420)
 * @param height Frame height (must be even for YUV420)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t gmf_ppa_convert_yuv420_to_rgb565(
    const uint8_t *yuv_in,
    uint8_t *rgb_out,
    uint16_t width,
    uint16_t height
);

/**
 * @brief Initialize GMF PPA converter
 * Must be called once before using gmf_ppa_convert_yuv420_to_rgb565()
 *
 * @return ESP_OK on success
 */
esp_err_t gmf_ppa_init();

/**
 * @brief Cleanup GMF PPA converter
 */
void gmf_ppa_deinit();

#endif // CONFIG_IDF_TARGET_ESP32P4
