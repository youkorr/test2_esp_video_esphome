#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esp_video {

/**
 * @brief Composant ESPHome pour ESP-Video d'Espressif
 *
 * Ce composant initialise ESP-Video en appelant esp_video_init().
 * esp_video créera son propre bus I2C (init_sccb = true).
 */
class ESPVideoComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    // Priorité HARDWARE pour initialiser esp_video_init() AVANT tout
    // Cela permet à esp_video de créer le bus I2C en premier
    return setup_priority::HARDWARE;
  }

  // Setters pour la configuration I2C
  void set_sda_pin(uint8_t pin) { this->sda_pin_ = pin; }
  void set_scl_pin(uint8_t pin) { this->scl_pin_ = pin; }
  void set_i2c_frequency(uint32_t freq) { this->i2c_frequency_ = freq; }

 protected:
  bool initialized_{false};
  uint8_t sda_pin_{31};
  uint8_t scl_pin_{32};
  uint32_t i2c_frequency_{400000};
};

}  // namespace esp_video
}  // namespace esphome
