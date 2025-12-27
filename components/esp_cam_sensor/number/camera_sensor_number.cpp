#include "camera_sensor_number.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esp_cam_sensor {

static const char *const TAG = "esp_cam_sensor.number";

void CameraSensorNumber::control(float value) {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Parent camera not set");
    return;
  }

  bool success = false;
  int int_value = static_cast<int>(value);

  // Appeler la méthode appropriée selon le control_method_
  if (this->control_method_ == "set_brightness") {
    success = this->parent_->set_brightness(int_value);
  } else if (this->control_method_ == "set_hue") {
    success = this->parent_->set_hue(int_value);
  } else if (this->control_method_ == "set_contrast") {
    success = this->parent_->set_contrast(int_value);
  } else if (this->control_method_ == "set_saturation") {
    success = this->parent_->set_saturation(int_value);
  } else if (this->control_method_ == "set_filter") {
    // TODO: Implémenter set_filter si nécessaire
    ESP_LOGW(TAG, "Filter control not yet implemented");
  } else if (this->control_method_ == "set_rgb_gain_red") {
    // Mettre à jour la valeur rouge et appliquer tous les gains
    this->rgb_gain_red_ = value;
    success = this->parent_->set_rgb_gains(this->rgb_gain_red_, this->rgb_gain_green_, this->rgb_gain_blue_);
  } else if (this->control_method_ == "set_rgb_gain_green") {
    // Mettre à jour la valeur verte et appliquer tous les gains
    this->rgb_gain_green_ = value;
    success = this->parent_->set_rgb_gains(this->rgb_gain_red_, this->rgb_gain_green_, this->rgb_gain_blue_);
  } else if (this->control_method_ == "set_rgb_gain_blue") {
    // Mettre à jour la valeur bleue et appliquer tous les gains
    this->rgb_gain_blue_ = value;
    success = this->parent_->set_rgb_gains(this->rgb_gain_red_, this->rgb_gain_green_, this->rgb_gain_blue_);
  } else {
    ESP_LOGW(TAG, "Unknown control method: %s", this->control_method_.c_str());
  }

  if (success) {
    this->publish_state(value);
  } else {
    ESP_LOGW(TAG, "Failed to set %s to %.2f", this->control_method_.c_str(), value);
  }
}

}  // namespace esp_cam_sensor
}  // namespace esphome
