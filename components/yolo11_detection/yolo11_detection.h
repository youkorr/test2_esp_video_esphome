#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include <vector>
#include <list>

#ifdef USE_YOLO11_MIPI_CAMERA
#include "esphome/components/esp_cam_sensor/esp_cam_sensor_camera.h"
#endif

#ifdef USE_YOLO11_ESP32_CAMERA
namespace esp32_camera {
class ESP32Camera;
class CameraImage;
}  // namespace esp32_camera
#endif

// ESP-DL forward declarations
namespace dl {
class Model;
namespace image {
class ImagePreprocessor;
}
namespace detect {
class yolo11PostProcessor;
struct result_t;
}
}

namespace esphome {
namespace yolo11_detection {

struct DetectionBox {
  int x1, y1, x2, y2;
  float score;
  int category;
};

class YOLO11DetectionComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return -200.0f; }

  // Camera setters (mutually exclusive)
#ifdef USE_YOLO11_MIPI_CAMERA
  void set_camera(esp_cam_sensor::MipiDSICamComponent *camera) { this->mipi_camera_ = camera; }
#endif
#ifdef USE_YOLO11_ESP32_CAMERA
  void set_esp32_camera(esp32_camera::ESP32Camera *camera) { this->esp32_camera_ = camera; }
#endif

  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }
  void set_score_threshold(float threshold) { this->score_threshold_ = threshold; }
  void set_nms_threshold(float threshold) { this->nms_threshold_ = threshold; }
  void set_detection_interval(int interval) { this->detection_interval_ = interval; }
  void set_draw_enabled(bool enabled) { this->draw_enabled_ = enabled; }
  void set_sdcard_model_path(const char *path) { this->sdcard_model_path_ = path; }

  // Getters
  int get_detected_count();
  std::vector<DetectionBox> get_detections();

  // Drawing (called by lvgl_camera_display if configured)
  void draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height);

  // Callbacks
  void add_on_object_detected_callback(std::function<void(int)> callback) {
    this->on_object_detected_callbacks_.push_back(callback);
  }

 protected:
  void process_frame_();
  void detect_objects_(uint8_t *img_data, uint16_t width, uint16_t height);
  void draw_results_(uint8_t *img_data, uint16_t width, uint16_t height);
  void draw_char_(uint8_t *img_data, uint16_t img_width, uint16_t img_height,
                  int x, int y, char c, uint16_t color, int scale);
  void draw_text_(uint8_t *img_data, uint16_t img_width, uint16_t img_height,
                  int x, int y, const char *text, uint16_t color, int scale);

#ifdef USE_YOLO11_ESP32_CAMERA
  void on_esp32_camera_image_(std::shared_ptr<esp32_camera::CameraImage> image);
  esp32_camera::ESP32Camera *esp32_camera_{nullptr};
#endif

#ifdef USE_YOLO11_MIPI_CAMERA
  esp_cam_sensor::MipiDSICamComponent *mipi_camera_{nullptr};
#endif

  // ESP-DL objects (direct, no wrapper)
  dl::Model *dl_model_{nullptr};
  dl::image::ImagePreprocessor *preprocessor_{nullptr};
  dl::detect::yolo11PostProcessor *postprocessor_{nullptr};

  // Configuration
  std::string canvas_id_{};
  float score_threshold_{0.3};
  float nms_threshold_{0.5};
  int detection_interval_{8};
  bool draw_enabled_{true};
  const char *sdcard_model_path_{nullptr};

  // State
  uint32_t frame_counter_{0};
  bool detector_initialized_{false};
  std::vector<DetectionBox> cached_detections_;
  SemaphoreHandle_t detections_mutex_{nullptr};

  // Callbacks
  std::vector<std::function<void(int)>> on_object_detected_callbacks_;
};

// Automation trigger
class ObjectDetectedTrigger : public Trigger<int> {
 public:
  explicit ObjectDetectedTrigger(YOLO11DetectionComponent *parent) {
    parent->add_on_object_detected_callback([this](int count) { this->trigger(count); });
  }
};

}  // namespace yolo11_detection
}  // namespace esphome
