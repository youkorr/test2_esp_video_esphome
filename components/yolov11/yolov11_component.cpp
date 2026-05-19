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

// Defined in yolo11_detect_inner.cpp. Sets the runtime override for the
// model bytes; pass nullptr to fall back to the build-embedded blob.
extern "C" void yolov11_set_runtime_model_data(const uint8_t *data);

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
// 5x7 bitmap font - same glyph set as in the P4 yolo11_detection so the
// labels look identical on both platforms.
// ---------------------------------------------------------------------------
static const uint8_t FONT_5X7[][7] = {
  {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
  {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, // B
  {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, // C
  {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, // D
  {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // E
  {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // F
  {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, // G
  {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // H
  {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // I
  {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, // J
  {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
  {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // L
  {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
  {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // N
  {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // O
  {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // P
  {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // Q
  {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // R
  {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E}, // S
  {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
  {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // U
  {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, // V
  {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, // W
  {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
  {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // Y
  {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, // Z
  {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
  {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
  {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
  {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E}, // 3
  {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
  {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
  {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
  {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
  {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
  {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Space
  {0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00}, // : (colon)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}, // , (comma)
  {0x11, 0x11, 0x09, 0x01, 0x12, 0x12, 0x0C}, // % (percent)
  {0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00}, // . (dot)
  {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, // - (hyphen)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // _ (underscore as space)
};

static int font_index_for(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a';
  if (c >= '0' && c <= '9') return c - '0' + 26;
  if (c == ' ') return 36;
  if (c == ':') return 37;
  if (c == ',') return 38;
  if (c == '%') return 39;
  if (c == '.') return 40;
  if (c == '-') return 41;
  if (c == '_') return 42;
  return -1;
}







// CameraListener callback - called each time the camera produces a new frame.
void YOLOv11Component::on_camera_image(const std::shared_ptr<camera::CameraImage> &image) {
  if (image == nullptr) return;
  uint8_t *data = image->get_data_buffer();
  size_t len = image->get_data_length();
  if (data == nullptr || len == 0) return;
  if (this->state_mutex_ == nullptr) return;
  if (xSemaphoreTake(this->state_mutex_, pdMS_TO_TICKS(2)) == pdTRUE) {
    this->pending_frame_data_ = data;
    this->pending_frame_size_ = len;
    xSemaphoreGive(this->state_mutex_);
    if (this->frame_signal_) xSemaphoreGive(this->frame_signal_);
  }
}


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

  // Register ourselves as a CameraListener to receive frames.
  this->camera_->add_listener(this);

#ifdef ESP_DL_MODEL_YOLO11
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

void YOLOv11Component::loop() {}

void YOLOv11Component::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLOv11 detector:");
  ESP_LOGCONFIG(TAG, "  Score threshold:    %.2f", this->score_threshold_);
  ESP_LOGCONFIG(TAG, "  NMS threshold:      %.2f", this->nms_threshold_);
  ESP_LOGCONFIG(TAG, "  Detection interval: %d ms", this->detection_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Max detections:     %d", this->max_detections_);
  ESP_LOGCONFIG(TAG, "  Draw enabled:       %s", this->draw_enabled_ ? "yes" : "no");
  if (this->external_model_data_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Model source:       file: buffer (%zu bytes @ %p) [runtime]",
                  this->external_model_size_, this->external_model_data_);
  } else {
    ESP_LOGCONFIG(TAG, "  Model source:       flash rodata (build-embedded)");
  }
  ESP_LOGCONFIG(TAG, "  Model ready:        %s", this->model_ready_ ? "yes" : "no");
}


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
      continue;
    }
    this->last_inference_ms_ = now;
    this->run_one_inference_();
  }
#else
  vTaskDelete(nullptr);
#endif
}


bool YOLOv11Component::initialise_detector_() {
#ifdef ESP_DL_MODEL_YOLO11
  // If the user provided a runtime model (model_id: pointing at a
  // jesserockz file: array), install it as the override BEFORE
  // YOLO11Detect's constructor reads the model bytes.
  if (this->external_model_data_ != nullptr) {
    ESP_LOGI(TAG, "Loading YOLO11 model from runtime buffer (%zu bytes @ %p)",
             this->external_model_size_, this->external_model_data_);
    yolov11_set_runtime_model_data(this->external_model_data_);
  } else {
    ESP_LOGI(TAG, "Loading YOLO11 model from flash rodata (build-embedded)");
    yolov11_set_runtime_model_data(nullptr);
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


void YOLOv11Component::run_one_inference_() {
#ifdef ESP_DL_MODEL_YOLO11
  if (this->model_ == nullptr || this->camera_ == nullptr) return;

  uint8_t *frame = nullptr;
  size_t frame_size = 0;
  if (xSemaphoreTake(this->state_mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
    frame = this->pending_frame_data_;
    frame_size = this->pending_frame_size_;
    xSemaphoreGive(this->state_mutex_);
  }
  if (!frame || !frame_size) return;

  uint16_t w = this->frame_width_;
  uint16_t h = this->frame_height_;
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


void YOLOv11Component::draw_text_(uint16_t *buffer, uint16_t width, uint16_t height,
                                   int x, int y, const char *text,
                                   uint16_t color, int scale) {
  if (buffer == nullptr || text == nullptr || scale < 1) return;
  while (*text) {
    int idx = font_index_for(*text);
    if (idx >= 0) {
      const uint8_t *glyph = FONT_5X7[idx];
      for (int row = 0; row < 7; row++) {
        uint8_t row_data = glyph[row];
        for (int col = 0; col < 5; col++) {
          if (row_data & (1 << (4 - col))) {
            for (int sy = 0; sy < scale; sy++) {
              for (int sx = 0; sx < scale; sx++) {
                int px = x + (col * scale) + sx;
                int py = y + (row * scale) + sy;
                if (px >= 0 && px < width && py >= 0 && py < height) {
                  buffer[py * width + px] = color;
                }
              }
            }
          }
        }
      }
    }
    x += 6 * scale;
    text++;
  }
}


void YOLOv11Component::draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (!this->draw_enabled_) return;
  if (img_data == nullptr || width == 0 || height == 0) return;
  if (this->state_mutex_ == nullptr) return;

  std::vector<DetectionBox> dets;
  if (xSemaphoreTake(this->state_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    dets = this->cached_detections_;
    xSemaphoreGive(this->state_mutex_);
  }
  if (dets.empty()) return;

  uint16_t *buffer = reinterpret_cast<uint16_t *>(img_data);

  const uint16_t COLOR_RED    = 0xF800;
  const uint16_t COLOR_GREEN  = 0x07E0;
  const uint16_t COLOR_BLUE   = 0x001F;
  const uint16_t COLOR_YELLOW = 0xFFE0;
  const uint16_t COLOR_WHITE  = 0xFFFF;

  const int line_width = 2;

  for (const auto &box : dets) {
    int x1 = std::max(2, std::min((int)box.x1, (int)width - 3));
    int y1 = std::max(2, std::min((int)box.y1, (int)height - 3));
    int x2 = std::max(x1 + 10, std::min((int)box.x2, (int)width - 3));
    int y2 = std::max(y1 + 10, std::min((int)box.y2, (int)height - 3));

    uint16_t color;
    switch (box.category) {
      case 0:  color = COLOR_RED;    break;
      case 2:  color = COLOR_GREEN;  break;
      case 16: color = COLOR_BLUE;   break;
      default: color = COLOR_YELLOW; break;
    }

    for (int x = x1; x <= x2; x++) {
      for (int t = 0; t < line_width; t++) {
        int top = (y1 + t) * width + x;
        if (top >= 0 && top < (int)(width * height)) buffer[top] = color;
        int bot = (y2 - t) * width + x;
        if (bot >= 0 && bot < (int)(width * height)) buffer[bot] = color;
      }
    }
    for (int y = y1; y <= y2; y++) {
      for (int t = 0; t < line_width; t++) {
        int left  = y * width + (x1 + t);
        if (left  >= 0 && left  < (int)(width * height)) buffer[left]  = color;
        int right = y * width + (x2 - t);
        if (right >= 0 && right < (int)(width * height)) buffer[right] = color;
      }
    }

    char label[64];
    const char *cls = (box.category >= 0 && box.category < COCO_CLASS_COUNT)
                          ? COCO_CLASSES[box.category]
                          : "unknown";
    snprintf(label, sizeof(label), "%s %d%%", cls,
             static_cast<int>(box.score * 100));

    int text_x = std::max(0, x1);
    int text_y = std::max(0, y1 - 9);
    draw_text_(buffer, width, height, text_x, text_y, label, COLOR_WHITE, 1);
  }
}

}  // namespace yolov11
}  // namespace esphome
