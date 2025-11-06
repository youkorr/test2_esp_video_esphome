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
 * le handle I2C du bus ESPHome, puis crée les devices vidéo.
 */
class ESPVideoComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    // Priorité HARDWARE pour initialiser esp_video_init() avant les devices
    return setup_priority::HARDWARE;
  }

  void set_i2c_bus(i2c::I2CBus *bus) { this->i2c_bus_ = bus; }

 protected:
  bool initialized_{false};
  i2c::I2CBus *i2c_bus_{nullptr};
};

}  // namespace esp_video
}  // namespace esphome
