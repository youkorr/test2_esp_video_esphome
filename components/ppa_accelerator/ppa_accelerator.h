#pragma once

// ============================================================================
// PPA Accelerator Component for ESP32-P4
// ============================================================================
// Hardware-accelerated image processing using ESP32-P4's PPA module
// Supports: Scaling, Rotation, Mirroring, Blending, Fill
// ============================================================================

#include "esphome/core/component.h"
#include "esphome/core/log.h"

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include <cstring>

namespace esphome {
namespace ppa_accelerator {

static const char *const TAG = "ppa";

// ============================================================================
// Supported pixel formats
// ============================================================================
enum class PixelFormat {
  RGB565,      // 16-bit RGB (5-6-5)
  RGB888,      // 24-bit RGB
  ARGB8888,    // 32-bit ARGB
};

// ============================================================================
// PPA Operation result
// ============================================================================
struct PPAResult {
  bool success;
  const char* error_message;
  uint32_t processing_time_us;
};

// ============================================================================
// Buffer configuration for PPA operations
// ============================================================================
struct PPABuffer {
  uint8_t* data;
  uint16_t width;
  uint16_t height;
  PixelFormat format;
  size_t stride;  // Bytes per row (0 = auto-calculate)

  // Constructor
  PPABuffer() : data(nullptr), width(0), height(0), format(PixelFormat::RGB565), stride(0) {}

  PPABuffer(uint8_t* d, uint16_t w, uint16_t h, PixelFormat fmt = PixelFormat::RGB565)
    : data(d), width(w), height(h), format(fmt), stride(0) {}

  // Get bytes per pixel
  size_t get_bpp() const {
    switch (format) {
      case PixelFormat::RGB565: return 2;
      case PixelFormat::RGB888: return 3;
      case PixelFormat::ARGB8888: return 4;
      default: return 2;
    }
  }

  // Get actual stride
  size_t get_stride() const {
    return stride > 0 ? stride : width * get_bpp();
  }

  // Get total buffer size
  size_t get_size() const {
    return get_stride() * height;
  }
};

// ============================================================================
// PPA Accelerator Component
// ============================================================================
class PPAAccelerator : public Component {
 public:
  PPAAccelerator() = default;
  ~PPAAccelerator();

  // Component lifecycle
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Check if PPA is available and initialized
  bool is_available() const { return this->initialized_; }

  // =========================================================================
  // Scale operation - Hardware accelerated scaling
  // =========================================================================
  PPAResult scale(const PPABuffer& input, PPABuffer& output,
                  float scale_x = 1.0f, float scale_y = 1.0f);

  // Scale with specific output dimensions
  PPAResult scale_to_size(const PPABuffer& input, PPABuffer& output,
                          uint16_t target_width, uint16_t target_height);

  // =========================================================================
  // Blend operation - Alpha blending of two images
  // =========================================================================
  PPAResult blend(const PPABuffer& foreground, const PPABuffer& background,
                  PPABuffer& output, uint8_t fg_alpha = 255);

  // =========================================================================
  // Fill operation - Fill buffer with solid color
  // =========================================================================
  PPAResult fill(PPABuffer& buffer, uint32_t color_argb8888);

  // Fill a region within buffer
  PPAResult fill_rect(PPABuffer& buffer, uint16_t x, uint16_t y,
                      uint16_t width, uint16_t height, uint32_t color_argb8888);

  // =========================================================================
  // Mirror/Rotate operations
  // =========================================================================
  PPAResult mirror_horizontal(const PPABuffer& input, PPABuffer& output);
  PPAResult mirror_vertical(const PPABuffer& input, PPABuffer& output);
  PPAResult rotate(const PPABuffer& input, PPABuffer& output, int angle_degrees);

  // =========================================================================
  // Buffer management helpers
  // =========================================================================

  // Allocate DMA-capable buffer for PPA operations
  static uint8_t* allocate_dma_buffer(size_t size);

  // Free DMA buffer
  static void free_dma_buffer(uint8_t* buffer);

  // Check if buffer is DMA-capable
  static bool is_dma_capable(const uint8_t* buffer);

  // =========================================================================
  // Utility functions
  // =========================================================================

  // Convert RGB888 color to RGB565
  static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  // Convert ARGB8888 to RGB565 (discards alpha)
  static uint16_t argb8888_to_rgb565(uint32_t argb) {
    uint8_t r = (argb >> 16) & 0xFF;
    uint8_t g = (argb >> 8) & 0xFF;
    uint8_t b = argb & 0xFF;
    return rgb888_to_rgb565(r, g, b);
  }

 protected:
  bool initialized_{false};

  // PPA client handles
  ppa_client_handle_t srm_client_{nullptr};    // Scale-Rotate-Mirror client
  ppa_client_handle_t blend_client_{nullptr};  // Blend client
  ppa_client_handle_t fill_client_{nullptr};   // Fill client

  // Convert our format enum to PPA format
  ppa_srm_color_mode_t to_ppa_srm_format(PixelFormat format) const;
  ppa_blend_color_mode_t to_ppa_blend_format(PixelFormat format) const;
  ppa_fill_color_mode_t to_ppa_fill_format(PixelFormat format) const;

  // Internal operation implementations
  PPAResult do_srm_operation(const PPABuffer& input, PPABuffer& output,
                              float scale_x, float scale_y,
                              int rotation_angle, bool mirror_x, bool mirror_y);
};

// ============================================================================
// Global accessor
// ============================================================================
PPAAccelerator* get_ppa_accelerator();

}  // namespace ppa_accelerator
}  // namespace esphome

#else  // Not ESP32-P4

// Stub for non-ESP32-P4 platforms
namespace esphome {
namespace ppa_accelerator {

class PPAAccelerator : public Component {
 public:
  bool is_available() const { return false; }
  void setup() override {
    ESP_LOGW("ppa", "PPA not available on this platform");
  }
  void dump_config() override {
    ESP_LOGCONFIG("ppa", "PPA Accelerator: NOT AVAILABLE (requires ESP32-P4)");
  }
};

}  // namespace ppa_accelerator
}  // namespace esphome

#endif  // CONFIG_IDF_TARGET_ESP32P4
