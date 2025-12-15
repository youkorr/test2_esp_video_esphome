#include "yolo11_detection.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

// ESP-DL detection components (only for YOLO11 model)
#ifdef ESP_DL_MODEL_YOLO11
#include "dl_detect.hpp"
#include "dl_image.hpp"
#endif

namespace esphome {
namespace yolo11_detection {

static const char *const TAG = "yolo11_detection";

void YOLO11DetectionComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up YOLO11 Object Detection...");

#ifndef ESP_DL_MODEL_YOLO11
  ESP_LOGE(TAG, "YOLO11 Detection component requires ESP_DL_MODEL_YOLO11 build flag");
  ESP_LOGE(TAG, "This component is for YOLO11 object detection only");
  this->mark_failed();
  return;
#else

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

  // Initialize YOLO11 detector
  ESP_LOGI(TAG, "Initializing YOLO11 object detector...");
  // Note: Actual YOLO11 initialization would go here
  // this->object_detector_ = new YOLODetectWrapper(...);
  // For now, we'll log that it's a placeholder
  ESP_LOGW(TAG, "YOLO11 detector initialization - implementation needed");
  ESP_LOGW(TAG, "This requires YOLO11 model and wrapper implementation");

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

  // Run YOLO11 detection
  // Note: This is a placeholder - actual YOLO11 implementation would go here
  // std::list<dl::detect::result_t> &results = this->object_detector_->run(img);

  // Cache results (mutex protected)
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    this->cached_detections_.clear();

    // Example of processing results (placeholder)
    // for (auto &result : results) {
    //   DetectionBox box;
    //   box.x1 = result.box[0];
    //   box.y1 = result.box[1];
    //   box.x2 = result.box[2];
    //   box.y2 = result.box[3];
    //   box.score = result.score;
    //   box.category = result.category;
    //   this->cached_detections_.push_back(box);
    // }

    xSemaphoreGive(this->detections_mutex_);
  }

  // Trigger callbacks
  int detection_count = this->cached_detections_.size();
  if (detection_count > 0) {
    for (auto &callback : this->on_object_detected_callbacks_) {
      callback(detection_count);
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

  // Create image structure for ESP-DL drawing
  dl::image::img_t img = {
    .data = img_data,
    .width = width,
    .height = height,
    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
  };

  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    // RGB565 colors (little-endian)
    std::vector<uint8_t> yellow = {0xE0, 0xFF};   // Yellow for detected objects
    std::vector<uint8_t> white = {0xFF, 0xFF};    // White for labels

    for (auto &box : this->cached_detections_) {
      // Clamp bounding box coordinates to valid range
      int x1 = std::max(3, std::min((int)box.x1, (int)width - 4));
      int y1 = std::max(3, std::min((int)box.y1, (int)height - 4));
      int x2 = std::max(x1 + 1, std::min((int)box.x2, (int)width - 4));
      int y2 = std::max(y1 + 1, std::min((int)box.y2, (int)height - 4));

      // Draw bounding box
      int line_width = 3;
      dl::image::draw_hollow_rectangle(img, x1, y1, x2, y2, yellow, line_width);

      // Draw category label above the box
      // Note: Category names would be defined elsewhere
      // For now, just draw the category number
      std::string label = "Obj " + std::to_string(box.category);
      // Text drawing would go here (ESP-DL doesn't have built-in text rendering)
    }

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
