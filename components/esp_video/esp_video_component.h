#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/i2c/i2c.h"
#include "driver/gpio.h"

namespace esphome {
namespace esp_video {

class ESPVideoComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    return setup_priority::DATA;
  }

  // --- Shared-bus mode (uses ESPHome's I2C bus) ---
  void set_i2c_bus(i2c::I2CBus *bus) { this->i2c_bus_ = bus; }
  void set_i2c_port(int8_t port) { this->i2c_port_ = port; }

  // --- Dedicated mode (camera manages its own I2C) ---
  void set_init_sccb(bool v) { this->init_sccb_ = v; }
  void set_sda_pin(gpio_num_t pin) { this->sda_pin_ = pin; }
  void set_scl_pin(gpio_num_t pin) { this->scl_pin_ = pin; }
  void set_sccb_port(uint8_t port) { this->sccb_port_ = port; }
  void set_sccb_freq(uint32_t freq) { this->sccb_freq_ = freq; }

  // --- XCLK ---
  void set_xclk_pin(gpio_num_t pin) { this->xclk_pin_ = pin; }
  void set_xclk_freq(uint32_t freq) { this->xclk_freq_ = freq; }
  void set_enable_xclk_init(bool enable) { this->enable_xclk_init_ = enable; }

 protected:
  bool initialized_{false};

  // Shared-bus mode
  i2c::I2CBus *i2c_bus_{nullptr};
  int8_t i2c_port_{-1};           // -1 = auto-detect

  // Dedicated mode
  bool init_sccb_{false};
  gpio_num_t sda_pin_{GPIO_NUM_NC};
  gpio_num_t scl_pin_{GPIO_NUM_NC};
  uint8_t sccb_port_{0};
  uint32_t sccb_freq_{400000};

  // XCLK
  gpio_num_t xclk_pin_{GPIO_NUM_36};
  uint32_t xclk_freq_{24000000};
  bool enable_xclk_init_{false};
};

}  // namespace esp_video
}  // namespace esphome
