#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/i2c/i2c.h"
#include <string>

#ifdef USE_ESP32_VARIANT_ESP32P4

// Headers ESP-Video
extern "C" {
  #include "esp_video_init.h"
  #include "esp_video_device.h"
  #include "linux/videodev2.h"
  #include "esp_cam_sensor.h"
  #include "driver/ppa.h"  // NOUVEAU - PPA comme Tab5
  #include "esp_heap_caps.h"
}

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#endif

namespace esphome {
namespace mipi_dsi_cam {

// Forward declarations
class MipiDsiCamV4L2Adapter;
class MipiDsiCamISPPipeline;

// Interface pour les drivers de capteurs
class ISensorDriver {
public:
  virtual ~ISensorDriver() = default;
  
  virtual const char* get_name() const = 0;
  virtual uint16_t get_pid() const = 0;
  virtual uint8_t get_i2c_address() const = 0;
  virtual uint8_t get_lane_count() const = 0;
  virtual uint8_t get_bayer_pattern() const = 0;
  virtual uint16_t get_lane_bitrate_mbps() const = 0;
  virtual uint16_t get_width() const = 0;
  virtual uint16_t get_height() const = 0;
  virtual uint8_t get_fps() const = 0;
  
  virtual esp_err_t init() = 0;
  virtual esp_err_t read_id(uint16_t* pid) = 0;
  virtual esp_err_t start_stream() = 0;
  virtual esp_err_t stop_stream() = 0;
  virtual esp_err_t set_gain(uint32_t gain_index) = 0;
  virtual esp_err_t set_exposure(uint32_t exposure) = 0;
  virtual esp_err_t write_register(uint16_t reg, uint8_t value) = 0;
  virtual esp_err_t read_register(uint16_t reg, uint8_t* value) = 0;
};

enum class PixelFormat {
  PIXEL_FORMAT_RGB565 = 0,
  PIXEL_FORMAT_YUV422 = 1,
  PIXEL_FORMAT_RAW8 = 2,
};

class MipiDsiCam : public Component, public i2c::I2CDevice {
public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  
  // Configuration
  void set_name(const std::string &name) { this->name_ = name; }
  void set_external_clock_pin(int pin) { this->external_clock_pin_ = pin; }
  void set_external_clock_frequency(uint32_t freq) { this->external_clock_frequency_ = freq; }
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_sensor_type(const std::string &type) { this->sensor_type_ = type; }
  void set_sensor_address(uint8_t addr) { this->address_ = addr; }
  void set_lane_count(uint8_t lanes) { this->lane_count_ = lanes; }
  void set_resolution(uint16_t width, uint16_t height) {
    this->width_ = width;
    this->height_ = height;
  }
  void set_bayer_pattern(uint8_t pattern) { this->bayer_pattern_ = pattern; }
  void set_lane_bitrate(uint16_t bitrate) { this->lane_bitrate_mbps_ = bitrate; }
  void set_pixel_format(PixelFormat format) { this->pixel_format_ = format; }
  void set_jpeg_quality(uint8_t quality) { this->jpeg_quality_ = quality; }
  void set_framerate(uint8_t fps) { this->framerate_ = fps; }
  
  // Configuration V4L2, ISP et encodeurs
  void set_enable_v4l2(bool enable) { this->enable_v4l2_on_setup_ = enable; }
  void set_enable_isp(bool enable) { this->enable_isp_on_setup_ = enable; }

  // ✅ Nouveaux setters pour les encodeurs
  void set_enable_jpeg(bool enable) { this->enable_jpeg_on_setup_ = enable; }
  void set_enable_h264(bool enable) { this->enable_h264_on_setup_ = enable; }

  // Getters
  std::string get_name() const { return this->name_; }
  uint16_t get_image_width() const { return this->width_; }
  uint16_t get_image_height() const { return this->height_; }
  size_t get_image_size() const { return this->frame_buffer_size_; }
  uint8_t* get_image_data() const { return this->current_frame_buffer_; }
  bool is_streaming() const { return this->streaming_; }
  bool is_initialized() const { return this->initialized_; }
  bool has_external_clock() const { return this->external_clock_pin_ >= 0; }
  
  // Getters pour les adaptateurs
  MipiDsiCamV4L2Adapter* get_v4l2_adapter() const { return this->v4l2_adapter_; }
  MipiDsiCamISPPipeline* get_isp_pipeline() const { return this->isp_pipeline_; }
  
  // Gestion de la séquence de frames
  uint32_t get_frame_sequence() const { return this->frame_sequence_; }
  uint32_t get_current_sequence() const { return this->locked_sequence_; }
  
  // Contrôle du streaming
  bool start_streaming();
  bool stop_streaming();
  
  // Nouvelle API de gestion des frames avec verrouillage
  bool acquire_frame(uint32_t last_served_sequence);
  void release_frame();
  
