#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/i2c/i2c.h"
#include "driver/gpio.h"  // For gpio_num_t

namespace esphome {
namespace esp_video {

/**
 * @brief Composant ESPHome pour ESP-Video d'Espressif
 *
 * Ce composant initialise ESP-Video en appelant esp_video_init()
 * avec init_sccb=false pour utiliser le bus I2C d'ESPHome.
 *
 * Avantage: Pas de conflit de bus, partage propre du bus I2C avec autres composants ESPHome.
 */
class ESPVideoComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    // Priorité DATA: s'exécute APRÈS I2C (BUS = 1000)
    // pour que le bus I2C ESPHome soit déjà créé
    return setup_priority::DATA;
  }

  // Setter pour le bus I2C ESPHome (kept for compatibility but not used with init_sccb=true)
  void set_i2c_bus(i2c::I2CBus *bus) { this->i2c_bus_ = bus; }

  // Setters pour I2C pins (REQUIRED - must match ESPHome i2c bus pins to avoid conflicts)
  void set_i2c_sda_pin(gpio_num_t pin) { this->i2c_sda_pin_ = pin; }
  void set_i2c_scl_pin(gpio_num_t pin) { this->i2c_scl_pin_ = pin; }
  void set_i2c_frequency(uint32_t freq) { this->i2c_frequency_ = freq; }

  // Setters pour XCLK (requis pour la détection des capteurs MIPI-CSI)
  void set_xclk_pin(gpio_num_t pin) { this->xclk_pin_ = pin; }
  void set_xclk_freq(uint32_t freq) { this->xclk_freq_ = freq; }
  void set_enable_xclk_init(bool enable) { this->enable_xclk_init_ = enable; }

 protected:
  bool initialized_{false};
  i2c::I2CBus *i2c_bus_{nullptr};

  // I2C pins - MUST match the ESPHome i2c bus configuration
  gpio_num_t i2c_sda_pin_{GPIO_NUM_8};   // Default SDA for ESP32-P4
  gpio_num_t i2c_scl_pin_{GPIO_NUM_9};   // Default SCL for ESP32-P4
  uint32_t i2c_frequency_{400000};       // Default 400kHz

  // XCLK configuration
  gpio_num_t xclk_pin_{GPIO_NUM_36};     // Default XCLK pin for ESP32-P4
  uint32_t xclk_freq_{24000000};         // Default 24MHz for MIPI-CSI sensors
  bool enable_xclk_init_{false};         // Enable XCLK initialization via LEDC (for non-M5Stack boards)
};

}  // namespace esp_video
}  // namespace esphome
