#include "face_detection.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esp_cache.h"

// ESP-DL detection components (only for face_recognition model)
#ifdef ESP_DL_MODEL_FACE_RECOGNITION
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#endif

#include "dl_image.hpp"

// File I/O for name persistence
#include <fstream>
#include <sstream>
#include <cstdio>

namespace esphome {
namespace face_detection {

static const char *const TAG = "face_detection";

void FaceDetectionComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Face Detection...");

#ifndef ESP_DL_MODEL_FACE_RECOGNITION
  ESP_LOGE(TAG, "Face Detection component requires model_type: face_recognition");
  ESP_LOGE(TAG, "Current model_type does not support face detection");
  this->mark_failed();
  return;
#else

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not configured");
    this->mark_failed();
    return;
  }

  // Create mutex for thread-safe access to cached results
  this->face_results_mutex_ = xSemaphoreCreateMutex();
  if (this->face_results_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create face results mutex");
    this->mark_failed();
    return;
  }

  // Initialize face detector
  ESP_LOGI(TAG, "Initializing face detector...");

#if CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
  if (this->sdcard_model_path_ != nullptr) {
    ESP_LOGI(TAG, "Waiting for SD card to mount (6 seconds)...");
    delay(6000);
    ESP_LOGI(TAG, "Loading face detection model from SD card: %s", this->sdcard_model_path_);
    this->face_detector_ = new HumanFaceDetect(this->sdcard_model_path_);
  } else {
    ESP_LOGW(TAG, "SD card mode enabled but no model_path provided, using default /sdcard");
    delay(6000);
    this->face_detector_ = new HumanFaceDetect("/sdcard");
  }
#else
  ESP_LOGI(TAG, "Loading face detection model from flash rodata");
  this->face_detector_ = new HumanFaceDetect();
#endif

  if (this->face_detector_ != nullptr) {
    this->face_detector_->set_score_thr(this->score_threshold_);
    this->face_detector_->set_nms_thr(this->nms_threshold_);
    ESP_LOGI(TAG, "Face detector initialized (score_thr=%.2f, nms_thr=%.2f)",
             this->score_threshold_, this->nms_threshold_);
  } else {
    ESP_LOGE(TAG, "Failed to initialize face detector");
    this->mark_failed();
    return;
  }

  // Initialize face recognizer if enabled
  if (this->recognition_enabled_) {
    ESP_LOGI(TAG, "Initializing face recognition...");
    ESP_LOGI(TAG, "  Database path: %s", this->face_db_path_.c_str());
    ESP_LOGI(TAG, "  Recognition threshold: %.2f", this->recognition_threshold_);

    this->face_recognizer_ = new HumanFaceRecognizer(
      this->face_db_path_.c_str(),
      nullptr,
      HumanFaceFeat::MFN_S8_V1,
      false
    );

    if (this->face_recognizer_ != nullptr) {
      int enrolled = this->face_recognizer_->get_num_feats();
      ESP_LOGI(TAG, "Face recognizer initialized (%d faces enrolled)", enrolled);
    } else {
      ESP_LOGE(TAG, "Failed to initialize face recognizer");
      this->recognition_enabled_ = false;
    }
  }

  // Load face names from SD card
  if (this->recognition_enabled_) {
    this->load_names_from_sd_();
  }

  ESP_LOGI(TAG, "Face Detection ready");
  ESP_LOGI(TAG, "  Detection interval: every %d frames", this->detection_interval_);
  ESP_LOGI(TAG, "  Recognition: %s", this->recognition_enabled_ ? "ENABLED" : "DISABLED");
  ESP_LOGI(TAG, "  Draw boxes: %s", this->draw_enabled_ ? "YES" : "NO");
#endif
}

void FaceDetectionComponent::loop() {
  // Check if camera is streaming
  if (this->camera_ == nullptr || !this->camera_->is_streaming()) {
    // Periodic diagnostic: log why we're not running
    static uint32_t last_not_streaming_log = 0;
    if (millis() - last_not_streaming_log > 5000) {
      last_not_streaming_log = millis();
      if (this->camera_ == nullptr) {
        ESP_LOGW(TAG, "DIAG: camera_ is null - cannot detect");
      } else {
        ESP_LOGD(TAG, "DIAG: camera not streaming - waiting for start_streaming()");
      }
    }
    return;
  }

  this->process_frame_();
}

