#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"
#include "esphome/components/esp32_camera/esp32_camera.h"

// Include the actual ESP-DL type definitions so that img_t/result_t are
// complete types in every translation unit that includes this header.
// Forward-declaring them as "struct" conflicts with the typedef'd
// struct definitions in the ESP-DL headers.
#include "dl_image_define.hpp"
#include "dl_detect_define.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

// Forward decl - only types whose full definition is NOT needed here
namespace dl {
class Model;
namespace image {
class ImagePreprocessor;
}
namespace detect {
class yolo11PostProcessor;
}
}

namespace esphome {
namespace yolov11 {

// COCO detection result returned by ESP-DL.
struct DetectionBox {
  int x1, y1, x2, y2;
  float score;
  int category;       // 0..79 COCO index
  const char *label;  // pointer into the static COCO_CLASSES table
};

// Listener interface used by the text_sensor sub-platform.
class YOLOv11Listener {
 public:
  virtual ~YOLOv11Listener() = default;
  virtual void on_detections(const std::vector<DetectionBox> &detections,
                              const std::string &summary) = 0;
};


class YOLOv11Component : public Component, public camera::CameraListener {
 public:
  // ---------- ESPHome lifecycle ----------
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // ---------- CameraListener interface ----------
  void on_camera_image(const std::shared_ptr<camera::CameraImage> &image) override;

  // ---------- YAML setters ----------
  void set_camera(esp32_camera::ESP32Camera *cam) { this->camera_ = cam; }
  void set_score_threshold(float v) { this->score_threshold_ = v; }
  void set_nms_threshold(float v) { this->nms_threshold_ = v; }
  void set_detection_interval_ms(int v) { this->detection_interval_ms_ = v; }
  void set_max_detections(int v) { this->max_detections_ = v; }
  void set_inference_task_stack_size(int v) { this->task_stack_size_ = v; }
  void set_inference_task_priority(int v) { this->task_priority_ = v; }
  void set_draw_enabled(bool v) { this->draw_enabled_ = v; }

  // Resolution setters - called from the YAML codegen since there are
  // no public getters on ESP32Camera for these values.
  void set_frame_width(uint16_t w) { this->frame_width_ = w; }
  void set_frame_height(uint16_t h) { this->frame_height_ = h; }

  // Model-from-file path (alternative to flash-rodata embed).
  void set_model_buffer(const uint8_t *data, size_t size) {
    this->external_model_data_ = data;
    this->external_model_size_ = size;
  }

  // ---------- triggers / listeners ----------
  void add_on_object_detected_callback(std::function<void(int, std::string)> cb) {
    this->on_object_detected_callbacks_.push_back(std::move(cb));
  }
  void add_listener(YOLOv11Listener *l) { this->listeners_.push_back(l); }

  // ---------- public API ----------
  // Force a one-shot inference on the most recently captured frame.
  void trigger_inference();

  // Get current cached detections. Mutex-protected.
  std::vector<DetectionBox> get_detections();
  int get_detected_count();
  std::string get_last_summary() const { return this->last_summary_; }

  // Draw current cached detections (boxes + class name in white) into
  // the supplied RGB565 buffer. Same convention as yolo11_detection on
  // the P4 side: 2-pixel hollow rectangle + 5x7 bitmap font label
  // above the box. Call from your YAML's `on_image:` automation if you
  // want the boxes to appear on the camera display.
  //
  // No-op when draw_enabled is false (default true).
  void draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height);

 protected:
  // Background inference task.
  static void inference_task_trampoline(void *arg);
  void inference_task_loop_();
  bool initialise_detector_();
  void run_one_inference_();

  // Helper: build a "label:score%,..." summary string.
  static std::string build_summary_(const std::vector<DetectionBox> &dets, int max_items);

  // 5x7 bitmap text helper, used by draw_on_frame for the class label.
  static void draw_text_(uint16_t *buffer, uint16_t width, uint16_t height,
                          int x, int y, const char *text,
                          uint16_t color, int scale);

  // ---------- members ----------
  esp32_camera::ESP32Camera *camera_{nullptr};

  // External model buffer (set when model_id: is provided).
  const uint8_t *external_model_data_{nullptr};
  size_t external_model_size_{0};

  float score_threshold_{0.30f};
  float nms_threshold_{0.50f};
  int detection_interval_ms_{200};
  int max_detections_{10};
  int task_stack_size_{8192};
  int task_priority_{5};
  bool draw_enabled_{true};

  // Frame resolution (set from YAML config or inferred from frame data).
  uint16_t frame_width_{320};
  uint16_t frame_height_{240};

  // ESP-DL state - opaque in the public header.
  dl::Model *model_{nullptr};
  bool model_ready_{false};

  // Single-slot frame queue.
  uint8_t *pending_frame_data_{nullptr};
  size_t pending_frame_size_{0};
  uint32_t last_inference_ms_{0};

  TaskHandle_t inference_task_handle_{nullptr};
  SemaphoreHandle_t frame_signal_{nullptr};
  SemaphoreHandle_t state_mutex_{nullptr};

  std::vector<DetectionBox> cached_detections_;
  std::string last_summary_;

  std::vector<std::function<void(int, std::string)>> on_object_detected_callbacks_;
  std::vector<YOLOv11Listener *> listeners_;
};


// =====================================================================
// Trigger fired after each detection pass (count + summary string).
// =====================================================================
class ObjectDetectedTrigger : public Trigger<int, std::string> {
 public:
  explicit ObjectDetectedTrigger(YOLOv11Component *parent) {
    parent->add_on_object_detected_callback(
        [this](int count, std::string summary) {
          this->trigger(count, std::move(summary));
        });
  }
};


// =====================================================================
// Action: yolov11.inference - force a one-shot inference now.
// =====================================================================
template<typename... Ts>
class RunInferenceAction : public Action<Ts...>, public Parented<YOLOv11Component> {
 public:
  void play(Ts... x) override { this->parent_->trigger_inference(); }
};


}  // namespace yolov11
}  // namespace esphome


