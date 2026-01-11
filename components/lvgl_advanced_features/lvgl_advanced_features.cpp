#include "lvgl_advanced_features.h"
#include "esphome/core/log.h"

#ifdef USE_LVGL
// Include LVGL to verify version
#include "lvgl.h"
#endif

namespace esphome {
namespace lvgl_advanced_features {

static const char *const TAG = "lvgl_advanced_features";

void LVGLAdvancedFeatures::setup() {
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "LVGL Advanced Features - Setup");
  ESP_LOGI(TAG, "========================================");

#ifdef USE_LVGL
  ESP_LOGI(TAG, "LVGL Version: %d.%d.%d",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

  // Version compatibility information
  if (LVGL_VERSION_MAJOR >= 9) {
    ESP_LOGI(TAG, "✅ LVGL v9+ detected - All features available");
  } else if (LVGL_VERSION_MAJOR == 8) {
    ESP_LOGW(TAG, "⚠️  LVGL v8 detected - Limited features available");
    ESP_LOGW(TAG, "   v8 compatible: PNG, JPEG, GIF, BMP, QRCode, FreeType");
    ESP_LOGW(TAG, "   v9 only: ThorVG, SVG, Lottie, Barcode, FFmpeg");
  }

  ESP_LOGI(TAG, "----------------------------------------");
  ESP_LOGI(TAG, "Feature Status:");
  ESP_LOGI(TAG, "----------------------------------------");

  // ========================================
  // LVGL V9 ONLY FEATURES
  // ========================================
#ifdef LV_USE_THORVG_INTERNAL
  ESP_LOGI(TAG, "  ✓ ThorVG Internal: ENABLED (v9)");
#else
  if (this->thorvg_internal_) {
    ESP_LOGW(TAG, "  ✗ ThorVG Internal: REQUESTED but requires LVGL v9");
  }
#endif

#ifdef LV_USE_THORVG_EXTERNAL
  ESP_LOGI(TAG, "  ✓ ThorVG External: ENABLED (v9)");
#else
  if (this->thorvg_external_) {
    ESP_LOGW(TAG, "  ✗ ThorVG External: REQUESTED but requires LVGL v9");
  }
#endif

#ifdef LV_USE_SVG
  ESP_LOGI(TAG, "  ✓ SVG Support: ENABLED (v9)");
#else
  if (this->svg_enabled_) {
    ESP_LOGW(TAG, "  ✗ SVG Support: REQUESTED but requires LVGL v9 + ThorVG");
  }
#endif

#ifdef LV_USE_LOTTIE
  ESP_LOGI(TAG, "  ✓ Lottie Support: ENABLED (v9)");
#else
  if (this->lottie_enabled_) {
    ESP_LOGW(TAG, "  ✗ Lottie Support: REQUESTED but requires LVGL v9 + ThorVG");
  }
#endif

#ifdef LV_USE_BARCODE
  ESP_LOGI(TAG, "  ✓ Barcode Widget: ENABLED (v9)");
#endif

  // ========================================
  // LVGL V8 AND V9 COMPATIBLE FEATURES
  // ========================================
#ifdef LV_USE_FREETYPE
  ESP_LOGI(TAG, "  ✓ FreeType: ENABLED (v8+v9)");
#else
  if (this->freetype_enabled_) {
    ESP_LOGW(TAG, "  ✗ FreeType: REQUESTED but not compiled");
  }
#endif

#ifdef LV_USE_LIBPNG
  ESP_LOGI(TAG, "  ✓ LibPNG: ENABLED (v8+v9)");
#endif

#ifdef LV_USE_LIBJPEG_TURBO
  ESP_LOGI(TAG, "  ✓ LibJPEG Turbo: ENABLED (v8+v9)");
#endif

#ifdef LV_USE_GIF
  ESP_LOGI(TAG, "  ✓ GIF: ENABLED (v8+v9)");
#endif

#ifdef LV_USE_BMP
  ESP_LOGI(TAG, "  ✓ BMP: ENABLED (v8+v9)");
#endif

#ifdef LV_USE_QRCODE
  ESP_LOGI(TAG, "  ✓ QR Code: ENABLED (v8+v9)");
#endif

  ESP_LOGI(TAG, "========================================");

#else
  ESP_LOGE(TAG, "LVGL not compiled - this component requires LVGL");
#endif

  ESP_LOGI(TAG, "Setup complete!");
}

void LVGLAdvancedFeatures::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Advanced Features:");

#ifdef USE_LVGL
  ESP_LOGCONFIG(TAG, "  LVGL Version: %d.%d.%d",
                LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
#endif

  ESP_LOGCONFIG(TAG, "  Vector Graphics:");
  ESP_LOGCONFIG(TAG, "    ThorVG Internal: %s",
                this->thorvg_internal_ ? "REQUESTED" : "DISABLED");
  ESP_LOGCONFIG(TAG, "    SVG Support: %s",
                this->svg_enabled_ ? "REQUESTED" : "DISABLED");
  ESP_LOGCONFIG(TAG, "    Lottie Support: %s",
                this->lottie_enabled_ ? "REQUESTED" : "DISABLED");

  ESP_LOGCONFIG(TAG, "  Image Formats:");
  ESP_LOGCONFIG(TAG, "    PNG: %s", this->libpng_enabled_ ? "REQUESTED" : "DISABLED");
  ESP_LOGCONFIG(TAG, "    JPEG Turbo: %s", this->libjpeg_turbo_enabled_ ? "REQUESTED" : "DISABLED");
  ESP_LOGCONFIG(TAG, "    GIF: %s", this->gif_enabled_ ? "REQUESTED" : "DISABLED");
  ESP_LOGCONFIG(TAG, "    BMP: %s", this->bmp_enabled_ ? "REQUESTED" : "DISABLED");

  ESP_LOGCONFIG(TAG, "  Widgets:");
  ESP_LOGCONFIG(TAG, "    QR Code: %s", this->qrcode_enabled_ ? "REQUESTED" : "DISABLED");
  ESP_LOGCONFIG(TAG, "    Barcode: %s", this->barcode_enabled_ ? "REQUESTED" : "DISABLED");
}

}  // namespace lvgl_advanced_features
}  // namespace esphome
