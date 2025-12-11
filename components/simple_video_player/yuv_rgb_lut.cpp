#include "yuv_rgb_lut.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "YUV_LUT";

// Lookup tables pour conversion YUV→RGB (optimisé pour vitesse)
// Basé sur : https://github.com/yangfuyuan/yuv2rgb
static int32_t tables[768];
static bool tables_initialized = false;

// Initialisation des lookup tables (une seule fois au démarrage)
void init_yuv_lut_tables() {
  if (tables_initialized) return;

  // Table Y (0-255)
  for (int i = 0; i < 256; i++) {
    int y = i - 16;
    tables[i] = (y < 0) ? 0 : ((y * 76284) >> 16);  // 1.164
  }

  // Table U (256-511)
  for (int i = 0; i < 256; i++) {
    int u = i - 128;
    tables[256 + i] = (u * 132252) >> 16;  // 2.017
  }

  // Table V (512-767)
  for (int i = 0; i < 256; i++) {
    int v = i - 128;
    tables[512 + i] = (v * 104595) >> 16;  // 1.596
  }

  tables_initialized = true;
  ESP_LOGI(TAG, "✓ YUV→RGB lookup tables initialized (3 × 256 entries = 3 KB)");
}

// Conversion I420 → RGB565 avec lookup tables
void yuv420_to_rgb565_lut(const uint8_t *yuv, uint8_t *rgb, int width, int height) {
  if (!tables_initialized) {
    ESP_LOGE(TAG, "Tables not initialized! Call init_yuv_lut_tables() first");
    return;
  }

  const uint8_t *y_plane = yuv;
  const uint8_t *u_plane = yuv + (width * height);
  const uint8_t *v_plane = u_plane + (width * height / 4);

  uint16_t *rgb565 = (uint16_t *)rgb;

  for (int row = 0; row < height; row++) {
    for (int col = 0; col < width; col++) {
      int y_idx = row * width + col;
      int uv_idx = (row / 2) * (width / 2) + (col / 2);

      int y = y_plane[y_idx];
      int u = u_plane[uv_idx];
      int v = v_plane[uv_idx];

      // Lookup table conversion
      int y_val = tables[y];
      int u_val = tables[256 + u];
      int v_val = tables[512 + v];

      // RGB calculation (integer only)
      int r = y_val + v_val;
      int g = y_val - ((u_val + v_val) >> 1);
      int b = y_val + u_val;

      // Clamp to 0-255
      r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
      g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
      b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

      // Pack to RGB565 : RRRRRGGG GGGBBBBB
      uint16_t rgb565_pixel = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

      rgb565[y_idx] = rgb565_pixel;
    }
  }
}
