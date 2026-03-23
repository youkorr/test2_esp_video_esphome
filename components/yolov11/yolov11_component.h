#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/file/file_component.h"
#include <set>

#ifdef USE_YOLOV11_MIPI_CAMERA
#include "esphome/components/esp_cam_sensor/esp_cam_sensor_camera.h"
#endif

#ifdef USE_YOLOV11_ESP32_CAMERA
namespace esp32_camera {
class ESP32Camera;
class CameraImage;
}  // namespace esp32_camera
#endif

#ifdef ESP_DL_MODEL_YOLO11
#include "dl_model_base.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_detect_yolo11_postprocessor.hpp"
#include "dl_detect_define.hpp"
#include "dl_image_define.hpp"
#include "fbs_model.hpp"
#endif

namespace esphome {
namespace yolov11 {

struct DetectionResult {
  int category;
  float score;
  int x1, y1, x2, y2;
};

class YOLOV11Component : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return -200.0f; }

  // Camera setters
#ifdef USE_YOLOV11_ESP32_CAMERA
  void set_esp32_camera(esp32_camera::ESP32Camera *camera) {
    this->esp32_camera_ = camera;
  }
#endif
#ifdef USE_YOLOV11_MIPI_CAMERA
  void set_mipi_camera(esp_cam_sensor::MipiDSICamComponent *camera) {
    this->mipi_camera_ = camera;
  }
#endif

  // Model
  void set_model(file_component::FileData *model) { this->model_file_ = model; }
  void set_score_threshold(float thr) { this->score_threshold_ = thr; }
  void set_nms_threshold(float thr) { this->nms_threshold_ = thr; }
  void add_class_label(const std::string &label) {
    this->class_labels_.push_back(label);
  }
  void add_detect_class(int class_id) {
    this->detect_classes_.insert(class_id);
  }
  void set_detection_interval(int interval) { this->detection_interval_ = interval; }
  void set_draw_enabled(bool enabled) { this->draw_enabled_ = enabled; }

  const char *get_class_name(int category) const {
    if (category >= 0 && category < (int)this->class_labels_.size())
      return this->class_labels_[category].c_str();
    return "unknown";
  }

  // Run inference
  void run_inference();

  // Request inference on next frame
  void request_inference() { this->inference_requested_ = true; }

  // Results access
  int get_detected_count();
  std::vector<DetectionResult> get_detections();
  std::string get_detection_class_string();
  std::string get_detection_bb_string();

  // Drawing (called by lvgl_camera_display if configured)
  void draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height);

  // Callbacks for text sensor updates
  void add_on_detection_class_callback(std::function<void(std::string)> callback) {
    this->detection_class_callbacks_.push_back(std::move(callback));
  }
  void add_on_detection_bb_callback(std::function<void(std::string)> callback) {
    this->detection_bb_callbacks_.push_back(std::move(callback));
  }

 protected:
  void init_detector_();
  void detect_objects_(uint8_t *rgb565_data, uint16_t width, uint16_t height);
  void draw_results_(uint8_t *img_data, uint16_t width, uint16_t height);
  bool is_class_allowed_(int category) const;

#ifdef USE_YOLOV11_ESP32_CAMERA
  void on_esp32_camera_image_(std::shared_ptr<esp32_camera::CameraImage> image);
  esp32_camera::ESP32Camera *esp32_camera_{nullptr};
#endif

#ifdef USE_YOLOV11_MIPI_CAMERA
  esp_cam_sensor::MipiDSICamComponent *mipi_camera_{nullptr};
#endif

  // Auto-detection state
  uint32_t frame_counter_{0};
  bool inference_requested_{false};
  bool first_frame_ready_{false};

  // Model
  file_component::FileData *model_file_{nullptr};
  float score_threshold_{0.3f};
  float nms_threshold_{0.5f};
  std::vector<std::string> class_labels_;
  std::set<int> detect_classes_;  // Empty = all classes, non-empty = filter
  int detection_interval_{1};
  bool draw_enabled_{true};

  // ESP-DL objects
#ifdef ESP_DL_MODEL_YOLO11
  dl::Model *dl_model_{nullptr};
  dl::image::ImagePreprocessor *preprocessor_{nullptr};
  dl::detect::yolo11PostProcessor *postprocessor_{nullptr};
#endif
  bool detector_initialized_{false};

  // Results
  std::vector<DetectionResult> cached_detections_;
  SemaphoreHandle_t detections_mutex_{nullptr};

  // Callbacks
  std::vector<std::function<void(std::string)>> detection_class_callbacks_;
  std::vector<std::function<void(std::string)>> detection_bb_callbacks_;
};

}  // namespace yolov11
}  // namespace esphome
