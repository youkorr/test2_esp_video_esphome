#pragma once

#include "esphome/core/component.h"
#include "esphome/components/lvgl/lvgl_esphome.h"

#include "esp_http_client.h"
#include "driver/jpeg_decode.h"

// H264 decoder
extern "C" {
#include "esp_h264_dec.h"
#include "esp_h264_dec_sw.h"
}

#include <sys/socket.h>
#include <netinet/in.h>

namespace esphome {
namespace network_camera {

enum class Protocol {
  MJPEG,
  RTSP,
};

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
  void set_protocol(const std::string &protocol) {
    if (protocol == "rtsp") {
      this->protocol_ = Protocol::RTSP;
    } else {
      this->protocol_ = Protocol::MJPEG;
    }
  }

  void configure_canvas(lv_obj_t *canvas);

  float get_setup_priority() const override { return setup_priority::LATE; }

  // Static callback for LVGL timer
  static void lvgl_timer_callback_(lv_timer_t *timer);

 protected:
  std::string url_{};
  uint16_t width_{640};
  uint16_t height_{480};
  Protocol protocol_{Protocol::MJPEG};

  lv_obj_t *canvas_obj_{nullptr};

  uint32_t update_interval_{100};
  uint32_t last_update_{0};

  uint32_t frame_count_{0};
  bool first_update_{true};
  bool canvas_warning_shown_{false};
  bool enabled_{true};

  lv_timer_t *lvgl_timer_{nullptr};

  // WiFi connection retry management
  uint32_t last_connection_attempt_{0};
  uint32_t connection_retry_delay_{15000};  // 15 seconds initial delay
  uint8_t connection_attempts_{0};

  // Network quality adaptation
  uint32_t last_quality_check_{0};
  uint32_t quality_check_interval_{5000};  // Check every 5 seconds
  uint8_t current_quality_level_{1};  // 0=low, 1=medium, 2=high

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

  // MJPEG parse buffer (dynamically allocated in PSRAM to save SRAM)
  uint8_t *parse_buffer_{nullptr};
  size_t parse_buffer_size_{0};
  size_t parse_buffer_len_{0};

  // HTTP client (MJPEG)
  esp_http_client_handle_t http_client_{nullptr};
  bool stream_connected_{false};
  uint32_t stream_connect_time_{0};  // Time when stream was connected
  uint32_t stream_reconnect_interval_{180000};  // Reconnect every 3 minutes (180 seconds)

  // MJPEG parsing state
  enum class MjpegState {
    SEARCHING_BOUNDARY,
    READING_HEADERS,
    READING_CONTENT,
  };
  MjpegState mjpeg_state_{MjpegState::SEARCHING_BOUNDARY};
  size_t content_length_{0};

  // RTSP client
  int rtsp_socket_{-1};
  int rtp_socket_{-1};
  uint16_t rtp_port_{0};
  std::string rtsp_session_{};
  std::string rtsp_auth_{};  // Base64 encoded credentials
  int cseq_{1};

  // H264 decoder
  esp_h264_dec_handle_t h264_decoder_{nullptr};

  // H264 receive buffer
  uint8_t *h264_buffer_{nullptr};
  size_t h264_buffer_size_{0};
  size_t h264_data_len_{0};

  // H264 SPS/PPS caching (required for proper decoder initialization)
  uint8_t sps_cache_[128]{0};  // SPS cache (typically < 50 bytes)
  size_t sps_len_{0};
  uint8_t pps_cache_[64]{0};   // PPS cache (typically < 20 bytes)
  size_t pps_len_{0};
  bool has_sps_{false};
  bool has_pps_{false};
  bool param_sets_sent_{false};      // Track if SPS/PPS sent for normal NAL units
  bool param_sets_sent_fua_{false};  // Track if SPS/PPS sent for fragmented units

  // YUV buffer for H264 output
  uint8_t *yuv_buffer_{nullptr};
  size_t yuv_buffer_size_{0};

  // Common methods
  bool init_buffers_();
  void free_buffers_();  // Free PSRAM buffers when camera is disabled
  bool init_jpeg_decoder_();
  void update_canvas_();
  void swap_buffers_();
  void check_network_quality_();  // Monitor network conditions
  void adapt_to_network_();

  // MJPEG methods
  bool connect_mjpeg_stream_();
  void disconnect_mjpeg_stream_();
  bool fetch_jpeg_frame_();
  size_t strip_jpeg_com_markers_(uint8_t *data, size_t len);  // Strip COM markers from JPEG
  size_t strip_jpeg_restart_markers_(uint8_t *data, size_t len);  // Strip RST markers (unsupported by hardware)
  bool decode_jpeg_to_rgb565_();

  // RTSP methods
  bool connect_rtsp_stream_();
  void disconnect_rtsp_stream_();
  bool init_h264_decoder_();
  bool send_rtsp_request_(const std::string &method, const std::string &url, const std::string &extra_headers = "", std::string *response_body = nullptr);
  bool parse_rtsp_response_(std::string &response);
  bool fetch_rtp_frame_();
  bool decode_h264_to_yuv_();
  void convert_yuv420_to_rgb565_(uint8_t *yuv, uint8_t *rgb565, int width, int height);
};

}  // namespace network_camera
}  // namespace esphome
