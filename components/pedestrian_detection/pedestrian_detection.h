#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/esp_cam_sensor/esp_cam_sensor_camera.h"
#include <vector>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Forward declaration for ESP-DL component
class PedestrianDetect;

namespace esphome {
namespace pedestrian_detection {

// Bounding box structure for detection results
struct PedestrianBox {
  int x1, y1, x2, y2;
  float score;
};

class PedestrianDetectionComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  // Configuration setters
  void set_camera(esp_cam_sensor::MipiDSICamComponent *camera) { this->camera_ = camera; }
  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }
  void set_score_threshold(float threshold) { this->score_threshold_ = threshold; }
  void set_nms_threshold(float threshold) { this->nms_threshold_ = threshold; }
  void set_detection_interval(int interval) { this->detection_interval_ = interval; }
  void set_draw_enabled(bool enabled) { this->draw_enabled_ = enabled; }

  // Detection API
  int get_detected_pedestrian_count();
  std::vector<PedestrianBox> get_detected_pedestrians();

  // External drawing - allows camera display to call drawing on its buffer
  void draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height);

  // Callbacks
  void add_on_pedestrian_detected_callback(std::function<void(int)> callback) {
    this->on_pedestrian_detected_callbacks_.push_back(std::move(callback));
  }

  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  esp_cam_sensor::MipiDSICamComponent *camera_{nullptr};
  std::string canvas_id_{};
  bool draw_enabled_{true};  // Draw bounding boxes on image buffer

  // Detection configuration
  float score_threshold_{0.5f};
  float nms_threshold_{0.5f};
  int detection_interval_{4};  // Run detection every N frames

  // Detection model
  PedestrianDetect *pedestrian_detector_{nullptr};

  // Frame counter for detection interval
  uint32_t frame_counter_{0};

  // Cached detection results
  std::vector<PedestrianBox> cached_pedestrian_results_;
  SemaphoreHandle_t pedestrian_results_mutex_{nullptr};

  // Callbacks
  std::vector<std::function<void(int)>> on_pedestrian_detected_callbacks_;

  // Internal methods
  void process_frame_();
  void detect_pedestrians_(uint8_t *img_data, uint16_t width, uint16_t height);
  void draw_results_(uint8_t *img_data, uint16_t width, uint16_t height);
};

// Automation trigger
class PedestrianDetectedTrigger : public Trigger<int> {
 public:
  explicit PedestrianDetectedTrigger(PedestrianDetectionComponent *parent) {
    parent->add_on_pedestrian_detected_callback([this](int pedestrian_count) {
      this->trigger(pedestrian_count);
    });
  }
};

}  // namespace pedestrian_detection
}  // namespace esphome
