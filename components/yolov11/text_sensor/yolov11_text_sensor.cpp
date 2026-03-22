#include "yolov11_text_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace yolov11 {

static const char *const TAG = "yolov11.text_sensor";

// --- Detection Class Sensor ---
void YOLOV11DetectionClassSensor::setup() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "YOLOV11 parent not set for detection_id sensor");
    this->mark_failed();
    return;
  }

  this->parent_->add_on_detection_class_callback(
      [this](const std::string &detection_class) {
        this->publish_state(detection_class);
      });

  this->publish_state("");
  ESP_LOGD(TAG, "YOLOV11 detection_id sensor ready");
}

void YOLOV11DetectionClassSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLOV11 Detection Class Sensor:");
  LOG_TEXT_SENSOR("  ", "Detection ID", this);
}

// --- Detection Bounding Box Sensor ---
void YOLOV11DetectionBBSensor::setup() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "YOLOV11 parent not set for detection_bb sensor");
    this->mark_failed();
    return;
  }

  this->parent_->add_on_detection_bb_callback(
      [this](const std::string &detection_bb) {
        this->publish_state(detection_bb);
      });

  this->publish_state("");
  ESP_LOGD(TAG, "YOLOV11 detection_bb sensor ready");
}

void YOLOV11DetectionBBSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLOV11 Detection BB Sensor:");
  LOG_TEXT_SENSOR("  ", "Detection BB", this);
}

}  // namespace yolov11
}  // namespace esphome
