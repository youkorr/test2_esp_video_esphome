#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"
#include <vector>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Forward declarations for ESP-DL components
class HumanFaceDetect;
class HumanFaceRecognizer;

namespace esphome {
namespace face_detection {

// Bounding box structure for detection results
struct FaceBox {
  int x1, y1, x2, y2;
  float score;
  int keypoints[10];  // 5 landmarks x 2 (x,y)
};

// Recognition result structure
struct RecognitionResult {
  int id;
  float similarity;
  bool recognized;
};

class FaceDetectionComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  // Configuration setters
  void set_camera(mipi_dsi_cam::MipiDSICamComponent *camera) { this->camera_ = camera; }
  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }
  void set_score_threshold(float threshold) { this->score_threshold_ = threshold; }
  void set_nms_threshold(float threshold) { this->nms_threshold_ = threshold; }
  void set_detection_interval(int interval) { this->detection_interval_ = interval; }
  void set_recognition_enabled(bool enabled) { this->recognition_enabled_ = enabled; }
  void set_face_db_path(const std::string &path) { this->face_db_path_ = path; }
  void set_recognition_threshold(float threshold) { this->recognition_threshold_ = threshold; }

  // Canvas configuration (called by LVGL component)
  void configure_canvas(lv_obj_t *canvas);

  // Detection API
  int get_detected_face_count();
  std::vector<FaceBox> get_detected_faces();

  // Face recognition API
  int enroll_face();
  bool delete_face(int id);
  void clear_all_faces();
  int get_enrolled_count();
  RecognitionResult get_last_recognition();
  void reset_last_recognition();

  // Callbacks
  void add_on_face_detected_callback(std::function<void(int)> callback) {
    this->on_face_detected_callbacks_.push_back(std::move(callback));
  }
  void add_on_face_recognized_callback(std::function<void(int, float)> callback) {
    this->on_face_recognized_callbacks_.push_back(std::move(callback));
  }

  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  mipi_dsi_cam::MipiDSICamComponent *camera_{nullptr};
  lv_obj_t *canvas_obj_{nullptr};
  std::string canvas_id_{};

  // Detection configuration
  float score_threshold_{0.3f};
  float nms_threshold_{0.5f};
  int detection_interval_{8};  // Run detection every N frames

  // Recognition configuration
  bool recognition_enabled_{false};
  std::string face_db_path_{"/sdcard/faces.db"};
  float recognition_threshold_{0.7f};

  // Detection models
  HumanFaceDetect *face_detector_{nullptr};
  HumanFaceRecognizer *face_recognizer_{nullptr};

  // Frame counter for detection interval
  uint32_t frame_counter_{0};

  // Cached detection results
  std::vector<FaceBox> cached_face_results_;
  SemaphoreHandle_t face_results_mutex_{nullptr};

  // Recognition state
  RecognitionResult last_recognition_{-1, 0.0f, false};
  bool enroll_pending_{false};

  // Callbacks
  std::vector<std::function<void(int)>> on_face_detected_callbacks_;
  std::vector<std::function<void(int, float)>> on_face_recognized_callbacks_;

  // Internal methods
  void process_frame_();
  void detect_faces_(uint8_t *img_data, uint16_t width, uint16_t height);
  void draw_results_(uint8_t *img_data, uint16_t width, uint16_t height);
};

// Automation triggers
class FaceDetectedTrigger : public Trigger<int> {
 public:
  explicit FaceDetectedTrigger(FaceDetectionComponent *parent) {
    parent->add_on_face_detected_callback([this](int face_count) {
      this->trigger(face_count);
    });
  }
};

class FaceRecognizedTrigger : public Trigger<int, float> {
 public:
  explicit FaceRecognizedTrigger(FaceDetectionComponent *parent) {
    parent->add_on_face_recognized_callback([this](int face_id, float similarity) {
      this->trigger(face_id, similarity);
    });
  }
};

}  // namespace face_detection
}  // namespace esphome
