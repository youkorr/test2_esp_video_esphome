#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esp_video {

/**
 * @brief Composant ESPHome pour ESP-Video d'Espressif
 *
 * Ce composant active les flags de compilation et bibliothèques pour ESP-Video.
 * La configuration matérielle (I2C, capteurs, LDO, XCLK) est gérée par:
 * - Le composant 'i2c' d'ESPHome pour la communication avec les capteurs
 * - Le composant 'mipi_dsi_cam' pour l'initialisation du pipeline caméra
 */
class ESPVideoComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    // Priorité BUS pour s'initialiser tôt (après I2C mais avant les devices)
    return setup_priority::BUS;
  }

 protected:
  bool initialized_{false};
};

}  // namespace esp_video
}  // namespace esphome
