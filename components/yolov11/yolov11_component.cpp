#include "yolov11_component.h"
#include "yolo11_detect.hpp"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#ifdef ESP_DL_MODEL_YOLO11
#include "dl_image.hpp"
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace yolov11 {

static const char *const TAG = "yolov11";

// COCO class names. Indexed 0..79 by ESP-DL category id.
static const char *const COCO_CLASSES[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic_light",
    "fire_hydrant", "stop_sign", "parking_meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports_ball", "kite", "baseball_bat", "baseball_glove", "skateboard", "surfboard",
    "tennis_racket", "bottle", "wine_glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot_dog", "pizza", "donut", "cake", "chair", "couch",
    "potted_plant", "bed", "dining_table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell_phone",
    "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy_bear",
    "hair_drier", "toothbrush",
};
static constexpr int COCO_CLASS_COUNT = sizeof(COCO_CLASSES) / sizeof(COCO_CLASSES[0]);


// ---------------------------------------------------------------------------
// stash_frame_ - shared helper between camera callback variants
// ---------------------------------------------------------------------------
// We accept any camera-image-like type via templating because newer
// ESPHome versions have renamed the parameter type from `CameraImage`
// to `CameraImageData`. The fields we touch (`get_data_buffer()` and
// `get_data_length()`) are stable across both.
namespace {
template<typename ImagePtr>
inline void stash_frame_impl(YOLOv11Component *self, const ImagePtr &img,
                             SemaphoreHandle_t state_mutex,
                             SemaphoreHandle_t frame_signal,
                             uint8_t **dst_data, size_t *dst_size) {
  if (img == nullptr) return;
  uint8_t *data = img->get_data_buffer();
  size_t len = img->get_data_length();
  if (data == nullptr || len == 0) return;
  if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
    *dst_data = data;
    *dst_size = len;
    xSemaphoreGive(state_mutex);
    xSemaphoreGive(frame_signal);
  }
  (void)self;
}
}  // namespace


// =========================================================================
// setup() - register a frame callback on the esp32_camera and spin up the
// background inference task. Note: ESP-DL model load is deferred to the
// task itself to keep the watchdog happy.
// =========================================================================
void YOLOv11Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up YOLOv11 (ESP32-S3)...");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "esp32_camera not configured");
    this->mark_failed();
    return;
  }

  this->state_mutex_ = xSemaphoreCreateMutex();
  this->frame_signal_ = xSemaphoreCreateBinary();
  if (!this->state_mutex_ || !this->frame_signal_) {
    ESP_LOGE(TAG, "Failed to create mutex/semaphore");
    this->mark_failed();
    return;
  }

  // Register on every captured frame. We use `auto` for the parameter
  // type so this code compiles unchanged whether ESPHome's
  // esp32_camera component calls back with CameraImage,
  // CameraImageData, or anything else exposing get_data_buffer()/
  // get_data_length() through a shared_ptr.
  this->camera_->add_image_callback(
      [this](auto img) {
        stash_frame_impl(this, img,
                         this->state_mutex_, this->frame_signal_,
                         &this->pending_frame_data_,
                         &this->pending_frame_size_);
      });

#ifdef ESP_DL_MODEL_YOLO11
  // Spin up the background inference task on core 1 (esp32_camera and
  // ESPHome main loop run on core 0).
  BaseType_t ok = xTaskCreatePinnedToCore(
      &YOLOv11Component::inference_task_trampoline, "yolov11_inf",
      this->task_stack_size_, this,
      this->task_priority_, &this->inference_task_handle_, 1);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create inference task");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "YOLOv11 inference task started (core=1, prio=%d, stack=%d)",
           this->task_priority_, this->task_stack_size_);
#else
  ESP_LOGE(TAG, "ESP_DL_MODEL_YOLO11 not defined - this component needs the");
  ESP_LOGE(TAG, "ESP-DL build flags from yolov11/__init__.py");
  this->mark_failed();
  return;
#endif
}

void YOLOv11Component::loop() {
  // Nothing to do on the main task; everything lives in the inference task.
}

void YOLOv11Component::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLOv11 detector:");
  ESP_LOGCONFIG(TAG, "  Score threshold:    %.2f", this->score_threshold_);
  ESP_LOGCONFIG(TAG, "  NMS threshold:      %.2f", this->nms_threshold_);
  ESP_LOGCONFIG(TAG, "  Detection interval: %d ms", this->detection_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Max detections:     %d", this->max_detections_);
  if (this->external_model_data_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Model source:       file: buffer (%zu bytes @ %p)",
                  this->external_model_size_, this->external_model_data_);
  } else {
    ESP_LOGCONFIG(TAG, "  Model source:       flash rodata (build-embedded)");
  }
  ESP_LOGCONFIG(TAG, "  Model ready:        %s", this->model_ready_ ? "yes" : "no");
}


// =========================================================================
// inference task
// =========================================================================
void YOLOv11Component::inference_task_trampoline(void *arg) {
  static_cast<YOLOv11Component *>(arg)->inference_task_loop_();
}

