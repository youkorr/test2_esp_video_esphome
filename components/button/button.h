#pragma once

#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace button {

#define LOG_BUTTON(prefix, type, obj) \
  if ((obj) != nullptr) { \
    ESP_LOGCONFIG(TAG, "%s%s '%s'", prefix, LOG_STR_LITERAL(type), (obj)->get_name().c_str()); \
    if (!(obj)->get_device_class().empty()) { \
      ESP_LOGCONFIG(TAG, "%s  Device Class: '%s'", prefix, (obj)->get_device_class().c_str()); \
    } \
  }

class Button : public EntityBase {
 public:
  explicit Button() = default;

  void press();
  void add_on_press_callback(std::function<void()> &&callback);
  void set_device_class(const std::string &device_class);
  std::string get_device_class();

 protected:
  virtual void press_action() = 0;

  CallbackManager<void()> press_callback_{};
  std::string device_class_{};
};

}  // namespace button
}  // namespace esphome
