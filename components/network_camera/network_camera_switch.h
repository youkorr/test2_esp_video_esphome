#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "network_camera.h"

namespace esphome {
namespace network_camera {

class NetworkCameraSwitch : public switch_::Switch, public Component {
 public:
  void setup() override;
  void dump_config() override;

  void set_camera(NetworkCamera *camera) { this->camera_ = camera; }

 protected:
  void write_state(bool state) override;

  NetworkCamera *camera_{nullptr};
  ESPPreferenceObject rtc_;
};

}  // namespace network_camera
}  // namespace esphome
