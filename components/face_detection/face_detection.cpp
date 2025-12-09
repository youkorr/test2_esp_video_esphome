#include "face_detection.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

// ESP-DL detection components
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "dl_image.hpp"

namespace esphome {
namespace face_detection {

static const char *const TAG = "face_detection";

void FaceDetectionComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Face Detection...");

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
  this->face_detector_ = new HumanFaceDetect();
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

  ESP_LOGI(TAG, "Face Detection ready");
  ESP_LOGI(TAG, "  Detection interval: every %d frames", this->detection_interval_);
  ESP_LOGI(TAG, "  Recognition: %s", this->recognition_enabled_ ? "ENABLED" : "DISABLED");
  ESP_LOGI(TAG, "  Draw boxes: %s", this->draw_enabled_ ? "YES" : "NO");
}

void FaceDetectionComponent::loop() {
  // Check if camera is streaming
  if (this->camera_ == nullptr || !this->camera_->is_streaming()) {
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
  mipi_dsi_cam::SimpleBufferElement *buffer = this->camera_->acquire_buffer();
  if (buffer == nullptr) {
    return;
  }

  uint8_t *img_data = this->camera_->get_buffer_data(buffer);
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data != nullptr) {
    this->detect_faces_(img_data, width, height);
    // NOTE: Drawing is now done externally via draw_on_frame()
    // called by lvgl_camera_display on its buffer
  }

  this->camera_->release_buffer(buffer);
}

void FaceDetectionComponent::draw_on_frame(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (this->draw_enabled_ && img_data != nullptr) {
    this->draw_results_(img_data, width, height);
  }
}

void FaceDetectionComponent::detect_faces_(uint8_t *img_data, uint16_t width, uint16_t height) {
  if (this->face_detector_ == nullptr) {
    return;
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
        ESP_LOGI(TAG, "Face enrolled with ID: %d", new_id);
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
}

void FaceDetectionComponent::draw_results_(uint8_t *img_data, uint16_t width, uint16_t height) {
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
      // Blue = recognized, Green = unknown
      std::vector<uint8_t> &box_color = this->last_recognition_.recognized ? blue : green;

      // Draw bounding box
      dl::image::draw_hollow_rectangle(img, x1, y1, x2, y2, box_color, 3);

      // Draw red keypoints (5 facial landmarks) - larger circles
      for (int i = 0; i < 5; i++) {
        int x = box.keypoints[i * 2];
        int y = box.keypoints[i * 2 + 1];

        // Check bounds with margin for the point radius
        if (x >= 8 && y >= 8 && x < width - 8 && y < height - 8) {
          // Draw larger filled circle (radius 6)
          dl::image::draw_point(img, x, y, red, 6);
        }
      }
    }

    xSemaphoreGive(this->face_results_mutex_);
  }
}

void FaceDetectionComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Face Detection:");
  ESP_LOGCONFIG(TAG, "  Score threshold: %.2f", this->score_threshold_);
  ESP_LOGCONFIG(TAG, "  NMS threshold: %.2f", this->nms_threshold_);
  ESP_LOGCONFIG(TAG, "  Detection interval: %d frames", this->detection_interval_);
  ESP_LOGCONFIG(TAG, "  Draw enabled: %s", this->draw_enabled_ ? "YES" : "NO");
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
  if (!this->recognition_enabled_ || this->face_recognizer_ == nullptr) {
    ESP_LOGE(TAG, "Face recognition not enabled or not initialized");
    return -1;
  }

  ESP_LOGI(TAG, "Enrollment requested - will capture on next face detection");
  this->enroll_pending_ = true;
  return 0;
}

bool FaceDetectionComponent::delete_face(int id) {
  if (!this->recognition_enabled_ || this->face_recognizer_ == nullptr) {
    ESP_LOGE(TAG, "Face recognition not enabled or not initialized");
    return false;
  }

  bool success = this->face_recognizer_->delete_feat(id);
  if (success) {
    ESP_LOGI(TAG, "Face ID %d deleted", id);
  }
  return success;
}

void FaceDetectionComponent::clear_all_faces() {
  if (!this->recognition_enabled_ || this->face_recognizer_ == nullptr) {
    ESP_LOGE(TAG, "Face recognition not enabled or not initialized");
    return;
  }

  this->face_recognizer_->clear_all_feats();
  ESP_LOGI(TAG, "All faces cleared from database");
}

int FaceDetectionComponent::get_enrolled_count() {
  if (!this->recognition_enabled_ || this->face_recognizer_ == nullptr) {
    return 0;
  }
  return this->face_recognizer_->get_num_feats();
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

}  // namespace face_detection
}  // namespace esphome
