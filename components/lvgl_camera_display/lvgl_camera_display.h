#pragma once

#include "esphome/core/component.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"
#include <vector>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Forward declarations pour les composants ESP-IDF
class HumanFaceDetect;
class PedestrianDetect;
class HumanFaceRecognizer;

// Simple bounding box structure for caching detection results
struct DetectionBox {
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

namespace esphome {
namespace lvgl_camera_display {

class LVGLCameraDisplay : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_camera(mipi_dsi_cam::MipiDSICamComponent *camera) { this->camera_ = camera; }
  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }
  void set_update_interval(uint32_t interval_ms) { this->update_interval_ = interval_ms; }
  void set_enabled(bool enabled) { this->enabled_ = enabled; }
  void set_face_detection_enabled(bool enabled) { this->face_detection_enabled_ = enabled; }
  void set_pedestrian_detection_enabled(bool enabled) { this->pedestrian_detection_enabled_ = enabled; }
  void set_face_recognition_enabled(bool enabled) { this->face_recognition_enabled_ = enabled; }
  void set_face_db_path(const std::string &path) { this->face_db_path_ = path; }
  void set_recognition_threshold(float threshold) { this->recognition_threshold_ = threshold; }

  void configure_canvas(lv_obj_t *canvas);

  // Face recognition API
  int enroll_face();  // Enroll current detected face, returns ID or -1
  bool delete_face(int id);  // Delete enrolled face by ID
  void clear_all_faces();  // Clear all enrolled faces
  int get_enrolled_count();  // Get number of enrolled faces
  RecognitionResult get_last_recognition();  // Get last recognition result

  // Callback for face recognized event
  void set_on_face_recognized(std::function<void(int, float)> callback) {
    this->on_face_recognized_ = callback;
  }

  float get_setup_priority() const override { return setup_priority::LATE; }

  // Static callback for LVGL timer
  static void lvgl_timer_callback_(lv_timer_t *timer);

 protected:
  mipi_dsi_cam::MipiDSICamComponent *camera_{nullptr};
  lv_obj_t *canvas_obj_{nullptr};
  std::string canvas_id_{};

  uint32_t update_interval_{33};
  uint32_t last_update_{0};

  uint32_t frame_count_{0};
  bool first_update_{true};
  bool canvas_warning_shown_{false};
  bool enabled_{false};  // LVGL camera display enabled/disabled by switch
  bool face_detection_enabled_{false};  // Face detection enabled/disabled
  bool pedestrian_detection_enabled_{false};  // Pedestrian detection enabled/disabled
  bool face_recognition_enabled_{false};  // Face recognition enabled/disabled
  std::string face_db_path_{"/sdcard/faces.db"};  // Path to face database
  float recognition_threshold_{0.7f};  // Similarity threshold for recognition

  uint32_t last_fps_time_{0};

  lv_timer_t *lvgl_timer_{nullptr};

  // Buffer pool tracking (pour release après affichage)
  mipi_dsi_cam::SimpleBufferElement *displayed_buffer_{nullptr};

  // Detection models
  HumanFaceDetect *face_detector_{nullptr};
  PedestrianDetect *pedestrian_detector_{nullptr};
  HumanFaceRecognizer *face_recognizer_{nullptr};

  // Frame skipping for face detection (to improve performance)
  uint32_t face_detection_frame_skip_{0};

  // Cached detection results (to avoid flickering when skipping frames)
  std::vector<DetectionBox> cached_face_results_;

  // Mutex for thread-safe access to cached results
  SemaphoreHandle_t face_results_mutex_{nullptr};

  // Recognition state
  RecognitionResult last_recognition_{-1, 0.0f, false};
  bool enroll_pending_{false};  // Flag to trigger enrollment on next detection
  std::function<void(int, float)> on_face_recognized_{nullptr};

  void update_camera_frame_();
  void update_canvas_();
  void detect_and_draw_objects_(uint8_t* img_data, uint16_t width, uint16_t height);
};

}  // namespace lvgl_camera_display
}  // namespace esphome