void YOLOv11Component::inference_task_loop_() {
#ifdef ESP_DL_MODEL_YOLO11
  esp_task_wdt_reset();
  if (!this->initialise_detector_()) {
    ESP_LOGE(TAG, "Detector initialisation failed; task exiting");
    vTaskDelete(nullptr);
    return;
  }
  this->model_ready_ = true;
  esp_task_wdt_reset();

  while (true) {
    if (xSemaphoreTake(this->frame_signal_, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    uint32_t now = millis();
    if ((now - this->last_inference_ms_) < (uint32_t) this->detection_interval_ms_) {
      // Frame came in too soon after the previous inference; throttle.
      continue;
    }
    this->last_inference_ms_ = now;

    this->run_one_inference_();
  }
#else
  vTaskDelete(nullptr);
#endif
}


// =========================================================================
// initialise_detector_ - constructs the ESP-DL YOLO11Detect wrapper.
//
// We always use the built-in YOLO11Detect (which loads from the
// _binary_yolo11_detect_espdl_start symbol). When the user supplied an
// `external_model_data_` buffer we still rely on the build-embedded
// model for the actual inference - the file: integration will be a
// no-op in this revision (logged so the user knows). Future work: pass
// the buffer to a dl::Model in-memory constructor.
// =========================================================================
bool YOLOv11Component::initialise_detector_() {
#ifdef ESP_DL_MODEL_YOLO11
  ESP_LOGI(TAG, "Loading YOLO11 model from flash rodata...");

  if (this->external_model_data_ != nullptr) {
    ESP_LOGW(TAG, "model_id was provided (%zu bytes) but runtime swapping is not",
             this->external_model_size_);
    ESP_LOGW(TAG, "implemented yet - using the build-embedded model instead.");
  }

  YOLO11Detect *detector = new YOLO11Detect();
  if (detector == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate YOLO11Detect");
    return false;
  }
  detector->set_score_thr(this->score_threshold_);
  detector->set_nms_thr(this->nms_threshold_);
  this->model_ = reinterpret_cast<dl::Model *>(detector);
  ESP_LOGI(TAG, "YOLO11 detector initialised (score=%.2f nms=%.2f)",
           this->score_threshold_, this->nms_threshold_);
  return true;
#else
  return false;
#endif
}


// =========================================================================
// run_one_inference_ - single inference pass on the latest pending frame.
// =========================================================================
void YOLOv11Component::run_one_inference_() {
#ifdef ESP_DL_MODEL_YOLO11
  if (this->model_ == nullptr) return;
  if (this->camera_ == nullptr) return;

  uint8_t *frame = nullptr;
  size_t frame_size = 0;
  if (xSemaphoreTake(this->state_mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
    frame = this->pending_frame_data_;
    frame_size = this->pending_frame_size_;
    xSemaphoreGive(this->state_mutex_);
  }
  if (!frame || !frame_size) return;

  uint16_t w = this->camera_->get_max_horizontal_resolution();
  uint16_t h = this->camera_->get_max_vertical_resolution();
  if (w == 0 || h == 0) return;

  if (frame_size < (size_t) w * h * 2) {
    static bool warned = false;
    if (!warned) {
      ESP_LOGE(TAG, "Frame size %zu < expected %u for RGB565 %ux%u - "
                    "set `pixel_format: rgb565` on your esp32_camera!",
               frame_size, (unsigned) (w * h * 2), w, h);
      warned = true;
    }
    return;
  }

  YOLO11Detect *detector = reinterpret_cast<YOLO11Detect *>(this->model_);
  dl::image::img_t img = {
      .data = frame,
      .width = w,
      .height = h,
      .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565,
  };

  std::list<dl::detect::result_t> &results = detector->run(img);

  std::vector<DetectionBox> dets;
  dets.reserve(results.size());
  for (const auto &r : results) {
    if ((int) dets.size() >= this->max_detections_) break;
    DetectionBox box;
    box.x1 = r.box[0];
    box.y1 = r.box[1];
    box.x2 = r.box[2];
    box.y2 = r.box[3];
    box.score = r.score;
    box.category = r.category;
    box.label = (r.category >= 0 && r.category < COCO_CLASS_COUNT) ?
                COCO_CLASSES[r.category] : "unknown";
    dets.push_back(box);
  }

  std::string summary = build_summary_(dets, this->max_detections_);

  if (xSemaphoreTake(this->state_mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
    this->cached_detections_ = dets;
    this->last_summary_ = summary;
    xSemaphoreGive(this->state_mutex_);
  }

  for (auto *l : this->listeners_) {
    l->on_detections(dets, summary);
  }
  for (auto &cb : this->on_object_detected_callbacks_) {
    cb(static_cast<int>(dets.size()), summary);
  }
#endif
}


std::string YOLOv11Component::build_summary_(
    const std::vector<DetectionBox> &dets, int max_items) {
  if (dets.empty()) return std::string("none");
  std::string out;
  int n = std::min<int>(dets.size(), max_items);
  out.reserve(n * 16);
  for (int i = 0; i < n; i++) {
    if (i > 0) out += ',';
    out += dets[i].label;
    out += ':';
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(dets[i].score * 100));
    out += buf;
  }
  return out;
}


void YOLOv11Component::trigger_inference() {
  this->last_inference_ms_ = 0;
  if (this->frame_signal_) xSemaphoreGive(this->frame_signal_);
}

std::vector<DetectionBox> YOLOv11Component::get_detections() {
  std::vector<DetectionBox> copy;
  if (xSemaphoreTake(this->state_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    copy = this->cached_detections_;
    xSemaphoreGive(this->state_mutex_);
  }
  return copy;
}

int YOLOv11Component::get_detected_count() {
  int n = 0;
  if (xSemaphoreTake(this->state_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    n = static_cast<int>(this->cached_detections_.size());
    xSemaphoreGive(this->state_mutex_);
  }
  return n;
}

}  // namespace yolov11
}  // namespace esphome
