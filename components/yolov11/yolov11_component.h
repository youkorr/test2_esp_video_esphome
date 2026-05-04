#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"
#include "esphome/components/esp32_camera/esp32_camera.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <list>
#include <string>
#include <vector>

// Forward decl - ESP-DL types stay opaque in the public header
namespace dl {
namespace image {
struct img_t;
class ImagePreprocessor;
}
namespace detect {
class yolo11PostProcessor;
struct result_t;
}
class Model;
}

// Forward decl for jesserockz file: component (used only when
// YOLOV11_MODEL_FROM_FILE is defined - kept loose for compile)
namespace esphome {
namespace file {
class File;
}
}

namespace esphome {
namespace yolov11 {

// COCO detection result returned by ESP-DL, plus the class label
// resolved through the embedded COCO_CLASSES table.
struct DetectionBox {
  int x1, y1, x2, y2;
  float score;
  int category;       // 0..79 COCO index
  const char *label;  // pointer into static COCO_CLASSES table
};

// Triggered after each successful inference pass. Fires with the
// number of detections and a short summary string ready for log /
// text_sensor output.
class ObjectDetectedTrigger;
class RunInferenceAction;

// Returned to the text_sensor so it can publish updated text
// without having to know the internal vector layout.
class YOLOv11Listener {
 public:
  virtual ~YOLOv11Listener() = default;
  virtual void on_detections(const std::vector<DetectionBox> &detections,
                              const std::string &summary) = 0;
};


class YOLOv11Component : public Component {
 public:
  // ---------- ESPHome lifecycle ----------
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // ---------- YAML setters ----------
  void set_camera(esp32_camera::ESP32Camera *cam) { this->camera_ = cam; }
  void set_score_threshold(float v) { this->score_threshold_ = v; }
  void set_nms_threshold(float v) { this->nms_threshold_ = v; }
  void set_detection_interval_ms(int v) { this->detection_interval_ms_ = v; }
  void set_max_detections(int v) { this->max_detections_ = v; }
  void set_inference_task_stack_size(int v) { this->task_stack_size_ = v; }
  void set_inference_task_priority(int v) { this->task_priority_ = v; }
#ifdef YOLOV11_MODEL_FROM_FILE
  // Pointer at the jesserockz `file:` component holding the .espdl bytes.
  // Stored as void* in the header to avoid pulling the file: header
  // into every translation unit; resolved in the cpp.
  void set_model_from_file(esphome::file::File *f) {
    this->model_file_ = reinterpret_cast<void *>(f);
  }
#endif

  // ---------- triggers / listeners ----------
  void add_on_object_detected_callback(std::function<void(int, std::string)> cb) {
    this->on_object_detected_callbacks_.push_back(std::move(cb));
  }
  void add_listener(YOLOv11Listener *l) { this->listeners_.push_back(l); }

  // ---------- public API ----------
  // Force a one-shot inference on the most recently captured frame.
  // Used by the `yolov11.inference` action and by the on_image automation
  // example shown in the user's YAML.
  void trigger_inference();

  // Get current cached detections. Mutex-protected.
  std::vector<DetectionBox> get_detections();
  int get_detected_count();

  // Listener used by the text_sensor sub-platform.
  // Returns the latest summary string.
  std::string get_last_summary() const { return this->last_summary_; }

 protected:
  // ---------- background task ----------
  static void inference_task_trampoline(void *arg);
  void inference_task_loop_();

  // Called from the esp32_camera image callback to stash the latest
  // frame buffer before signalling the inference task.
  void on_camera_image_(const std::shared_ptr<esp32_camera::CameraImage> &img);

  // Initialise the ESP-DL Model + preprocessor + postprocessor.
  // Called once from inference_task_loop_() (we defer this off the main
  // task to avoid the watchdog firing during the few-second model load).
  bool initialise_detector_();

  // Run a single inference cycle on the buffer pointed to by
  // pending_frame_data_/pending_frame_size_.
  void run_one_inference_();

  // Build a "person:87,car:62" style summary string.
  static std::string build_summary_(const std::vector<DetectionBox> &dets, int max_items);

  // ---------- members ----------
  esp32_camera::ESP32Camera *camera_{nullptr};
  void *model_file_{nullptr};  // really esphome::file::File *

  float score_threshold_{0.30f};
  float nms_threshold_{0.50f};
  int detection_interval_ms_{200};
  int max_detections_{10};
  int task_stack_size_{8192};
  int task_priority_{5};

  // ESP-DL state - all opaque pointers in the public header.
  dl::Model *model_{nullptr};
  dl::image::ImagePreprocessor *preprocessor_{nullptr};
  dl::detect::yolo11PostProcessor *postprocessor_{nullptr};
  bool model_ready_{false};

  // ---------- frame queue (single-slot) ----------
  // The camera callback updates pending_frame_*; the inference task
  // reads them when signalled. We only keep the LATEST frame, dropping
  // older ones if the inference is slower than the camera. This keeps
  // memory usage bounded and matches what M5Stack/Espressif do.
  uint8_t *pending_frame_data_{nullptr};
  uint16_t pending_frame_w_{0};
  uint16_t pending_frame_h_{0};
  size_t pending_frame_size_{0};
  uint32_t last_inference_ms_{0};

  TaskHandle_t inference_task_handle_{nullptr};
  SemaphoreHandle_t frame_signal_{nullptr};
  SemaphoreHandle_t state_mutex_{nullptr};

  // ---------- cached output ----------
  std::vector<DetectionBox> cached_detections_;
  std::string last_summary_;

  // ---------- callbacks ----------
  std::vector<std::function<void(int, std::string)>> on_object_detected_callbacks_;
  std::vector<YOLOv11Listener *> listeners_;
};


// =====================================================================
// Trigger fired after each detection pass
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
// Action: yolov11.inference
// =====================================================================
template<typename... Ts>
class RunInferenceAction : public Action<Ts...>, public Parented<YOLOv11Component> {
 public:
  void play(Ts... x) override { this->parent_->trigger_inference(); }
};


}  // namespace yolov11
}  // namespace esphome
