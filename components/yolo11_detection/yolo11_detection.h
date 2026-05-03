#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/esp_cam_sensor/esp_cam_sensor_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <vector>
#include <list>

// ESP-DL forward declaration
class YOLO11Detect;

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
  void loop();
  void dump_config() override;
  float get_setup_priority() const override { return -200.0f; }

  // Configuration setters
  void set_camera(esp_cam_sensor::MipiDSICamComponent *camera) { this->camera_ = camera; }
  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }
  void set_score_threshold(float threshold) { this->score_threshold_ = threshold; }
  void set_nms_threshold(float threshold) { this->nms_threshold_ = threshold; }
  void set_detection_interval(int interval) { this->detection_interval_ = interval; }
  void set_draw_enabled(bool enabled) { this->draw_enabled_ = enabled; }
  void set_sdcard_model_path(const char *path) { this->sdcard_model_path_ = path; }

  // Getters
  int get_detected_count();
  std::vector<DetectionBox> get_detections();

  // Drawing
  void draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height);

  // Callbacks
  void add_on_object_detected_callback(std::function<void(int)> callback) {
    this->on_object_detected_callbacks_.push_back(callback);
  }

 protected:
  void process_frame_();
  void draw_results_(uint8_t *img_data, uint16_t width, uint16_t height);
  void draw_text(uint16_t *buffer, uint16_t width, uint16_t height, int x, int y, const char *text, uint16_t color, int scale);

  // Components
  esp_cam_sensor::MipiDSICamComponent *camera_{nullptr};
  YOLO11Detect *object_detector_{nullptr};

  // Configuration
  std::string canvas_id_{};
  float score_threshold_{0.3};
  float nms_threshold_{0.5};
  int detection_interval_{8};
  bool draw_enabled_{true};
  const char *sdcard_model_path_{nullptr};

  // State
  uint32_t frame_counter_{0};
  std::vector<DetectionBox> cached_detections_;
  SemaphoreHandle_t detections_mutex_{nullptr};
  
  bool is_model_loaded_{false};
  bool init_attempted_{false};

  // Callbacks
  std::vector<std::function<void(int)>> on_object_detected_callbacks_;
};

class ObjectDetectedTrigger : public Trigger<int> {
 public:
  explicit ObjectDetectedTrigger(YOLO11DetectionComponent *parent) {
    parent->add_on_object_detected_callback([this](int count) { this->trigger(count); });
  }
};

}  // namespace yolo11_detection
}  // namespace esphome
