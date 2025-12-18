#include "yolo11_detection.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

// ESP-DL detection components (only for YOLO11 model)
#ifdef ESP_DL_MODEL_YOLO11
#include "dl_detect_base.hpp"
#include "dl_detect_yolo11_postprocessor.hpp"
#include "dl_image.hpp"
#endif

namespace esphome {
namespace yolo11_detection {

static const char *const TAG = "yolo11_detection";

void YOLO11DetectionComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up YOLO11 Object Detection...");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not configured");
    this->mark_failed();
    return;
  }

  // Create mutex for thread-safe access to cached results
  this->detections_mutex_ = xSemaphoreCreateMutex();
  if (this->detections_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create detections mutex");
    this->mark_failed();
    return;
  }

#ifndef ESP_DL_MODEL_YOLO11
  ESP_LOGE(TAG, "YOLO11 Detection component requires ESP_DL_MODEL_YOLO11 flag");
  ESP_LOGE(TAG, "YOLO11 model is not available or not compiled");
  ESP_LOGE(TAG, "Please ensure a YOLO11 model file (.espdl) is available and properly configured");
  this->mark_failed();
  return;
#else
  // Initialize YOLO11 object detector
  ESP_LOGI(TAG, "Initializing YOLO11 object detector...");

  // TODO: Load YOLO11 model and create detector instance
  // This requires a YOLO11 model file (.espdl) similar to pedestrian_detect or face_detect
  ESP_LOGE(TAG, "YOLO11 model loading not yet implemented");
  ESP_LOGE(TAG, "A YOLO11 .espdl model file is required but not provided");
  this->mark_failed();
  return;

  ESP_LOGI(TAG, "YOLO11 Object Detection ready");
  ESP_LOGI(TAG, "  Detection interval: every %d frames", this->detection_interval_);
  ESP_LOGI(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGI(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  ESP_LOGI(TAG, "  Draw boxes: %s", this->draw_enabled_ ? "YES" : "NO");
#endif
}

void YOLO11DetectionComponent::loop() {
  // Check if camera is streaming
  if (this->camera_ == nullptr || !this->camera_->is_streaming()) {
    return;
  }

  this->process_frame_();
}

void YOLO11DetectionComponent::process_frame_() {
  this->frame_counter_++;

  // Only run detection every N frames
  if (this->frame_counter_ < this->detection_interval_) {
    return;
  }

  this->frame_counter_ = 0;

  // Acquire buffer from camera
  esp_cam_sensor::SimpleBufferElement *buffer = this->camera_->acquire_buffer();
  if (buffer == nullptr) {
    return;
  }

  uint8_t* img_data = this->camera_->get_buffer_data(buffer);
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data != nullptr) {
    this->detect_objects_(img_data, width, height);
  }

  // Release buffer
  this->camera_->release_buffer(buffer);
}

void YOLO11DetectionComponent::detect_objects_(uint8_t *img_data, uint16_t width, uint16_t height) {
#ifdef ESP_DL_MODEL_YOLO11
  if (this->object_detector_ == nullptr) {
    return;
  }

  // Create image structure for ESP-DL
  dl::image::img_t img = {
    .data = img_data,
    .width = width,
    .height = height,
    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
  };

  // Run YOLO11 object detection
  std::list<dl::detect::result_t> &detection_results = this->object_detector_->run(img);

  // Cache results (mutex protected)
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    this->cached_detections_.clear();

    for (auto &result : detection_results) {
      DetectionBox box;
      box.x1 = result.box[0];
      box.y1 = result.box[1];
      box.x2 = result.box[2];
      box.y2 = result.box[3];
      box.score = result.score;
      box.category = result.category;

      this->cached_detections_.push_back(box);
    }

    xSemaphoreGive(this->detections_mutex_);
  }

  // Trigger callbacks
  if (detection_results.size() > 0) {
    for (auto &callback : this->on_object_detected_callbacks_) {
      callback(detection_results.size());
    }
  }