void FaceDetectionComponent::process_frame_() {
  this->frame_counter_++;

  // Only run detection every N frames
  if (this->frame_counter_ < this->detection_interval_) {
    return;
  }

  this->frame_counter_ = 0;

  // Acquire buffer from camera
  esp_cam_sensor::SimpleBufferElement *buffer = this->camera_->acquire_buffer();
  if (buffer == nullptr) {
    // Periodic diagnostic: log buffer acquisition failure
    static uint32_t acquire_fail_count = 0;
    static uint32_t last_acquire_fail_log = 0;
    acquire_fail_count++;
    if (millis() - last_acquire_fail_log > 3000) {
      last_acquire_fail_log = millis();
      ESP_LOGW(TAG, "DIAG: acquire_buffer failed %u times (buffer pool exhausted?)", acquire_fail_count);
      acquire_fail_count = 0;
    }
    return;
  }

  uint8_t *img_data = this->camera_->get_buffer_data(buffer);
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data != nullptr) {
    // ESP32-P4: Invalidate CPU cache before reading PSRAM buffer.
    // Camera DMA writes directly to PSRAM, but CPU cache may hold stale data.
    // Without this, ESP-DL reads garbage and never detects faces.
    uint32_t buf_size = width * height * 2;  // RGB565
    esp_cache_msync(img_data, buf_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

    this->detect_faces_(img_data, width, height);
    // NOTE: Don't draw here to avoid flickering!
    // Drawing is done by lvgl_camera_display via draw_on_frame()
  }

  this->camera_->release_buffer(buffer);
}

void FaceDetectionComponent::draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (img_data == nullptr) return;

  // DIAG: Always draw a bright green test rectangle at top-left corner
  // This verifies the entire drawing pipeline works (face_detection → display)
  // Remove this block once face detection is confirmed working
  {
    dl::image::img_t test_img = {
      .data = img_data,
      .width = width,
      .height = height,
      .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
    };
    std::vector<uint8_t> green = {0xE0, 0x07};  // Bright green RGB565
    dl::image::draw_hollow_rectangle(test_img, 10, 10, 100, 100, green, 4);

    // Also draw cached face count as indicator
    int face_count = 0;
    if (xSemaphoreTake(this->face_results_mutex_, pdMS_TO_TICKS(2)) == pdTRUE) {
      face_count = this->cached_face_results_.size();
      xSemaphoreGive(this->face_results_mutex_);
    }
    // Draw red rectangle if no faces detected, blue if faces detected
    std::vector<uint8_t> indicator = face_count > 0 ?
      std::vector<uint8_t>{0x1F, 0x00} :   // Blue = faces found
      std::vector<uint8_t>{0x00, 0xF8};     // Red = no faces
    dl::image::draw_hollow_rectangle(test_img, 10, 110, 100, 150, indicator, 4);
  }

  if (this->draw_enabled_) {
    this->draw_results_(img_data, width, height);
  }
}

