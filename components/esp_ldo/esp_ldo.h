#pragma once

#include "esphome/core/component.h"

#ifdef USE_ESP_IDF

namespace esphome {
namespace esp_ldo {

class EspLdo : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_channel_id(uint8_t channel_id) { this->channel_id_ = channel_id; }
  void set_voltage_mv(uint16_t voltage_mv) { this->voltage_mv_ = voltage_mv; }

 protected:
  uint8_t channel_id_;
  uint16_t voltage_mv_;
};

}  // namespace esp_ldo
}  // namespace esphome

#endif  // USE_ESP_IDF
