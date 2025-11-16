#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"

#ifdef USE_ESP_IDF
#include <esp_http_server.h>
#include <esp_event.h>
#include <lwip/sockets.h>
#include "esp_h264_enc_single.h"
#include "esp_h264_enc_single_hw.h"
#include "esp_h264_types.h"
#endif

namespace esphome {
namespace webrtc_camera {

#ifdef USE_ESP_IDF

// RTP Header structure (RFC 3550)
struct RTPHeader {
  uint8_t cc : 4;       // CSRC count
  uint8_t x : 1;        // Extension bit
  uint8_t p : 1;        // Padding bit
  uint8_t v : 2;        // Version (2)
  uint8_t pt : 7;       // Payload type
  uint8_t m : 1;        // Marker bit
  uint16_t seq;         // Sequence number
  uint32_t timestamp;   // Timestamp
  uint32_t ssrc;        // Synchronization source
} __attribute__((packed));

// H.264 NAL Unit types
enum class NALUnitType : uint8_t {
  UNDEFINED = 0,
  SLICE = 1,
  DPA = 2,
  DPB = 3,
  DPC = 4,
  IDR_SLICE = 5,
  SEI = 6,
  SPS = 7,
  PPS = 8,
  AUD = 9,
  END_SEQUENCE = 10,
  END_STREAM = 11,
  FILLER = 12
};

class WebRTCCamera : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_camera(mipi_dsi_cam::MipiDSICamComponent *camera) { camera_ = camera; }
  void set_signaling_port(uint16_t port) { signaling_port_ = port; }
  void set_rtp_port(uint16_t port) { rtp_port_ = port; }
  void set_bitrate(uint32_t bitrate) { bitrate_ = bitrate; }
  void set_gop(uint8_t gop) { gop_ = gop; }
  void set_qp_min(uint8_t qp_min) { qp_min_ = qp_min; }
  void set_qp_max(uint8_t qp_max) { qp_max_ = qp_max; }

 protected:
  mipi_dsi_cam::MipiDSICamComponent *camera_{nullptr};
  uint16_t signaling_port_{8443};
  uint16_t rtp_port_{5004};
  uint32_t bitrate_{2000000};
  uint8_t gop_{30};
  uint8_t qp_min_{10};
  uint8_t qp_max_{40};

  // WebSocket/HTTP server for signaling
  httpd_handle_t signaling_server_{nullptr};

  // RTP streaming
  int rtp_socket_{-1};
  struct sockaddr_in client_addr_{};
  bool client_connected_{false};
  uint16_t rtp_seq_num_{0};
  uint32_t rtp_timestamp_{0};
  uint32_t rtp_ssrc_{0x12345678};

  // H.264 Encoder
  esp_h264_enc_handle_t h264_encoder_{nullptr};
  uint8_t *yuv_buffer_{nullptr};
  size_t yuv_buffer_size_{0};
  uint8_t *h264_buffer_{nullptr};
  size_t h264_buffer_size_{0};

  // Streaming state
  bool streaming_active_{false};
  uint32_t frame_count_{0};
  uint32_t last_idr_frame_{0};

  // Internal methods
  esp_err_t start_signaling_server_();
  void stop_signaling_server_();
  esp_err_t init_h264_encoder_();
  void cleanup_h264_encoder_();
  esp_err_t init_rtp_socket_();
  void cleanup_rtp_socket_();

  // Video streaming
  esp_err_t convert_rgb565_to_yuv420_(const uint8_t *rgb565, uint8_t *yuv420,
                                       uint16_t width, uint16_t height);
  esp_err_t encode_and_send_frame_();
  esp_err_t send_h264_over_rtp_(const uint8_t *data, size_t len,
                                 esp_h264_frame_type_t frame_type, uint32_t timestamp);
  esp_err_t send_rtp_packet_(const uint8_t *payload, size_t len, bool marker);

  // Parse H.264 NAL units
  std::vector<std::pair<const uint8_t *, size_t>> parse_nal_units_(const uint8_t *data, size_t len);

  // HTTP/WebSocket handlers
  static esp_err_t ws_handler_(httpd_req_t *req);
  static esp_err_t sdp_handler_(httpd_req_t *req);
  static esp_err_t index_handler_(httpd_req_t *req);

  // Helper to get instance from httpd request
  static WebRTCCamera *get_instance_(httpd_req_t *req);
};

#endif  // USE_ESP_IDF

}  // namespace webrtc_camera
}  // namespace esphome
