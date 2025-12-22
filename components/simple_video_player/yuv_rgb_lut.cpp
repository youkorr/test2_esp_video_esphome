#include "yuv_rgb_lut.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "YUV_LUT";

// Optimized lookup tables for YUV→RGB conversion (BT.601 limited range)
// R = 1.164(Y - 16) + 1.596(V - 128)
// G = 1.164(Y - 16) - 0.391(U - 128) - 0.813(V - 128)
// B = 1.164(Y - 16) + 2.018(U - 128)
static int16_t y_table[256];      // Y component (1.164 * (Y - 16))
static int16_t u_b_table[256];    // U for blue (2.018 * (U - 128))
static int16_t u_g_table[256];    // U for green (-0.391 * (U - 128))
static int16_t v_r_table[256];    // V for red (1.596 * (V - 128))
static int16_t v_g_table[256];    // V for green (-0.813 * (V - 128))
static bool tables_initialized = false;

// Initialize lookup tables (called once at startup)
void init_yuv_lut_tables() {
  if (tables_initialized) return;

  // BT.601 limited range coefficients (scaled by 256 for integer math)
  // Y range: 16-235, UV range: 16-240
  for (int i = 0; i < 256; i++) {
    int y = i - 16;
    int u = i - 128;
    int v = i - 128;

    // Y table: 1.164 * (Y - 16)
    y_table[i] = (y * 298) >> 8;  // 1.164 * 256 = 298

    // U tables (Cb component)
    u_b_table[i] = (u * 517) >> 8;  // 2.018 * 256 = 517
    u_g_table[i] = (u * -100) >> 8; // -0.391 * 256 = -100

    // V tables (Cr component)
    v_r_table[i] = (v * 409) >> 8;  // 1.596 * 256 = 409
    v_g_table[i] = (v * -208) >> 8; // -0.813 * 256 = -208
  }

  tables_initialized = true;
  ESP_LOGI(TAG, "✓ YUV→RGB lookup tables initialized (BT.601 limited range, 5 × 256 entries)");
}

// Optimized I420 → RGB565 conversion using 2×2 block processing
// Performance target: <15ms for 480×272 (vs previous 24ms)
void yuv420_to_rgb565_lut(const uint8_t *yuv, uint8_t *rgb, int width, int height) {
  if (!tables_initialized) {
    ESP_LOGE(TAG, "Tables not initialized! Call init_yuv_lut_tables() first");
    return;
  }

  // YUV420 requires even dimensions (due to 2×2 subsampling)
  if ((width & 1) || (height & 1)) {
    ESP_LOGE(TAG, "Invalid dimensions %dx%d - YUV420 requires even width and height", width, height);
    return;
  }

  if (yuv == nullptr || rgb == nullptr) {
    ESP_LOGE(TAG, "Null pointer: yuv=%p rgb=%p", yuv, rgb);
    return;
  }

  const uint8_t *y_plane = yuv;
  const uint8_t *u_plane = yuv + (width * height);
  const uint8_t *v_plane = u_plane + (width * height / 4);

  uint16_t *rgb565 = (uint16_t *)rgb;

  // Process 2×2 blocks (since U/V are subsampled 2×2 in YUV420)
  // This eliminates division operations and improves cache locality
  int uv_width = width >> 1;  // width / 2

  for (int row = 0; row < height; row += 2) {
    const uint8_t *y_row0 = y_plane + row * width;
    const uint8_t *y_row1 = y_row0 + width;
    uint16_t *rgb_row0 = rgb565 + row * width;
    uint16_t *rgb_row1 = rgb_row0 + width;

    const uint8_t *uv_row = u_plane + (row >> 1) * uv_width;
    const uint8_t *vv_row = v_plane + (row >> 1) * uv_width;

    for (int col = 0; col < width; col += 2) {
      // Load one U/V pair for this 2×2 block
      int uv_idx = col >> 1;
      uint8_t u = uv_row[uv_idx];
      uint8_t v = vv_row[uv_idx];

      // Pre-fetch lookup values (shared by 4 pixels)
      int16_t u_b = u_b_table[u];
      int16_t u_g = u_g_table[u];
      int16_t v_r = v_r_table[v];
      int16_t v_g = v_g_table[v];

      // Process 2×2 block (4 Y pixels, same U/V)
      // Pixel [0,0]
      int y0 = y_table[y_row0[col]];
      int r0 = y0 + v_r;
      int g0 = y0 + u_g + v_g;
      int b0 = y0 + u_b;
      r0 = (r0 < 0) ? 0 : ((r0 > 255) ? 255 : r0);
      g0 = (g0 < 0) ? 0 : ((g0 > 255) ? 255 : g0);
      b0 = (b0 < 0) ? 0 : ((b0 > 255) ? 255 : b0);
      rgb_row0[col] = ((r0 & 0xF8) << 8) | ((g0 & 0xFC) << 3) | (b0 >> 3);

      // Pixel [0,1]
      int y1 = y_table[y_row0[col + 1]];
      int r1 = y1 + v_r;
      int g1 = y1 + u_g + v_g;
      int b1 = y1 + u_b;
      r1 = (r1 < 0) ? 0 : ((r1 > 255) ? 255 : r1);
      g1 = (g1 < 0) ? 0 : ((g1 > 255) ? 255 : g1);
      b1 = (b1 < 0) ? 0 : ((b1 > 255) ? 255 : b1);
      rgb_row0[col + 1] = ((r1 & 0xF8) << 8) | ((g1 & 0xFC) << 3) | (b1 >> 3);

      // Pixel [1,0]
      int y2 = y_table[y_row1[col]];
      int r2 = y2 + v_r;
      int g2 = y2 + u_g + v_g;
      int b2 = y2 + u_b;
      r2 = (r2 < 0) ? 0 : ((r2 > 255) ? 255 : r2);
      g2 = (g2 < 0) ? 0 : ((g2 > 255) ? 255 : g2);
      b2 = (b2 < 0) ? 0 : ((b2 > 255) ? 255 : b2);
      rgb_row1[col] = ((r2 & 0xF8) << 8) | ((g2 & 0xFC) << 3) | (b2 >> 3);

      // Pixel [1,1]
      int y3 = y_table[y_row1[col + 1]];
      int r3 = y3 + v_r;
      int g3 = y3 + u_g + v_g;
      int b3 = y3 + u_b;
      r3 = (r3 < 0) ? 0 : ((r3 > 255) ? 255 : r3);
      g3 = (g3 < 0) ? 0 : ((g3 > 255) ? 255 : g3);
      b3 = (b3 < 0) ? 0 : ((b3 > 255) ? 255 : b3);
      rgb_row1[col + 1] = ((r3 & 0xF8) << 8) | ((g3 & 0xFC) << 3) | (b3 >> 3);
    }
  }
}
