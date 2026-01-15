/**
 * @file button.h
 * @brief Minimal button header for ESPHome core compatibility
 *
 * This header provides the minimal button interface required by ESPHome core
 * (application.h and controller.h) without loading any Python component.
 *
 * The actual button functionality comes from ESPHome's built-in button component.
 * This file only exists to satisfy header includes in external_components setup.
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace button {

#define LOG_BUTTON(prefix, type, obj) \
  if ((obj) != nullptr) { \
    ESP_LOGCONFIG(TAG, "%s%s '%s'", prefix, LOG_STR_LITERAL(type), (obj)->get_name().c_str()); \
    ESP_LOGCONFIG(TAG, "%s  Device Class: '%s'", prefix, (obj)->get_device_class().c_str()); \
  }

/** Base class for all buttons.
 *
 * A button is just a momentary switch that does not have a state, only a trigger.
 */
class Button : public EntityBase {
 public:
  explicit Button() = default;

  /** Press the button.
   *
   * This is called by the front-end and then calls press_action().
   */
  void press();

  /** Set callback for state changes.
   *
   * @param callback The callback.
   */
  void add_on_press_callback(std::function<void()> &&callback);

  /** Get the device class for this button.
   *
   * @return The device class, empty string if not set.
   */
  std::string get_device_class();

  /** Set the device class for this button.
   *
   * @param device_class The device class.
   */
  void set_device_class(const std::string &device_class);

 protected:
  /** Internal method to trigger the press.
   *
   * This should be called by the frontend to trigger the press.
   */
  virtual void press_action() = 0;

  CallbackManager<void()> press_callback_{};
  std::string device_class_{};
};

}  // namespace button
}  // namespace esphome
