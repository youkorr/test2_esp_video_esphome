#include "human_face_detect.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"

#ifdef USE_ESP_IDF
#include "human_face_detect_espdl.h"
#endif

namespace esphome {
namespace human_face_detect {

static const char *const TAG = "human_face_detect";

void HumanFaceDetectComponent::setup() {
  ESP_LOGI(TAG, "Setting up Human Face Detection");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not set!");
    this->mark_failed();
    return;
  }

  if (!this->enable_detection_) {
    ESP_LOGI(TAG, "Face detection disabled (enable_detection: false)");
    return;
  }

  // Initialiser le modèle ESP-DL
  if (!this->init_model_()) {
    ESP_LOGW(TAG, "Face detection model not available - component disabled");
    this->initialized_ = false;
    // Ne pas marquer comme failed - c'est optionnel
    return;
  }

  this->initialized_ = true;
  ESP_LOGI(TAG, "Face detection initialized successfully");
}

void HumanFaceDetectComponent::loop() {
  // Ne rien faire ici - la détection est appelée manuellement
  // ou par des actions ESPHome
}

void HumanFaceDetectComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Human Face Detection:");
  ESP_LOGCONFIG(TAG, "  Enabled: %s", YESNO(this->enable_detection_));
  ESP_LOGCONFIG(TAG, "  Confidence threshold: %.2f", this->confidence_threshold_);
  ESP_LOGCONFIG(TAG, "  Model type: %d (MSRMNP_S8_V1)", this->model_type_);
  ESP_LOGCONFIG(TAG, "  Initialized: %s", YESNO(this->initialized_));

  if (this->camera_ == nullptr) {
    ESP_LOGCONFIG(TAG, "  Camera: NOT SET");
  }
}

bool HumanFaceDetectComponent::init_model_() {
#ifdef USE_ESP_IDF
  ESP_LOGI(TAG, "Initializing ESP-DL face detection models...");
  ESP_LOGI(TAG, "  Model directory: %s", this->model_dir_.c_str());
  ESP_LOGI(TAG, "  MSR model: %s", this->msr_model_filename_.c_str());
  ESP_LOGI(TAG, "  MNP model: %s", this->mnp_model_filename_.c_str());

  // Build full paths to model files
  std::string msr_path = this->model_dir_ + "/" + this->msr_model_filename_;
  std::string mnp_path = this->model_dir_ + "/" + this->mnp_model_filename_;

  // Verify model files exist (SD card must be mounted)
  FILE *f_msr = fopen(msr_path.c_str(), "r");
  if (f_msr == nullptr) {
    ESP_LOGE(TAG, "❌ MSR model file not found: %s", msr_path.c_str());
    ESP_LOGE(TAG, "   Make sure SD card is mounted and models are present");
    return false;
  }
  fclose(f_msr);

  FILE *f_mnp = fopen(mnp_path.c_str(), "r");
  if (f_mnp == nullptr) {
    ESP_LOGE(TAG, "❌ MNP model file not found: %s", mnp_path.c_str());
    ESP_LOGE(TAG, "   Make sure SD card is mounted and models are present");
    return false;
  }
  fclose(f_mnp);

  ESP_LOGI(TAG, "✓ Model files found on SD card");

  try {
    // Create MSR+MNP detector
    auto *detector = new MSRMNPDetector(msr_path.c_str(), mnp_path.c_str());
    this->detector_ = static_cast<void *>(detector);

    ESP_LOGI(TAG, "✅ ESP-DL face detection initialized successfully");
    ESP_LOGI(TAG, "   Confidence threshold: %.2f", this->confidence_threshold_);
    ESP_LOGI(TAG, "   Model type: MSRMNP_S8_V1");
    return true;

  } catch (const std::exception &e) {
    ESP_LOGE(TAG, "❌ Failed to initialize face detection: %s", e.what());
    this->detector_ = nullptr;
    return false;
  }
#else
  ESP_LOGW(TAG, "⚠️  ESP-IDF required for face detection");
  ESP_LOGW(TAG, "Component requires:");
  ESP_LOGW(TAG, "  1. esp-dl library 3.1.0 (Espressif Deep Learning)");
  ESP_LOGW(TAG, "  2. Face detection models in %s:", this->model_dir_.c_str());
  ESP_LOGW(TAG, "     - %s", this->msr_model_filename_.c_str());
  ESP_LOGW(TAG, "     - %s", this->mnp_model_filename_.c_str());
  ESP_LOGW(TAG, "  3. ESP32-P4 target with sufficient PSRAM");
  return false;
#endif
}

