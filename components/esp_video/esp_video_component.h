#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace esp_video {

/**
 * @brief Composant ESPHome pour ESP-Video d'Espressif
 *
 * Ce composant initialise ESP-Video en appelant esp_video_init() avec
 * le handle I2C du bus ESPHome (via héritage de i2c::I2CDevice).
 *
 * IMPORTANT: N'initialise PAS son propre bus I2C (init_sccb = false)
 * pour éviter les conflits matériels avec le bus I2C d'ESPHome.
 */
class ESPVideoComponent : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    // Priorité HARDWARE pour initialiser esp_video_init() avant les devices
    return setup_priority::HARDWARE;
  }

 protected:
  bool initialized_{false};
};

}  // namespace esp_video
}  // namespace esphome
