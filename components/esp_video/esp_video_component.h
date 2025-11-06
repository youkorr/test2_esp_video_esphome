#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esp_video {

/**
 * @brief Composant ESPHome pour ESP-Video d'Espressif
 *
 * Ce composant active les flags de compilation pour ESP-Video et crée
 * les devices vidéo (ISP, JPEG, H.264) sur ESP32-P4.
 *
 * La configuration matérielle (I2C, LDO, XCLK) est gérée par:
 * - Les composants ESPHome standards (i2c, esp_ldo)
 * - Le composant mipi_dsi_cam qui initialise le capteur
 */
class ESPVideoComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    // Priorité DATA pour s'initialiser après i2c et esp_ldo
    // mais avant mipi_dsi_cam
    return setup_priority::DATA;
  }

 protected:
  bool initialized_{false};
};

}  // namespace esp_video
}  // namespace esphome
