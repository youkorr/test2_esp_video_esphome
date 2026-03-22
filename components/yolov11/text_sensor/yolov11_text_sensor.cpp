#include "yolov11_text_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace yolov11 {

static const char *const TAG = "yolov11.text_sensor";

void YOLOV11TextSensor::setup() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "YOLOV11 parent not set");
    this->mark_failed();
    return;
  }

  // Register callback to receive detection updates
  this->parent_->add_on_detection_callback(
      [this](std::string detection) {
        this->publish_state(detection);
      });

  // Publish initial empty state
  this->publish_state("");

  ESP_LOGD(TAG, "YOLOV11 text sensor ready");
}

void YOLOV11TextSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLOV11 Text Sensor:");
  LOG_TEXT_SENSOR("  ", "Detection", this);
}

}  // namespace yolov11
}  // namespace esphome
