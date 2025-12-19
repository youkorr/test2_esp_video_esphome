#include "yolo11_detection.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

// ESP-DL detection components (only for YOLO11 model)
#ifdef ESP_DL_MODEL_YOLO11
#include "yolo11_detect.hpp"
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

  this->object_detector_ = new YOLO11Detect();
  if (this->object_detector_ != nullptr) {
    this->object_detector_->set_score_thr(this->score_threshold_);
    this->object_detector_->set_nms_thr(this->nms_threshold_);
    ESP_LOGI(TAG, "YOLO11 detector initialized (score_thr=%.2f, nms_thr=%.2f)",
             this->score_threshold_, this->nms_threshold_);
  } else {
    ESP_LOGE(TAG, "Failed to initialize YOLO11 detector");
    this->mark_failed();
    return;
  }

  // Create frame queue for detection task (queue of 2 frames max)
  this->frame_queue_ = xQueueCreate(2, sizeof(esp_cam_sensor::SimpleBufferElement*));
  if (this->frame_queue_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create frame queue");
    this->mark_failed();
    return;
  }

  // Create detection task (runs on CPU 0, priority 5, 8KB stack)
  BaseType_t result = xTaskCreatePinnedToCore(
    detection_task_func_,      // Task function
    "yolo11_detect",           // Task name
    8192,                      // Stack size (8KB)
    this,                      // Task parameter (this pointer)
    5,                         // Priority
    &this->detection_task_handle_,  // Task handle
    0                          // CPU 0 (separate from main loop on CPU 1)
  );

  if (result != pdPASS || this->detection_task_handle_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create detection task");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "YOLO11 Object Detection ready");
  ESP_LOGI(TAG, "  Detection interval: every %d frames", this->detection_interval_);
  ESP_LOGI(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGI(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  ESP_LOGI(TAG, "  Draw boxes: %s", this->draw_enabled_ ? "YES" : "NO");
  ESP_LOGI(TAG, "  Detection task running on CPU 0 (non-blocking)");
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

  // Send buffer to detection task (non-blocking)
  // If queue is full, skip this frame (detection task is still processing previous frame)
  if (xQueueSend(this->frame_queue_, &buffer, 0) != pdTRUE) {
    // Queue full - detection task is busy, release buffer and skip this frame
    this->camera_->release_buffer(buffer);
  }
  // Note: Detection task will release the buffer when done
}

void YOLO11DetectionComponent::detect_objects_(uint8_t *img_data, uint16_t width, uint16_t height) {
#ifdef ESP_DL_MODEL_YOLO11
  if (this->object_detector_ == nullptr) {
    return;
  }

  // Reset watchdog before long operation
  App.feed_wdt();

  // Create image structure for ESP-DL
  dl::image::img_t img = {
    .data = img_data,
    .width = width,
    .height = height,
    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
  };

  // Run YOLO11 object detection (can take several seconds)
  std::list<dl::detect::result_t> &detection_results = this->object_detector_->run(img);

  // Reset watchdog after inference
  App.feed_wdt();

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

void YOLO11DetectionComponent::detection_task_func_(void *param) {
  YOLO11DetectionComponent *component = static_cast<YOLO11DetectionComponent*>(param);
  ESP_LOGI(TAG, "Detection task started on CPU %d", xPortGetCoreID());

  esp_cam_sensor::SimpleBufferElement *buffer = nullptr;

  while (true) {
    // Wait for frame from queue (blocks until frame available)
    if (xQueueReceive(component->frame_queue_, &buffer, portMAX_DELAY) == pdTRUE && buffer != nullptr) {
      uint8_t* img_data = component->camera_->get_buffer_data(buffer);
      uint16_t width = component->camera_->get_image_width();
      uint16_t height = component->camera_->get_image_height();

      if (img_data != nullptr) {
        // Run detection (can take several seconds - no problem in separate task!)
        component->detect_objects_(img_data, width, height);
      }

      // Release buffer back to camera
      component->camera_->release_buffer(buffer);
      buffer = nullptr;

      // Yield to let other tasks run
      taskYIELD();
    }
  }

  // Task should never exit, but clean up if it does
  vTaskDelete(nullptr);
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
