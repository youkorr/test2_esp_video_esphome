#include "human_face_detect.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"
#include "human_face_detect_espdl.h"  // Includes ESP-DL conditionally via __has_include

#ifdef USE_ESP_IDF
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include <sys/stat.h>
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

  // Mount SPIFFS to access embedded model files
  if (!this->mount_spiffs_()) {
    ESP_LOGW(TAG, "Failed to mount SPIFFS - face detection unavailable");
    ESP_LOGW(TAG, "Models should be embedded in SPIFFS partition or placed on SD card");
    this->initialized_ = false;
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
#ifdef ESPHOME_HAS_ESP_DL
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

bool HumanFaceDetectComponent::mount_spiffs_() {
#ifdef USE_ESP_IDF
  // Check if SPIFFS is already mounted
  struct stat st;
  if (stat("/spiffs", &st) == 0) {
    ESP_LOGI(TAG, "SPIFFS already mounted at /spiffs");
    return true;
  }

  ESP_LOGI(TAG, "Mounting SPIFFS partition for embedded models...");

  esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "spiffs",  // Must match partition table
      .max_files = 5,
      .format_if_mount_failed = false  // Don't format - we need pre-populated data
  };

  esp_err_t ret = esp_vfs_spiffs_register(&conf);
  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount SPIFFS partition");
      ESP_LOGE(TAG, "Make sure 'spiffs' partition exists in partitions.csv");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "SPIFFS partition not found in partition table");
      ESP_LOGE(TAG, "Add 'spiffs' partition to partitions.csv or use SD card");
    } else {
      ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
    }
    return false;
  }

  // Check SPIFFS status
  size_t total = 0, used = 0;
  ret = esp_spiffs_info(conf.partition_label, &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get SPIFFS partition info (%s)", esp_err_to_name(ret));
    esp_vfs_spiffs_unregister(conf.partition_label);
    return false;
  }

  ESP_LOGI(TAG, "✅ SPIFFS mounted successfully");
  ESP_LOGI(TAG, "   Partition size: %d KB, Used: %d KB", total / 1024, used / 1024);
  return true;
#else
  ESP_LOGE(TAG, "SPIFFS requires ESP-IDF framework");
  return false;
#endif
}

void HumanFaceDetectComponent::cleanup_model_() {
#ifdef ESPHOME_HAS_ESP_DL
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

#ifdef ESPHOME_HAS_ESP_DL
  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not set!");
    return -1;
  }

  // Get current RGB frame from camera
  esphome::mipi_dsi_cam::SimpleBufferElement *buffer = nullptr;
  uint8_t *rgb_data = nullptr;
  int width = 0, height = 0;

  if (!this->camera_->get_current_rgb_frame(&buffer, &rgb_data, &width, &height)) {
    ESP_LOGW(TAG, "Failed to get camera frame (streaming inactive or no buffer)");
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

  // Release buffer immediately after detection (no longer needed)
  this->camera_->release_buffer(buffer);

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
  ESP_LOGI(TAG, "Detected %d face(s) with confidence >= %.2f", this->face_count_, this->confidence_threshold_);

  return this->face_count_;
#else
  ESP_LOGW(TAG, "ESP-DL library not available - face detection disabled");
  ESP_LOGW(TAG, "Install ESP-DL component via 'idf.py add-dependency espressif/esp-dl^3.1.0'");
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
