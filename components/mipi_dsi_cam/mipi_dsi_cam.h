#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/i2c/i2c.h"
#include <string>

#ifdef USE_ESP32_VARIANT_ESP32P4

// ==== ESP-Video (v1.3.1) ====
extern "C" {
  #include "esp_video_init.h"
  #include "esp_video_device.h"
  #include "linux/videodev2.h"
  #include "esp_cam_sensor.h"
  #include "driver/ppa.h"
  #include "esp_heap_caps.h"
  #include "esp_err.h"
  #include "driver/gpio.h"
}

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#endif  // USE_ESP32_VARIANT_ESP32P4

namespace esphome {
namespace mipi_dsi_cam {

// ============================================================
// ENUMS ET INTERFACES
// ============================================================

enum PixelFormat {
  PIXEL_FORMAT_RGB565 = 0,
  PIXEL_FORMAT_YUV422 = 1,
  PIXEL_FORMAT_RAW8   = 2,
  PIXEL_FORMAT_JPEG   = 3,
  PIXEL_FORMAT_H264   = 4,
};

// Interface de pilote capteur
class ISensorDriver {
 public:
  virtual ~ISensorDriver() = default;

  virtual const char *get_name() const = 0;
  virtual uint16_t get_pid() const = 0;
  virtual uint8_t get_i2c_address() const = 0;
  virtual uint8_t get_lane_count() const = 0;
  virtual uint8_t get_bayer_pattern() const = 0;
  virtual uint16_t get_lane_bitrate_mbps() const = 0;
  virtual uint16_t get_width() const = 0;
  virtual uint16_t get_height() const = 0;
  virtual uint8_t get_fps() const = 0;

  virtual esp_err_t init() = 0;
  virtual esp_err_t read_id(uint16_t *pid) = 0;
  virtual esp_err_t start_stream() = 0;
  virtual esp_err_t stop_stream() = 0;
  virtual esp_err_t set_gain(uint32_t gain_index) = 0;
  virtual esp_err_t set_exposure(uint32_t exposure) = 0;
  virtual esp_err_t write_register(uint16_t reg, uint8_t value) = 0;
  virtual esp_err_t read_register(uint16_t reg, uint8_t *value) = 0;
};

// ============================================================
// CLASSE PRINCIPALE
// ============================================================

/**
 * @brief Composant caméra MIPI-CSI avec ESP-Video (équivalent Tab5)
 * 
 * Pipeline :
 *   Sensor → CSI → ESP-Video → /dev/video0 → PPA → Display Buffer
 */
class MipiDsiCam : public Component, public i2c::I2CDevice {
 public:
  // --- Cycle de vie ---
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // --- Configuration ---
  void set_name(const std::string &name) { this->name_ = name; }
  void set_external_clock_pin(uint8_t pin) { this->external_clock_pin_ = pin; }
  void set_external_clock_frequency(uint32_t freq) { this->external_clock_frequency_ = freq; }
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_sensor_type(const std::string &type) { this->sensor_type_ = type; }
  void set_sensor_address(uint8_t addr) { this->sensor_address_ = addr; }
  void set_lane_count(uint8_t lanes) { this->lane_count_ = lanes; }
  void set_bayer_pattern(uint8_t pattern) { this->bayer_pattern_ = pattern; }
  void set_lane_bitrate(uint16_t mbps) { this->lane_bitrate_mbps_ = mbps; }
  void set_resolution(uint16_t w, uint16_t h) { this->width_ = w; this->height_ = h; }
  void set_pixel_format(PixelFormat fmt) { this->pixel_format_ = fmt; }
  void set_jpeg_quality(uint8_t q) { this->jpeg_quality_ = q; }
  void set_framerate(uint8_t fps) { this->framerate_ = fps; }

  // --- PPA (Post-Processing Accelerator) ---
  void set_mirror_x(bool enable) { this->mirror_x_ = enable; }
  void set_mirror_y(bool enable) { this->mirror_y_ = enable; }
  void set_rotation(uint8_t angle) { this->rotation_angle_ = angle; }

  // --- API publique ---
  bool capture_frame();
  bool start_streaming();
  bool stop_streaming();
  bool is_streaming() const { return this->streaming_; }

  uint8_t *get_image_data() { return this->display_buffer_; }
  size_t get_image_size() const { return this->display_buffer_size_; }
  uint16_t get_image_width() const { return this->width_; }
  uint16_t get_image_height() const { return this->height_; }
  PixelFormat get_pixel_format() const { return this->pixel_format_; }

 protected:
  // --- Configuration interne ---
  std::string name_{"MIPI Camera"};
  uint8_t external_clock_pin_{36};
  uint32_t external_clock_frequency_{24000000};
  GPIOPin *reset_pin_{nullptr};

  std::string sensor_type_{""};
  uint8_t sensor_address_{0x36};
  uint8_t lane_count_{1};
  uint8_t bayer_pattern_{0};
  uint16_t lane_bitrate_mbps_{576};
  uint16_t width_{1280};
  uint16_t height_{720};

  PixelFormat pixel_format_{PIXEL_FORMAT_RGB565};
  uint8_t jpeg_quality_{10};
  uint8_t framerate_{30};

  // --- PPA ---
  bool mirror_x_{true};
  bool mirror_y_{false};
  uint8_t rotation_angle_{0};

  bool initialized_{false};
  bool streaming_{false};

  uint32_t total_frames_captured_{0};
  uint32_t last_fps_report_time_{0};

  ISensorDriver *sensor_driver_{nullptr};

#ifdef USE_ESP32_VARIANT_ESP32P4
  // --- V4L2 ---
  int video_fd_{-1};
  struct v4l2_buffer *v4l2_buffers_{nullptr};
  uint32_t buffer_count_{2};

  struct BufferMapping {
    void *start{nullptr};
    size_t length{0};
  };
  BufferMapping *buffer_mappings_{nullptr};

  // --- PPA ---
  ppa_client_handle_t ppa_handle_{nullptr};

  // --- Buffers d'affichage ---
  uint8_t *display_buffer_{nullptr};
  size_t display_buffer_size_{0};

  // --- Méthodes internes ---
  bool create_sensor_driver_();
  bool init_sensor_();
  bool init_esp_video_();              // ✅ version CSI+JPEG uniquement
  bool open_video_device_();
  bool configure_video_format_();
  bool setup_video_buffers_();
  bool init_ppa_();
  bool allocate_display_buffer_();
  bool start_video_stream_();

  uint32_t get_v4l2_pixformat_() const;
  ppa_srm_rotation_angle_t get_ppa_rotation_() const;
#endif
};

}  // namespace mipi_dsi_cam
}  // namespace esphome

