/**
 * ESP-DL Face Detection Wrapper
 * Adapted from Waveshare ESP32-P4-WIFI6-Touch-LCD-7B
 * https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B
 */

#pragma once

// Only compile if ESP-IDF and ESP-DL are available
#if defined(USE_ESP_IDF) && __has_include("dl_detect_base.hpp")

// Define macro to indicate ESP-DL is available
#define ESPHOME_HAS_ESP_DL 1

#include "dl_detect_base.hpp"
#include "dl_detect_mnp_postprocessor.hpp"
#include "dl_detect_msr_postprocessor.hpp"

namespace esphome {
namespace human_face_detect {

/**
 * @brief MSR (Multi-Scale Region) detector - first stage
 * Detects face candidates in the image
 */
class MSRDetector : public dl::detect::DetectImpl {
 public:
  MSRDetector(const char *model_path);
  ~MSRDetector() = default;
};

/**
 * @brief MNP (Multi-Neck Post-processing) detector - second stage
 * Refines face candidates from MSR
 */
class MNPDetector {
 private:
  dl::Model *model_;
  dl::image::ImagePreprocessor *image_preprocessor_;
  dl::detect::MNPPostprocessor *postprocessor_;

 public:
  MNPDetector(const char *model_path);
  ~MNPDetector();

  /**
   * @brief Run MNP detection on MSR candidates
   * @param img Input image (RGB format)
   * @param candidates Face candidates from MSR
   * @return Refined detection results
   */
  std::list<dl::detect::result_t> &run(const dl::image::img_t &img, std::list<dl::detect::result_t> &candidates);
};

/**
 * @brief Combined MSR+MNP face detector
 */
class MSRMNPDetector : public dl::detect::Detect {
 private:
  MSRDetector *msr_;
  MNPDetector *mnp_;

 public:
  MSRMNPDetector(const char *msr_model_path, const char *mnp_model_path);
  ~MSRMNPDetector();

  /**
   * @brief Run full face detection pipeline
   * @param img Input image (RGB format)
   * @return List of detected faces with bounding boxes
   */
  std::list<dl::detect::result_t> &run(const dl::image::img_t &img) override;
};

}  // namespace human_face_detect
}  // namespace esphome

#endif  // defined(USE_ESP_IDF) && __has_include("dl_detect_base.hpp")
