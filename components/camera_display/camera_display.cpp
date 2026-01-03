/*
 * camera_display.cpp - Waveshare-style camera display implementation
 *
 * Based on Waveshare ESP32-P4 Camera.cpp
 * https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B
 */

#include "camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace camera_display {

static const char *const TAG = "camera_display";

// Singleton instance for callback
CameraDisplay *CameraDisplay::instance_ = nullptr;

void CameraDisplay::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Waveshare Camera Display...");

  // Register singleton for callback
  if (instance_ != nullptr) {
    ESP_LOGW(TAG, "Multiple CameraDisplay instances detected - only one supported");
  }
  instance_ = this;

  // Initialize app_video (esp_video + camera sensor)
  app_video_config_t config = {
    .sensor_name = this->sensor_name_.c_str(),
    .width = this->width_,
    .height = this->height_,
    .pixel_format = this->pixel_format_,
    .buffer_count = this->buffer_count_,
    .i2c_bus = nullptr,  // Will use default BSP I2C
  };

  esp_err_t ret = app_video_main(&config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize camera: %s", esp_err_to_name(ret));
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Camera initialized successfully");
  ESP_LOGI(TAG, "  Sensor: %s", this->sensor_name_.c_str());
  ESP_LOGI(TAG, "  Resolution: %lux%lu", this->width_, this->height_);
  ESP_LOGI(TAG, "  Buffers: %lu (DMA-aligned in SPIRAM)", this->buffer_count_);
  ESP_LOGI(TAG, "  CPU Core: %d", this->cpu_core_);
  ESP_LOGI(TAG, "  Mirror: H=%d, V=%d", this->mirror_x_, this->mirror_y_);

  this->initialized_ = true;
}

void CameraDisplay::loop() {
  // Nothing needed - streaming runs in dedicated task
}

void CameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "Waveshare Camera Display:");
  ESP_LOGCONFIG(TAG, "  Sensor: %s", this->sensor_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Resolution: %lux%lu", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Buffer Count: %lu", this->buffer_count_);
  ESP_LOGCONFIG(TAG, "  CPU Core: %d", this->cpu_core_);
  ESP_LOGCONFIG(TAG, "  Status: %s", this->streaming_ ? "Streaming" : "Stopped");
  if (this->streaming_) {
    ESP_LOGCONFIG(TAG, "  FPS: %.1f", this->current_fps_);
  }
}

void CameraDisplay::set_pixel_format(const std::string &fmt) {
  if (fmt == "RGB565") {
    this->pixel_format_ = APP_VIDEO_FMT_RGB565;
  } else if (fmt == "RGB888") {
    this->pixel_format_ = APP_VIDEO_FMT_RGB888;
  } else if (fmt == "RAW8") {
    this->pixel_format_ = APP_VIDEO_FMT_RAW8;
  } else if (fmt == "GRAY") {
    this->pixel_format_ = APP_VIDEO_FMT_GRAY;
  } else {
    ESP_LOGW(TAG, "Unknown pixel format '%s', using RGB565", fmt.c_str());
    this->pixel_format_ = APP_VIDEO_FMT_RGB565;
  }
}

void CameraDisplay::set_canvas(lv_obj_t *canvas) {
  this->canvas_obj_ = canvas;
  ESP_LOGI(TAG, "Canvas configured: %p", canvas);

  if (canvas != nullptr) {
    lv_coord_t w = lv_obj_get_width(canvas);
    lv_coord_t h = lv_obj_get_height(canvas);
    ESP_LOGI(TAG, "  Canvas size: %dx%d", w, h);

    if (w != this->width_ || h != this->height_) {
      ESP_LOGW(TAG, "  Canvas size mismatch! Expected %lux%lu",
               this->width_, this->height_);
    }
  }
}

void CameraDisplay::start_streaming() {
  if (this->streaming_) {
    ESP_LOGW(TAG, "Already streaming");
    return;
  }

  if (!this->initialized_) {
    ESP_LOGE(TAG, "Cannot start streaming - not initialized");
    return;
  }

  if (this->canvas_obj_ == nullptr) {
    ESP_LOGE(TAG, "Cannot start streaming - canvas not configured");
    ESP_LOGE(TAG, "Call set_canvas() first or configure canvas_id in YAML");
    return;
  }

  ESP_LOGI(TAG, "Starting camera streaming...");

  // Open camera device
  this->camera_fd_ = app_video_open(
    this->width_, this->height_, this->pixel_format_,
    this->mirror_x_, this->mirror_y_
  );

  if (this->camera_fd_ < 0) {
    ESP_LOGE(TAG, "Failed to open camera device");
    return;
  }

  ESP_LOGI(TAG, "Camera device opened: fd=%d", this->camera_fd_);

  // Setup buffers
  esp_err_t ret = app_video_set_bufs(this->camera_fd_, this->buffer_count_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to setup buffers");
    close(this->camera_fd_);
    this->camera_fd_ = -1;
    return;
  }

  // Register frame callback
  app_video_register_frame_operation_cb(CameraDisplay::frame_callback_);

  // Start streaming task
  ret = app_video_stream_task_start(
    this->camera_fd_, this->cpu_core_, this->width_, this->height_
  );

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start streaming task");
    close(this->camera_fd_);
    this->camera_fd_ = -1;
    return;
  }

  this->streaming_ = true;
  this->frame_count_ = 0;
  this->last_fps_time_ = millis();

  ESP_LOGI(TAG, "✅ Camera streaming started!");
  ESP_LOGI(TAG, "Expected FPS: 25-30 (Waveshare DMA architecture)");
}

void CameraDisplay::stop_streaming() {
  if (!this->streaming_) {
    ESP_LOGW(TAG, "Not streaming");
    return;
  }

  ESP_LOGI(TAG, "Stopping camera streaming...");

  // Stop streaming task
  app_video_stream_task_stop();
  app_video_stream_wait_stop();

  // Close camera device
  if (this->camera_fd_ >= 0) {
    close(this->camera_fd_);
    this->camera_fd_ = -1;
  }

  this->streaming_ = false;
  ESP_LOGI(TAG, "Camera streaming stopped (total frames: %lu)", this->frame_count_);
}

/**
 * Frame callback - called from streaming task (app_video.c)
 *
 * This is the critical path for performance:
 * 1. Update LVGL canvas buffer pointer (zero-copy)
 * 2. Trigger LVGL refresh (lv_refr_now)
 * 3. Buffer stays valid until next frame (no copy needed)
 */
void CameraDisplay::frame_callback_(uint8_t *buf, uint8_t buf_idx,
                                    uint32_t width, uint32_t height, size_t len) {
  if (instance_ == nullptr || instance_->canvas_obj_ == nullptr) {
    return;
  }

  CameraDisplay *self = instance_;

  // Update canvas to point to DMA buffer (zero-copy!)
  lv_canvas_set_buffer(self->canvas_obj_, buf, width, height, LV_IMG_CF_TRUE_COLOR);

  // Immediate refresh (Waveshare style - LVGL v8)
  lv_refr_now(NULL);

  // Update statistics
  self->frame_count_++;

  if (self->frame_count_ % 100 == 0) {
    uint32_t now = millis();
    if (self->last_fps_time_ > 0) {
      float elapsed = (now - self->last_fps_time_) / 1000.0f;
      self->current_fps_ = 100.0f / elapsed;
      ESP_LOGI(TAG, "Streaming: %lu frames (%.1f FPS) [buffer %d]",
               self->frame_count_, self->current_fps_, buf_idx);
    }
    self->last_fps_time_ = now;
  }
}

}  // namespace camera_display
}  // namespace esphome
