#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include <string>
#include <mutex>

extern "C" {
#include "linux/videodev2.h"
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

  // Configuration
  void set_resolution(const std::string &res) { this->resolution_ = res; }
  void set_pixel_format(const std::string &fmt) { this->pixel_format_ = fmt; }
  void set_framerate(uint8_t fps) { this->framerate_ = fps; }

  // API pour lvgl_camera_display
  bool start_streaming();
  bool stop_streaming();
  bool is_streaming() const;
  bool capture_frame();
  uint8_t* get_image_data();
  size_t get_image_size() const { return this->frame_size_; }
  uint16_t get_image_width() const { return this->width_; }
  uint16_t get_image_height() const { return this->height_; }

 protected:
  // Configuration
  std::string resolution_{"720P"};
  std::string pixel_format_{"RGB565"};
  uint8_t framerate_{30};

  // État
  bool initialized_{false};
  bool streaming_{false};
  std::mutex camera_mutex_;

  // V4L2
  int video_fd_{-1};
  uint16_t width_{0};
  uint16_t height_{0};
  uint32_t v4l2_pixelformat_{0};
  size_t frame_size_{0};

  // Buffers mmap
  uint8_t* buffers_[VIDEO_BUFFER_COUNT]{nullptr};
  uint8_t* current_frame_{nullptr};

  // Helpers
  bool open_video_device_();
  bool setup_buffers_();
  bool start_stream_();
  bool stop_stream_();
  uint32_t map_pixel_format_(const std::string &fmt);
  bool parse_resolution_(const std::string &res, uint16_t &w, uint16_t &h);
};

// Alias pour compatibilité
using MipiDsiCam = MipiDSICamComponent;

}  // namespace mipi_dsi_cam
}  // namespace esphome
