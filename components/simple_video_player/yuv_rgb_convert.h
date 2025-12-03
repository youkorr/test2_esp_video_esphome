#pragma once

#include <cstdint>
#include "esp_attr.h"

/**
 * @brief Optimized YUV (I420) to RGB565 conversion
 *
 * Features:
 * - Lookup tables for fast YUV coefficient calculation (5-10x faster than naive)
 * - BT.709 colorspace support (HD video standard)
 * - SPIRAM-friendly sequential access pattern
 * - Optimized for ESP32-P4
 */

class YuvRgbConverter {
public:
  enum class Colorspace {
    BT601,  // Standard definition (legacy)
    BT709   // High definition (recommended for H.264)
  };

  YuvRgbConverter(Colorspace cs = Colorspace::BT709);
  ~YuvRgbConverter() = default;

  /**
   * @brief Convert I420 (YUV420) to RGB565
   *
   * @param yuv Input I420 buffer (Y plane + U plane + V plane)
   * @param rgb Output RGB565 buffer (must be pre-allocated)
   * @param width Frame width (must be multiple of 2)
   * @param height Frame height (must be multiple of 2)
   */
  void convert_i420_to_rgb565(const uint8_t *yuv, uint8_t *rgb, int width, int height);

private:
  void init_lut_bt601_();
  void init_lut_bt709_();

  // Lookup tables for fast conversion (256 entries each)
  int16_t y_lut_[256];      // Y coefficient
  int16_t u_r_lut_[256];    // U contribution to R
  int16_t u_g_lut_[256];    // U contribution to G
  int16_t v_r_lut_[256];    // V contribution to R
  int16_t v_g_lut_[256];    // V contribution to G
  int16_t v_b_lut_[256];    // V contribution to B

  Colorspace colorspace_;
  bool lut_initialized_{false};

  // Inline helper for clamping and packing to RGB565
  inline uint16_t pack_rgb565(int r, int g, int b) {
    r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
    g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
    b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

    // RGB565: RRRRR GGGGGG BBBBB
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }
};
