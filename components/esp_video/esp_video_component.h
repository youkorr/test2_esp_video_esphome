#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esp_video {

/**
 * @brief Composant ESPHome pour ESP-Video d'Espressif
 *
 * Ce composant initialise ESP-Video en appelant esp_video_init()
 * SANS initialiser SCCB (init_sccb = false).
 *
 * La communication I2C avec le capteur est gérée par mipi_dsi_cam
 * via son héritage de i2c::I2CDevice.
 */
class ESPVideoComponent : public Component {
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
};

}  // namespace esp_video
}  // namespace esphome
