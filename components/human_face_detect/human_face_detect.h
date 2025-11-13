#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include <vector>
#include <string>

namespace esphome {

// Forward declaration de la caméra (en dehors du namespace human_face_detect)
namespace mipi_dsi_cam {
class MipiDSICamComponent;
}

namespace human_face_detect {

/**
 * @brief Human Face Detection Component (Optional)
 *
 * Détection de visages humains utilisant ESP-DL (Espressif Deep Learning)
 * Basé sur l'implémentation Waveshare ESP32-P4
 *
 * Modèles supportés :
 * - MSR+MNP (Multi-Scale Region + Multi-Neck Post-processing)
 *
 * Format d'entrée recommandé :
 * - Résolution : 120x160 RGB (MSR), 48x48 RGB (MNP)
 * - Format pixel : RGB888 ou RGB565
 */
class HumanFaceDetectComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Configuration
  void set_camera(esphome::mipi_dsi_cam::MipiDSICamComponent *camera) { camera_ = camera; }
  void set_enable_detection(bool enable) { enable_detection_ = enable; }
  void set_confidence_threshold(float threshold) { confidence_threshold_ = threshold; }
  void set_model_type(int model_type) { model_type_ = model_type; }
  void set_model_dir(const std::string &dir) { model_dir_ = dir; }
  void set_msr_model_filename(const std::string &filename) { msr_model_filename_ = filename; }
  void set_mnp_model_filename(const std::string &filename) { mnp_model_filename_ = filename; }

  // API publique
  bool is_detection_enabled() const { return enable_detection_ && initialized_; }
  int get_face_count() const { return face_count_; }

  /**
   * @brief Détecte les visages dans le frame actuel de la caméra
   * @return Nombre de visages détectés, -1 si erreur
   */
  int detect_faces();

  /**
   * @brief Obtient les coordonnées du visage détecté
   * @param index Index du visage (0 à face_count-1)
   * @param x, y, w, h Sorties : coordonnées et dimensions du rectangle
   * @param confidence Sortie : niveau de confiance (0.0-1.0)
   * @return true si succès
   */
  inline bool get_face_box(int index, int &x, int &y, int &w, int &h, float &confidence) {
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

 protected:
  esphome::mipi_dsi_cam::MipiDSICamComponent *camera_{nullptr};

  bool enable_detection_{false};
  bool initialized_{false};
  float confidence_threshold_{0.5f};
  int model_type_{0};  // MSRMNP_S8_V1

  // Résultats de détection
  int face_count_{0};

  // ESP-DL detector
  void *detector_{nullptr};  // Pointer to MSRMNPDetector (kept as void* to avoid header dependency)

  // Detection results storage
  struct FaceBox {
    int x, y, w, h;
    float confidence;
  };
  std::vector<FaceBox> detected_faces_;

  // Model paths configuration
  std::string model_dir_{"/spiffs"};  // Changed from /sdcard to /spiffs (embedded models)
  std::string msr_model_filename_{"human_face_detect_msr_s8_v1.espdl"};
  std::string mnp_model_filename_{"human_face_detect_mnp_s8_v1.espdl"};

  bool init_model_();
  bool mount_spiffs_();  // Mount SPIFFS partition for embedded models
  void cleanup_model_();
};

}  // namespace human_face_detect
}  // namespace esphome
