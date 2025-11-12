#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

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
  bool get_face_box(int index, int &x, int &y, int &w, int &h, float &confidence);

 protected:
  esphome::mipi_dsi_cam::MipiDSICamComponent *camera_{nullptr};

  bool enable_detection_{false};
  bool initialized_{false};
  float confidence_threshold_{0.5f};
  int model_type_{0};  // MSRMNP_S8_V1

  // Résultats de détection
  int face_count_{0};

  // ESP-DL handle (opaque pointer)
  void *model_handle_{nullptr};

  bool init_model_();
  void cleanup_model_();
};

}  // namespace human_face_detect
}  // namespace esphome
