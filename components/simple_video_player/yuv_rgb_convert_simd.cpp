#include "yuv_rgb_convert_simd.h"
#include "yuv_rgb_convert.h"  // Fallback software converter
#include <cstring>
#include "esp_timer.h"  // For timing measurements

// Try to include esp_image_effects headers
// Note: HAVE_ESP_IMGFX_H may be defined by build script
#ifndef HAVE_ESP_IMGFX_H
  #if USE_ESP_IMAGE_EFFECTS
    #if __has_include("esp_imgfx_color_convert.h")
      #define HAVE_ESP_IMGFX_H 1
    #else
      #define HAVE_ESP_IMGFX_H 0
    #endif
  #else
    #define HAVE_ESP_IMGFX_H 0
  #endif
#endif

// Include headers if available
#if USE_ESP_IMAGE_EFFECTS && HAVE_ESP_IMGFX_H
  #include "esp_imgfx_color_convert.h"
  #include "esp_imgfx_types.h"
#endif

namespace esphome {
namespace simple_video_player {

static const char *TAG = "yuv_rgb_simd";

YuvRgbConverterSIMD::YuvRgbConverterSIMD(Colorspace colorspace) : colorspace_(colorspace) {
#if USE_ESP_IMAGE_EFFECTS && HAVE_ESP_IMGFX_H
  // Initialize esp_imgfx SIMD YUVRGB converter
  ESP_LOGI(TAG, "Initializing esp_imgfx SIMD YUVRGB converter...");

  // Note: Resolution will be set on first conversion call
  // We'll create the handle during first use to know the actual dimensions
  this->simd_available_ = true;
  ESP_LOGI(TAG, "esp_imgfx library available");
  ESP_LOGI(TAG, "  Colorspace: %s",
           colorspace_ == Colorspace::BT709 ? "BT.709 (HD)" : "BT.601 (SD)");
  ESP_LOGI(TAG, "  Expected: 3-5x faster than software (~3-5ms @ 480x272)");
  ESP_LOGI(TAG, "  FPS boost: 640×480 35+ FPS, 480×272 100+ FPS");
#else
  ESP_LOGW(TAG, "esp_imgfx not available, using optimized software converter");
  this->simd_available_ = false;
#endif
}

YuvRgbConverterSIMD::~YuvRgbConverterSIMD() {
#if USE_ESP_IMAGE_EFFECTS && HAVE_ESP_IMGFX_H
  if (this->image_effects_handle_ != nullptr) {
    // Cleanup esp_imgfx resources
    esp_imgfx_color_convert_close((esp_imgfx_color_convert_handle_t)this->image_effects_handle_);
    this->image_effects_handle_ = nullptr;
    ESP_LOGD(TAG, "esp_imgfx color converter cleaned up");
  }
#endif
}

void YuvRgbConverterSIMD::convert_i420_to_rgb565(const uint8_t *yuv, uint8_t *rgb, int width, int height) {
#if USE_ESP_IMAGE_EFFECTS && HAVE_ESP_IMGFX_H
  if (this->simd_available_) {
    // Lazy initialization: create handle on first use with actual dimensions
    if (this->image_effects_handle_ == nullptr) {
      esp_imgfx_color_convert_cfg_t cfg = {
        .in_res = {(int16_t)width, (int16_t)height},
        .in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_I420,
        .out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE,
        .color_space_std = this->colorspace_ == Colorspace::BT709 ?
                           ESP_IMGFX_COLOR_SPACE_STD_BT709 :
                           ESP_IMGFX_COLOR_SPACE_STD_BT601,
      };

      esp_imgfx_err_t ret = esp_imgfx_color_convert_open(&cfg, (esp_imgfx_color_convert_handle_t*)&this->image_effects_handle_);

      if (ret != ESP_IMGFX_ERR_OK || this->image_effects_handle_ == nullptr) {
        ESP_LOGW(TAG, "esp_imgfx_color_convert_open failed: %d, using software fallback", ret);
        this->simd_available_ = false;
        this->convert_i420_to_rgb565_software_(yuv, rgb, width, height);
        return;
      }

      ESP_LOGI(TAG, "esp_imgfx SIMD converter initialized for %dx%d", width, height);
    }

    // Use SIMD-accelerated conversion via esp_imgfx
    // Expected performance: 3-5ms @ 480x272 (vs 10-15ms software)
    size_t yuv_size = width * height * 3 / 2;  // I420: Y + U/4 + V/4
    size_t rgb_size = width * height * 2;       // RGB565: 2 bytes per pixel

    esp_imgfx_data_t in = {
      .data = (uint8_t*)yuv,
      .data_len = (uint32_t)yuv_size,
    };

    esp_imgfx_data_t out = {
      .data = rgb,
      .data_len = (uint32_t)rgb_size,
    };

    // Execute SIMD color conversion
    uint32_t simd_start = esp_timer_get_time() / 1000;
    esp_imgfx_err_t ret = esp_imgfx_color_convert_process(
        (esp_imgfx_color_convert_handle_t)this->image_effects_handle_,
        &in,
        &out
    );
    uint32_t simd_time = (esp_timer_get_time() / 1000) - simd_start;

    if (ret == ESP_IMGFX_ERR_OK) {
      // SIMD conversion succeeded - log timing every 30 frames
      static int simd_log_counter = 0;
      if (++simd_log_counter >= 30) {
        ESP_LOGI(TAG, "    esp_imgfx SIMD actual time: %lums (expected 3-5ms)", (unsigned long)simd_time);
        simd_log_counter = 0;
      }
      return;
    }

    // If SIMD fails, log warning and fall through to software
    ESP_LOGW(TAG, "esp_imgfx_color_convert_process failed: %d, using software fallback", ret);
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
