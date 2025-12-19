#include "pedestrian_detection.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

// ESP-DL detection component
#include "pedestrian_detect.hpp"
#include "dl_image.hpp"

namespace esphome {
namespace pedestrian_detection {

static const char *const TAG = "pedestrian_detection";

void PedestrianDetectionComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Pedestrian Detection...");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not configured");
    this->mark_failed();
    return;
  }

  // Create mutex for thread-safe access to cached results
  this->pedestrian_results_mutex_ = xSemaphoreCreateMutex();
  if (this->pedestrian_results_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create pedestrian results mutex");
    this->mark_failed();
    return;
  }

  // Initialize pedestrian detector
  ESP_LOGI(TAG, "Initializing pedestrian detector...");
  this->pedestrian_detector_ = new PedestrianDetect();
  if (this->pedestrian_detector_ != nullptr) {
    this->pedestrian_detector_->set_score_thr(this->score_threshold_);
    this->pedestrian_detector_->set_nms_thr(this->nms_threshold_);
    ESP_LOGI(TAG, "Pedestrian detector initialized (score_thr=%.2f, nms_thr=%.2f)",
             this->score_threshold_, this->nms_threshold_);
  } else {
    ESP_LOGE(TAG, "Failed to initialize pedestrian detector");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Pedestrian Detection ready");
  ESP_LOGI(TAG, "  Detection interval: every %d frames", this->detection_interval_);
  ESP_LOGI(TAG, "  Draw boxes: %s", this->draw_enabled_ ? "YES" : "NO");
}

void PedestrianDetectionComponent::loop() {
  // Check if camera is streaming
  if (this->camera_ == nullptr || !this->camera_->is_streaming()) {
    return;
  }

  this->process_frame_();
}

void PedestrianDetectionComponent::process_frame_() {
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

  uint8_t *img_data = this->camera_->get_buffer_data(buffer);
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data != nullptr) {
    this->detect_pedestrians_(img_data, width, height);
    // NOTE: Don't draw here to avoid flickering!
    // Drawing is done by lvgl_camera_display via draw_on_frame()
  }

  this->camera_->release_buffer(buffer);
}

void PedestrianDetectionComponent::detect_pedestrians_(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (this->pedestrian_detector_ == nullptr) {
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

  // Run pedestrian detection (PICO model)
  std::list<dl::detect::result_t> &ped_results = this->pedestrian_detector_->run(img);

  // Reset watchdog after detection
  App.feed_wdt();

  // Cache results (mutex protected)
  if (xSemaphoreTake(this->pedestrian_results_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    this->cached_pedestrian_results_.clear();

    for (auto &result : ped_results) {
      PedestrianBox box;
      box.x1 = result.box[0];
      box.y1 = result.box[1];
      box.x2 = result.box[2];
      box.y2 = result.box[3];
      box.score = result.score;

      this->cached_pedestrian_results_.push_back(box);
    }

    xSemaphoreGive(this->pedestrian_results_mutex_);
  }

  // Trigger pedestrian detected callback
  if (ped_results.size() > 0) {
    for (auto &callback : this->on_pedestrian_detected_callbacks_) {
      callback(ped_results.size());
    }
  }
}

void PedestrianDetectionComponent::draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (this->draw_enabled_ && img_data != nullptr) {
    this->draw_results_(img_data, width, height);
  }
}

void PedestrianDetectionComponent::draw_results_(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (img_data == nullptr || this->pedestrian_results_mutex_ == nullptr) {
    return;
  }

  // Create image structure for ESP-DL drawing
  dl::image::img_t img = {
    .data = img_data,
    .width = width,
    .height = height,
    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
  };

  if (xSemaphoreTake(this->pedestrian_results_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    // RGB565 little-endian: Blue
    std::vector<uint8_t> blue = {0x1F, 0x00};

    for (auto &box : this->cached_pedestrian_results_) {
      // Draw blue bounding box
      dl::image::draw_hollow_rectangle(
        img,
        box.x1, box.y1,
        box.x2, box.y2,
        blue, 3
      );
    }

    xSemaphoreGive(this->pedestrian_results_mutex_);
  }
}

void PedestrianDetectionComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Pedestrian Detection:");
  ESP_LOGCONFIG(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGCONFIG(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  ESP_LOGCONFIG(TAG, "  Detection interval: %d frames", this->detection_interval_);
  ESP_LOGCONFIG(TAG, "  Draw enabled: %s", this->draw_enabled_ ? "YES" : "NO");
}

int PedestrianDetectionComponent::get_detected_pedestrian_count() {
  int count = 0;
  if (xSemaphoreTake(this->pedestrian_results_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    count = this->cached_pedestrian_results_.size();
    xSemaphoreGive(this->pedestrian_results_mutex_);
  }
  return count;
}

std::vector<PedestrianBox> PedestrianDetectionComponent::get_detected_pedestrians() {
  std::vector<PedestrianBox> pedestrians;
  if (xSemaphoreTake(this->pedestrian_results_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    pedestrians = this->cached_pedestrian_results_;
    xSemaphoreGive(this->pedestrian_results_mutex_);
  }
  return pedestrians;
}

}  // namespace pedestrian_detection
}  // namespace esphome
