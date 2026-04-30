#include "yolo11_detection.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

// ESP-DL detection components (only for YOLO11 model)
#ifdef ESP_DL_MODEL_YOLO11
#include "yolo11_detect.hpp"
#include "dl_image.hpp"
#endif

namespace esphome {
namespace yolo11_detection {

static const char *const TAG = "yolo11_detection";

#ifdef ESP_DL_MODEL_YOLO11
// Initialize the underlying YOLO11Detect detector. In SD-card mode this is
// deferred to the first loop() iteration so we don't block setup() (which
// would trip the task watchdog and reboot the board).
static bool initialize_detector(YOLO11DetectionComponent *self,
                                YOLO11Detect *&detector_out,
                                const char *sdcard_model_path,
                                float score_thr,
                                float nms_thr) {
#ifdef CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD
  if (sdcard_model_path == nullptr) {
    ESP_LOGE(TAG, "SD card mode enabled but no model path configured");
    return false;
  }
  ESP_LOGI(TAG, "Loading YOLO11 model from SD card directory: %s", sdcard_model_path);
  detector_out = new YOLO11Detect(sdcard_model_path);
#else
  ESP_LOGI(TAG, "Loading YOLO11 model from flash rodata");
  detector_out = new YOLO11Detect();
#endif

  if (detector_out == nullptr) {
    ESP_LOGE(TAG, "Failed to construct YOLO11Detect");
    return false;
  }
  detector_out->set_score_thr(score_thr);
  detector_out->set_nms_thr(nms_thr);
  ESP_LOGI(TAG, "YOLO11 detector initialized (score_thr=%.2f, nms_thr=%.2f)",
           score_thr, nms_thr);
  return true;
}
#endif  // ESP_DL_MODEL_YOLO11

void YOLO11DetectionComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up YOLO11 Object Detection...");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not configured");
    this->mark_failed();
    return;
  }

  this->detections_mutex_ = xSemaphoreCreateMutex();
  if (this->detections_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create detections mutex");
    this->mark_failed();
    return;
  }

#ifndef ESP_DL_MODEL_YOLO11
  ESP_LOGE(TAG, "YOLO11 Detection component requires ESP_DL_MODEL_YOLO11 flag");
  this->mark_failed();
  return;
#else
  // ------------------------------------------------------------------
  // Flash-rodata mode: load now, model is part of the firmware so it's
  // basically free. SD-card mode: defer to loop() to avoid blocking
  // setup() and tripping the task watchdog.
  // ------------------------------------------------------------------
#ifdef CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD
  ESP_LOGI(TAG, "SD card mode: model load deferred to first loop() iteration");
  ESP_LOGI(TAG, "  expected file: %s/yolo11_detect_s8_v1.espdl",
           this->sdcard_model_path_ != nullptr ? this->sdcard_model_path_ : "(unset)");
#else
  if (!initialize_detector(this, this->object_detector_,
                           this->sdcard_model_path_,
                           this->score_threshold_, this->nms_threshold_)) {
    this->mark_failed();
    return;
  }
#endif

  ESP_LOGI(TAG, "YOLO11 Object Detection ready");
  ESP_LOGI(TAG, "  Detection interval: every %d frames", this->detection_interval_);
  ESP_LOGI(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGI(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  ESP_LOGI(TAG, "  Draw boxes: %s", this->draw_enabled_ ? "YES" : "NO");
#endif
}

void YOLO11DetectionComponent::loop() {
#ifdef ESP_DL_MODEL_YOLO11
#ifdef CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD
  // Lazy init for SD-card mode. We do it once, on the first loop() call,
  // so setup() never blocks and the task watchdog stays happy. The model
  // file load itself can take several seconds; we feed the WDT around it
  // and handle errors by marking the component failed.
  static bool init_attempted = false;
  if (!init_attempted) {
    init_attempted = true;
    ESP_LOGI(TAG, "Lazy-loading YOLO11 model from SD card...");
    esp_task_wdt_reset();
    if (!initialize_detector(this, this->object_detector_,
                             this->sdcard_model_path_,
                             this->score_threshold_, this->nms_threshold_)) {
      this->mark_failed();
      return;
    }
    esp_task_wdt_reset();
  }
  if (this->object_detector_ == nullptr) {
    return;  // load is in progress (or failed)
  }
#endif
#endif

  // Check if camera is streaming
  if (this->camera_ == nullptr || !this->camera_->is_streaming()) {
    return;
  }

  this->process_frame_();
}

void YOLO11DetectionComponent::process_frame_() {
  this->frame_counter_++;

  if (this->frame_counter_ < (uint32_t) this->detection_interval_) {
    return;
  }

  this->frame_counter_ = 0;

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

  this->camera_->release_buffer(buffer);
}

void YOLO11DetectionComponent::detect_objects_(uint8_t *img_data, uint16_t width, uint16_t height) {
#ifdef ESP_DL_MODEL_YOLO11
  if (this->object_detector_ == nullptr) {
    return;
  }

  dl::image::img_t img = {
    .data = img_data,
    .width = width,
    .height = height,
    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888
  };

  std::list<dl::detect::result_t> &detection_results = this->object_detector_->run(img);

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
    const uint16_t COLOR_RED    = 0xF800;
    const uint16_t COLOR_GREEN  = 0x07E0;
    const uint16_t COLOR_BLUE   = 0x001F;
    const uint16_t COLOR_YELLOW = 0xFFE0;

    for (auto &box : this->cached_detections_) {
      int x1 = std::max(2, std::min((int)box.x1, (int)width - 3));
      int y1 = std::max(2, std::min((int)box.y1, (int)height - 3));
      int x2 = std::max(x1 + 10, std::min((int)box.x2, (int)width - 3));
      int y2 = std::max(y1 + 10, std::min((int)box.y2, (int)height - 3));

      uint16_t color;
      switch (box.category) {
        case 0:  color = COLOR_RED; break;
        case 2:  color = COLOR_GREEN; break;
        case 16: color = COLOR_BLUE; break;
        default: color = COLOR_YELLOW; break;
      }

      const int line_width = 2;
      uint16_t *buffer = (uint16_t *)img_data;

      for (int x = x1; x <= x2; x++) {
        for (int t = 0; t < line_width; t++) {
          int top_offset = (y1 + t) * width + x;
          if (top_offset >= 0 && top_offset < width * height) {
            buffer[top_offset] = color;
          }
          int bottom_offset = (y2 - t) * width + x;
          if (bottom_offset >= 0 && bottom_offset < width * height) {
            buffer[bottom_offset] = color;
          }
        }
      }

      for (int y = y1; y <= y2; y++) {
        for (int t = 0; t < line_width; t++) {
          int left_offset = y * width + (x1 + t);
          if (left_offset >= 0 && left_offset < width * height) {
            buffer[left_offset] = color;
          }
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
#ifdef CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD
  ESP_LOGCONFIG(TAG, "  Model location: SD card");
  if (this->sdcard_model_path_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Model path (directory): %s", this->sdcard_model_path_);
    ESP_LOGCONFIG(TAG, "  Expected file: %s/coco_detect_yolo11n_s8_v1.espdl",
                  this->sdcard_model_path_);
  }
#else
  ESP_LOGCONFIG(TAG, "  Model location: Flash rodata");
#endif
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
