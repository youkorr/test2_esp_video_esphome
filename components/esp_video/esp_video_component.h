#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esp_video {

/**
 * @brief Composant ESPHome pour ESP-Video d'Espressif
 *
 * Ce composant initialise le pipeline vidéo ESP-Video en appelant esp_video_init()
 * et crée les devices vidéo (ISP, JPEG, H.264) sur ESP32-P4.
 */
class ESPVideoComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    // Priorité HARDWARE pour initialiser esp_video_init() avant les autres composants
    return setup_priority::HARDWARE;
  }

  // Configuration I2C
  void set_i2c_pins(int sda, int scl) {
    i2c_sda_ = sda;
    i2c_scl_ = scl;
    has_i2c_config_ = true;
  }
  void set_i2c_port(int port) { i2c_port_ = port; }
  void set_i2c_frequency(int freq) { i2c_frequency_ = freq; }

  // Pins de contrôle capteur
  void set_reset_pin(int pin) { reset_pin_ = pin; }
  void set_pwdn_pin(int pin) { pwdn_pin_ = pin; }

 protected:
  bool initialized_{false};
  bool has_i2c_config_{false};

  // Configuration I2C
  int i2c_sda_{-1};
  int i2c_scl_{-1};
  int i2c_port_{0};
  int i2c_frequency_{400000};

  // Pins de contrôle
  int reset_pin_{-1};
  int pwdn_pin_{-1};
};

}  // namespace esp_video
}  // namespace esphome
