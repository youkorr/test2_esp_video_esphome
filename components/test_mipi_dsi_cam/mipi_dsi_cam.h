#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "driver/gpio.h"
#include <string>

extern "C" {
  #include "esp_err.h"
  #include "esp_cam_sensor.h"
  #include "esp_cam_sensor_types.h"
  #include "esp_video_init.h"
  #include "esp_video_device.h"
  #include "esp_video_ioctl.h"
  #include "esp_video_isp_ioctl.h"
  #include "esp_ipa.h"
  #include "esp_ipa_types.h"
}

namespace esphome {
namespace mipi_dsi_cam {

class MipiDSICamComponent : public Component {
 public:
  void set_sensor_type(int type) { this->sensor_type_ = type; }
  void set_i2c_id(int id) { this->i2c_id_ = id; }
  void set_lane(int lane) { this->lane_ = lane; }
  void set_xclk_pin(const std::string &pin) { this->xclk_pin_ = pin; }
  void set_xclk_freq(int freq) { this->xclk_freq_ = freq; }
  void set_sensor_addr(int addr) { this->sensor_addr_ = addr; }
  void set_resolution(const std::string &r) { this->resolution_ = r; }
  void set_pixel_format(const std::string &f) { this->pixel_format_ = f; }
  void set_framerate(int f) { this->framerate_ = f; }
  void set_jpeg_quality(int q) { this->jpeg_quality_ = q; }

  void setup() override;
  void loop() override;
  void dump_config() override {
    ESP_LOGCONFIG("mipi_dsi_cam", "MIPI-DSI-CAM Config:");
    ESP_LOGCONFIG("mipi_dsi_cam", "  Capteur     : %d", this->sensor_type_);
    ESP_LOGCONFIG("mipi_dsi_cam", "  Format      : %s", this->pixel_format_.c_str());
    ESP_LOGCONFIG("mipi_dsi_cam", "  Résolution  : %s", this->resolution_.c_str());
    ESP_LOGCONFIG("mipi_dsi_cam", "  Framerate   : %d", this->framerate_);
  }

  // Fonction snapshot (capture -> fichier)
  bool capture_snapshot_to_file(const std::string &path);

 protected:
  int sensor_type_{0};
  int i2c_id_{0};
  int lane_{1};
  std::string xclk_pin_{"GPIO36"};
  int xclk_freq_{24000000};
  int sensor_addr_{0x36};
  std::string resolution_{"720P"};
  std::string pixel_format_{"JPEG"};
  int framerate_{30};
  int jpeg_quality_{10};

  esp_cam_sensor_device_t *sensor_dev_{nullptr};
  esp_video_init_config_t init_cfg_{};
  esp_video_isp_config_t isp_cfg_{};
  bool pipeline_started_{false};
};

}  // namespace mipi_dsi_cam
}  // namespace esphome
