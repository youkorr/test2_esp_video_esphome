/**
 * ESP-DL Face Detection Wrapper Implementation
 * Adapted from Waveshare ESP32-P4-WIFI6-Touch-LCD-7B
 */

#include "human_face_detect_espdl.h"

// Only compile if ESP-DL is available (checked in header)
#ifdef ESPHOME_HAS_ESP_DL

#include "esphome/core/log.h"

namespace esphome {
namespace human_face_detect {

static const char *const TAG = "human_face_detect.espdl";

// ============================================================================
// MSR Detector (Multi-Scale Region - First Stage)
// ============================================================================

MSRDetector::MSRDetector(const char *model_path) {
  // Load model from SD card
  m_model = new dl::Model(model_path, fbs::model_location_type_t::MODEL_LOCATION_IN_SDCARD);

  // Image preprocessor for ESP32-P4 with RGB565 big-endian support
  m_image_preprocessor = new dl::image::ImagePreprocessor(
      m_model, {0, 0, 0}, {1, 1, 1}, dl::image::DL_IMAGE_CAP_RGB_SWAP | dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);

  // MSR postprocessor with anchor configuration
  // Confidence threshold: 0.5, IOU threshold: 0.5, max detections: 10
  // Anchors: 2 feature maps with different scales
  m_postprocessor = new dl::detect::MSRPostprocessor(
      m_model, m_image_preprocessor, 0.5f, 0.5f, 10,
      {{8, 8, 9, 9, {{16, 16}, {32, 32}}},   // Feature map 1: stride 8, anchors 16x16, 32x32
       {16, 16, 9, 9, {{64, 64}, {128, 128}}}}  // Feature map 2: stride 16, anchors 64x64, 128x128
  );

  ESP_LOGI(TAG, "MSR detector initialized with model: %s", model_path);
}

// ============================================================================
// MNP Detector (Multi-Neck Post-processing - Second Stage)
// ============================================================================

MNPDetector::MNPDetector(const char *model_path) {
  // Load model from SD card
  model_ = new dl::Model(model_path, fbs::model_location_type_t::MODEL_LOCATION_IN_SDCARD);

  // Image preprocessor for ESP32-P4 with RGB565 big-endian support
  image_preprocessor_ = new dl::image::ImagePreprocessor(
      model_, {0, 0, 0}, {1, 1, 1}, dl::image::DL_IMAGE_CAP_RGB_SWAP | dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);

  // MNP postprocessor with single feature map
  // Confidence threshold: 0.5, IOU threshold: 0.5, max detections: 10
  // Single feature map: stride 1, 48x48 anchor
  postprocessor_ = new dl::detect::MNPPostprocessor(model_, image_preprocessor_, 0.5f, 0.5f, 10, {{1, 1, 0, 0, {{48, 48}}}});

  ESP_LOGI(TAG, "MNP detector initialized with model: %s", model_path);
}

MNPDetector::~MNPDetector() {
  if (model_) {
    delete model_;
    model_ = nullptr;
  }
  if (image_preprocessor_) {
    delete image_preprocessor_;
    image_preprocessor_ = nullptr;
  }
  if (postprocessor_) {
    delete postprocessor_;
    postprocessor_ = nullptr;
  }
}

std::list<dl::detect::result_t> &MNPDetector::run(const dl::image::img_t &img,
                                                   std::list<dl::detect::result_t> &candidates) {
  // Clear previous results
  postprocessor_->clear_result();

  // Process each candidate from MSR
  for (auto &candidate : candidates) {
    // Convert bounding box to square centered on face
    int center_x = (candidate.box[0] + candidate.box[2]) >> 1;
    int center_y = (candidate.box[1] + candidate.box[3]) >> 1;
    int side = DL_MAX(candidate.box[2] - candidate.box[0], candidate.box[3] - candidate.box[1]);

    candidate.box[0] = center_x - (side >> 1);
    candidate.box[1] = center_y - (side >> 1);
    candidate.box[2] = candidate.box[0] + side;
    candidate.box[3] = candidate.box[1] + side;
    candidate.limit_box(img.width, img.height);

    // Preprocess image region
    image_preprocessor_->preprocess(img, candidate.box);

    // Run model inference
    model_->run();

    // Postprocess results (ImagePreprocessor already passed to constructor)
    postprocessor_->postprocess();
  }

  // Apply Non-Maximum Suppression to remove overlapping detections
  postprocessor_->nms();

  // Get final results
  return postprocessor_->get_result(img.width, img.height);
}

// ============================================================================
// MSR+MNP Combined Detector
// ============================================================================

MSRMNPDetector::MSRMNPDetector(const char *msr_model_path, const char *mnp_model_path) {
  msr_ = new MSRDetector(msr_model_path);
  mnp_ = new MNPDetector(mnp_model_path);
  ESP_LOGI(TAG, "MSR+MNP detector initialized successfully");
}

MSRMNPDetector::~MSRMNPDetector() {
  if (msr_) {
    delete msr_;
    msr_ = nullptr;
  }
  if (mnp_) {
    delete mnp_;
    mnp_ = nullptr;
  }
}

std::list<dl::detect::result_t> &MSRMNPDetector::run(const dl::image::img_t &img) {
  // Stage 1: MSR detects face candidates
  std::list<dl::detect::result_t> &candidates = msr_->run(img);

  ESP_LOGV(TAG, "MSR found %d face candidates", candidates.size());

  // Stage 2: MNP refines candidates
  return mnp_->run(img, candidates);
}

dl::detect::Detect &MSRMNPDetector::set_score_thr(float score_thr, int idx) {
  // idx 0 = MSR, idx 1 = MNP
  if (idx == 0 && msr_) {
    msr_->set_score_thr(score_thr);
  }
  // MNP doesn't inherit from Detect, so we can't set its threshold via this interface
  return *this;
}

dl::detect::Detect &MSRMNPDetector::set_nms_thr(float nms_thr, int idx) {
  // idx 0 = MSR, idx 1 = MNP
  if (idx == 0 && msr_) {
    msr_->set_nms_thr(nms_thr);
  }
  // MNP doesn't inherit from Detect, so we can't set its threshold via this interface
  return *this;
}

dl::Model *MSRMNPDetector::get_raw_model(int idx) {
  // idx 0 = MSR, idx 1 = MNP
  if (idx == 0 && msr_) {
    return msr_->get_raw_model();
  }
  return nullptr;
}

}  // namespace human_face_detect
}  // namespace esphome

#endif  // ESPHOME_HAS_ESP_DL
