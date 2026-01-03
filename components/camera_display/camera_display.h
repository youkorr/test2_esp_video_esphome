/*
 * camera_display.h - Waveshare-style camera display for ESPHome
 *
 * Based on:
 * - Waveshare ESP32-P4 Camera.cpp/hpp
 * - esp_brookesia architecture (DMA camera → LCD)
 *
 * Architecture:
 * ┌─────────────────────────────────────────┐
 * │ Camera DMA → Triple Buffer → LVGL Canvas│
 * │ (Zero-copy, 25-30 FPS expected)         │
 * └─────────────────────────────────────────┘
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "app_video.h"

namespace esphome {
namespace camera_display {

class CameraDisplay : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::LATE; }

  // Configuration setters (called by ESPHome Python)
  void set_sensor_name(const std::string &name) { this->sensor_name_ = name; }
  void set_width(uint32_t width) { this->width_ = width; }
  void set_height(uint32_t height) { this->height_ = height; }
  void set_buffer_count(uint32_t count) { this->buffer_count_ = count; }
  void set_cpu_core(int core) { this->cpu_core_ = core; }
  void set_pixel_format(const std::string &fmt);
  void set_mirror_x(bool mirror) { this->mirror_x_ = mirror; }
  void set_mirror_y(bool mirror) { this->mirror_y_ = mirror; }

  // LVGL canvas integration
  void set_canvas(lv_obj_t *canvas);
  lv_obj_t *get_canvas() { return this->canvas_obj_; }

  // Control API
  void start_streaming();
  void stop_streaming();
  bool is_streaming() { return this->streaming_; }

  // Statistics
  float get_fps() { return this->current_fps_; }

 protected:
  // Camera configuration
  std::string sensor_name_{"sc202cs"};
  uint32_t width_{800};
  uint32_t height_{600};
  uint32_t buffer_count_{2};
  int cpu_core_{0};
  video_fmt_t pixel_format_{APP_VIDEO_FMT_RGB565};
  bool mirror_x_{false};
  bool mirror_y_{false};

  // LVGL canvas
  lv_obj_t *canvas_obj_{nullptr};

  // Runtime state
  int camera_fd_{-1};
  bool streaming_{false};
  bool initialized_{false};

  // Statistics
  float current_fps_{0.0f};
  uint32_t frame_count_{0};
  uint32_t last_fps_time_{0};

  // Frame callback - called from app_video streaming task
  static void frame_callback_(uint8_t *buf, uint8_t buf_idx,
                             uint32_t width, uint32_t height, size_t len);

  // Singleton reference for callback
  static CameraDisplay *instance_;
};

}  // namespace camera_display
}  // namespace esphome
