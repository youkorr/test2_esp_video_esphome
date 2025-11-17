#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/mipi_dsi_cam/mipi_dsi_cam.h"

#ifdef USE_ESP_IDF
#include <lwip/sockets.h>
#include <string>
#include <vector>
#include <map>
#include "esp_h264_enc_single.h"
#include "esp_h264_enc_single_hw.h"  // Hardware encoder (ESP32-P4)
#include "esp_h264_types.h"
#endif

namespace esphome {
namespace rtsp_server {

#ifdef USE_ESP_IDF

// RTP Header (RFC 3550)
struct RTPHeader {
  uint8_t cc : 4;       // CSRC count
  uint8_t x : 1;        // Extension
  uint8_t p : 1;        // Padding
  uint8_t v : 2;        // Version
  uint8_t pt : 7;       // Payload type
  uint8_t m : 1;        // Marker
  uint16_t seq;         // Sequence number
  uint32_t timestamp;   // Timestamp
  uint32_t ssrc;        // SSRC
} __attribute__((packed));

// RTSP Methods
enum class RTSPMethod {
  OPTIONS,
  DESCRIBE,
  SETUP,
  PLAY,
  PAUSE,
  TEARDOWN,
  UNKNOWN
};

// RTSP Session state
enum class RTSPState {
  INIT,
  READY,
  PLAYING
};

// Client session info
struct RTSPSession {
  int socket_fd;
  std::string session_id;
  RTSPState state;
  uint16_t client_rtp_port;
  uint16_t client_rtcp_port;
  struct sockaddr_in client_addr;
  uint32_t last_activity;
  bool active;
};

class RTSPServer : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Configuration setters
  void set_camera(mipi_dsi_cam::MipiDSICamComponent *camera) { camera_ = camera; }
  void set_port(uint16_t port) { rtsp_port_ = port; }
  void set_stream_path(const std::string &path) { stream_path_ = path; }
  void set_rtp_port(uint16_t port) { rtp_port_ = port; }
  void set_rtcp_port(uint16_t port) { rtcp_port_ = port; }
  void set_bitrate(uint32_t bitrate) { bitrate_ = bitrate; }
  void set_gop(uint8_t gop) { gop_ = gop; }
  void set_qp_min(uint8_t qp) { qp_min_ = qp; }
  void set_qp_max(uint8_t qp) { qp_max_ = qp; }
  void set_max_clients(uint8_t max) { max_clients_ = max; }
  void set_username(const std::string &username) { username_ = username; }
  void set_password(const std::string &password) { password_ = password; }

 protected:
  mipi_dsi_cam::MipiDSICamComponent *camera_{nullptr};
  uint16_t rtsp_port_{554};
  std::string stream_path_{"/stream"};
  uint16_t rtp_port_{5004};
  uint16_t rtcp_port_{5005};
  uint32_t bitrate_{2000000};
  uint8_t gop_{30};
  uint8_t qp_min_{10};
  uint8_t qp_max_{40};
  uint8_t max_clients_{3};
  std::string username_{""};
  std::string password_{""};

  // RTSP server socket
  int rtsp_socket_{-1};
  std::vector<RTSPSession> sessions_;

  // RTP streaming
  int rtp_socket_{-1};
  int rtcp_socket_{-1};
  uint16_t rtp_seq_num_{0};
  uint32_t rtp_timestamp_{0};
  uint32_t rtp_ssrc_{0};

  // H.264 encoder
  esp_h264_enc_handle_t h264_encoder_{nullptr};
  uint8_t *yuv_buffer_{nullptr};
  size_t yuv_buffer_size_{0};
  uint8_t *h264_buffer_{nullptr};
  size_t h264_buffer_size_{0};

  // Streaming state
  bool streaming_active_{false};
  uint32_t frame_count_{0};
  uint8_t *sps_data_{nullptr};
  size_t sps_size_{0};
  uint8_t *pps_data_{nullptr};
  size_t pps_size_{0};

  // Streaming task (separate from loopTask to avoid stack overflow)
  TaskHandle_t streaming_task_handle_{nullptr};
  static void streaming_task_wrapper_(void *param);

  // Preallocated buffers to reduce stack usage
  uint8_t *rtp_packet_buffer_{nullptr};  // Reusable RTP packet buffer (2KB)
  std::vector<std::pair<const uint8_t *, size_t>> nal_units_cache_;  // Reusable NAL units vector

  // Internal methods
  esp_err_t init_rtsp_server_();
  esp_err_t init_rtp_sockets_();
  esp_err_t init_h264_encoder_();
  void cleanup_h264_encoder_();
  void cleanup_sockets_();

  // RTSP protocol handling
  void handle_rtsp_connections_();
  void handle_rtsp_request_(RTSPSession &session);
  RTSPMethod parse_rtsp_method_(const std::string &request);
  void send_rtsp_response_(int socket_fd, int code, const std::string &status,
                           const std::map<std::string, std::string> &headers,
                           const std::string &body = "");

  // RTSP method handlers
  void handle_options_(RTSPSession &session, const std::string &request);
  void handle_describe_(RTSPSession &session, const std::string &request);
  void handle_setup_(RTSPSession &session, const std::string &request);
  void handle_play_(RTSPSession &session, const std::string &request);
  void handle_teardown_(RTSPSession &session, const std::string &request);

  // SDP generation
  std::string generate_sdp_();
  std::string base64_encode_(const uint8_t *data, size_t len);

  // Video streaming
  esp_err_t stream_video_();
  esp_err_t encode_and_stream_frame_();
  esp_err_t convert_rgb565_to_yuv420_(const uint8_t *rgb565, uint8_t *yuv420,
                                       uint16_t width, uint16_t height);
  esp_err_t send_h264_rtp_(const uint8_t *data, size_t len, bool marker);
  void parse_and_cache_nal_units_(const uint8_t *data, size_t len);
  std::vector<std::pair<const uint8_t *, size_t>> parse_nal_units_(const uint8_t *data, size_t len);

  // Session management
  std::string generate_session_id_();
  RTSPSession *find_session_(int socket_fd);
  RTSPSession *find_session_by_id_(const std::string &session_id);
  void remove_session_(int socket_fd);
  void cleanup_inactive_sessions_();

  // Utility
  std::string get_request_line_(const std::string &request, const std::string &field);
  int get_cseq_(const std::string &request);
  bool check_authentication_(const std::string &request);
};

#endif  // USE_ESP_IDF

}  // namespace rtsp_server
}  // namespace esphome
