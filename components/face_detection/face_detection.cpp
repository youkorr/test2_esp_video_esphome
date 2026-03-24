#include "face_detection.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

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
#include <sys/stat.h>

namespace esphome {
namespace face_detection {

static const char *const TAG = "face_detection";

// Ensure parent directory exists for a file path (creates one level)
static bool ensure_parent_dir_(const std::string &file_path) {
  size_t last_slash = file_path.rfind('/');
  if (last_slash == std::string::npos || last_slash == 0) return true;
  std::string dir = file_path.substr(0, last_slash);
  struct stat st;
  if (stat(dir.c_str(), &st) == 0) {
    return true;  // Directory already exists
  }
  if (mkdir(dir.c_str(), 0755) == 0) {
    ESP_LOGI(TAG, "Created directory: %s", dir.c_str());
    return true;
  }
  ESP_LOGE(TAG, "Failed to create directory: %s", dir.c_str());
  return false;
}

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

  // Pre-reserve capacity for cached results to avoid dynamic reallocation
  this->cached_face_results_.reserve(8);

  // Initialize face detector
  uint32_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  uint32_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  ESP_LOGI(TAG, "Heap before model load: PSRAM=%u KB free, internal=%u KB free",
           psram_before / 1024, internal_before / 1024);

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
  this->face_detector_ = new HumanFaceDetect();
#endif

  uint32_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  uint32_t internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  ESP_LOGI(TAG, "Heap after model load: PSRAM=%u KB free (used %u KB), internal=%u KB free (used %u KB)",
           psram_after / 1024, (psram_before - psram_after) / 1024,
           internal_after / 1024, (internal_before - internal_after) / 1024);
  ESP_LOGI(TAG, "Largest free PSRAM block: %u KB",
           heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024);

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

    // Ensure the database directory exists on SD card
    if (!ensure_parent_dir_(this->face_db_path_)) {
      ESP_LOGE(TAG, "Cannot create database directory - recognition disabled");
      this->recognition_enabled_ = false;
    }

    // Check for existing corrupt database file BEFORE creating recognizer.
    // A corrupt DB causes enroll()/recognize() to deadlock (watchdog crash).
    // Delete any suspicious file so the recognizer starts fresh.
    bool had_existing_db = false;
    {
      struct stat db_stat;
      if (stat(this->face_db_path_.c_str(), &db_stat) == 0) {
        had_existing_db = true;
        ESP_LOGI(TAG, "  Existing database file found (%ld bytes)", (long)db_stat.st_size);
        // A valid face DB with even 0 enrolled faces has a proper header.
        // Files < 100 bytes are always corrupt (partial header/metadata).
        if (db_stat.st_size < 100) {
          ESP_LOGW(TAG, "  Database file too small (%ld bytes) - deleting corrupt file", (long)db_stat.st_size);
          std::remove(this->face_db_path_.c_str());
          had_existing_db = false;
        }
      }
    }

    // WDT protection: HumanFaceRecognizer constructor can take several seconds
    // loading model weights, which would trigger the 5s task watchdog.
    esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

    this->face_recognizer_ = new HumanFaceRecognizer(
      this->face_db_path_.c_str(),
      nullptr,
      HumanFaceFeat::MFN_S8_V1,
      false
    );

    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    esp_task_wdt_reset();

