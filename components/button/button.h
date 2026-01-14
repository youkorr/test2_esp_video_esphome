#pragma once

#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"

namespace esphome {
namespace button {

/**
 * Button component stub for ESPHome core compatibility.
 * This is a minimal implementation to satisfy ESPHome core includes.
 */
class Button : public EntityBase, public Component {
 public:
  Button() = default;

  void press() {}

  void setup() override {}
  void dump_config() override {}

  float get_setup_priority() const override { return setup_priority::DATA; }
};

}  // namespace button
}  // namespace esphome
