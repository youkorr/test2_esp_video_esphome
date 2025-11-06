#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace esp_video {

/**
 * @brief Composant ESPHome pour ESP-Video d'Espressif
 *
 * Ce composant initialise la bibliothèque ESP-Video qui fournit
 * le support pour les caméras MIPI-CSI, ISP et encodeurs H.264/JPEG
 * sur ESP32-P4.
 *
 * Configuration I2C et LDO personnalisables via YAML.
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

  // Setters pour configuration I2C
  void set_i2c_sda(uint8_t pin) { this->i2c_sda_pin_ = pin; }
  void set_i2c_scl(uint8_t pin) { this->i2c_scl_pin_ = pin; }
  void set_i2c_port(uint8_t port) { this->i2c_port_ = port; }
  void set_i2c_frequency(uint32_t freq) { this->i2c_frequency_ = freq; }
  void set_sensor_address(uint8_t addr) { this->sensor_address_ = addr; }

  // Setters pour configuration LDO
  void set_ldo_voltage(float voltage) {
    this->ldo_voltage_ = voltage;
    this->use_ldo_ = true;
  }
  void set_ldo_channel(uint8_t channel) {
    this->ldo_channel_ = channel;
    this->use_ldo_ = true;
  }

  // Setters pour External Clock (XCLK)
  void set_external_clock_pin(uint8_t pin) {
    this->external_clock_pin_ = pin;
    this->has_external_clock_ = true;
  }
  void set_external_clock_frequency(uint32_t freq) {
    this->external_clock_frequency_ = freq;
  }

  // Setters pour pins reset/powerdown
  void set_reset_pin(int8_t pin) { this->reset_pin_ = pin; }
  void set_pwdn_pin(int8_t pin) { this->pwdn_pin_ = pin; }

  // Getters
  uint8_t get_sensor_address() const { return this->sensor_address_; }
  bool has_external_clock() const { return this->has_external_clock_; }

 protected:
  // Méthodes d'initialisation étape par étape
  bool init_external_clock_();
  bool init_ldo_();

  bool initialized_{false};

  // Configuration I2C
  uint8_t i2c_sda_pin_{7};      // GPIO7 par défaut (ESP32-P4)
  uint8_t i2c_scl_pin_{8};      // GPIO8 par défaut (ESP32-P4)
  uint8_t i2c_port_{0};         // Port I2C 0 par défaut
  uint32_t i2c_frequency_{100000};  // 100kHz par défaut
  uint8_t sensor_address_{0x36};    // SC202CS par défaut

  // Configuration LDO
  bool use_ldo_{false};
  float ldo_voltage_{0.0f};
  uint8_t ldo_channel_{0};
  void *ldo_handle_{nullptr};   // esp_ldo_channel_handle_t

  // Configuration External Clock (XCLK)
  bool has_external_clock_{false};
  uint8_t external_clock_pin_{0};
  uint32_t external_clock_frequency_{24000000};  // 24MHz par défaut

  // Pins optionnels
  int8_t reset_pin_{-1};
  int8_t pwdn_pin_{-1};
};

}  // namespace esp_video
}  // namespace esphome
