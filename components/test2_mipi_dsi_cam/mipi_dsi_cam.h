#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"

#include <string>
#include <vector>

extern "C" {
  #include "esp_err.h"
  #include "esp_ldo.h"
  #include "esp_cam_sensor.h"
  #include "esp_video_init.h"
  #include "esp_video_isp_ioctl.h"
}

/**
 * Composant ESPHome MIPI-DSI-CAM
 * pour ESP32-P4 et ESP-Video (H.264 + JPEG)
 *
 * Supporte :
 *  - Initialisation complète pipeline CSI → ISP → encodeur
 *  - Contrôle du LDO 2.5V via esp_ldo
 *  - Capture snapshot JPEG vers SD
 *  - Configuration dynamique résolution / framerate / format
 */

namespace esphome {
namespace mipi_dsi_cam {

class MipiDSICamComponent : public Component {
 public:
  // Configuration de base
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

  // Handles & état
  bool pipeline_started_{false};
  esp_cam_sensor_device_t *sensor_dev_{nullptr};
  esp_video_init_config_t init_cfg_{};
  esp_video_isp_config_t isp_cfg_{};
  esp_ldo_channel_handle_t ldo_handle_{nullptr};

  // Fonctions principales
  void setup() override;
  void loop() override;
  void dump_config() override {}

  // Capture
  bool capture_snapshot_to_file(const std::string &path);

 protected:
  bool init_ldo_();
};

}  // namespace mipi_dsi_cam
}  // namespace esphome
