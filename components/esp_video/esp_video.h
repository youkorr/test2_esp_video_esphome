#pragma once
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esp_video {

class ESPVideoComponent : public Component {
 public:
  void setup() override {
    ESP_LOGI("esp_video", "ESP-Video initialized");
  }
  void loop() override {}
  float get_setup_priority() const override { 
    return setup_priority::HARDWARE; 
  }
};

}  // namespace esp_video
}  // namespace esphome
