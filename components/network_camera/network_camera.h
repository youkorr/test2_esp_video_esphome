#pragma once

#include "esphome/core/component.h"
#include "esphome/components/lvgl/lvgl_esphome.h"

#include "esp_http_client.h"
#include "driver/jpeg_decode.h"

namespace esphome {
namespace network_camera {

class NetworkCamera : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_url(const std::string &url) { this->url_ = url; }
  void set_width(uint16_t width) { this->width_ = width; }
  void set_height(uint16_t height) { this->height_ = height; }
  void set_update_interval(uint32_t interval_ms) { this->update_interval_ = interval_ms; }
  void set_enabled(bool enabled) { this->enabled_ = enabled; }

  void configure_canvas(lv_obj_t *canvas);

  float get_setup_priority() const override { return setup_priority::LATE; }

  // Static callback for LVGL timer
  static void lvgl_timer_callback_(lv_timer_t *timer);

 protected:
  std::string url_{};
  uint16_t width_{640};
  uint16_t height_{480};

  lv_obj_t *canvas_obj_{nullptr};

  uint32_t update_interval_{100};
  uint32_t last_update_{0};

  uint32_t frame_count_{0};
  bool first_update_{true};
  bool canvas_warning_shown_{false};
  bool enabled_{true};  // Enabled by default for network camera

  lv_timer_t *lvgl_timer_{nullptr};

  // JPEG decoder
  jpeg_decoder_handle_t jpeg_decoder_{nullptr};

  // Double buffer for RGB565 output
  uint8_t *rgb565_buffer_a_{nullptr};
  uint8_t *rgb565_buffer_b_{nullptr};
  uint8_t *current_display_buffer_{nullptr};
  uint8_t *current_decode_buffer_{nullptr};
  size_t rgb565_buffer_size_{0};

  // JPEG receive buffer
  uint8_t *jpeg_buffer_{nullptr};
  size_t jpeg_buffer_size_{0};
  size_t jpeg_data_len_{0};

  // HTTP client
  esp_http_client_handle_t http_client_{nullptr};
  bool stream_connected_{false};

  // MJPEG parsing state
  enum class MjpegState {
    SEARCHING_BOUNDARY,
    READING_HEADERS,
    READING_CONTENT,
  };
  MjpegState mjpeg_state_{MjpegState::SEARCHING_BOUNDARY};
  size_t content_length_{0};

  bool init_buffers_();
  bool init_jpeg_decoder_();
  bool connect_stream_();
  void disconnect_stream_();
  bool fetch_jpeg_frame_();
  bool decode_jpeg_to_rgb565_();
  void update_canvas_();
  void swap_buffers_();
};

}  // namespace network_camera
}  // namespace esphome
