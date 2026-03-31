#include "yolo11_detection.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

// ESP-DL headers (direct usage, no wrapper)
#include "dl_model_base.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_detect_yolo11_postprocessor.hpp"
#include "dl_image.hpp"

#ifdef USE_YOLO11_ESP32_CAMERA
#include "esphome/components/esp32_camera/esp32_camera.h"
#endif

#if defined(USE_ESP_IDF) && defined(USE_YOLO11_MIPI_CAMERA)
#include "esp_cache.h"
#endif

// Model data - embedded in flash rodata by build script
extern const uint8_t yolo11_detect_espdl[] asm("_binary_yolo11_detect_espdl_start");

namespace esphome {
namespace yolo11_detection {

static const char *const TAG = "yolo11_detection";

// 5x7 bitmap font for drawing text on RGB565 frame buffer
static const uint8_t FONT_5X7[][7] = {
  // A-Z (index 0-25)
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
  // 0-9 (index 26-35)
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
  // Special characters (index 36-40)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Space
  {0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00}, // : (colon)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}, // , (comma)
  {0x11, 0x11, 0x09, 0x01, 0x12, 0x12, 0x0C}, // % (percent)
  {0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00}, // . (dot)
};

// COCO class names (80 classes)
static const char *const COCO_CLASSES[] = {
  "person", "bicycle", "car", "motorcycle", "airplane",
  "bus", "train", "truck", "boat", "traffic light",
  "fire hydrant", "stop sign", "parking meter", "bench", "bird",
  "cat", "dog", "horse", "sheep", "cow",
  "elephant", "bear", "zebra", "giraffe", "backpack",
  "umbrella", "handbag", "tie", "suitcase", "frisbee",
  "skis", "snowboard", "sports ball", "kite", "baseball bat",
  "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
  "wine glass", "cup", "fork", "knife", "spoon",
  "bowl", "banana", "apple", "sandwich", "orange",
  "broccoli", "carrot", "hot dog", "pizza", "donut",
  "cake", "chair", "couch", "potted plant", "bed",
  "dining table", "toilet", "tv", "laptop", "mouse",
  "remote", "keyboard", "cell phone", "microwave", "oven",
  "toaster", "sink", "refrigerator", "book", "clock",
  "vase", "scissors", "teddy bear", "hair drier", "toothbrush",
};

void YOLO11DetectionComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up YOLO11 Object Detection...");

  this->detections_mutex_ = xSemaphoreCreateMutex();
  if (this->detections_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create detections mutex");
    this->mark_failed();
    return;
  }

  // Load model directly from flash rodata
  ESP_LOGI(TAG, "Loading YOLO11 model from flash...");

  const char *model_data = (const char *)yolo11_detect_espdl;
  if (model_data == nullptr) {
    ESP_LOGE(TAG, "Model data is null");
    this->mark_failed();
    return;
  }

  this->dl_model_ = new dl::Model(
      model_data,
      "yolo11_detect_s8_v1.espdl",
      fbs::MODEL_LOCATION_IN_FLASH_RODATA,
      0,
      dl::MEMORY_MANAGER_GREEDY,
      nullptr,
      true);

  if (this->dl_model_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create dl::Model");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Model loaded, creating preprocessor...");

  // ESP32-P4 MIPI CSI camera stores RGB565 big-endian in memory
  // std={255,255,255} normalizes [0,255] to [0,1] before quantization
  this->preprocessor_ = new dl::image::ImagePreprocessor(
      this->dl_model_, {0, 0, 0}, {255, 255, 255},
      dl::image::DL_IMAGE_CAP_RGB_SWAP | dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);

  ESP_LOGI(TAG, "Creating YOLO11 postprocessor...");

  this->postprocessor_ = new dl::detect::yolo11PostProcessor(
      this->dl_model_,
      this->preprocessor_,
      this->score_threshold_,
      this->nms_threshold_,
      10,0.25, 0.7,
      {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});

  this->detector_initialized_ = true;

  // Boot diagnostics: log model input shape and preprocessing caps
  ESP_LOGI(TAG, "YOLO11 model input: [1, H, W, 3]");
  ESP_LOGI(TAG, "YOLO11: Using RGB565 BIG ENDIAN caps=0x%x",
           dl::image::DL_IMAGE_CAP_RGB_SWAP | dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);

