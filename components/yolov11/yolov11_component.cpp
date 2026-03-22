#include "yolov11_component.h"
#include "esphome/core/log.h"

#ifdef ESP_DL_MODEL_YOLO11
#include "dl_model_base.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_detect_yolo11_postprocessor.hpp"
#include "dl_detect_define.hpp"
#include "dl_image_define.hpp"
#include "fbs_model.hpp"
#endif

#ifdef USE_YOLOV11_ESP32_CAMERA
#include "esphome/components/esp32_camera/esp32_camera.h"
#endif

namespace esphome {
namespace yolov11 {

static const char *const TAG = "yolov11";

void YOLOV11Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up YOLOV11...");

  this->detections_mutex_ = xSemaphoreCreateMutex();
  if (this->detections_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create detections mutex");
    this->mark_failed();
    return;
  }

  if (this->model_file_ == nullptr) {
    ESP_LOGE(TAG, "Model file not configured");
    this->mark_failed();
    return;
  }

  // Initialize the detector
  this->init_detector_();

#ifdef USE_YOLOV11_ESP32_CAMERA
  if (this->esp32_camera_ != nullptr) {
    // Register image callback with ESP32 camera
    this->esp32_camera_->add_image_callback(
        [this](std::shared_ptr<esp32_camera::CameraImage> image) {
          this->on_esp32_camera_image_(std::move(image));
        });
    ESP_LOGI(TAG, "Registered with ESP32 camera");
  }
#endif

