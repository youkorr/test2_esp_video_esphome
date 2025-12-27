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

  // Contrôles RGB gains via CCM (Color Correction Matrix)
  // Note: V4L2 controls (brightness, contrast, saturation, hue) ne sont PAS supportés
  // par l'ISP ESP32-P4 pour les capteurs SC202CS/OV02C10
  if (this->control_method_ == "set_rgb_gain_red") {
    this->rgb_gain_red_ = value;
    success = this->parent_->set_rgb_gains(this->rgb_gain_red_, this->rgb_gain_green_, this->rgb_gain_blue_);
    ESP_LOGI(TAG, "RGB gains: R=%.2f G=%.2f B=%.2f", this->rgb_gain_red_, this->rgb_gain_green_, this->rgb_gain_blue_);
  } else if (this->control_method_ == "set_rgb_gain_green") {
    this->rgb_gain_green_ = value;
    success = this->parent_->set_rgb_gains(this->rgb_gain_red_, this->rgb_gain_green_, this->rgb_gain_blue_);
    ESP_LOGI(TAG, "RGB gains: R=%.2f G=%.2f B=%.2f", this->rgb_gain_red_, this->rgb_gain_green_, this->rgb_gain_blue_);
  } else if (this->control_method_ == "set_rgb_gain_blue") {
    this->rgb_gain_blue_ = value;
    success = this->parent_->set_rgb_gains(this->rgb_gain_red_, this->rgb_gain_green_, this->rgb_gain_blue_);
    ESP_LOGI(TAG, "RGB gains: R=%.2f G=%.2f B=%.2f", this->rgb_gain_red_, this->rgb_gain_green_, this->rgb_gain_blue_);
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
