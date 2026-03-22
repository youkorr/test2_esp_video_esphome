#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "../yolov11_component.h"

namespace esphome {
namespace yolov11 {

class YOLOV11DetectionClassSensor : public text_sensor::TextSensor, public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_yolov11(YOLOV11Component *parent) { this->parent_ = parent; }

 protected:
  YOLOV11Component *parent_{nullptr};
};

class YOLOV11DetectionBBSensor : public text_sensor::TextSensor, public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_yolov11(YOLOV11Component *parent) { this->parent_ = parent; }

 protected:
  YOLOV11Component *parent_{nullptr};
};

}  // namespace yolov11
}  // namespace esphome
