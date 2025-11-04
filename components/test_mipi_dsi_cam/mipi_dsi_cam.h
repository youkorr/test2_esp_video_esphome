#pragma once
#include "esphome/core/component.h"
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "esp_video_isp_ioctl.h"
#include "esp_cam_sensor.h"
#include "esp_cam_sensor_types.h"
#include "esp_err.h"
}
#endif

namespace esphome {
namespace mipi_dsi_cam {

class MipiDSICamComponent : public Component {
 public:
  void setup() override;
  void loop() override;

  // Configuration setters (depuis __init__.py)
  void set_sensor_type(const std::string &s) { sensor_name_ = s; }
  void set_i2c_id(int id) { i2c_id_ = id; }
  void set_lane(int l) { lane_ = l; }
  void set_xclk_pin(const std::string &p) { xclk_pin_ = p; }
  void set_xclk_freq(int f) { xclk_freq_ = f; }
  void set_sensor_addr(int a) { sensor_addr_ = a; }
  void set_resolution(const std::string &r) { resolution_ = r; }
  void set_pixel_format(const std::string &f) { pixel_format_ = f; }
  void set_framerate(int f) { framerate_ = f; }
  void set_jpeg_quality(int q) { jpeg_quality_ = q; }
  void set_sd_card(void *sd) { sd_card_ = sd; }

  // Capture snapshot vers SD
  bool capture_snapshot_to_file(const std::string &path);

 protected:
  std::string sensor_name_{"sc202cs"};
  int i2c_id_{0};
  int lane_{1};
  std::string xclk_pin_{"GPIO36"};
  int xclk_freq_{24000000};
  int sensor_addr_{0x36};
  std::string resolution_{"720P"};
  std::string pixel_format_{"JPEG"};
  int framerate_{30};
  int jpeg_quality_{10};
  void *sd_card_{nullptr};

  esp_cam_sensor_device_t *sensor_dev_{nullptr};
  esp_video_init_config_t init_cfg_{};
  esp_video_isp_config_t isp_cfg_{};

  bool pipeline_started_{false};
};

}  // namespace mipi_dsi_cam
}  // namespace esphome

