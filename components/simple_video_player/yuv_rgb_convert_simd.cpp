#include "yuv_rgb_convert_simd.h"
#include "yuv_rgb_convert.h"  // Fallback software converter
#include <cstring>

namespace esphome {
namespace simple_video_player {

static const char *TAG = "yuv_rgb_simd";

YuvRgbConverterSIMD::YuvRgbConverterSIMD(Colorspace colorspace) : colorspace_(colorspace) {
#if USE_ESP_IMAGE_EFFECTS
  // Try to initialize esp_image_effects for SIMD acceleration
  ESP_LOGI(TAG, "Initializing esp_image_effects SIMD YUV→RGB converter...");

  // Note: Actual esp_image_effects initialization will go here when library is available
  // For now, we'll detect if the functions exist at runtime

  // Check if SIMD functions are available
  // This will be implemented based on esp_image_effects API documentation
  this->simd_available_ = false;  // Will be set to true once library is integrated

  if (this->simd_available_) {
    ESP_LOGI(TAG, "✓ SIMD acceleration enabled via esp_image_effects");
    ESP_LOGI(TAG, "  Expected: 3-5x faster than software (~3-5ms @ 480x272)");
  } else {
    ESP_LOGW(TAG, "esp_image_effects not available, using optimized software converter");
  }
#else
  ESP_LOGW(TAG, "esp_image_effects not compiled in, using optimized software converter");
  this->simd_available_ = false;
#endif
}

YuvRgbConverterSIMD::~YuvRgbConverterSIMD() {
#if USE_ESP_IMAGE_EFFECTS
  if (this->image_effects_handle_ != nullptr) {
    // Cleanup esp_image_effects resources
    this->image_effects_handle_ = nullptr;
  }
#endif
}

void YuvRgbConverterSIMD::convert_i420_to_rgb565(const uint8_t *yuv, uint8_t *rgb, int width, int height) {
#if USE_ESP_IMAGE_EFFECTS
  if (this->simd_available_ && this->image_effects_handle_ != nullptr) {
    // Use SIMD-accelerated conversion via esp_image_effects
    //
    // API will be something like:
    // esp_image_effects_yuv420_to_rgb565(this->image_effects_handle_, yuv, rgb, width, height);
    //
    // This typically uses:
    // - RISC-V vector extensions (if available)
    // - Optimized memory access patterns
    // - Parallel processing
    //
    // Expected performance: 3-5ms @ 480x272 (vs 10-15ms software)

    // TODO: Integrate actual esp_image_effects API when available
    // For now, fall through to software conversion
  }
#endif

  // Fallback to optimized software conversion
  this->convert_i420_to_rgb565_software_(yuv, rgb, width, height);
}

void YuvRgbConverterSIMD::convert_i420_to_rgb565_software_(const uint8_t *yuv, uint8_t *rgb, int w, int h) {
  // Use the existing optimized software converter
  // This is already 5-10x faster than naive conversion
  YuvRgbConverter temp_converter(
      this->colorspace_ == Colorspace::BT709 ?
      YuvRgbConverter::Colorspace::BT709 :
      YuvRgbConverter::Colorspace::BT601
  );

  temp_converter.convert_i420_to_rgb565(yuv, rgb, w, h);
}

}  // namespace simple_video_player
}  // namespace esphome
