#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/i2c/i2c.h"
#include <string>
#include <mutex>

extern "C" {
#include "linux/videodev2.h"
#include "driver/ppa.h"
#include "driver/jpeg_decode.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
}

namespace esphome {
namespace mipi_dsi_cam {

// Nombre de buffers vidéo pour le double buffering
#define VIDEO_BUFFER_COUNT 2

enum PixelFormat {
  PIXEL_FORMAT_RGB565 = 0,
  PIXEL_FORMAT_YUV422 = 1,
  PIXEL_FORMAT_RAW8 = 2,
};

/**
 * @brief Composant caméra MIPI CSI utilisant V4L2 API directement
 *
 * Ce composant suit le pattern de la démo M5Stack:
 * - Utilise /dev/video0 créé par esp_video_init()
 * - API V4L2 pure (VIDIOC_*)
 * - mmap() pour les buffers
 * - Mutex pour la thread-safety
 */
class MipiDSICamComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
    // DATA: s'initialise après esp_video qui crée les devices
    return setup_priority::DATA;
  }

  // Configuration I2C et sensor
  void set_i2c_bus(i2c::I2CBus *bus) { this->i2c_bus_ = bus; }
  void set_sensor(const std::string &sensor) { this->sensor_ = sensor; }
  void set_external_clock_pin(uint8_t pin) { this->external_clock_pin_ = pin; }
  void set_frequency(uint32_t freq) { this->frequency_ = freq; }

  // Configuration résolution/format
  void set_resolution(const std::string &res) { this->resolution_ = res; }
  void set_pixel_format(const std::string &fmt) { this->pixel_format_ = fmt; }
  void set_framerate(uint8_t fps) { this->framerate_ = fps; }
  void set_jpeg_quality(uint8_t quality) { this->jpeg_quality_ = quality; }
  void set_auto_start(bool auto_start) { this->auto_start_ = auto_start; }

  // Configuration PPA
  void set_mirror_x(bool mirror) { this->mirror_x_ = mirror; }
  void set_mirror_y(bool mirror) { this->mirror_y_ = mirror; }
  void set_rotation(int angle) { this->rotation_angle_ = angle; }

  // API pour lvgl_camera_display
  bool is_pipeline_ready() const { return this->initialized_; }
  bool start_streaming();
  bool stop_streaming();
  bool is_streaming() const;
  bool capture_frame();
  uint8_t* get_image_data();
  size_t get_image_size() const { return this->frame_size_; }
  uint16_t get_image_width() const { return this->width_; }
  uint16_t get_image_height() const { return this->height_; }

  // FreeRTOS task pour capture haute performance (comme M5Stack demo)
  bool start_camera_task(lv_obj_t* canvas);
  void stop_camera_task();

  // Friend function pour accès depuis la tâche FreeRTOS
  friend void camera_task_function(void* arg);

 protected:
  // Configuration I2C et sensor
  i2c::I2CBus *i2c_bus_{nullptr};
  std::string sensor_{"sc202cs"};
  uint8_t external_clock_pin_{36};
  uint32_t frequency_{24000000};

  // Configuration résolution/format
  std::string resolution_{"720P"};
  std::string pixel_format_{"RGB565"};
  uint8_t framerate_{30};
  uint8_t jpeg_quality_{10};
  bool auto_start_{false};  // Ne PAS démarrer automatiquement le streaming

  // Configuration PPA
  bool mirror_x_{true};     // Mirror horizontal (comme la demo M5Stack)
  bool mirror_y_{false};
  int rotation_angle_{0};   // 0, 90, 180, 270

  // État
  bool initialized_{false};
  bool streaming_{false};
  std::mutex camera_mutex_;

  // FreeRTOS task (pour performance optimale comme M5Stack)
  TaskHandle_t camera_task_handle_{nullptr};
  lv_obj_t* canvas_{nullptr};
  bool task_running_{false};
  uint32_t frame_count_{0};
  uint32_t last_fps_time_{0};

  // V4L2
  int video_fd_{-1};
  uint16_t width_{0};
  uint16_t height_{0};
  uint32_t v4l2_pixelformat_{0};
  size_t frame_size_{0};

  // Buffers mmap (entrée V4L2)
  uint8_t* buffers_[VIDEO_BUFFER_COUNT]{nullptr};

  // JPEG Hardware Decoder (ESP32-P4)
  jpeg_decoder_handle_t jpeg_handle_{nullptr};
  uint8_t* jpeg_decode_buffer_{nullptr};  // Buffer RGB565 décodé (DMA + SPIRAM)
  size_t jpeg_decode_buffer_size_{0};

  // PPA (Pixel Processing Accelerator)
  ppa_client_handle_t ppa_handle_{nullptr};
  uint8_t* output_buffer_{nullptr};  // Buffer de sortie PPA (DMA + SPIRAM)
  size_t output_buffer_size_{0};

  // Helpers
  bool open_video_device_();
  bool setup_buffers_();
  bool setup_jpeg_decoder_();
  bool setup_ppa_();
  bool start_stream_();
  bool stop_stream_();
  uint32_t map_pixel_format_(const std::string &fmt);
  bool parse_resolution_(const std::string &res, uint16_t &w, uint16_t &h);
  ppa_srm_rotation_angle_t map_rotation_(int angle);
};

// Alias pour compatibilité
using MipiDsiCam = MipiDSICamComponent;

}  // namespace mipi_dsi_cam
}  // namespace esphome