#endif
}

void YOLO11DetectionComponent::draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (!this->draw_enabled_) {
    return;
  }
  this->draw_results_(img_data, width, height);
}

void YOLO11DetectionComponent::draw_results_(uint8_t *img_data, uint16_t width, uint16_t height) {
#ifdef ESP_DL_MODEL_YOLO11
  if (img_data == nullptr || this->detections_mutex_ == nullptr) {
    return;
  }

  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    // RGB565 colors for different detection categories
    // Format: RGB565 in little-endian (LSB, MSB)
    const uint16_t COLOR_RED    = 0xF800;  // Person (category 0)
    const uint16_t COLOR_GREEN  = 0x07E0;  // Car (category 2)
    const uint16_t COLOR_BLUE   = 0x001F;  // Dog (category 16)
    const uint16_t COLOR_YELLOW = 0xFFE0;  // Default

    for (auto &box : this->cached_detections_) {
      // Clamp bounding box coordinates to valid range
      int x1 = std::max(2, std::min((int)box.x1, (int)width - 3));
      int y1 = std::max(2, std::min((int)box.y1, (int)height - 3));
      int x2 = std::max(x1 + 10, std::min((int)box.x2, (int)width - 3));
      int y2 = std::max(y1 + 10, std::min((int)box.y2, (int)height - 3));

      // Choose color based on category
      uint16_t color;
      switch (box.category) {
        case 0:  color = COLOR_RED; break;    // Person
        case 2:  color = COLOR_GREEN; break;  // Car
        case 16: color = COLOR_BLUE; break;   // Dog
        default: color = COLOR_YELLOW; break;
      }

      // Draw 2-pixel thick rectangle
      const int line_width = 2;
      uint16_t *buffer = (uint16_t *)img_data;

      // Draw top and bottom horizontal lines
      for (int x = x1; x <= x2; x++) {
        for (int t = 0; t < line_width; t++) {
          // Top line
          int top_offset = (y1 + t) * width + x;
          if (top_offset >= 0 && top_offset < width * height) {
            buffer[top_offset] = color;
          }
          // Bottom line
          int bottom_offset = (y2 - t) * width + x;
          if (bottom_offset >= 0 && bottom_offset < width * height) {
            buffer[bottom_offset] = color;
          }
        }
      }

      // Draw left and right vertical lines
      for (int y = y1; y <= y2; y++) {
        for (int t = 0; t < line_width; t++) {
          // Left line
          int left_offset = y * width + (x1 + t);
          if (left_offset >= 0 && left_offset < width * height) {
            buffer[left_offset] = color;
          }
          // Right line
          int right_offset = y * width + (x2 - t);
          if (right_offset >= 0 && right_offset < width * height) {
            buffer[right_offset] = color;
          }
        }
      }
    }

    ESP_LOGD(TAG, "Drew %d detection boxes on frame", this->cached_detections_.size());
    xSemaphoreGive(this->detections_mutex_);
  }
#endif
}

void YOLO11DetectionComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLO11 Object Detection:");
  ESP_LOGCONFIG(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGCONFIG(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  ESP_LOGCONFIG(TAG, "  Detection interval: %d frames", this->detection_interval_);
  ESP_LOGCONFIG(TAG, "  Draw enabled: %s", this->draw_enabled_ ? "YES" : "NO");
}

int YOLO11DetectionComponent::get_detected_count() {
  int count = 0;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    count = this->cached_detections_.size();
    xSemaphoreGive(this->detections_mutex_);
  }
  return count;
}

std::vector<DetectionBox> YOLO11DetectionComponent::get_detections() {
  std::vector<DetectionBox> detections;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    detections = this->cached_detections_;
    xSemaphoreGive(this->detections_mutex_);
  }
  return detections;
}

}  // namespace yolo11_detection
}  // namespace esphome
