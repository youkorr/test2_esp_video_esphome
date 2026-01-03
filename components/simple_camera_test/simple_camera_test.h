#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

extern "C" {
#include "esp_cam_sensor.h"
#include "esp_cam_sensor_detect.h"
#include "esp_video.h"
#include "esp_video_isp.h"
#include "esp_ipa.h"
}

namespace esphome {
namespace simple_camera_test {

class SimpleCameraTest : public Component {
 public:
  void set_parent(lv_obj_t *parent) { parent_ = parent; }
  void set_width(int w) { width_ = w; }
  void set_height(int h) { height_ = h; }

  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  bool init_camera_sensor_();
  bool init_isp_pipeline_();
  void capture_and_display_();
  void calculate_fps_();

  static void capture_task_(void *arg);

  lv_obj_t *parent_{nullptr};
  lv_obj_t *canvas_{nullptr};
  lv_obj_t *fps_label_{nullptr};

  int width_{800};
  int height_{600};

  esp_cam_sensor_device_t *cam_dev_{nullptr};
  esp_video_isp_handle_t isp_handle_{nullptr};

  uint8_t *rgb_buffer_{nullptr};
  size_t rgb_buffer_size_{0};

  lv_img_dsc_t img_dsc_{};

  TaskHandle_t capture_task_handle_{nullptr};
  volatile bool running_{false};

  // FPS tracking
  uint32_t frame_count_{0};
  uint32_t last_fps_time_{0};
  float current_fps_{0.0f};

  SemaphoreHandle_t buffer_mutex_{nullptr};
};

}  // namespace simple_camera_test
}  // namespace esphome

#endif
