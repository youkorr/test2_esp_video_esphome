#pragma once

#include "esphome/core/component.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/esp_cam_sensor/esp_cam_sensor_camera.h"

// Forward declarations
namespace esphome {
namespace face_detection {
class FaceDetectionComponent;
}
namespace yolo11_detection {
class YOLO11DetectionComponent;
}
namespace pedestrian_detection {
class PedestrianDetectionComponent;
}
}

namespace esphome {
namespace lvgl_camera_display {

class LVGLCameraDisplay : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_camera(esp_cam_sensor::MipiDSICamComponent *camera) { this->camera_ = camera; }
  void set_canvas_id(const std::string &canvas_id) { this->canvas_id_ = canvas_id; }
  void set_update_interval(uint32_t interval_ms) { this->update_interval_ = interval_ms; }
  void set_enabled(bool enabled) { this->enabled_ = enabled; }
#ifdef USE_FACE_DETECTION
  void set_face_detection(face_detection::FaceDetectionComponent *face_detect) { this->face_detection_ = face_detect; }
#endif
#ifdef USE_YOLO11_DETECTION
  void set_yolo11_detection(yolo11_detection::YOLO11DetectionComponent *yolo11_detect) { this->yolo11_detection_ = yolo11_detect; }
#endif
#ifdef USE_PEDESTRIAN_DETECTION
  void set_pedestrian_detection(pedestrian_detection::PedestrianDetectionComponent *ped_detect) { this->pedestrian_detection_ = ped_detect; }
#endif

  void configure_canvas(lv_obj_t *canvas);

  float get_setup_priority() const override { return setup_priority::LATE; }

  // Static callback for LVGL timer
  static void lvgl_timer_callback_(lv_timer_t *timer);

 protected:
  esp_cam_sensor::MipiDSICamComponent *camera_{nullptr};
#ifdef USE_FACE_DETECTION
  face_detection::FaceDetectionComponent *face_detection_{nullptr};  // Optional
#endif
#ifdef USE_YOLO11_DETECTION
  yolo11_detection::YOLO11DetectionComponent *yolo11_detection_{nullptr};  // Optional
#endif
#ifdef USE_PEDESTRIAN_DETECTION
  pedestrian_detection::PedestrianDetectionComponent *pedestrian_detection_{nullptr};  // Optional
#endif
  lv_obj_t *canvas_obj_{nullptr};
  std::string canvas_id_{};

  uint32_t update_interval_{33};
  uint32_t last_update_{0};

  uint32_t frame_count_{0};
  bool first_update_{true};
  bool canvas_warning_shown_{false};
  bool enabled_{false};  // LVGL camera display enabled/disabled by switch

  uint32_t last_fps_time_{0};

  lv_timer_t *lvgl_timer_{nullptr};

  // Buffer pool tracking (pour release apres affichage)
  esp_cam_sensor::SimpleBufferElement *displayed_buffer_{nullptr};

  // Frame callback system (V4L2 Snippet #3 - called from streaming task)
  volatile bool frame_ready_{false};  // Atomic flag set by callback, cleared by loop()
  void on_frame_callback_(uint8_t* buffer, uint32_t size, uint32_t index);

  void update_camera_frame_();
  void update_canvas_();
};

}  // namespace lvgl_camera_display
}  // namespace esphome