void HumanFaceDetectComponent::cleanup_model_() {
#ifdef USE_ESP_IDF
  if (this->detector_ != nullptr) {
    auto *detector = static_cast<MSRMNPDetector *>(this->detector_);
    delete detector;
    this->detector_ = nullptr;
    ESP_LOGD(TAG, "Face detection model cleaned up");
  }
#endif
}

int HumanFaceDetectComponent::detect_faces() {
  if (!this->is_detection_enabled()) {
    ESP_LOGW(TAG, "Detection not enabled or not initialized");
    return -1;
  }

#ifdef USE_ESP_IDF
  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not set!");
    return -1;
  }

  // TODO: Get current RGB frame from camera
  // The mipi_dsi_cam component needs to expose a method like:
  //   bool get_rgb_frame(uint8_t **data, size_t *width, size_t *height, pixel_format *format);
  //
  // For now, this is a placeholder showing the structure

  ESP_LOGW(TAG, "⚠️  Camera frame capture not yet implemented");
  ESP_LOGW(TAG, "Need to add method in mipi_dsi_cam:");
  ESP_LOGW(TAG, "  bool get_current_rgb_frame(uint8_t **data, int *width, int *height);");

  // Example of what the implementation would look like:
  /*
  uint8_t *rgb_data = nullptr;
  int width = 0, height = 0;

  if (!this->camera_->get_current_rgb_frame(&rgb_data, &width, &height)) {
    ESP_LOGE(TAG, "Failed to get camera frame");
    return -1;
  }

  // Create ESP-DL image structure
  dl::image::img_t img;
  img.data = rgb_data;
  img.width = width;
  img.height = height;
  img.pix_type = dl::image::IMAGE_PIX_TYPE_RGB565_BIG_ENDIAN;

  // Run detection
  auto *detector = static_cast<MSRMNPDetector *>(this->detector_);
  std::list<dl::detect::result_t> &results = detector->run(img);

  // Store results and filter by confidence threshold
  this->detected_faces_.clear();
  for (const auto &result : results) {
    if (result.score >= this->confidence_threshold_) {
      FaceBox face;
      face.x = result.box[0];
      face.y = result.box[1];
      face.w = result.box[2] - result.box[0];
      face.h = result.box[3] - result.box[1];
      face.confidence = result.score;
      this->detected_faces_.push_back(face);
    }
  }

  this->face_count_ = this->detected_faces_.size();
  ESP_LOGI(TAG, "Detected %d face(s)", this->face_count_);
  return this->face_count_;
  */

  this->face_count_ = 0;
  return 0;  // Placeholder until camera integration is complete
#else
  ESP_LOGW(TAG, "ESP-IDF required for face detection");
  return -1;
#endif
}

bool HumanFaceDetectComponent::get_face_box(int index, int &x, int &y, int &w, int &h, float &confidence) {
  if (index < 0 || index >= static_cast<int>(this->detected_faces_.size())) {
    return false;
  }

  const FaceBox &face = this->detected_faces_[index];
  x = face.x;
  y = face.y;
  w = face.w;
  h = face.h;
  confidence = face.confidence;

  return true;
}

}  // namespace human_face_detect
}  // namespace esphome
