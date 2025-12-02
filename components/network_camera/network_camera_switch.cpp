#include "network_camera_switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace network_camera {

static const char *const TAG = "network_camera.switch";

void NetworkCameraSwitch::setup() {
  // Restore previous state or default to OFF
  bool initial_state = false;
  this->rtc_ = global_preferences->make_preference<bool>(this->get_object_id_hash());
  if (this->rtc_.load(&initial_state)) {
    this->write_state(initial_state);
  } else {
    this->write_state(false);
  }
}

void NetworkCameraSwitch::dump_config() {
  LOG_SWITCH("", "Network Camera Switch", this);
}

void NetworkCameraSwitch::write_state(bool state) {
  if (this->camera_ != nullptr) {
    this->camera_->set_enabled(state);
    ESP_LOGI(TAG, "Network Camera %s", state ? "enabled" : "disabled");
  }

  // Save state to RTC
  this->rtc_.save(&state);

  // Publish the new state
  this->publish_state(state);
}

}  // namespace network_camera
}  // namespace esphome
