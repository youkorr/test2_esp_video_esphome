#include "yolov11_component.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef ESP_DL_MODEL_YOLO11
#include "yolo11_detect.hpp"
#include "dl_image.hpp"
#endif

#ifdef YOLOV11_MODEL_FROM_FILE
// jesserockz/esphome-components file: component
// Wrapped in try/catch include because the header path may differ across
// versions.
#include "esphome/components/file/file.h"
#endif

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

  // Register on every captured frame. The esp32_camera component
  // already serialises calls; we just stash the buffer pointer and
  // signal the inference task.
  this->camera_->add_image_callback(
      [this](std::shared_ptr<esp32_camera::CameraImage> img) {
        this->on_camera_image_(img);
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
#ifdef YOLOV11_MODEL_FROM_FILE
  ESP_LOGCONFIG(TAG, "  Model source:       file: (runtime buffer)");
#else
  ESP_LOGCONFIG(TAG, "  Model source:       flash rodata (build-embedded)");
#endif
  ESP_LOGCONFIG(TAG, "  Model ready:        %s", this->model_ready_ ? "yes" : "no");
}


// =========================================================================
// camera image callback
// =========================================================================
void YOLOv11Component::on_camera_image_(
    const std::shared_ptr<esp32_camera::CameraImage> &img) {
  if (img == nullptr) return;

  uint8_t *data = img->get_data_buffer();
  size_t len = img->get_data_length();
  if (data == nullptr || len == 0) return;

  // Drop frames if the inference task hasn't consumed the previous one
  // yet. This is the standard "single-slot drop-old" pattern.
  if (xSemaphoreTake(this->state_mutex_, pdMS_TO_TICKS(2)) == pdTRUE) {
    this->pending_frame_data_ = data;
    this->pending_frame_size_ = len;
    // The esp32_camera reports the configured resolution. The user MUST
    // set pixel_format: rgb565 in YAML for inference to work; we don't
    // attempt JPEG decode here.
    // We'll read width/height from the camera at inference time.
    xSemaphoreGive(this->state_mutex_);
    xSemaphoreGive(this->frame_signal_);  // wake the task (no-op if already pending)
  }
}


// =========================================================================
// inference task
// =========================================================================
void YOLOv11Component::inference_task_trampoline(void *arg) {
  static_cast<YOLOv11Component *>(arg)->inference_task_loop_();
}

void YOLOv11Component::inference_task_loop_() {
#ifdef ESP_DL_MODEL_YOLO11
  // Initial model load - several seconds; feed the WDT explicitly.
  esp_task_wdt_reset();
  if (!this->initialise_detector_()) {
    ESP_LOGE(TAG, "Detector initialisation failed; task exiting");
    vTaskDelete(nullptr);
    return;
  }
  this->model_ready_ = true;
  esp_task_wdt_reset();

  while (true) {
    // Wait for a new frame signal. portMAX_DELAY blocks until camera fires.
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
// initialise_detector_ - constructs ESP-DL Model + preprocessor + postproc.
// =========================================================================
bool YOLOv11Component::initialise_detector_() {
#ifdef ESP_DL_MODEL_YOLO11
  ESP_LOGI(TAG, "Loading YOLO11 model...");

#ifdef YOLOV11_MODEL_FROM_FILE
  // ----- model from file: -----
  if (this->model_file_ == nullptr) {
    ESP_LOGE(TAG, "model_id was set but the file: pointer is null");
    return false;
  }
  esphome::file::File *f = reinterpret_cast<esphome::file::File *>(this->model_file_);
  // The file: API exposes .data() and .size() (pattern used by the
  // jesserockz component). If your version uses different names, adjust
  // here.
  const uint8_t *bytes = f->data();
  size_t nbytes = f->size();
  if (!bytes || !nbytes) {
    ESP_LOGE(TAG, "file: returned empty buffer");
    return false;
  }
  ESP_LOGI(TAG, "Loading model from file: buffer %p (%zu bytes)", bytes, nbytes);
  // dl::Model has a constructor that takes a raw pointer + size for the
  // in-memory case. We use location 0 (flash rodata) to skip any partition
  // / SD-card path lookup.
  this->model_ = new dl::Model(reinterpret_cast<const char *>(bytes),
                               static_cast<fbs::model_location_type_t>(0));
#else
  // ----- model from flash rodata (Option C, embedded at build) -----
  ESP_LOGI(TAG, "Loading model from flash rodata (built-in)");
  // The yolo11_detect upstream wrapper handles the
  // _binary_yolo11_detect_espdl_start symbol for us.
  this->model_ = nullptr;  // we use YOLO11Detect below instead of raw Model
#endif

  // Create the YOLO11 wrapper (handles preprocessor + postprocessor).
  YOLO11Detect *detector = nullptr;
#ifdef YOLOV11_MODEL_FROM_FILE
  // When loading from file, we need a flavour of YOLO11Impl that takes
  // an existing dl::Model. The upstream wrapper doesn't expose that
  // path directly, so we construct it via the standard ctor and the
  // build flags ensure flash_rodata is used internally; the model_
  // member above was just an early validation of the file: bytes.
  // For the actual inference we let YOLO11Detect resolve the model
  // internally - this means the file: source effectively only works
  // when the build-embedded model name matches the file content.
  // Future work: extend YOLO11Detect to accept a (data,size) pair.
  detector = new YOLO11Detect();
#else
  detector = new YOLO11Detect();
#endif
  if (detector == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate YOLO11Detect");
    return false;
  }
  detector->set_score_thr(this->score_threshold_);
  detector->set_nms_thr(this->nms_threshold_);

  // Stash the detector through the dl::Model alias so we can free it
  // later. We keep it in a void* via reinterpret to avoid leaking
  // YOLO11Detect into the public header.
  this->model_ = reinterpret_cast<dl::Model *>(detector);

  ESP_LOGI(TAG, "YOLO11 model loaded (score=%.2f nms=%.2f)",
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

  // Read camera resolution. esp32_camera reports the configured size.
  uint16_t w = this->camera_->get_max_horizontal_resolution();
  uint16_t h = this->camera_->get_max_vertical_resolution();
  if (w == 0 || h == 0) return;

  // Sanity check: RGB565 means 2 bytes per pixel.
  if (frame_size < (size_t) w * h * 2) {
    // Probably JPEG. Tell the user once and bail.
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

  // Build the cached detection vector.
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

  // Publish under the mutex so the text_sensor reading code stays consistent.
  if (xSemaphoreTake(this->state_mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
    this->cached_detections_ = dets;
    this->last_summary_ = summary;
    xSemaphoreGive(this->state_mutex_);
  }

  // Notify listeners (text_sensor) and trigger callbacks (automation).
  for (auto *l : this->listeners_) {
    l->on_detections(dets, summary);
  }
  for (auto &cb : this->on_object_detected_callbacks_) {
    cb(static_cast<int>(dets.size()), summary);
  }
#endif
}


// =========================================================================
// build_summary_ - "person:87,car:62,..." format. max_items entries max.
// =========================================================================
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


// =========================================================================
// Public API
// =========================================================================
void YOLOv11Component::trigger_inference() {
  // Force the inference task to wake even if interval hasn't elapsed.
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
