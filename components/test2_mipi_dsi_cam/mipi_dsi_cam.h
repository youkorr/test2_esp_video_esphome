#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "../sd_mmc_card/sd_mmc_card.h"   

#include <string>
#include <vector>

extern "C" {
  #include "esp_err.h"
  #include "esp_ldo.h"
  #include "esp_cam_sensor.h"
  #include "esp_video_init.h"
  #include "esp_video_isp_ioctl.h"
  #include "esp_log.h"
}

namespace esphome {
namespace mipi_dsi_cam {

using namespace esphome::sd_mmc_card;

class MipiDSICamComponent : public Component {
 public:
  // Config YAML
  std::string sensor_name_{"unknown"};
  int i2c_id_{0};
  int lane_{1};
  std::string xclk_pin_{"GPIO36"};
  int xclk_freq_{24000000};
  int sensor_addr_{0x36};
  std::string resolution_{"720P"};
  std::string pixel_format_{"RGB565"};
  int framerate_{30};
  int jpeg_quality_{10};

  // Liaison carte SD
  SDMMCCardComponent *sd_card_ = nullptr;  // 👈 correspond à ton lecteur
  void set_sd_card(SDMMCCardComponent *sd) { this->sd_card_ = sd; }

  // Vidéo / pipeline
  esp_cam_sensor_device_t *sensor_dev_{nullptr};
  esp_video_init_config_t init_cfg_{};
  esp_video_isp_config_t isp_cfg_{};
  esp_ldo_channel_handle_t ldo_handle_{nullptr};
  bool pipeline_started_{false};

  void setup() override;
  void loop() override;
  void dump_config() override {}

  bool capture_snapshot_to_file(const std::string &path);
  bool start_streaming();
  bool stop_streaming();

 protected:
  bool init_ldo_();
};

}  // namespace mipi_dsi_cam
}  // namespace esphome

