#include "yolov11_text_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace yolov11 {

static const char *const TAG = "yolov11.text_sensor";

void YOLOv11TextSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "YOLOv11 detection text sensor:");
  ESP_LOGCONFIG(TAG, "  Last value: %s",
                this->last_published_.empty() ? "<no detections yet>"
                                              : this->last_published_.c_str());
}

}  // namespace yolov11
}  // namespace esphome
