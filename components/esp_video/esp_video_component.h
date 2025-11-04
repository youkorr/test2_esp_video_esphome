#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esp_video {

/**
 * @brief Composant ESPHome pour ESP-Video d'Espressif
 * 
 * Ce composant initialise la bibliothèque ESP-Video qui fournit
 * le support pour les caméras MIPI-CSI, ISP et encodeurs H.264/JPEG
 * sur ESP32-P4.
 */
class ESPVideoComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  float get_setup_priority() const override { 
    // Haute priorité pour initialiser avant les composants de caméra
    return setup_priority::HARDWARE; 
  }

 protected:
  bool initialized_{false};
};

}  // namespace esp_video
}  // namespace esphome
