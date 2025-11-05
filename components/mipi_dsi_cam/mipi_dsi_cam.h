#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include <string>
#include <vector>

// Forward declarations des types C
struct esp_cam_sensor_device_t;
struct esp_video_init_config_t;

// Définition du type ISP config basée sur le code source ESP-Video
// Voir esp_video_pipeline_isp.c ligne 1053+
#ifndef ESP_VIDEO_ISP_CONFIG_DEFINED
#define ESP_VIDEO_ISP_CONFIG_DEFINED

typedef struct {
    const char *isp_dev;      // Device ISP (ex: "/dev/video20")
    const char *cam_dev;      // Device caméra source (ex: "/dev/video0")
    void *ipa_config;         // Configuration IPA (Image Processing Algorithms)
} esp_video_isp_config_t;

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
  void set_i2c_id(int id) { i2c_id_ = id; i2c_bus_name_.clear(); }
  void set_i2c_id(const std::string &bus_name) {
    i2c_bus_name_ = bus_name;
    // Essayer de parser comme int si possible (sans exceptions)
    char *end;
    long val = strtol(bus_name.c_str(), &end, 10);
    if (end != bus_name.c_str() && *end == '\0') {
      i2c_id_ = (int)val;
    } else {
      i2c_id_ = 0; // Défaut
    }
  }
  void set_lane(int l) { lane_ = l; }
  void set_xclk_pin(const std::string &p) { xclk_pin_ = p; }
  void set_xclk_freq(int f) { xclk_freq_ = f; }
  void set_sensor_addr(int a) { sensor_addr_ = a; }
  void set_resolution(const std::string &r) { resolution_ = r; }
  void set_pixel_format(const std::string &f) { pixel_format_ = f; }
  void set_framerate(int f) { framerate_ = f; }
  void set_jpeg_quality(int q) { jpeg_quality_ = q; }

  // Capture snapshot vers SD
  bool capture_snapshot_to_file(const std::string &path);

  // Vérification de l'état du pipeline
  bool is_pipeline_ready() const { return pipeline_started_; }

  // Méthodes pour lvgl_camera_display (stubs pour compatibilité)
  bool is_streaming() const { return pipeline_started_; }
  bool capture_frame() { return true; }  // Stub - pas utilisé avec ESP-Video
  uint8_t* get_image_data() { return nullptr; }  // Stub - géré différemment
  uint16_t get_image_width() const { return 0; }
  uint16_t get_image_height() const { return 0; }

 protected:
  // Configuration
  std::string sensor_name_{"sc202cs"};
  int i2c_id_{0};
  std::string i2c_bus_name_;  // Nom optionnel du bus I2C (ex: "bsp_bus")
  int lane_{1};
  std::string xclk_pin_{"GPIO36"};
  int xclk_freq_{24000000};
  int sensor_addr_{0x36};
  std::string resolution_{"720P"};
  std::string pixel_format_{"JPEG"};
  int framerate_{30};
  int jpeg_quality_{10};

  // État du pipeline - utilisation de pointeurs opaques
  void *sensor_dev_{nullptr};
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

// Alias pour compatibilité avec lvgl_camera_display
using MipiDsiCam = MipiDSICamComponent;

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












