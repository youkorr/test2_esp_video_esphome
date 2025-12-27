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
  ESP_LOGI(TAG, "Setting up LVGL Advanced Features");

#ifdef USE_LVGL
  ESP_LOGI(TAG, "LVGL Version: %d.%d.%d",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

  // Log ThorVG status
#ifdef LV_USE_THORVG_INTERNAL
  ESP_LOGI(TAG, "  ThorVG Internal: ENABLED");
#else
  if (this->thorvg_internal_) {
    ESP_LOGW(TAG, "  ThorVG Internal: REQUESTED but not compiled in LVGL");
  }
#endif

#ifdef LV_USE_THORVG_EXTERNAL
  ESP_LOGI(TAG, "  ThorVG External: ENABLED");
#else
  if (this->thorvg_external_) {
    ESP_LOGW(TAG, "  ThorVG External: REQUESTED but not compiled in LVGL");
  }
#endif

  // Log SVG status
#ifdef LV_USE_SVG
  ESP_LOGI(TAG, "  SVG Support: ENABLED");
#else
  if (this->svg_enabled_) {
    ESP_LOGW(TAG, "  SVG Support: REQUESTED but not compiled in LVGL");
    ESP_LOGW(TAG, "    Note: SVG requires ThorVG to be enabled");
  }
#endif

  // Log Lottie status
#ifdef LV_USE_LOTTIE
  ESP_LOGI(TAG, "  Lottie Support: ENABLED");
#else
  if (this->lottie_enabled_) {
    ESP_LOGW(TAG, "  Lottie Support: REQUESTED but not compiled in LVGL");
    ESP_LOGW(TAG, "    Note: Lottie requires ThorVG to be enabled");
  }
#endif

  // Log FreeType status
#ifdef LV_USE_FREETYPE
  ESP_LOGI(TAG, "  FreeType: ENABLED");
#else
  if (this->freetype_enabled_) {
    ESP_LOGW(TAG, "  FreeType: REQUESTED but not available");
  }
#endif

  // Log image format support
#ifdef LV_USE_LIBPNG
  ESP_LOGI(TAG, "  LibPNG: ENABLED");
#endif

#ifdef LV_USE_LIBJPEG_TURBO
  ESP_LOGI(TAG, "  LibJPEG Turbo: ENABLED");
#endif

#ifdef LV_USE_GIF
  ESP_LOGI(TAG, "  GIF: ENABLED");
#endif

#ifdef LV_USE_BMP
  ESP_LOGI(TAG, "  BMP: ENABLED");
#endif

  // Log widget support
#ifdef LV_USE_QRCODE
  ESP_LOGI(TAG, "  QR Code: ENABLED");
#endif

#ifdef LV_USE_BARCODE
  ESP_LOGI(TAG, "  Barcode: ENABLED");
#endif

  // Check LVGL version for V9 features
  if (LVGL_VERSION_MAJOR >= 9) {
    ESP_LOGI(TAG, "LVGL V9+ detected - Advanced features available");
  } else {
    ESP_LOGW(TAG, "LVGL V%d detected - Some features require V9+", LVGL_VERSION_MAJOR);
  }

#else
  ESP_LOGE(TAG, "LVGL not compiled - this component requires LVGL");
#endif

  ESP_LOGI(TAG, "LVGL Advanced Features setup complete");
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