    if (this->face_recognizer_ != nullptr) {
      int enrolled = this->face_recognizer_->get_num_feats();
      ESP_LOGI(TAG, "Face recognizer initialized (%d faces enrolled)", enrolled);

      // Only validate if a DB file existed BEFORE we created the recognizer.
      // After a fresh creation, the constructor may create a new empty DB - that's normal.
      if (had_existing_db && enrolled > 0) {
        struct stat db_stat;
        if (stat(this->face_db_path_.c_str(), &db_stat) == 0 &&
            db_stat.st_size < (enrolled * 512 + 64)) {
          ESP_LOGW(TAG, "DB claims %d faces but file is only %ld bytes - corrupt!",
                   enrolled, (long)db_stat.st_size);
          ESP_LOGW(TAG, "Deleting corrupt database and recreating...");
          delete this->face_recognizer_;
          this->face_recognizer_ = nullptr;
          std::remove(this->face_db_path_.c_str());

          esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
          this->face_recognizer_ = new HumanFaceRecognizer(
            this->face_db_path_.c_str(),
            nullptr,
            HumanFaceFeat::MFN_S8_V1,
            false
          );
          esp_task_wdt_add(xTaskGetCurrentTaskHandle());
          esp_task_wdt_reset();

          if (this->face_recognizer_ != nullptr) {
            ESP_LOGI(TAG, "Face recognizer re-created with fresh database");
          } else {
            ESP_LOGE(TAG, "Failed to re-create face recognizer");
            this->recognition_enabled_ = false;
          }
        }
      }
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
  if (this->camera_ == nullptr || !this->camera_->is_streaming()) {
    return;
  }

  this->process_frame_();

  // Periodic memory diagnostics (every ~500 detection cycles)
  this->diag_counter_++;
  if (this->diag_counter_ % (500 * this->detection_interval_) == 0) {
    uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Memory: PSRAM=%uKB free (largest=%uKB), internal=%uKB free",
             psram_free / 1024, psram_largest / 1024, internal_free / 1024);
  }
}

void FaceDetectionComponent::process_frame_() {
  this->frame_counter_++;

  // Only run detection every N frames
  if (this->frame_counter_ < this->detection_interval_) {
    return;
  }

  this->frame_counter_ = 0;

  esp_cam_sensor::SimpleBufferElement *buffer = this->camera_->acquire_buffer();
  if (buffer == nullptr) {
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

  if (this->draw_enabled_) {
    this->draw_results_(img_data, width, height);
  }
}

void FaceDetectionComponent::detect_faces_(uint8_t *img_data, uint16_t width, uint16_t height) {
#ifdef ESP_DL_MODEL_FACE_RECOGNITION
  if (this->face_detector_ == nullptr) {
    return;
  }

  // Post-enrollment cooldown: skip detection to let ESP-DL internal state settle
  // and give LVGL time to catch up after the long enrollment blocking period.
  if (this->post_enroll_cooldown_ > 0) {
    this->post_enroll_cooldown_--;
    ESP_LOGD(TAG, "Post-enrollment cooldown: %d cycles remaining", this->post_enroll_cooldown_);
    return;
  }

  dl::image::img_t img = {
    .data = img_data,
    .width = width,
    .height = height,
    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
  };

  // Debug: log image brightness on first detection
  static bool face_img_logged = false;
  if (!face_img_logged) {
    face_img_logged = true;
    uint8_t *raw = img_data;
    uint32_t total_pixels = width * height;
    uint32_t r_sum_be = 0, g_sum_be = 0, b_sum_be = 0;
    uint32_t sample_count = std::min(total_pixels, (uint32_t)10000);
    uint32_t step = total_pixels / sample_count;
    for (uint32_t i = 0; i < total_pixels; i += step) {
      // Read as BE RGB565: byte[0]=MSB, byte[1]=LSB
      uint8_t b0 = raw[i * 2];
      uint8_t b1 = raw[i * 2 + 1];
      // BE extraction (same as esp-dl extract_channel*_from_rgb565be)
      r_sum_be += (b0 & 0xF8);         // R: top 5 bits of byte 0
      g_sum_be += ((b0 & 0x07) << 5) | ((b1 >> 5) & 0x07);  // G: split across bytes
      b_sum_be += ((b1 & 0x1F) << 3);  // B: low 5 bits of byte 1
    }
    ESP_LOGI(TAG, "Face detect input: %ux%u, raw bytes: %02X %02X %02X %02X %02X %02X",
             width, height, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);
    ESP_LOGI(TAG, "  Avg RGB (BE): (%.0f, %.0f, %.0f) / 255",
             (float)r_sum_be / sample_count, (float)g_sum_be / sample_count, (float)b_sum_be / sample_count);
  }

  // WDT protection
  esp_task_wdt_reset();
  std::list<dl::detect::result_t> &face_results = this->face_detector_->run(img);
  esp_task_wdt_reset();

  // Debug: log detection results
  static int face_log_count = 0;
  if (face_log_count < 20) {
    face_log_count++;
    ESP_LOGI(TAG, "Face detection result: %d faces found (frame %d)", (int)face_results.size(), face_log_count);
    for (auto &r : face_results) {
      ESP_LOGI(TAG, "  Face: box=[%d,%d,%d,%d] score=%.3f", r.box[0], r.box[1], r.box[2], r.box[3], r.score);
    }
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

  // Reset recognition state when no face is detected (person walked away)
  if (face_results.empty()) {
    if (this->last_recognition_.recognized) {
      this->last_recognition_.recognized = false;
      this->last_recognition_.id = -1;
      this->last_recognition_.similarity = 0.0f;
      this->cached_recognized_name_.clear();
      this->cached_recognized_id_ = -1;
    }
  }

  // Face recognition (if enabled and faces detected)
  if (this->recognition_enabled_ && this->face_recognizer_ != nullptr && face_results.size() > 0) {
    // COPY face result (not reference) - recognize() may invalidate detector internals
    dl::detect::result_t first_face_result = face_results.front();

    // Check if enrollment is pending
    if (this->enroll_pending_) {
      ESP_LOGI(TAG, "Enrolling face (score=%.2f, box=[%d,%d,%d,%d])...",
               first_face_result.score,
               first_face_result.box[0], first_face_result.box[1],
               first_face_result.box[2], first_face_result.box[3]);

      // Remove main task from WDT monitoring - enrollment can take several seconds
      // and would otherwise trigger the 5s task watchdog.
      esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

      int new_id = this->face_recognizer_->enroll(img, first_face_result);

      // Re-add task to WDT monitoring
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
      esp_task_wdt_reset();

      // If enrollment failed, likely stale/incompatible DB.
      // Auto-recover: delete DB, recreate recognizer, retry once.
      if (new_id < 0) {
        ESP_LOGW(TAG, "Enrollment failed (returned %d) - attempting DB reset and retry...", new_id);

        // Safely recreate recognizer with fresh database
        HumanFaceRecognizer *old = this->face_recognizer_;
        this->face_recognizer_ = nullptr;
        delete old;

        std::remove(this->face_db_path_.c_str());
        ensure_parent_dir_(this->face_db_path_);

        // WDT protection: constructor loads model weights
        esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
        this->face_recognizer_ = new HumanFaceRecognizer(
          this->face_db_path_.c_str(),
          nullptr,
          HumanFaceFeat::MFN_S8_V1,
          false
        );
        esp_task_wdt_add(xTaskGetCurrentTaskHandle());
        esp_task_wdt_reset();

        if (this->face_recognizer_ != nullptr) {
          ESP_LOGI(TAG, "Recognizer recreated with fresh DB, retrying enrollment...");
          esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
          new_id = this->face_recognizer_->enroll(img, first_face_result);
          esp_task_wdt_add(xTaskGetCurrentTaskHandle());
          esp_task_wdt_reset();
        } else {
          ESP_LOGE(TAG, "Failed to recreate recognizer");
          this->recognition_enabled_ = false;
          this->enroll_pending_ = false;
          this->pending_enroll_name_.clear();
          return;
        }
      }

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
        ESP_LOGE(TAG, "Enrollment failed after DB reset (returned %d)", new_id);
        ESP_LOGE(TAG, "  Face may not be clear enough for feature extraction");
      }
      this->enroll_pending_ = false;
      // Skip next 5 detection cycles to let ESP-DL state settle after enrollment.
      this->post_enroll_cooldown_ = 5;
    } else if (!this->last_recognition_.recognized) {
      // Only call recognize() when NOT yet recognized.
      // Once recognized, stop calling recognize() - the callback already fired
      // and the YAML handles unlock. Re-calling recognize() repeatedly causes
      // PSRAM corruption and crashes (instruction access fault at invalid address).
      // Recognition resets automatically when face disappears (see above).
      esp_task_wdt_reset();
      dl::recognition::result_t *rec_result = this->face_recognizer_->recognize(img, first_face_result);
      esp_task_wdt_reset();

      if (rec_result != nullptr && rec_result->similarity >= this->recognition_threshold_) {
        this->cached_recognized_name_.clear();
        this->cached_recognized_id_ = rec_result->id;
        this->last_recognition_.id = rec_result->id;
        this->last_recognition_.similarity = rec_result->similarity;
        this->last_recognition_.recognized = true;

        ESP_LOGI(TAG, "Face RECOGNIZED! ID=%d, similarity=%.2f",
                 rec_result->id, rec_result->similarity);

        // Trigger callbacks (once - no repeated calls)
        for (auto &callback : this->on_face_recognized_callbacks_) {
          callback(rec_result->id, rec_result->similarity);
        }
      }
    }
    // If already recognized: skip recognize() entirely, keep cached result.
    // Detection still runs for bounding boxes, but no heavy recognition inference.
  }
#endif
}

// Static RGB565 colors (little-endian) - avoids heap allocation every frame
static const uint8_t COLOR_GREEN[] = {0xE0, 0x07};   // Green - unknown face
static const uint8_t COLOR_BLUE[] = {0x1F, 0x00};    // Blue - recognized face
static const uint8_t COLOR_RED[] = {0x00, 0xF8};     // Red for keypoints
static const uint8_t COLOR_WHITE[] = {0xFF, 0xFF};   // White for text

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
    // Use static color vectors for ESP-DL draw API (constructed once)
    static const std::vector<uint8_t> green_vec(COLOR_GREEN, COLOR_GREEN + 2);
    static const std::vector<uint8_t> blue_vec(COLOR_BLUE, COLOR_BLUE + 2);
    static const std::vector<uint8_t> red_vec(COLOR_RED, COLOR_RED + 2);

    bool is_recognized = this->last_recognition_.recognized;
    const std::vector<uint8_t> &box_color = is_recognized ? blue_vec : green_vec;
    int line_width = is_recognized ? 4 : 3;

    for (auto &box : this->cached_face_results_) {
      // Clamp bounding box coordinates to valid range
      int x1 = std::max(3, std::min((int)box.x1, (int)width - 4));
      int y1 = std::max(3, std::min((int)box.y1, (int)height - 4));
      int x2 = std::max(x1 + 1, std::min((int)box.x2, (int)width - 4));
      int y2 = std::max(y1 + 1, std::min((int)box.y2, (int)height - 4));

      // Draw bounding box
      dl::image::draw_hollow_rectangle(img, x1, y1, x2, y2, box_color, line_width);

      // Draw name above the box if recognized
      if (is_recognized) {
        // Use cached name to avoid map lookup + string allocation every frame
        if (this->cached_recognized_name_.empty()) {
          this->cached_recognized_name_ = this->get_face_name(this->last_recognition_.id);
          if (this->cached_recognized_name_.empty()) {
            this->cached_recognized_name_ = "ID " + std::to_string(this->last_recognition_.id);
          }
        }
        int text_y = std::max(2, y1 - 18);  // 18 pixels above box
        this->draw_text_(img_data, width, height, x1, text_y, this->cached_recognized_name_, COLOR_WHITE, 2);
      }

      // Draw red keypoints (5 facial landmarks) as hollow rectangles
      for (int i = 0; i < 5; i++) {
        int kp_x = box.keypoints[i * 2];
        int kp_y = box.keypoints[i * 2 + 1];

        // Check bounds with margin
        if (kp_x >= 12 && kp_y >= 12 && kp_x < (int)width - 12 && kp_y < (int)height - 12) {
          int size = 10;  // 20x20 pixel rectangle
          int rx1 = kp_x - size;
          int ry1 = kp_y - size;
          int rx2 = kp_x + size;
          int ry2 = kp_y + size;
          dl::image::draw_hollow_rectangle(img, rx1, ry1, rx2, ry2, red_vec, 2);
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

  ESP_LOGI(TAG, "Clearing all faces...");

  // Nullify recognizer pointer FIRST to prevent detection loop from using it
  // during delete/recreate. The detection loop checks this pointer.
  HumanFaceRecognizer *old_recognizer = this->face_recognizer_;
  this->face_recognizer_ = nullptr;

  // Clear cached recognition state
  this->last_recognition_.recognized = false;
  this->last_recognition_.id = -1;
  this->last_recognition_.similarity = 0.0f;
  this->cached_recognized_name_.clear();
  this->cached_recognized_id_ = -1;

  // Clear names
  this->face_names_.clear();
  this->save_names_to_sd_();

  // Delete the old recognizer safely
  delete old_recognizer;

  // Delete the database file from SD card
  if (std::remove(this->face_db_path_.c_str()) == 0) {
    ESP_LOGI(TAG, "Deleted face database: %s", this->face_db_path_.c_str());
  } else {
    ESP_LOGW(TAG, "Could not delete face database (may not exist): %s", this->face_db_path_.c_str());
  }

  // Ensure directory still exists before recreating
  ensure_parent_dir_(this->face_db_path_);

  // Reinitialize the recognizer with empty database
  // WDT protection: constructor loads model weights which can take several seconds
  esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

  this->face_recognizer_ = new HumanFaceRecognizer(
    this->face_db_path_.c_str(),
    nullptr,
    HumanFaceFeat::MFN_S8_V1,
    false
  );

  esp_task_wdt_add(xTaskGetCurrentTaskHandle());
  esp_task_wdt_reset();

  if (this->face_recognizer_ != nullptr) {
    ESP_LOGI(TAG, "All faces and names cleared, database reset");
  } else {
    ESP_LOGE(TAG, "Failed to reinitialize face recognizer after clear");
    this->recognition_enabled_ = false;
  }
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
  this->cached_recognized_name_.clear();
  this->cached_recognized_id_ = -1;
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
                                        int x, int y, char c, const uint8_t *color, int scale) {
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

  // Pre-compute clipping bounds to avoid per-pixel checks
  int char_w = 5 * scale;
  int char_h = 7 * scale;
  if (x + char_w <= 0 || x >= img_width || y + char_h <= 0 || y >= img_height) return;

  for (int row = 0; row < 7; row++) {
    uint8_t row_data = FONT_5X7[font_idx][row];
    for (int col = 0; col < 5; col++) {
      if (row_data & (0x10 >> col)) {
        // Draw scaled pixel
        int base_px = x + col * scale;
        int base_py = y + row * scale;
        for (int sy = 0; sy < scale; sy++) {
          int py = base_py + sy;
          if (py < 0 || py >= img_height) continue;
          int row_offset = py * img_width;
          for (int sx = 0; sx < scale; sx++) {
            int px = base_px + sx;
            if (px >= 0 && px < img_width) {
              int offset = (row_offset + px) * 2;  // RGB565 = 2 bytes
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
                                        const uint8_t *color, int scale) {
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
