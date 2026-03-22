#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/file/file_component.h"

#ifdef USE_YOLOV11_MIPI_CAMERA
#include "esphome/components/esp_cam_sensor/esp_cam_sensor_camera.h"
#endif

#ifdef USE_YOLOV11_ESP32_CAMERA
namespace esp32_camera {
class ESP32Camera;
class CameraImage;
}  // namespace esp32_camera
#endif

// Forward declarations for ESP-DL
namespace dl {
class Model;
namespace image {
class ImagePreprocessor;
}
namespace detect {
class DetectPostprocessor;
struct result_t;
}  // namespace detect
}  // namespace dl

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

#ifdef USE_YOLOV11_ESP32_CAMERA
  void on_esp32_camera_image_(std::shared_ptr<esp32_camera::CameraImage> image);
  esp32_camera::ESP32Camera *esp32_camera_{nullptr};
#endif

#ifdef USE_YOLOV11_MIPI_CAMERA
  esp_cam_sensor::MipiDSICamComponent *mipi_camera_{nullptr};
#endif

  // Inference request flag (shared by both camera types)
  bool inference_requested_{false};

  // Model
  file_component::FileData *model_file_{nullptr};
  float score_threshold_{0.3f};
  float nms_threshold_{0.5f};
  std::vector<std::string> class_labels_;

  // ESP-DL objects
  dl::Model *dl_model_{nullptr};
  dl::image::ImagePreprocessor *preprocessor_{nullptr};
  dl::detect::DetectPostprocessor *postprocessor_{nullptr};
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
