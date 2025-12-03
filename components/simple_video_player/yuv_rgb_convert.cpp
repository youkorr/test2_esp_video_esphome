#include "yuv_rgb_convert.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "yuv_rgb";

YuvRgbConverter::YuvRgbConverter(Colorspace cs) : colorspace_(cs) {
  if (cs == Colorspace::BT601) {
    init_lut_bt601_();
  } else {
    init_lut_bt709_();
  }
}

void YuvRgbConverter::init_lut_bt601_() {
  // BT.601 coefficients (SD video)
  // R = 1.164(Y-16) + 1.596(V-128)
  // G = 1.164(Y-16) - 0.391(U-128) - 0.813(V-128)
  // B = 1.164(Y-16) + 2.018(U-128)

  for (int i = 0; i < 256; i++) {
    y_lut_[i] = (298 * (i - 16) + 128) >> 8;  // 1.164 = 298/256

    int u_offset = i - 128;
    int v_offset = i - 128;

    u_r_lut_[i] = 0;                          // U doesn't affect R
    u_g_lut_[i] = (-100 * u_offset) >> 8;     // -0.391 ≈ -100/256

    v_r_lut_[i] = (409 * v_offset) >> 8;      // 1.596 ≈ 409/256
    v_g_lut_[i] = (-208 * v_offset) >> 8;     // -0.813 ≈ -208/256
    v_b_lut_[i] = (516 * v_offset) >> 8;      // 2.018 ≈ 516/256
  }

  lut_initialized_ = true;
  ESP_LOGI(TAG, "YUV→RGB conversion initialized (BT.601 colorspace)");
}

void YuvRgbConverter::init_lut_bt709_() {
  // BT.709 coefficients (HD video - matches FFmpeg BT.709 output)
  // R = 1.164(Y-16) + 1.793(V-128)
  // G = 1.164(Y-16) - 0.213(U-128) - 0.533(V-128)
  // B = 1.164(Y-16) + 2.112(U-128)

  for (int i = 0; i < 256; i++) {
    y_lut_[i] = (298 * (i - 16) + 128) >> 8;  // 1.164 = 298/256

    int u_offset = i - 128;
    int v_offset = i - 128;

    u_r_lut_[i] = 0;                          // U doesn't affect R
    u_g_lut_[i] = (-55 * u_offset) >> 8;      // -0.213 ≈ -55/256

    v_r_lut_[i] = (459 * v_offset) >> 8;      // 1.793 ≈ 459/256
    v_g_lut_[i] = (-136 * v_offset) >> 8;     // -0.533 ≈ -136/256
    v_b_lut_[i] = (541 * v_offset) >> 8;      // 2.112 ≈ 541/256
  }

  lut_initialized_ = true;
  ESP_LOGI(TAG, "YUV→RGB conversion initialized (BT.709 colorspace - HD standard)");
}

void IRAM_ATTR YuvRgbConverter::convert_i420_to_rgb565(const uint8_t *yuv, uint8_t *rgb,
                                                       int width, int height) {
  const uint8_t *y_plane = yuv;
  const uint8_t *u_plane = yuv + width * height;
  const uint8_t *v_plane = u_plane + (width * height / 4);

  uint16_t *rgb565 = (uint16_t *)rgb;
  const int uv_width = width >> 1;

  // Process 2x2 pixel blocks (I420 chroma subsampling)
  for (int y = 0; y < height; y++) {
    const int uv_y = y >> 1;
    const uint8_t *y_row = y_plane + y * width;
    const uint8_t *u_row = u_plane + uv_y * uv_width;
    const uint8_t *v_row = v_plane + uv_y * uv_width;

    uint16_t *rgb_row = rgb565 + y * width;

    for (int x = 0; x < width; x++) {
      const int uv_x = x >> 1;

      // Lookup pre-computed coefficients
      const int y_val = y_lut_[y_row[x]];
      const uint8_t u = u_row[uv_x];
      const uint8_t v = v_row[uv_x];

      // Compute RGB using lookup tables (much faster than multiplication)
      const int r = y_val + v_r_lut_[v];
      const int g = y_val + u_g_lut_[u] + v_g_lut_[v];
      const int b = y_val + v_b_lut_[v];

      // Pack to RGB565 with clamping
      rgb_row[x] = pack_rgb565(r, g, b);
    }
  }
}
