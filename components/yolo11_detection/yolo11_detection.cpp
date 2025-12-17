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

  // PLACEHOLDER: Using test detection boxes instead of real YOLO11 model
  ESP_LOGI(TAG, "Initializing YOLO11 object detector (PLACEHOLDER MODE)...");
  ESP_LOGI(TAG, "  Using test rectangles for demonstration");
  ESP_LOGI(TAG, "  Real YOLO11 model integration pending");

  ESP_LOGI(TAG, "YOLO11 Object Detection ready");
  ESP_LOGI(TAG, "  Detection interval: every %d frames", this->detection_interval_);
  ESP_LOGI(TAG, "  Score threshold: %.2f (not used in placeholder)", this->score_threshold_);
  ESP_LOGI(TAG, "  NMS threshold: %.2f (not used in placeholder)", this->nms_threshold_);
  ESP_LOGI(TAG, "  Draw boxes: %s", this->draw_enabled_ ? "YES" : "NO");
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
  // PLACEHOLDER IMPLEMENTATION: Create fake detection boxes for testing
  // This simulates YOLO11 object detection with test rectangles

  // Cache results (mutex protected)
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    this->cached_detections_.clear();

    // Create 3 test detection boxes at different positions
    // Box 1: Top-left (simulates "person" detection)
    DetectionBox box1;
    box1.x1 = width * 0.1f;   // 10% from left
    box1.y1 = height * 0.1f;  // 10% from top
    box1.x2 = width * 0.35f;  // 35% from left
    box1.y2 = height * 0.5f;  // 50% from top
    box1.score = 0.85f;
    box1.category = 0;  // Category 0 = person
    this->cached_detections_.push_back(box1);

    // Box 2: Center (simulates "car" detection)
    DetectionBox box2;
    box2.x1 = width * 0.4f;   // 40% from left
    box2.y1 = height * 0.3f;  // 30% from top
    box2.x2 = width * 0.7f;   // 70% from left
    box2.y2 = height * 0.7f;  // 70% from top
    box2.score = 0.92f;
    box2.category = 2;  // Category 2 = car
    this->cached_detections_.push_back(box2);

    // Box 3: Bottom-right (simulates "dog" detection)
    DetectionBox box3;
    box3.x1 = width * 0.6f;   // 60% from left
    box3.y1 = height * 0.6f;  // 60% from top
    box3.x2 = width * 0.9f;   // 90% from left
    box3.y2 = height * 0.9f;  // 90% from top
    box3.score = 0.78f;
    box3.category = 16;  // Category 16 = dog
    this->cached_detections_.push_back(box3);

    ESP_LOGI(TAG, "PLACEHOLDER: Generated %d test detection boxes", this->cached_detections_.size());

    xSemaphoreGive(this->detections_mutex_);
  }

  // Trigger callbacks
  int detection_count = this->cached_detections_.size();
  if (detection_count > 0) {
    for (auto &callback : this->on_object_detected_callbacks_) {
      callback(detection_count);
    }
  }
}

void YOLO11DetectionComponent::draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (!this->draw_enabled_) {
    return;
  }
  this->draw_results_(img_data, width, height);
}

void YOLO11DetectionComponent::draw_results_(uint8_t *img_data, uint16_t width, uint16_t height) {
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
