#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/esp_cam_sensor/esp_cam_sensor_camera.h"
#include <vector>
#include <functional>
#include <map>
#include <string>
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
  void set_camera(esp_cam_sensor::MipiDSICamComponent *camera) { this->camera_ = camera; }
  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }
  void set_score_threshold(float threshold) { this->score_threshold_ = threshold; }
  void set_nms_threshold(float threshold) { this->nms_threshold_ = threshold; }
  void set_detection_interval(int interval) { this->detection_interval_ = interval; }
  void set_recognition_enabled(bool enabled) { this->recognition_enabled_ = enabled; }
  void set_face_db_path(const std::string &path) { this->face_db_path_ = path; }
  void set_recognition_threshold(float threshold) { this->recognition_threshold_ = threshold; }
  void set_draw_enabled(bool enabled) { this->draw_enabled_ = enabled; }
  void set_sdcard_model_path(const char *path) { this->sdcard_model_path_ = path; }

  // Get canvas ID for LVGL integration
  const std::string &get_canvas_id() { return this->canvas_id_; }

  // Detection API
  int get_detected_face_count();
  std::vector<FaceBox> get_detected_faces();

  // Face recognition API
  int enroll_face();
  int enroll_face_with_name(const std::string &name);
  bool delete_face(int id);
  void clear_all_faces();
  int get_enrolled_count();
  RecognitionResult get_last_recognition();
  void reset_last_recognition();

  // Name management API
  void set_face_name(int id, const std::string &name);
  std::string get_face_name(int id);
  std::string get_last_recognized_name();

  // External drawing - allows camera display to call drawing on its buffer
  void draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height);

  // Callbacks
  void add_on_face_detected_callback(std::function<void(int)> callback) {
    this->on_face_detected_callbacks_.push_back(std::move(callback));
  }
  void add_on_face_recognized_callback(std::function<void(int, float)> callback) {
    this->on_face_recognized_callbacks_.push_back(std::move(callback));
  }

  float get_setup_priority() const override { return -200.0f; }  // Setup after SD card (very low priority)

 protected:
  esp_cam_sensor::MipiDSICamComponent *camera_{nullptr};
  std::string canvas_id_{};  // Canvas ID for LVGL integration
  bool draw_enabled_{true};  // Draw bounding boxes on image buffer

  // Detection configuration
  float score_threshold_{0.3f};
  float nms_threshold_{0.5f};
  int detection_interval_{8};  // Run detection every N frames
  const char *sdcard_model_path_{nullptr};

  // Recognition configuration
  bool recognition_enabled_{false};
  std::string face_db_path_{"/sdcard/faces.db"};
  float recognition_threshold_{0.9f};

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
  std::string pending_enroll_name_{};
  std::string cached_recognized_name_{};  // Cached name to avoid map lookup + allocation per frame
  int cached_recognized_id_{-1};  // ID for which cached_recognized_name_ is valid

  // Post-enrollment cooldown: skip N detection cycles after enroll to let
  // ESP-DL internal state settle (prevents crash from unstable recognizer state)
  uint8_t post_enroll_cooldown_{0};

  // Memory diagnostics
  uint32_t diag_counter_{0};

  // Face name mapping (ID -> name)
  std::map<int, std::string> face_names_;

  // Callbacks
  std::vector<std::function<void(int)>> on_face_detected_callbacks_;
  std::vector<std::function<void(int, float)>> on_face_recognized_callbacks_;

  // Internal methods
  void process_frame_();
  void detect_faces_(uint8_t *img_data, uint16_t width, uint16_t height);
  void draw_results_(uint8_t *img_data, uint16_t width, uint16_t height);
  void draw_char_(uint8_t *img_data, uint16_t img_width, uint16_t img_height,
                  int x, int y, char c, const uint8_t *color, int scale);
  void draw_text_(uint8_t *img_data, uint16_t img_width, uint16_t img_height,
                  int x, int y, const std::string &text, const uint8_t *color, int scale);

  // SD card persistence for names
  std::string get_names_file_path_();
  void load_names_from_sd_();
  void save_names_to_sd_();
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

// Actions
template<typename... Ts>
class EnrollFaceAction : public Action<Ts...>, public Parented<FaceDetectionComponent> {
 public:
  void play(Ts... x) override {
    this->parent_->enroll_face();
  }
};

template<typename... Ts>
class EnrollFaceWithNameAction : public Action<Ts...>, public Parented<FaceDetectionComponent> {
 public:
  TEMPLATABLE_VALUE(std::string, name)

  void play(Ts... x) override {
    this->parent_->enroll_face_with_name(this->name_.value(x...));
  }
};

template<typename... Ts>
class SetFaceNameAction : public Action<Ts...>, public Parented<FaceDetectionComponent> {
 public:
  TEMPLATABLE_VALUE(int, face_id)
  TEMPLATABLE_VALUE(std::string, name)

  void play(Ts... x) override {
    this->parent_->set_face_name(this->face_id_.value(x...), this->name_.value(x...));
  }
};

template<typename... Ts>
class DeleteFaceAction : public Action<Ts...>, public Parented<FaceDetectionComponent> {
 public:
  TEMPLATABLE_VALUE(int, face_id)

  void play(Ts... x) override {
    this->parent_->delete_face(this->face_id_.value(x...));
  }
};

template<typename... Ts>
class ClearAllFacesAction : public Action<Ts...>, public Parented<FaceDetectionComponent> {
 public:
  void play(Ts... x) override {
    this->parent_->clear_all_faces();
  }
};

}  // namespace face_detection
}  // namespace esphome