  // Legacy API (pour compatibilité)
  bool capture_frame();
  
  // Copie de frame avec options
  size_t copy_frame_rgb565(uint8_t *dest, size_t max_size, bool apply_white_balance = false);
  
  // Contrôle exposition/gain
  void set_auto_exposure(bool enabled);
  void set_ae_target_brightness(uint8_t target);
  void set_manual_exposure(uint16_t exposure);
  void set_manual_gain(uint8_t gain_index);
  void set_brightness_level(uint8_t level);
  void adjust_exposure(uint16_t exposure_value);
  void adjust_gain(uint8_t gain_index);

  uint8_t get_fps() const { return this->framerate_; }
  // Balance des blancs
  void set_auto_white_balance(bool enable);
  void set_white_balance_gains(float red, float green, float blue, bool update_fixed = true);
  
  // Activer les adaptateurs
  void enable_v4l2_adapter();
  void enable_isp_pipeline();
  
protected:
  // Configuration
  std::string name_;
  int external_clock_pin_{-1};
  uint32_t external_clock_frequency_{24000000};
  GPIOPin *reset_pin_{nullptr};
  std::string sensor_type_;
  
  // Paramètres capteur
  uint8_t lane_count_{1};
  uint8_t bayer_pattern_{0};
  uint16_t lane_bitrate_mbps_{576};
  uint16_t width_{1280};
  uint16_t height_{720};
  uint8_t framerate_{30};
  PixelFormat pixel_format_{PixelFormat::PIXEL_FORMAT_RGB565};
  uint8_t jpeg_quality_{10};
  
  // État
  bool initialized_{false};
  bool streaming_{false};
  
  // Système de verrouillage de frames
  bool frame_ready_{false};
  bool frame_locked_{false};
  uint32_t frame_sequence_{0};
  uint32_t locked_sequence_{0};
  
  // Buffers
  uint8_t *frame_buffers_[2]{nullptr, nullptr};
  size_t frame_buffer_size_{0};
  uint8_t *current_frame_buffer_{nullptr};
  uint8_t buffer_index_{0};
  
  // Hardware handles
  ISensorDriver *sensor_driver_{nullptr};
  esp_ldo_channel_handle_t ldo_handle_{nullptr};
  esp_cam_ctlr_handle_t csi_handle_{nullptr};
  isp_proc_handle_t isp_handle_{nullptr};
  isp_awb_ctlr_t awb_ctlr_{nullptr};
  
  // Statistiques
  uint32_t total_frames_received_{0};
  uint32_t last_frame_log_time_{0};
  
  // Auto Exposure
  bool auto_exposure_enabled_{true};
  uint8_t ae_target_brightness_{128};
  uint16_t current_exposure_{0x9C0};
  uint8_t current_gain_index_{0};
  uint32_t last_ae_update_{0};
  
  // White Balance
  bool auto_white_balance_enabled_{false};
  float wb_red_gain_{1.0f};
  float wb_green_gain_{1.0f};
  float wb_blue_gain_{1.0f};
  uint16_t wb_red_gain_fixed_{256};
  uint16_t wb_green_gain_fixed_{256};
  uint16_t wb_blue_gain_fixed_{256};
  uint32_t last_awb_update_{0};
  
  // Adaptateurs optionnels
  MipiDsiCamV4L2Adapter *v4l2_adapter_{nullptr};
  MipiDsiCamISPPipeline *isp_pipeline_{nullptr};
  bool enable_v4l2_on_setup_{false};
  bool enable_isp_on_setup_{false};

  // ✅ Nouveaux drapeaux encodeurs
  bool enable_jpeg_on_setup_{false};
  bool enable_h264_on_setup_{false};
  
  // Méthodes d'initialisation
  bool create_sensor_driver_();
  bool init_sensor_();
  bool init_external_clock_();
  bool init_ldo_();
  bool init_csi_();
  bool init_isp_();
  bool allocate_buffer_();
  void configure_white_balance_();
  
  // Auto Exposure & White Balance
  void update_auto_exposure_();
  void update_auto_white_balance_();
  uint32_t calculate_brightness_();
  
  // Callbacks CSI
  static bool IRAM_ATTR on_csi_new_frame_(esp_cam_ctlr_handle_t handle,
                                          esp_cam_ctlr_trans_t *trans,
                                          void *user_data);
  
  static bool IRAM_ATTR on_csi_frame_done_(esp_cam_ctlr_handle_t handle,
                                           esp_cam_ctlr_trans_t *trans,
                                           void *user_data);
};

// Factory function (déclarée dans le header généré)
extern ISensorDriver* create_sensor_driver(const std::string& sensor_type, i2c::I2CDevice* i2c);

} // namespace mipi_dsi_cam
} // namespace esphome

#endif // USE_ESP32_VARIANT_ESP32P4