  if (!this->detect_classes_.empty()) {
    ESP_LOGI(TAG, "Class filter: %d classes enabled", (int)this->detect_classes_.size());
    for (int cls_id : this->detect_classes_) {
      const char *name = (cls_id >= 0 && cls_id < 80) ? COCO_CLASSES[cls_id] : "unknown";
      ESP_LOGI(TAG, "  - [%d] %s", cls_id, name);
    }
  } else {
    ESP_LOGI(TAG, "Class filter: all 80 COCO classes");
  }

  // Register with ESP32 camera (callback-based)
#ifdef USE_YOLO11_ESP32_CAMERA
  if (this->esp32_camera_ != nullptr) {
    this->esp32_camera_->add_image_callback(
        [this](std::shared_ptr<esp32_camera::CameraImage> image) {
          this->on_esp32_camera_image_(std::move(image));
        });
    ESP_LOGI(TAG, "Registered with ESP32 camera (callback mode)");
  }
#endif

#ifdef USE_YOLO11_MIPI_CAMERA
  if (this->mipi_camera_ != nullptr) {
    ESP_LOGI(TAG, "Registered with MIPI camera (polling mode)");
  }
#endif

  ESP_LOGI(TAG, "YOLO11 Object Detection ready");
  ESP_LOGI(TAG, "  Detection interval: every %d frames", this->detection_interval_);
  ESP_LOGI(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGI(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  ESP_LOGI(TAG, "  Draw boxes: %s", this->draw_enabled_ ? "YES" : "NO");
}

void YOLO11DetectionComponent::loop() {
  if (!this->detector_initialized_) {
    return;
  }

#ifdef USE_YOLO11_MIPI_CAMERA
  if (this->mipi_camera_ != nullptr) {
    if (!this->mipi_camera_->is_streaming()) {
      return;
    }
    this->process_frame_();
  }
#endif
  // For ESP32 camera, inference is handled via on_esp32_camera_image_ callback
}

void YOLO11DetectionComponent::process_frame_() {
  this->frame_counter_++;

  if (this->frame_counter_ < this->detection_interval_) {
    return;
  }

  this->frame_counter_ = 0;

#ifdef USE_YOLO11_MIPI_CAMERA
  if (this->mipi_camera_ != nullptr) {
    esp_cam_sensor::SimpleBufferElement *buffer = this->mipi_camera_->acquire_buffer();
    if (buffer == nullptr) {
      return;
    }

    uint8_t *img_data = this->mipi_camera_->get_buffer_data(buffer);
    uint16_t width = this->mipi_camera_->get_image_width();
    uint16_t height = this->mipi_camera_->get_image_height();

    if (img_data != nullptr) {
      // ESP32-P4: Invalidate CPU cache before reading SPIRAM buffer filled by DMA
      uint32_t frame_size = width * height * 2;  // RGB565
      esp_cache_msync(img_data, frame_size,
                      ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
      this->detect_objects_(img_data, width, height);
    }

    this->mipi_camera_->release_buffer(buffer);
  }
#endif
}

#ifdef USE_YOLO11_ESP32_CAMERA
void YOLO11DetectionComponent::on_esp32_camera_image_(
    std::shared_ptr<esp32_camera::CameraImage> image) {
  if (!this->detector_initialized_) {
    return;
  }

  // Rate-limit detection using frame counter
  this->frame_counter_++;
  if (this->frame_counter_ < this->detection_interval_) {
    return;
  }
  this->frame_counter_ = 0;

  uint8_t *data = image->get_data_buffer();
  size_t len = image->get_data_length();

  if (data == nullptr || len == 0) {
    return;
  }

  // Determine dimensions from data length (RGB565 = 2 bytes per pixel)
  const uint16_t resolutions[][2] = {
      {320, 240}, {640, 480}, {160, 120}, {800, 600}, {1024, 768},
  };

  for (auto &res : resolutions) {
    if (len == (size_t)res[0] * res[1] * 2) {
      this->detect_objects_(data, res[0], res[1]);
      return;
    }
  }
  ESP_LOGW(TAG, "Unsupported image size: %u bytes (need RGB565 format)", (unsigned)len);
}
#endif

void YOLO11DetectionComponent::detect_objects_(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (this->dl_model_ == nullptr || this->postprocessor_ == nullptr) {
    return;
  }

  // Create image structure for ESP-DL
  dl::image::img_t img;
  img.data = img_data;
  img.width = width;
  img.height = height;
  img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565;

  // Run inference pipeline
  this->preprocessor_->preprocess(img);
  this->dl_model_->run();

  this->postprocessor_->clear_result();
  this->postprocessor_->postprocess();
  auto &results = this->postprocessor_->get_result(width, height);

  // Streaming diagnostics
  ESP_LOGD(TAG, "YOLO11 stage: %d candidates from %ux%u image",
           (int)results.size(), width, height);

  // Cache results (mutex protected), apply class filter
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    this->cached_detections_.clear();

    for (auto &result : results) {
      // Class filtering: skip classes not in detect_classes_ (if filter is set)
      if (!this->detect_classes_.empty()) {
        bool found = false;
        for (int cls_id : this->detect_classes_) {
          if (result.category == cls_id) { found = true; break; }
        }
        if (!found) continue;
      }

      DetectionBox box;
      box.x1 = result.box[0];
      box.y1 = result.box[1];
      box.x2 = result.box[2];
      box.y2 = result.box[3];
      box.score = result.score;
      box.category = result.category;
      this->cached_detections_.push_back(box);

      const char *name = (box.category >= 0 && box.category < 80) ? COCO_CLASSES[box.category] : "?";
      ESP_LOGD(TAG, "  -> %s score=%.2f box=[%d,%d,%d,%d]",
               name, box.score, box.x1, box.y1, box.x2, box.y2);
    }

    xSemaphoreGive(this->detections_mutex_);
  }

  // Trigger callbacks
  int filtered_count = 0;
  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    filtered_count = this->cached_detections_.size();
    xSemaphoreGive(this->detections_mutex_);
  }
  if (filtered_count > 0) {
    ESP_LOGD(TAG, "Detected %d object(s) (after filter)", filtered_count);
    for (auto &callback : this->on_object_detected_callbacks_) {
      callback(filtered_count);
    }
  }
}

void YOLO11DetectionComponent::draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (!this->draw_enabled_) {
    return;
  }
  this->draw_results_(img_data, width, height);
}

void YOLO11DetectionComponent::draw_results_(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (img_data == nullptr || this->detections_mutex_ == nullptr) {
    return;
  }

  if (xSemaphoreTake(this->detections_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    const uint16_t COLOR_RED    = 0xF800;  // Person
    const uint16_t COLOR_GREEN  = 0x07E0;  // Car
    const uint16_t COLOR_BLUE   = 0x001F;  // Dog
    const uint16_t COLOR_YELLOW = 0xFFE0;  // Default
    const uint16_t COLOR_CYAN   = 0x07FF;
    const uint16_t COLOR_MAGENTA = 0xF81F;

    for (auto &box : this->cached_detections_) {
      int x1 = std::max(2, std::min((int)box.x1, (int)width - 3));
      int y1 = std::max(2, std::min((int)box.y1, (int)height - 3));
      int x2 = std::max(x1 + 10, std::min((int)box.x2, (int)width - 3));
      int y2 = std::max(y1 + 10, std::min((int)box.y2, (int)height - 3));

      uint16_t color;
      switch (box.category) {
        case 0:  color = COLOR_RED; break;      // person
        case 1:  color = COLOR_GREEN; break;    // bicycle
        case 2:  color = COLOR_CYAN; break;     // car
        case 14: color = COLOR_MAGENTA; break;  // bird
        case 15: color = COLOR_BLUE; break;     // cat
        case 16: color = COLOR_GREEN; break;    // dog
        default: color = COLOR_YELLOW; break;
      }

      const int line_width = 2;
      uint16_t *buffer = (uint16_t *)img_data;

      for (int x = x1; x <= x2; x++) {
        for (int t = 0; t < line_width; t++) {
          int top = (y1 + t) * width + x;
          if (top >= 0 && top < width * height) buffer[top] = color;
          int bot = (y2 - t) * width + x;
          if (bot >= 0 && bot < width * height) buffer[bot] = color;
        }
      }
      for (int y = y1; y <= y2; y++) {
        for (int t = 0; t < line_width; t++) {
          int left = y * width + (x1 + t);
          if (left >= 0 && left < width * height) buffer[left] = color;
          int right = y * width + (x2 - t);
          if (right >= 0 && right < width * height) buffer[right] = color;
        }
      }

      // Build label: "CLASS XX%"
      const char *class_name = "unknown";
      if (box.category >= 0 && box.category < 80) {
        class_name = COCO_CLASSES[box.category];
      }
      char label[32];
      int pct = (int)(box.score * 100.0f);
      snprintf(label, sizeof(label), "%s %d%%", class_name, pct);

      // Label dimensions
      int label_len = strlen(label);
      int char_w = 6;  // 5px + 1px spacing
      int char_h = 9;  // 7px + 2px padding
      int label_w = label_len * char_w + 2;
      int label_h = char_h + 2;

      // Position: above the box, or inside if no room
      int label_x = x1;
      int label_y = y1 - label_h;
      if (label_y < 0) label_y = y1 + 2;  // Inside box if no room above

      // Draw label background (filled rectangle in box color)
      uint16_t *buffer = (uint16_t *)img_data;
      for (int by = std::max(0, label_y); by < std::min((int)height, label_y + label_h); by++) {
        for (int bx = std::max(0, label_x); bx < std::min((int)width, label_x + label_w); bx++) {
          buffer[by * width + bx] = color;
        }
      }

      // Draw text in white on colored background
      const uint16_t COLOR_WHITE = 0xFFFF;
      this->draw_text_(img_data, width, height, label_x + 1, label_y + 1, label, COLOR_WHITE, 1);
    }

    if (!this->cached_detections_.empty()) {
      ESP_LOGD(TAG, "Drew %d detection boxes", (int)this->cached_detections_.size());
    }
    xSemaphoreGive(this->detections_mutex_);
  }
}

void YOLO11DetectionComponent::draw_char_(uint8_t *img_data, uint16_t img_width, uint16_t img_height,
                                           int x, int y, char c, uint16_t color, int scale) {
  int font_idx = -1;

  if (c >= 'A' && c <= 'Z') {
    font_idx = c - 'A';
  } else if (c >= 'a' && c <= 'z') {
    font_idx = c - 'a';  // Map to uppercase
  } else if (c >= '0' && c <= '9') {
    font_idx = 26 + (c - '0');
  } else if (c == ' ') {
    font_idx = 36;
  } else if (c == ':') {
    font_idx = 37;
  } else if (c == ',') {
    font_idx = 38;
  } else if (c == '%') {
    font_idx = 39;
  } else if (c == '.') {
    font_idx = 40;
  }

  if (font_idx < 0) return;

  int char_w = 5 * scale;
  int char_h = 7 * scale;
  if (x + char_w <= 0 || x >= img_width || y + char_h <= 0 || y >= img_height) return;

  uint16_t *buffer = (uint16_t *)img_data;

  for (int row = 0; row < 7; row++) {
    uint8_t row_data = FONT_5X7[font_idx][row];
    for (int col = 0; col < 5; col++) {
      if (row_data & (0x10 >> col)) {
        for (int sy = 0; sy < scale; sy++) {
          int py = y + row * scale + sy;
          if (py < 0 || py >= img_height) continue;
          for (int sx = 0; sx < scale; sx++) {
            int px = x + col * scale + sx;
            if (px >= 0 && px < img_width) {
              buffer[py * img_width + px] = color;
            }
          }
        }
      }
    }
  }
}

void YOLO11DetectionComponent::draw_text_(uint8_t *img_data, uint16_t img_width, uint16_t img_height,
                                           int x, int y, const char *text, uint16_t color, int scale) {
  int char_width = 6 * scale;  // 5 pixels + 1 spacing
  int current_x = x;

  for (const char *p = text; *p != '\0'; p++) {
    if (current_x + 5 * scale >= img_width) break;
    this->draw_char_(img_data, img_width, img_height, current_x, y, *p, color, scale);
    current_x += char_width;
  }
}

void YOLO11DetectionComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLO11 Object Detection:");
#ifdef USE_YOLO11_ESP32_CAMERA
  ESP_LOGCONFIG(TAG, "  Camera: ESP32 Camera");
#endif
#ifdef USE_YOLO11_MIPI_CAMERA
  ESP_LOGCONFIG(TAG, "  Camera: MIPI DSI Camera (esp_cam_sensor)");
#endif
  ESP_LOGCONFIG(TAG, "  Model: direct ESP-DL (flash rodata)");
  ESP_LOGCONFIG(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGCONFIG(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  ESP_LOGCONFIG(TAG, "  Detection interval: %d frames", this->detection_interval_);
  ESP_LOGCONFIG(TAG, "  Draw enabled: %s", this->draw_enabled_ ? "YES" : "NO");
  if (!this->detect_classes_.empty()) {
    ESP_LOGCONFIG(TAG, "  Class filter: %d classes", (int)this->detect_classes_.size());
  } else {
    ESP_LOGCONFIG(TAG, "  Class filter: all 80 COCO classes");
  }
  ESP_LOGCONFIG(TAG, "  Initialized: %s", this->detector_initialized_ ? "YES" : "NO");
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