  ESP_LOGI(TAG, "YOLOV11 ready (score_thr=%.2f, nms_thr=%.2f)",
           this->score_threshold_, this->nms_threshold_);
}

void YOLOV11Component::init_detector_() {
#ifndef ESP_DL_MODEL_YOLO11
  ESP_LOGE(TAG, "ESP_DL_MODEL_YOLO11 not defined - cannot initialize detector");
  this->mark_failed();
  return;
#else
  const uint8_t *model_data = this->model_file_->get_data();
  size_t model_size = this->model_file_->get_size();

  if (model_data == nullptr || model_size == 0) {
    ESP_LOGE(TAG, "Model data is empty");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Loading model (%u bytes)...", (unsigned)model_size);

  // Create dl::Model from raw data pointer
  // When location is MODEL_LOCATION_IN_FLASH_RODATA, the first parameter
  // is treated as a memory address pointing to the model data
  this->dl_model_ = new dl::Model(
      (const char *)model_data,
      fbs::MODEL_LOCATION_IN_FLASH_RODATA,
      0,                          // max_internal_size
      dl::MEMORY_MANAGER_GREEDY,
      nullptr,                    // key (not encrypted)
      true                        // param_copy to PSRAM
  );

  if (this->dl_model_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create dl::Model");
    this->mark_failed();
    return;
  }

  // Create image preprocessor (RGB565 input, no mean/std normalization)
  this->preprocessor_ = new dl::image::ImagePreprocessor(
      this->dl_model_, {0, 0, 0}, {1, 1, 1});

  // Create YOLO11 postprocessor
  // 3 detection stages with strides 8, 16, 32
  this->postprocessor_ = new dl::detect::yolo11PostProcessor(
      this->dl_model_,
      this->preprocessor_,
      this->score_threshold_,
      this->nms_threshold_,
      10,  // top_k
      {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});

  this->detector_initialized_ = true;
  ESP_LOGI(TAG, "YOLO11 detector initialized");
#endif
}

void YOLOV11Component::loop() {
#ifdef USE_YOLOV11_MIPI_CAMERA
  // For MIPI camera, we don't auto-process in loop
  // Inference is triggered by the action
  // But if no action setup, we could auto-process here
  if (this->mipi_camera_ != nullptr && this->inference_requested_) {
    this->run_inference();
  }
#endif
}

void YOLOV11Component::run_inference() {
  if (!this->detector_initialized_) {
    return;
  }

#ifdef USE_YOLOV11_MIPI_CAMERA
  if (this->mipi_camera_ != nullptr) {
    if (!this->mipi_camera_->is_streaming()) {
      return;
    }

    auto *buffer = this->mipi_camera_->acquire_buffer();
    if (buffer == nullptr) {
      return;
    }

    uint8_t *img_data = this->mipi_camera_->get_buffer_data(buffer);
    uint16_t width = this->mipi_camera_->get_image_width();
    uint16_t height = this->mipi_camera_->get_image_height();

    if (img_data != nullptr) {
      this->detect_objects_(img_data, width, height);
    }

    this->mipi_camera_->release_buffer(buffer);
  }
#endif
}

#ifdef USE_YOLOV11_ESP32_CAMERA
void YOLOV11Component::on_esp32_camera_image_(
    std::shared_ptr<esp32_camera::CameraImage> image) {
  if (!this->detector_initialized_ || !this->inference_requested_) {
    return;
  }

  this->inference_requested_ = false;

  uint8_t *data = image->get_data_buffer();
  size_t len = image->get_data_length();

  if (data == nullptr || len == 0) {
    return;
  }

  // ESP32 camera typically provides RGB565 when pixel_format is RGB565
  // For JPEG format, decoding would be needed (not supported in this version)
  // Assume RGB565 format for detection
  // The width/height must match the camera resolution
  // For 320x240 RGB565: len = 320 * 240 * 2 = 153600
  uint16_t width = 320;
  uint16_t height = 240;

  // Try to determine dimensions from data length (RGB565 = 2 bytes per pixel)
  size_t expected_rgb565 = (size_t)width * height * 2;
  if (len == expected_rgb565) {
    this->detect_objects_(data, width, height);
  } else {
    // Try common resolutions
    const uint16_t resolutions[][2] = {
        {320, 240}, {640, 480}, {160, 120}, {800, 600}, {1024, 768},
    };
    bool found = false;
    for (auto &res : resolutions) {
      if (len == (size_t)res[0] * res[1] * 2) {
        this->detect_objects_(data, res[0], res[1]);
        found = true;
        break;
      }
    }
    if (!found) {
      ESP_LOGW(TAG, "Unsupported image size: %u bytes (need RGB565 format)", (unsigned)len);
    }
  }
}
#endif

void YOLOV11Component::detect_objects_(uint8_t *rgb565_data, uint16_t width,
                                        uint16_t height) {
#ifdef ESP_DL_MODEL_YOLO11
  if (this->dl_model_ == nullptr || this->postprocessor_ == nullptr) {
    return;
  }

  // Create image structure for ESP-DL
  dl::image::img_t img;
  img.data = rgb565_data;
  img.width = width;
  img.height = height;
  img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565;

  // Run preprocessing (resize + normalize)
  this->preprocessor_->preprocess(img);

  // Run model inference
  this->dl_model_->run();

  // Run postprocessing (decode boxes + NMS)
  this->postprocessor_->clear_result();
  this->postprocessor_->postprocess();
  auto &results = this->postprocessor_->get_result(width, height);

  // Cache results
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    this->cached_detections_.clear();

    for (auto &result : results) {
      DetectionResult det;
      det.category = result.category;
      det.score = result.score;
      det.x1 = result.box[0];
      det.y1 = result.box[1];
      det.x2 = result.box[2];
      det.y2 = result.box[3];
      this->cached_detections_.push_back(det);
    }

    xSemaphoreGive(this->detections_mutex_);
  }

  // Build detection string and notify callbacks
  std::string detection_str = this->get_detection_string();

  for (auto &callback : this->detection_callbacks_) {
    callback(detection_str);
  }

  if (!results.empty()) {
    ESP_LOGD(TAG, "Detected %d object(s): %s", (int)results.size(),
             detection_str.c_str());
  }
#endif
}

void YOLOV11Component::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLOV11:");
#ifdef USE_YOLOV11_ESP32_CAMERA
  ESP_LOGCONFIG(TAG, "  Camera: ESP32 Camera");
#endif
#ifdef USE_YOLOV11_MIPI_CAMERA
  ESP_LOGCONFIG(TAG, "  Camera: MIPI DSI Camera");
#endif
  ESP_LOGCONFIG(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGCONFIG(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  if (this->model_file_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Model size: %u bytes",
                  (unsigned)this->model_file_->get_size());
  }
  ESP_LOGCONFIG(TAG, "  Classes: %d", (int)this->class_labels_.size());
  for (size_t i = 0; i < this->class_labels_.size(); i++) {
    ESP_LOGCONFIG(TAG, "    [%d] %s", (int)i, this->class_labels_[i].c_str());
  }
}

int YOLOV11Component::get_detected_count() {
  int count = 0;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    count = this->cached_detections_.size();
    xSemaphoreGive(this->detections_mutex_);
  }
  return count;
}

std::vector<DetectionResult> YOLOV11Component::get_detections() {
  std::vector<DetectionResult> detections;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    detections = this->cached_detections_;
    xSemaphoreGive(this->detections_mutex_);
  }
  return detections;
}

std::string YOLOV11Component::get_detection_string() {
  std::string result;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    for (size_t i = 0; i < this->cached_detections_.size(); i++) {
      auto &det = this->cached_detections_[i];
      if (i > 0)
        result += ",";
      char buf[64];
      snprintf(buf, sizeof(buf), "%s:%.0f%%",
               this->get_class_name(det.category), det.score * 100.0f);
      result += buf;
    }
    xSemaphoreGive(this->detections_mutex_);
  }
  return result;
}

}  // namespace yolov11
}  // namespace esphome
