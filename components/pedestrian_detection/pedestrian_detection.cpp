#include "pedestrian_detection.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

// ESP-DL detection component (only for pedestrian detection model)
#ifdef ESP_DL_MODEL_PEDESTRIAN
#include "pedestrian_detect.hpp"
#include "dl_image.hpp"
#endif

namespace esphome {
namespace pedestrian_detection {

static const char *const TAG = "pedestrian_detection";

void PedestrianDetectionComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Pedestrian Detection...");

#ifndef ESP_DL_MODEL_PEDESTRIAN
  ESP_LOGE(TAG, "Pedestrian Detection component requires ESP_DL_MODEL_PEDESTRIAN flag");
  ESP_LOGE(TAG, "Pedestrian detection model is not available or not compiled");
  ESP_LOGE(TAG, "Please ensure a pedestrian detection model file (.espdl) is available and properly configured");
  this->mark_failed();
  return;
#else

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

#if CONFIG_PEDESTRIAN_DETECT_MODEL_IN_SDCARD
  if (this->sdcard_model_path_ != nullptr) {
    ESP_LOGI(TAG, "Waiting for SD card to mount (6 seconds)...");
    delay(6000);  // Wait for SD card to be mounted
    ESP_LOGI(TAG, "Loading pedestrian detection model from SD card: %s", this->sdcard_model_path_);
    this->pedestrian_detector_ = new PedestrianDetect(this->sdcard_model_path_);
  } else {
    ESP_LOGE(TAG, "SD card mode enabled but no model path configured");
    this->mark_failed();
    return;
  }
#else
  ESP_LOGI(TAG, "Loading pedestrian detection model from flash rodata");
  this->pedestrian_detector_ = new PedestrianDetect();
#endif

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
#endif
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
#ifdef ESP_DL_MODEL_PEDESTRIAN
  if (this->pedestrian_detector_ == nullptr) {
    return;
  }

  // Create image structure for ESP-DL
  dl::image::img_t img = {
    .data = img_data,
    .width = width,
    .height = height,
    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
  };

  // Run pedestrian detection
  std::list<dl::detect::result_t> &ped_results = this->pedestrian_detector_->run(img);

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
#endif
}

void PedestrianDetectionComponent::draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (this->draw_enabled_ && img_data != nullptr) {
    this->draw_results_(img_data, width, height);
  }
}

void PedestrianDetectionComponent::draw_results_(uint8_t *img_data, uint16_t width, uint16_t height) {
#ifdef ESP_DL_MODEL_PEDESTRIAN
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
      // Clamp bounding box coordinates to valid range
      int x1 = std::max(2, std::min((int)box.x1, (int)width - 3));
      int y1 = std::max(2, std::min((int)box.y1, (int)height - 3));
      int x2 = std::max(x1 + 10, std::min((int)box.x2, (int)width - 3));
      int y2 = std::max(y1 + 10, std::min((int)box.y2, (int)height - 3));

      // Draw blue bounding box with 2-pixel thickness
      dl::image::draw_hollow_rectangle(
        img,
        x1, y1,
        x2, y2,
        blue, 2
      );
    }

    xSemaphoreGive(this->pedestrian_results_mutex_);
  }
#endif
}

void PedestrianDetectionComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Pedestrian Detection:");
#ifdef CONFIG_PEDESTRIAN_DETECT_MODEL_IN_SDCARD
  ESP_LOGCONFIG(TAG, "  Model location: SD card");
  if (this->sdcard_model_path_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Model path: %s", this->sdcard_model_path_);
  }
#else
  ESP_LOGCONFIG(TAG, "  Model location: Flash rodata");
#endif
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
