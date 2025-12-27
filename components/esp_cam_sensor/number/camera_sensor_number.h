#pragma once

#include "esphome/core/component.h"
#include "esphome/components/number/number.h"
#include "../esp_cam_sensor_camera.h"

namespace esphome {
namespace esp_cam_sensor {

class CameraSensorNumber : public number::Number, public Component {
 public:
  void set_parent(MipiDSICamComponent *parent) { this->parent_ = parent; }
  void set_control_method(const std::string &method) { this->control_method_ = method; }

 protected:
  void control(float value) override;

  MipiDSICamComponent *parent_{nullptr};
  std::string control_method_;

  // Stocker les valeurs RGB pour ajustements indépendants
  float rgb_gain_red_{1.0f};
  float rgb_gain_green_{1.0f};
  float rgb_gain_blue_{1.0f};
};

}  // namespace esp_cam_sensor
}  // namespace esphome
