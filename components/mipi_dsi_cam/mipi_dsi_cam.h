#pragma once
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include <string>
#include <vector>

#ifdef USE_SD_CARD
#include "esphome/components/sd_card/sd_card.h"
#endif

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
  float get_setup_priority() const override { return setup_priority::DATA; }
  void dump_config() override;

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
  
#ifdef USE_SD_CARD
  void set_sd_card(sd_card::SDCardComponent *sd) { sd_card_ = sd; }
#endif

  // Capture snapshot vers SD
  bool capture_snapshot_to_file(const std::string &path);

  // Vérification de l'état du pipeline
  bool is_pipeline_ready() const { return pipeline_started_; }

 protected:
  // Configuration
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

#ifdef USE_SD_CARD
  sd_card::SDCardComponent *sd_card_{nullptr};
#endif

  // État du pipeline
  esp_cam_sensor_device_t *sensor_dev_{nullptr};
  esp_video_init_config_t init_cfg_{};
  esp_video_isp_config_t isp_cfg_{};
  bool pipeline_started_{false};

  // Monitoring
  uint32_t last_health_check_{0};
  uint32_t snapshot_count_{0};
  uint32_t error_count_{0};

  // Méthodes internes
  bool check_pipeline_health_();
  void cleanup_pipeline_();
};

// Action pour Home Assistant
template<typename... Ts>
class CaptureSnapshotAction : public Action<Ts...>, public Parented<MipiDSICamComponent> {
 public:
  TEMPLATABLE_VALUE(std::string, filename)

  void play(Ts... x) override {
    auto filename = this->filename_.value(x...);
    if (!this->parent_->capture_snapshot_to_file(filename)) {
      ESP_LOGE("mipi_dsi_cam", "Échec de la capture snapshot vers: %s", filename.c_str());
    }
  }
};

}  // namespace mipi_dsi_cam
}  // namespace esphome



