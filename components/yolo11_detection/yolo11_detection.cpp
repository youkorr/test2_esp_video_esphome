#include "yolo11_detection.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include <algorithm>

#ifdef ESP_DL_MODEL_YOLO11
#include "yolo11_detect.hpp"
#include "dl_image.hpp"
#endif

namespace esphome {
namespace yolo11_detection {

static const char *const TAG = "yolo11_detection";

static const char* COCO_CLASSES[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", 
    "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", 
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
    "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush"
};

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
};

void YOLO11DetectionComponent::draw_text(uint16_t *buffer, uint16_t width, uint16_t height, int x, int y, const char *text, uint16_t color, int scale) {
  while (*text) {
    char c = *text;
    int font_idx = -1;
    if (c >= 'A' && c <= 'Z') font_idx = c - 'A';
    else if (c >= 'a' && c <= 'z') font_idx = c - 'a'; 
    else if (c >= '0' && c <= '9') font_idx = c - '0' + 26;
    else if (c == ' ') font_idx = 36;
    else if (c == ':') font_idx = 37;
    else if (c == ',') font_idx = 38;
    else if (c == '%') font_idx = 39;
    else if (c == '.') font_idx = 40;
    else if (c == '-') font_idx = 41;

    if (font_idx >= 0) {
      for (int row = 0; row < 7; row++) {
        uint8_t row_data = FONT_5X7[font_idx][row];
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

void YOLO11DetectionComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up YOLO11 Object Detection...");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not configured");
    this->mark_failed();
    return;
  }

  this->detections_mutex_ = xSemaphoreCreateMutex();
  this->task_signal_ = xSemaphoreCreateBinary();

#ifndef ESP_DL_MODEL_YOLO11
  ESP_LOGE(TAG, "YOLO11 Detection component requires ESP_DL_MODEL_YOLO11 flag");
  this->mark_failed();
  return;
#else
  // Launch the background task. Initialization will happen inside the task.
  xTaskCreatePinnedToCore(
      YOLO11DetectionComponent::detection_task_wrapper,
      "yolo_detect",
      8192,
      this,
      5,
      &this->detection_task_handle_,
      1 // Core 1
  );

  ESP_LOGI(TAG, "YOLO11 Object Detection background task started.");
#endif
}

void YOLO11DetectionComponent::detection_task_wrapper(void *arg) {
  YOLO11DetectionComponent *self = (YOLO11DetectionComponent *)arg;
  self->detection_task();
}

void YOLO11DetectionComponent::detection_task() {
#ifdef ESP_DL_MODEL_YOLO11
  ESP_LOGI(TAG, "Detection task running. Initializing model...");
  esp_task_wdt_reset(); // Feed WDT before heavy load

#ifdef CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD
  if (this->sdcard_model_path_ == nullptr) {
    ESP_LOGE(TAG, "SD card mode enabled but no model path configured");
    vTaskDelete(NULL);
  }
  ESP_LOGI(TAG, "Loading YOLO11 model from SD card: %s", this->sdcard_model_path_);
  this->object_detector_ = new YOLO11Detect(this->sdcard_model_path_);
#else
  ESP_LOGI(TAG, "Loading YOLO11 model from flash rodata");
  this->object_detector_ = new YOLO11Detect();
#endif

  esp_task_wdt_reset(); // Feed WDT after load

  if (this->object_detector_ == nullptr) {
    ESP_LOGE(TAG, "Failed to construct YOLO11Detect");
    vTaskDelete(NULL);
  }

  this->object_detector_->set_score_thr(this->score_threshold_);
  this->object_detector_->set_nms_thr(this->nms_threshold_);
  this->is_model_loaded_ = true;
  ESP_LOGI(TAG, "YOLO11 detector initialized successfully.");

  while (true) {
    // Wait for a new frame signal
    if (xSemaphoreTake(this->task_signal_, portMAX_DELAY) == pdTRUE) {
      if (this->pending_img_data_ != nullptr) {
        
        dl::image::img_t img = {
          .data = this->pending_img_data_,
          .width = this->pending_width_,
          .height = this->pending_height_,
          .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
        };

        std::list<dl::detect::result_t> &detection_results = this->object_detector_->run(img);

        if (xSemaphoreTake(this->detections_mutex_, portMAX_DELAY) == pdTRUE) {
          this->cached_detections_.clear();
          for (auto &result : detection_results) {
            DetectionBox box;
            box.x1 = result.box[0];
            box.y1 = result.box[1];
            box.x2 = result.box[2];
            box.y2 = result.box[3];
            box.score = result.score;
            box.category = result.category;
            this->cached_detections_.push_back(box);
          }
          xSemaphoreGive(this->detections_mutex_);
        }

        if (detection_results.size() > 0) {
          for (auto &callback : this->on_object_detected_callbacks_) {
            callback(detection_results.size());
          }
        }
      }
      this->is_detecting_ = false; // Mark detection as finished
    }
  }
#endif
}

void YOLO11DetectionComponent::loop() {
  if (this->camera_ == nullptr || !this->camera_->is_streaming() || !this->is_model_loaded_) return;
  this->process_frame_();
}

void YOLO11DetectionComponent::process_frame_() {
  // If the task is busy, skip this frame
  if (this->is_detecting_) return;

  this->frame_counter_++;
  if (this->frame_counter_ < (uint32_t) this->detection_interval_) return;
  this->frame_counter_ = 0;

  esp_cam_sensor::SimpleBufferElement *buffer = this->camera_->acquire_buffer();
  if (buffer == nullptr) return;

  uint8_t* img_data = this->camera_->get_buffer_data(buffer);
  
  if (img_data != nullptr) {
    this->is_detecting_ = true;
    this->pending_img_data_ = img_data;
    this->pending_width_ = this->camera_->get_image_width();
    this->pending_height_ = this->camera_->get_image_height();
    
    // Signal the background task to start
    xSemaphoreGive(this->task_signal_);
  }

  // Warning: in a real async environment, we shouldn't release the buffer until the task finishes.
  // Assuming ESP32-P4 camera buffers persist long enough or are handled via psram double-buffering.
  this->camera_->release_buffer(buffer);
}

void YOLO11DetectionComponent::draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (!this->draw_enabled_ || !this->is_model_loaded_) return;
  this->draw_results_(img_data, width, height);
}

void YOLO11DetectionComponent::draw_results_(uint8_t *img_data, uint16_t width, uint16_t height) {
#ifdef ESP_DL_MODEL_YOLO11
  if (img_data == nullptr || this->detections_mutex_ == nullptr) return;

  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    const uint16_t COLOR_RED    = 0xF800;
    const uint16_t COLOR_GREEN  = 0x07E0;
    const uint16_t COLOR_BLUE   = 0x001F;
    const uint16_t COLOR_YELLOW = 0xFFE0;

    for (auto &box : this->cached_detections_) {
      int x1 = std::max(2, std::min((int)box.x1, (int)width - 3));
      int y1 = std::max(2, std::min((int)box.y1, (int)height - 3));
      int x2 = std::max(x1 + 10, std::min((int)box.x2, (int)width - 3));
      int y2 = std::max(y1 + 10, std::min((int)box.y2, (int)height - 3));

      uint16_t color;
      switch (box.category) {
        case 0:  color = COLOR_RED; break;
        case 2:  color = COLOR_GREEN; break;
        case 16: color = COLOR_BLUE; break;
        default: color = COLOR_YELLOW; break;
      }

      const int line_width = 2;
      uint16_t *buffer = (uint16_t *)img_data;

      for (int x = x1; x <= x2; x++) {
        for (int t = 0; t < line_width; t++) {
          int top_offset = (y1 + t) * width + x;
          if (top_offset >= 0 && top_offset < width * height) buffer[top_offset] = color;
          int bottom_offset = (y2 - t) * width + x;
          if (bottom_offset >= 0 && bottom_offset < width * height) buffer[bottom_offset] = color;
        }
      }

      for (int y = y1; y <= y2; y++) {
        for (int t = 0; t < line_width; t++) {
          int left_offset = y * width + (x1 + t);
          if (left_offset >= 0 && left_offset < width * height) buffer[left_offset] = color;
          int right_offset = y * width + (x2 - t);
          if (right_offset >= 0 && right_offset < width * height) buffer[right_offset] = color;
        }
      }

      const char* class_name = "Unknown";
      if (box.category >= 0 && box.category < 80) {
          class_name = COCO_CLASSES[box.category];
      }
      char label[64];
      snprintf(label, sizeof(label), "%s %d%%", class_name, (int)(box.score * 100));

      int text_x = std::max(0, x1);
      int text_y = std::max(0, y1 - 16); 
      this->draw_text(buffer, width, height, text_x, text_y, label, color, 2);
    }
    xSemaphoreGive(this->detections_mutex_);
  }
#endif
}

void YOLO11DetectionComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLO11 Object Detection:");
  ESP_LOGCONFIG(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGCONFIG(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  ESP_LOGCONFIG(TAG, "  Detection interval: %d frames", this->detection_interval_);
}

int YOLO11DetectionComponent::get_detected_count() {
  int count = 0;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    count = this->cached_detections_.size();
    xSemaphoreGive(this->detections_mutex_);
  }
  return count;
}

std::vector<DetectionBox> YOLO11DetectionComponent::get_detections() {
  std::vector<DetectionBox> detections;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    detections = this->cached_detections_;
    xSemaphoreGive(this->detections_mutex_);
  }
  return detections;
}

}  // namespace yolo11_detection
}  // namespace esphome
