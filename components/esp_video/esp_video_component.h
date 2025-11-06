#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esp_video {

// Forward declaration de l'adaptateur
struct ESPHomeI2CSCCBAdapter;

/**
 * @brief Composant ESPHome pour ESP-Video d'Espressif
 *
 * Ce composant initialise ESP-Video en appelant esp_video_init()
 * avec un adaptateur I2C-SCCB qui utilise le bus I2C d'ESPHome.
 *
 * Hérite de i2c::I2CDevice pour accéder au bus I2C configuré en YAML.
 */
class ESPVideoComponent : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    // Priorité HARDWARE pour initialiser esp_video_init() tôt
    return setup_priority::HARDWARE;
  }

 protected:
  bool initialized_{false};
  ESPHomeI2CSCCBAdapter *sccb_adapter_{nullptr};
};

}  // namespace esp_video
}  // namespace esphome