void FaceDetectionComponent::detect_faces_(uint8_t *img_data, uint16_t width, uint16_t height) {
#ifdef ESP_DL_MODEL_FACE_RECOGNITION
  if (this->face_detector_ == nullptr) {
    static bool logged_null = false;
    if (!logged_null) {
      ESP_LOGE(TAG, "DIAG: face_detector_ is null - detection disabled");
      logged_null = true;
    }
    return;
  }

  // Periodic diagnostic logging
  static uint32_t detect_calls = 0;
  static uint32_t detect_found = 0;
  static uint32_t last_detect_log = 0;
  detect_calls++;

  // DIAG: Log pixel values to verify buffer content is valid camera data
  if (detect_calls <= 3 || (millis() - last_detect_log > 10000)) {
    uint16_t *pixels = (uint16_t *)img_data;
    uint32_t mid = (height / 2) * width + (width / 2);  // center pixel
    ESP_LOGI(TAG, "DIAG pixels: [0]=%04x [1]=%04x [100]=%04x [center]=%04x [last]=%04x",
             pixels[0], pixels[1], pixels[100], pixels[mid], pixels[width * height - 1]);

    // Check if buffer is all zeros (invalid)
    bool all_zero = true;
    for (int i = 0; i < 1000 && i < width * height; i += 37) {
      if (pixels[i] != 0) { all_zero = false; break; }
    }
    if (all_zero) {
      ESP_LOGE(TAG, "DIAG: Buffer appears to be ALL ZEROS - camera data not received!");
    }
  }

  // Create image structure for ESP-DL
  dl::image::img_t img = {
    .data = img_data,
    .width = width,
    .height = height,
    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
  };

  // Run face detection
  std::list<dl::detect::result_t> &face_results = this->face_detector_->run(img);

  if (face_results.size() > 0) {
    detect_found++;
    ESP_LOGI(TAG, "DETECTED %d face(s)! (run #%u, img=%ux%u, data=%p)",
             (int)face_results.size(), detect_calls, width, height, img_data);
  }

  // Periodic stats every 10 seconds
  if (millis() - last_detect_log > 10000) {
    last_detect_log = millis();
    ESP_LOGI(TAG, "DIAG: %u detection runs, %u found faces, img=%ux%u, data=%p",
             detect_calls, detect_found, width, height, img_data);
    detect_calls = 0;
    detect_found = 0;
  }

  // Cache results (mutex protected)
  if (xSemaphoreTake(this->face_results_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
    this->cached_face_results_.clear();

    for (auto &result : face_results) {
      FaceBox box;
      box.x1 = result.box[0];
      box.y1 = result.box[1];
      box.x2 = result.box[2];
      box.y2 = result.box[3];
      box.score = result.score;

      for (int i = 0; i < 10; i++) {
        box.keypoints[i] = result.keypoint[i];
      }

      this->cached_face_results_.push_back(box);
    }

    xSemaphoreGive(this->face_results_mutex_);
  }

  // Trigger face detected callback
  if (face_results.size() > 0) {
    for (auto &callback : this->on_face_detected_callbacks_) {
      callback(face_results.size());
    }
  }

  // Face recognition (if enabled and faces detected)
  if (this->recognition_enabled_ && this->face_recognizer_ != nullptr && face_results.size() > 0) {
    auto &first_face_result = face_results.front();

    // Check if enrollment is pending
    if (this->enroll_pending_) {
      int new_id = this->face_recognizer_->enroll(img, first_face_result);
      if (new_id >= 0) {
        // ESP-DL returns ID from enroll, but recognition returns ID+1
        // So we save the name with ID+1 to match recognition results
        int recognition_id = new_id + 1;
        ESP_LOGI(TAG, "Face enrolled with ID: %d (recognition ID: %d)", new_id, recognition_id);
        // Save name if provided
        if (!this->pending_enroll_name_.empty()) {
          this->face_names_[recognition_id] = this->pending_enroll_name_;
          ESP_LOGI(TAG, "Name '%s' saved for ID %d", this->pending_enroll_name_.c_str(), recognition_id);
          this->pending_enroll_name_.clear();
          this->save_names_to_sd_();
        }
      } else {
        ESP_LOGE(TAG, "Failed to enroll face");
      }
      this->enroll_pending_ = false;
    } else {
      // Try to recognize
      dl::recognition::result_t *rec_result = this->face_recognizer_->recognize(img, first_face_result);
      if (rec_result != nullptr && rec_result->similarity >= this->recognition_threshold_) {
        this->last_recognition_.id = rec_result->id;
        this->last_recognition_.similarity = rec_result->similarity;
        this->last_recognition_.recognized = true;

        ESP_LOGI(TAG, "Face RECOGNIZED! ID=%d, similarity=%.2f",
                 rec_result->id, rec_result->similarity);

        // Trigger callbacks
        for (auto &callback : this->on_face_recognized_callbacks_) {
          callback(rec_result->id, rec_result->similarity);
        }
      } else {
        this->last_recognition_.recognized = false;
      }
    }
  }
#endif
}

void FaceDetectionComponent::draw_results_(uint8_t *img_data, uint16_t width, uint16_t height) {
#ifdef ESP_DL_MODEL_FACE_RECOGNITION
  if (img_data == nullptr || this->face_results_mutex_ == nullptr) {
    return;
  }

  // Create image structure for ESP-DL drawing
  dl::image::img_t img = {
    .data = img_data,
    .width = width,
    .height = height,
    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
  };

  if (xSemaphoreTake(this->face_results_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    // RGB565 colors (little-endian)
    std::vector<uint8_t> green = {0xE0, 0x07};   // Green - unknown face
    std::vector<uint8_t> blue = {0x1F, 0x00};    // Blue - recognized face
    std::vector<uint8_t> red = {0x00, 0xF8};     // Red for keypoints

    for (auto &box : this->cached_face_results_) {
      // Clamp bounding box coordinates to valid range
      int x1 = std::max(3, std::min((int)box.x1, (int)width - 4));
      int y1 = std::max(3, std::min((int)box.y1, (int)height - 4));
      int x2 = std::max(x1 + 1, std::min((int)box.x2, (int)width - 4));
      int y2 = std::max(y1 + 1, std::min((int)box.y2, (int)height - 4));

      // Choose color based on recognition status
      std::vector<uint8_t> &box_color = this->last_recognition_.recognized ? blue : green;

      // Draw bounding box (thicker for recognized faces)
      int line_width = this->last_recognition_.recognized ? 4 : 3;
      dl::image::draw_hollow_rectangle(img, x1, y1, x2, y2, box_color, line_width);

      // Draw name above the box if recognized
      if (this->last_recognition_.recognized) {
        std::string name = this->get_face_name(this->last_recognition_.id);
        if (name.empty()) {
          // Show ID if no name set
          name = "ID " + std::to_string(this->last_recognition_.id);
        }
        // Draw name above the box with white color, scale 2
        std::vector<uint8_t> white = {0xFF, 0xFF};
        int text_y = std::max(2, y1 - 18);  // 18 pixels above box
        this->draw_text_(img_data, width, height, x1, text_y, name, white, 2);
      }

      // Draw red keypoints (5 facial landmarks) as hollow rectangles
      for (int i = 0; i < 5; i++) {
        int kp_x = box.keypoints[i * 2];
        int kp_y = box.keypoints[i * 2 + 1];

        // Check bounds with margin
        if (kp_x >= 12 && kp_y >= 12 && kp_x < (int)width - 12 && kp_y < (int)height - 12) {
          // Draw hollow rectangle (not filled)
          int size = 10;  // 20x20 pixel rectangle
          int rx1 = kp_x - size;
          int ry1 = kp_y - size;
          int rx2 = kp_x + size;
          int ry2 = kp_y + size;
          dl::image::draw_hollow_rectangle(img, rx1, ry1, rx2, ry2, red, 2);
        }
      }
    }

    xSemaphoreGive(this->face_results_mutex_);
  }
#endif
}

void FaceDetectionComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Face Detection:");
  ESP_LOGCONFIG(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGCONFIG(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  ESP_LOGCONFIG(TAG, "  Detection interval: %d frames", this->detection_interval_);
  ESP_LOGCONFIG(TAG, "  Draw enabled: %s", this->draw_enabled_ ? "YES" : "NO");
#ifdef CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
  ESP_LOGCONFIG(TAG, "  Model location: SD card");
  if (this->sdcard_model_path_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Model path: %s", this->sdcard_model_path_);
  }
#else
  ESP_LOGCONFIG(TAG, "  Model location: flash rodata");
#endif
  ESP_LOGCONFIG(TAG, "  Recognition enabled: %s", this->recognition_enabled_ ? "YES" : "NO");
  if (this->recognition_enabled_) {
    ESP_LOGCONFIG(TAG, "  Face DB path: %s", this->face_db_path_.c_str());
    ESP_LOGCONFIG(TAG, "  Recognition threshold: %.2f", this->recognition_threshold_);
  }
}

int FaceDetectionComponent::get_detected_face_count() {
  int count = 0;
  if (xSemaphoreTake(this->face_results_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    count = this->cached_face_results_.size();
    xSemaphoreGive(this->face_results_mutex_);
  }
  return count;
}

std::vector<FaceBox> FaceDetectionComponent::get_detected_faces() {
  std::vector<FaceBox> faces;
  if (xSemaphoreTake(this->face_results_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    faces = this->cached_face_results_;
    xSemaphoreGive(this->face_results_mutex_);
  }
  return faces;
}

int FaceDetectionComponent::enroll_face() {
#ifdef ESP_DL_MODEL_FACE_RECOGNITION
  if (!this->recognition_enabled_ || this->face_recognizer_ == nullptr) {
    ESP_LOGE(TAG, "Face recognition not enabled or not initialized");
    return -1;
  }

  ESP_LOGI(TAG, "Enrollment requested - will capture on next face detection");
  this->enroll_pending_ = true;
  return 0;
#else
  ESP_LOGE(TAG, "Face recognition not available (requires model_type: face_recognition)");
  return -1;
#endif
}

bool FaceDetectionComponent::delete_face(int id) {
#ifdef ESP_DL_MODEL_FACE_RECOGNITION
  if (!this->recognition_enabled_ || this->face_recognizer_ == nullptr) {
    ESP_LOGE(TAG, "Face recognition not enabled or not initialized");
    return false;
  }

  bool success = this->face_recognizer_->delete_feat(id);
  if (success) {
    ESP_LOGI(TAG, "Face ID %d deleted", id);
    // Also remove the name if exists
    auto it = this->face_names_.find(id);
    if (it != this->face_names_.end()) {
      this->face_names_.erase(it);
      this->save_names_to_sd_();
    }
  }
  return success;
#else
  ESP_LOGE(TAG, "Face recognition not available (requires model_type: face_recognition)");
  return false;
#endif
}

void FaceDetectionComponent::clear_all_faces() {
#ifdef ESP_DL_MODEL_FACE_RECOGNITION
  if (!this->recognition_enabled_ || this->face_recognizer_ == nullptr) {
    ESP_LOGE(TAG, "Face recognition not enabled or not initialized");
    return;
  }

  // Clear in-memory features
  this->face_recognizer_->clear_all_feats();

  // Clear names
  this->face_names_.clear();
  this->save_names_to_sd_();

  // Delete the database file from SD card
  if (std::remove(this->face_db_path_.c_str()) == 0) {
    ESP_LOGI(TAG, "Deleted face database: %s", this->face_db_path_.c_str());
  } else {
    ESP_LOGW(TAG, "Could not delete face database (may not exist): %s", this->face_db_path_.c_str());
  }

  // Reinitialize the recognizer with empty database
  delete this->face_recognizer_;
  this->face_recognizer_ = new HumanFaceRecognizer(
    this->face_db_path_.c_str(),
    nullptr,
    HumanFaceFeat::MFN_S8_V1,
    false
  );

  ESP_LOGI(TAG, "All faces and names cleared, database reset");
#else
  ESP_LOGE(TAG, "Face recognition not available (requires model_type: face_recognition)");
#endif
}

int FaceDetectionComponent::get_enrolled_count() {
#ifdef ESP_DL_MODEL_FACE_RECOGNITION
  if (!this->recognition_enabled_ || this->face_recognizer_ == nullptr) {
    return 0;
  }
  return this->face_recognizer_->get_num_feats();
#else
  return 0;
#endif
}

RecognitionResult FaceDetectionComponent::get_last_recognition() {
  return this->last_recognition_;
}

void FaceDetectionComponent::reset_last_recognition() {
  this->last_recognition_.id = -1;
  this->last_recognition_.similarity = 0.0f;
  this->last_recognition_.recognized = false;
  ESP_LOGI(TAG, "Recognition result reset");
}

int FaceDetectionComponent::enroll_face_with_name(const std::string &name) {
#ifdef ESP_DL_MODEL_FACE_RECOGNITION
  if (!this->recognition_enabled_ || this->face_recognizer_ == nullptr) {
    ESP_LOGE(TAG, "Face recognition not enabled or not initialized");
    return -1;
  }

  ESP_LOGI(TAG, "Enrollment requested with name '%s' - will capture on next face detection", name.c_str());
  this->pending_enroll_name_ = name;
  this->enroll_pending_ = true;
  return 0;
#else
  ESP_LOGE(TAG, "Face recognition not available (requires model_type: face_recognition)");
  return -1;
#endif
}

void FaceDetectionComponent::set_face_name(int id, const std::string &name) {
  this->face_names_[id] = name;
  ESP_LOGI(TAG, "Set name for face ID %d: %s", id, name.c_str());
  this->save_names_to_sd_();
}

std::string FaceDetectionComponent::get_face_name(int id) {
  auto it = this->face_names_.find(id);
  if (it != this->face_names_.end()) {
    return it->second;
  }
  return "";
}

std::string FaceDetectionComponent::get_last_recognized_name() {
  if (this->last_recognition_.recognized) {
    return this->get_face_name(this->last_recognition_.id);
  }
  return "";
}

// Simple 5x7 bitmap font for digits and uppercase letters
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
  // Space (index 36)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Space
};

void FaceDetectionComponent::draw_char_(uint8_t *img_data, uint16_t img_width, uint16_t img_height,
                                        int x, int y, char c, const std::vector<uint8_t> &color, int scale) {
  int font_idx = -1;

  if (c >= 'A' && c <= 'Z') {
    font_idx = c - 'A';
  } else if (c >= 'a' && c <= 'z') {
    font_idx = c - 'a';  // Convert to uppercase
  } else if (c >= '0' && c <= '9') {
    font_idx = 26 + (c - '0');
  } else if (c == ' ') {
    font_idx = 36;
  }

  if (font_idx < 0) return;

  for (int row = 0; row < 7; row++) {
    uint8_t row_data = FONT_5X7[font_idx][row];
    for (int col = 0; col < 5; col++) {
      if (row_data & (0x10 >> col)) {
        // Draw scaled pixel
        for (int sy = 0; sy < scale; sy++) {
          for (int sx = 0; sx < scale; sx++) {
            int px = x + col * scale + sx;
            int py = y + row * scale + sy;
            if (px >= 0 && px < img_width && py >= 0 && py < img_height) {
              int offset = (py * img_width + px) * 2;  // RGB565 = 2 bytes
              img_data[offset] = color[0];
              img_data[offset + 1] = color[1];
            }
          }
        }
      }
    }
  }
}

void FaceDetectionComponent::draw_text_(uint8_t *img_data, uint16_t img_width, uint16_t img_height,
                                        int x, int y, const std::string &text,
                                        const std::vector<uint8_t> &color, int scale) {
  int char_width = 6 * scale;  // 5 pixels + 1 spacing
  int current_x = x;

  for (char c : text) {
    if (current_x + 5 * scale >= img_width) break;  // Stop if out of bounds
    draw_char_(img_data, img_width, img_height, current_x, y, c, color, scale);
    current_x += char_width;
  }
}

std::string FaceDetectionComponent::get_names_file_path_() {
  // Create names file path next to face database
  // e.g., /sdcard/reconnaisance_faciale/faces.db -> /sdcard/reconnaisance_faciale/faces_names.txt
  std::string names_path = this->face_db_path_;
  size_t dot_pos = names_path.rfind('.');
  if (dot_pos != std::string::npos) {
    names_path = names_path.substr(0, dot_pos) + "_names.txt";
  } else {
    names_path += "_names.txt";
  }
  return names_path;
}

void FaceDetectionComponent::load_names_from_sd_() {
  std::string names_path = this->get_names_file_path_();

  std::ifstream file(names_path);
  if (!file.is_open()) {
    ESP_LOGI(TAG, "No names file found at %s (will create on first save)", names_path.c_str());
    return;
  }

  this->face_names_.clear();
  std::string line;
  int loaded_count = 0;

  while (std::getline(file, line)) {
    // Format: ID:NAME
    size_t colon_pos = line.find(':');
    if (colon_pos != std::string::npos && colon_pos > 0) {
      std::string id_str = line.substr(0, colon_pos);
      std::string name = line.substr(colon_pos + 1);

      // Check if id_str contains only digits
      bool valid_id = true;
      for (char c : id_str) {
        if (c < '0' || c > '9') {
          valid_id = false;
          break;
        }
      }

      if (valid_id && !id_str.empty()) {
        int id = std::stoi(id_str);
        this->face_names_[id] = name;
        loaded_count++;
        ESP_LOGD(TAG, "Loaded name: ID=%d, Name=%s", id, name.c_str());
      } else {
        ESP_LOGW(TAG, "Invalid line in names file: %s", line.c_str());
      }
    }
  }

  file.close();
  ESP_LOGI(TAG, "Loaded %d face names from %s", loaded_count, names_path.c_str());
}

void FaceDetectionComponent::save_names_to_sd_() {
  std::string names_path = this->get_names_file_path_();

  std::ofstream file(names_path);
  if (!file.is_open()) {
    ESP_LOGE(TAG, "Failed to open names file for writing: %s", names_path.c_str());
    return;
  }

  int saved_count = 0;
  for (const auto &pair : this->face_names_) {
    file << pair.first << ":" << pair.second << "\n";
    saved_count++;
  }

  file.close();
  ESP_LOGI(TAG, "Saved %d face names to %s", saved_count, names_path.c_str());
}

}  // namespace face_detection
}  // namespace esphome
