#include "rtsp_server.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include <cstring>
#include <sstream>

#include <esp_random.h>
#include <arpa/inet.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <errno.h>
#include <fcntl.h>

namespace esphome {
namespace rtsp_server {

static const char *const TAG = "rtsp_server";

// Base64 encoding table
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Static lookup tables for RGB565 to YUV conversion
int16_t RTSPServer::y_r_lut_[32];
int16_t RTSPServer::y_g_lut_[64];
int16_t RTSPServer::y_b_lut_[32];
int16_t RTSPServer::u_r_lut_[32];
int16_t RTSPServer::u_g_lut_[64];
int16_t RTSPServer::u_b_lut_[32];
int16_t RTSPServer::v_r_lut_[32];
int16_t RTSPServer::v_g_lut_[64];
int16_t RTSPServer::v_b_lut_[32];
bool RTSPServer::yuv_lut_initialized_ = false;

void RTSPServer::setup() {
  ESP_LOGI(TAG, "Setting up RTSP Server...");

  // Generate random SSRC
  rtp_ssrc_ = esp_random();

  // Initialize RTP/RTCP sockets
  if (init_rtp_sockets_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize RTP sockets");
    this->mark_failed();
    return;
  }

  // Initialize RTSP server
  if (init_rtsp_server_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize RTSP server");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "RTSP Server setup complete");
  ESP_LOGI(TAG, "Stream URL: rtsp://<IP>:%d%s", rtsp_port_, stream_path_.c_str());

  if (!username_.empty() && !password_.empty()) {
    ESP_LOGI(TAG, "Authentication: ENABLED (user='%s')", username_.c_str());
    ESP_LOGI(TAG, "Connect with: rtsp://%s:***@<IP>:%d%s", username_.c_str(), rtsp_port_, stream_path_.c_str());
  } else {
    ESP_LOGI(TAG, "Authentication: DISABLED");
  }

  ESP_LOGI(TAG, "Note: H.264 encoder will initialize when first client connects");
}

void RTSPServer::loop() {
  // Controlled by switch / config
  if (!enabled_) {
    if (streaming_active_) {
      ESP_LOGI(TAG, "RTSP server disabled, stopping streaming...");
      streaming_active_ = false;

      if (streaming_task_handle_ != nullptr) {
        for (int i = 0; i < 50; i++) {
          eTaskState state = eTaskGetState(streaming_task_handle_);
          if (state == eSuspended) {
            break;
          }
          vTaskDelay(pdMS_TO_TICKS(10));
        }
        vTaskDelete(streaming_task_handle_);
        streaming_task_handle_ = nullptr;
      }
    }
    return;
  }

  // Handle RTSP connections
  handle_rtsp_connections_();

  // Cleanup timed-out sessions
  cleanup_inactive_sessions_();
}

void RTSPServer::dump_config() {
  ESP_LOGCONFIG(TAG, "RTSP Server:");
  ESP_LOGCONFIG(TAG, "  Status: %s", enabled_ ? "ENABLED" : "DISABLED");
  ESP_LOGCONFIG(TAG, "  Port: %d", rtsp_port_);
  ESP_LOGCONFIG(TAG, "  Stream Path: %s", stream_path_.c_str());
  ESP_LOGCONFIG(TAG, "  RTP Port: %d", rtp_port_);
  ESP_LOGCONFIG(TAG, "  RTCP Port: %d", rtcp_port_);
  ESP_LOGCONFIG(TAG, "  Bitrate: %d bps", bitrate_);
  ESP_LOGCONFIG(TAG, "  GOP: %d", gop_);
  ESP_LOGCONFIG(TAG, "  QP Range: %d-%d", qp_min_, qp_max_);
  ESP_LOGCONFIG(TAG, "  Max Clients: %d", max_clients_);
  if (!username_.empty()) {
    ESP_LOGCONFIG(TAG, "  Authentication: Enabled (user: %s)", username_.c_str());
  } else {
    ESP_LOGCONFIG(TAG, "  Authentication: Disabled");
  }
}

// ====================== Encoder init / cleanup ======================

esp_err_t RTSPServer::init_h264_encoder_() {
  ESP_LOGI(TAG, "Initializing H.264 hardware encoder (ESP32-P4)...");

  if (!camera_) {
    ESP_LOGE(TAG, "Camera not set");
    return ESP_FAIL;
  }

  // Ensure camera streaming
  if (!camera_->is_streaming()) {
    ESP_LOGW(TAG, "Camera not streaming yet, starting stream...");
    if (!camera_->start_streaming()) {
      ESP_LOGE(TAG, "Failed to start camera streaming");
      return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  uint16_t cam_w = camera_->get_image_width();
  uint16_t cam_h = camera_->get_image_height();

  if (cam_w == 0 || cam_h == 0) {
    ESP_LOGE(TAG, "Invalid camera dimensions: %dx%d", cam_w, cam_h);
    return ESP_FAIL;
  }

  // Encoder needs multiples of 16
  uint16_t width = ((cam_w + 15) >> 4) << 4;
  uint16_t height = ((cam_h + 15) >> 4) << 4;

  ESP_LOGI(TAG, "Encoder resolution: %dx%d (from camera %dx%d)", width, height, cam_w, cam_h);

  // YUV buffer (O_UYY_E_VYY, YUV420)
  yuv_buffer_size_ = width * height * 3 / 2;
  yuv_buffer_ = (uint8_t *) heap_caps_aligned_alloc(64, yuv_buffer_size_,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!yuv_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate YUV buffer (%zu bytes)", yuv_buffer_size_);
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "YUV buffer: %zu bytes @ %p", yuv_buffer_size_, yuv_buffer_);

  // H.264 output buffer
  h264_buffer_size_ = yuv_buffer_size_ * 2;
  h264_buffer_ = (uint8_t *) heap_caps_aligned_alloc(64, h264_buffer_size_,
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!h264_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate H.264 buffer (%zu bytes)", h264_buffer_size_);
    free(yuv_buffer_);
    yuv_buffer_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "H.264 buffer: %zu bytes @ %p", h264_buffer_size_, h264_buffer_);

  // Reusable RTP packet buffer
  rtp_packet_buffer_ = (uint8_t *) heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!rtp_packet_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate RTP packet buffer");
    free(yuv_buffer_);
    yuv_buffer_ = nullptr;
    free(h264_buffer_);
    h264_buffer_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  // Preallocate NAL units cache
  nal_units_cache_.reserve(20);

  // Configure encoder
  esp_h264_enc_cfg_hw_t cfg = {
      .pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY,
      .gop = gop_,
      .fps = 30,
      .res = {.width = width, .height = height},
      .rc = {.bitrate = bitrate_, .qp_min = qp_min_, .qp_max = qp_max_},
  };

  ESP_LOGI(TAG, "Encoder config: %dx%d @30fps, GOP=%d, bitrate=%u, QP=%u-%u",
           width, height, gop_, bitrate_, qp_min_, qp_max_);

  esp_h264_err_t ret = esp_h264_enc_hw_new(&cfg, &h264_encoder_);
  if (ret != ESP_H264_ERR_OK || !h264_encoder_) {
    ESP_LOGE(TAG, "Failed to create H.264 hardware encoder: %d", ret);
    cleanup_h264_encoder_();
    return ESP_FAIL;
  }

  ret = esp_h264_enc_open(h264_encoder_);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open H.264 encoder: %d", ret);
    cleanup_h264_encoder_();
    return ESP_FAIL;
  }

  frame_count_ = 0;
  rtp_timestamp_ = 0;

  ESP_LOGI(TAG, "H.264 hardware encoder initialized");
  return ESP_OK;
}

void RTSPServer::cleanup_h264_encoder_() {
  if (h264_encoder_) {
    esp_h264_enc_close(h264_encoder_);
    esp_h264_enc_del(h264_encoder_);
    h264_encoder_ = nullptr;
  }
  if (yuv_buffer_) {
    free(yuv_buffer_);
    yuv_buffer_ = nullptr;
    yuv_buffer_size_ = 0;
  }
  if (h264_buffer_) {
    free(h264_buffer_);
    h264_buffer_ = nullptr;
    h264_buffer_size_ = 0;
  }
  if (rtp_packet_buffer_) {
    free(rtp_packet_buffer_);
    rtp_packet_buffer_ = nullptr;
  }
  if (sps_data_) {
    free(sps_data_);
    sps_data_ = nullptr;
    sps_size_ = 0;
  }
  if (pps_data_) {
    free(pps_data_);
    pps_data_ = nullptr;
    pps_size_ = 0;
  }
}

// ====================== Sockets ======================

esp_err_t RTSPServer::init_rtp_sockets_() {
  ESP_LOGI(TAG, "Initializing RTP / RTCP sockets...");

  // RTP
  rtp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (rtp_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create RTP socket");
    return ESP_FAIL;
  }

  sockaddr_in rtp_addr{};
  rtp_addr.sin_family = AF_INET;
  rtp_addr.sin_addr.s_addr = INADDR_ANY;
  rtp_addr.sin_port = htons(rtp_port_);

  if (bind(rtp_socket_, (sockaddr *) &rtp_addr, sizeof(rtp_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind RTP socket");
    close(rtp_socket_);
    rtp_socket_ = -1;
    return ESP_FAIL;
  }

  // RTCP
  rtcp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (rtcp_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create RTCP socket");
    close(rtp_socket_);
    rtp_socket_ = -1;
    return ESP_FAIL;
  }

  sockaddr_in rtcp_addr{};
  rtcp_addr.sin_family = AF_INET;
  rtcp_addr.sin_addr.s_addr = INADDR_ANY;
  rtcp_addr.sin_port = htons(rtcp_port_);

  if (bind(rtcp_socket_, (sockaddr *) &rtcp_addr, sizeof(rtcp_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind RTCP socket");
    close(rtp_socket_);
    rtp_socket_ = -1;
    close(rtcp_socket_);
    rtcp_socket_ = -1;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "RTP / RTCP sockets ready");
  return ESP_OK;
}

esp_err_t RTSPServer::init_rtsp_server_() {
  ESP_LOGI(TAG, "Starting RTSP server on port %d", rtsp_port_);

  rtsp_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (rtsp_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create RTSP socket");
    return ESP_FAIL;
  }

  int reuse = 1;
  setsockopt(rtsp_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(rtsp_port_);

  if (bind(rtsp_socket_, (sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind RTSP socket");
    close(rtsp_socket_);
    rtsp_socket_ = -1;
    return ESP_FAIL;
  }

  if (listen(rtsp_socket_, 5) < 0) {
    ESP_LOGE(TAG, "Failed to listen on RTSP socket");
    close(rtsp_socket_);
    rtsp_socket_ = -1;
    return ESP_FAIL;
  }

  int flags = fcntl(rtsp_socket_, F_GETFL, 0);
  fcntl(rtsp_socket_, F_SETFL, flags | O_NONBLOCK);

  ESP_LOGI(TAG, "RTSP server started");
  return ESP_OK;
}

void RTSPServer::cleanup_sockets_() {
  if (rtsp_socket_ >= 0) {
    close(rtsp_socket_);
    rtsp_socket_ = -1;
  }
  if (rtp_socket_ >= 0) {
    close(rtp_socket_);
    rtp_socket_ = -1;
  }
  if (rtcp_socket_ >= 0) {
    close(rtcp_socket_);
    rtcp_socket_ = -1;
  }
}

// ====================== RTSP handling ======================

void RTSPServer::handle_rtsp_connections_() {
  // Accept new connections
  sockaddr_in client_addr{};
  socklen_t addr_len = sizeof(client_addr);

  int client_fd = accept(rtsp_socket_, (sockaddr *) &client_addr, &addr_len);
  if (client_fd >= 0) {
    if (sessions_.size() < max_clients_) {
      ESP_LOGI(TAG, "New RTSP client: %s", inet_ntoa(client_addr.sin_addr));

      RTSPSession s{};
      s.socket_fd = client_fd;
      s.state = RTSPState::INIT;
      s.client_addr = client_addr;
      s.client_rtp_port = 0;
      s.client_rtcp_port = 0;
      s.last_activity = millis();
      s.active = true;

      int flags = fcntl(client_fd, F_GETFL, 0);
      fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

      sessions_.push_back(s);
    } else {
      ESP_LOGW(TAG, "Max clients reached, rejecting new client");
      close(client_fd);
    }
  }

  // Handle existing sessions
  for (auto &session : sessions_) {
    if (!session.active)
      continue;
    handle_rtsp_request_(session);
  }
}

void RTSPServer::handle_rtsp_request_(RTSPSession &session) {
  char buffer[2048];
  int len = recv(session.socket_fd, buffer, sizeof(buffer) - 1, 0);

  if (len > 0) {
    buffer[len] = '\0';
    std::string request(buffer);
    session.last_activity = millis();

    ESP_LOGD(TAG, "RTSP Request:\n%s", request.c_str());

    RTSPMethod method = parse_rtsp_method_(request);

    // Authentication (except OPTIONS)
    if (method != RTSPMethod::OPTIONS && !check_authentication_(request)) {
      ESP_LOGW(TAG, "Authentication failed");
      int cseq = get_cseq_(request);
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      headers["WWW-Authenticate"] = "Basic realm=\"RTSP Server\"";
      send_rtsp_response_(session.socket_fd, 401, "Unauthorized", headers);
      return;
    }

    switch (method) {
      case RTSPMethod::OPTIONS:
        handle_options_(session, request);
        break;
      case RTSPMethod::DESCRIBE:
        handle_describe_(session, request);
        break;
      case RTSPMethod::SETUP:
        handle_setup_(session, request);
        break;
      case RTSPMethod::PLAY:
        handle_play_(session, request);
        break;
      case RTSPMethod::TEARDOWN:
        handle_teardown_(session, request);
        break;
      default:
        ESP_LOGW(TAG, "Unknown RTSP method");
        break;
    }
  } else if (len == 0 || (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
    ESP_LOGI(TAG, "Client disconnected");
    remove_session_(session.socket_fd);
  }
}

RTSPMethod RTSPServer::parse_rtsp_method_(const std::string &request) {
  if (request.rfind("OPTIONS", 0) == 0) return RTSPMethod::OPTIONS;
  if (request.rfind("DESCRIBE", 0) == 0) return RTSPMethod::DESCRIBE;
  if (request.rfind("SETUP", 0) == 0) return RTSPMethod::SETUP;
  if (request.rfind("PLAY", 0) == 0) return RTSPMethod::PLAY;
  if (request.rfind("PAUSE", 0) == 0) return RTSPMethod::PAUSE;
  if (request.rfind("TEARDOWN", 0) == 0) return RTSPMethod::TEARDOWN;
  return RTSPMethod::UNKNOWN;
}

void RTSPServer::send_rtsp_response_(int socket_fd, int code, const std::string &status,
                                     const std::map<std::string, std::string> &headers,
                                     const std::string &body) {
  std::ostringstream resp;
  resp << "RTSP/1.0 " << code << " " << status << "\r\n";
  for (const auto &h : headers) {
    resp << h.first << ": " << h.second << "\r\n";
  }
  if (!body.empty()) {
    resp << "Content-Length: " << body.length() << "\r\n";
  }
  resp << "\r\n";
  if (!body.empty()) {
    resp << body;
  }

  std::string s = resp.str();
  send(socket_fd, s.c_str(), s.size(), 0);

  ESP_LOGD(TAG, "RTSP Response:\n%s", s.c_str());
}

void RTSPServer::handle_options_(RTSPSession &session, const std::string &request) {
  int cseq = get_cseq_(request);
  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Public"] = "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN";
  send_rtsp_response_(session.socket_fd, 200, "OK", headers);
}

void RTSPServer::handle_describe_(RTSPSession &session, const std::string &request) {
  int cseq = get_cseq_(request);

  if (!h264_encoder_) {
    ESP_LOGI(TAG, "Initializing encoder for DESCRIBE...");
    if (init_h264_encoder_() != ESP_OK) {
      ESP_LOGE(TAG, "Failed to init encoder");
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }

    // Try to get SPS/PPS from first frame (best effort)
    if (!sps_data_ || !pps_data_) {
      ESP_LOGI(TAG, "Trying to cache SPS/PPS from first encoded frame");
      encode_and_stream_frame_();  // will cache SPS/PPS but send will just fail if no clients
    }
  }

  std::string sdp = generate_sdp_();

  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Content-Type"] = "application/sdp";

  send_rtsp_response_(session.socket_fd, 200, "OK", headers, sdp);
}

void RTSPServer::handle_setup_(RTSPSession &session, const std::string &request) {
  int cseq = get_cseq_(request);

  std::string transport_line = get_request_line_(request, "Transport");
  ESP_LOGD(TAG, "Transport header: %s", transport_line.c_str());

  // Reject TCP interleaved for now
  if (transport_line.find("interleaved") != std::string::npos ||
      transport_line.find("RTP/AVP/TCP") != std::string::npos) {
    ESP_LOGW(TAG, "TCP interleaved not supported");
    std::map<std::string, std::string> headers;
    headers["CSeq"] = std::to_string(cseq);
    send_rtsp_response_(session.socket_fd, 461, "Unsupported Transport", headers);
    return;
  }

  // Extract client ports
  size_t pos = transport_line.find("client_port=");
  if (pos != std::string::npos) {
    int rtp_port, rtcp_port;
    if (sscanf(transport_line.c_str() + pos, "client_port=%d-%d", &rtp_port, &rtcp_port) == 2) {
      session.client_rtp_port = rtp_port;
      session.client_rtcp_port = rtcp_port;
    } else {
      ESP_LOGW(TAG, "Failed to parse client_port");
    }
  } else {
    ESP_LOGW(TAG, "No client_port in Transport header");
    std::map<std::string, std::string> headers;
    headers["CSeq"] = std::to_string(cseq);
    send_rtsp_response_(session.socket_fd, 461, "Unsupported Transport", headers);
    return;
  }

  if (session.session_id.empty()) {
    session.session_id = generate_session_id_();
  }
  session.state = RTSPState::READY;

  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Session"] = session.session_id;
  headers["Transport"] = "RTP/AVP;unicast;client_port=" +
                         std::to_string(session.client_rtp_port) + "-" +
                         std::to_string(session.client_rtcp_port) +
                         ";server_port=" + std::to_string(rtp_port_) + "-" +
                         std::to_string(rtcp_port_);

  send_rtsp_response_(session.socket_fd, 200, "OK", headers);

  ESP_LOGI(TAG, "Session %s setup, client RTP=%d RTCP=%d",
           session.session_id.c_str(), session.client_rtp_port, session.client_rtcp_port);
}

void RTSPServer::handle_play_(RTSPSession &session, const std::string &request) {
  int cseq = get_cseq_(request);

  if (!h264_encoder_) {
    ESP_LOGW(TAG, "Encoder not initialized before PLAY, initializing...");
    if (init_h264_encoder_() != ESP_OK) {
      ESP_LOGE(TAG, "Failed to init encoder");
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }
  }

  session.state = RTSPState::PLAYING;
  streaming_active_ = true;

  if (streaming_task_handle_ == nullptr) {
    BaseType_t res = xTaskCreatePinnedToCore(
        streaming_task_wrapper_, "rtsp_stream",
        16384,  // 16 KB stack
        this,
        5,
        &streaming_task_handle_,
        1  // core 1
    );

    if (res != pdPASS || streaming_task_handle_ == nullptr) {
      ESP_LOGE(TAG, "Failed to create streaming task (res=%d)", res);
      streaming_active_ = false;
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }
    ESP_LOGI(TAG, "Streaming task created");
  }

  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Session"] = session.session_id;
  headers["RTP-Info"] = "url=" + stream_path_ + ";seq=" + std::to_string(rtp_seq_num_);

  send_rtsp_response_(session.socket_fd, 200, "OK", headers);

  ESP_LOGI(TAG, "Session %s PLAY", session.session_id.c_str());
}

void RTSPServer::handle_teardown_(RTSPSession &session, const std::string &request) {
  int cseq = get_cseq_(request);

  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Session"] = session.session_id;

  send_rtsp_response_(session.socket_fd, 200, "OK", headers);

  ESP_LOGI(TAG, "Session %s TEARDOWN", session.session_id.c_str());

  remove_session_(session.socket_fd);

  bool any_playing = false;
  for (const auto &s : sessions_) {
    if (s.active && s.state == RTSPState::PLAYING) {
      any_playing = true;
      break;
    }
  }

  if (!any_playing && streaming_active_) {
    ESP_LOGI(TAG, "No more PLAY sessions, stopping streaming");
    streaming_active_ = false;

    if (streaming_task_handle_ != nullptr) {
      for (int i = 0; i < 50; i++) {
        eTaskState st = eTaskGetState(streaming_task_handle_);
        if (st == eSuspended) {
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      vTaskDelete(streaming_task_handle_);
      streaming_task_handle_ = nullptr;
    }
  }
}

// ====================== SDP / Base64 ======================

std::string RTSPServer::generate_sdp_() {
  std::string local_ip = "0.0.0.0";  // Could be improved: query WiFi IP, etc.

  uint16_t width = camera_ ? camera_->get_image_width() : 0;
  uint16_t height = camera_ ? camera_->get_image_height() : 0;

  std::ostringstream sdp;
  sdp << "v=0\r\n";
  sdp << "o=- 0 0 IN IP4 " << local_ip << "\r\n";
  sdp << "s=ESP32-P4 RTSP Camera\r\n";
  sdp << "c=IN IP4 0.0.0.0\r\n";
  sdp << "t=0 0\r\n";
  sdp << "a=control:*\r\n";
  sdp << "a=range:npt=0-\r\n";
  sdp << "m=video 0 RTP/AVP 96\r\n";
  sdp << "a=rtpmap:96 H264/90000\r\n";
  sdp << "a=fmtp:96 packetization-mode=1";

  if (sps_data_ && sps_size_ > 0 && pps_data_ && pps_size_ > 0) {
    std::string sps_b64 = base64_encode_(sps_data_, sps_size_);
    std::string pps_b64 = base64_encode_(pps_data_, pps_size_);
    sdp << ";sprop-parameter-sets=" << sps_b64 << "," << pps_b64;
    ESP_LOGI(TAG, "SDP includes SPS/PPS (SPS %u bytes, PPS %u bytes)",
             (unsigned) sps_size_, (unsigned) pps_size_);
  } else {
    ESP_LOGW(TAG, "SDP without SPS/PPS, client must extract from stream");
  }

  sdp << "\r\n";
  sdp << "a=control:track1\r\n";
  sdp << "a=framerate:30\r\n";
  if (width && height) {
    sdp << "a=framesize:96 " << width << "-" << height << "\r\n";
  }

  return sdp.str();
}

std::string RTSPServer::base64_encode_(const uint8_t *data, size_t len) {
  std::string ret;
  int i = 0;
  uint8_t char_array_3[3];
  uint8_t char_array_4[4];

  while (len--) {
    char_array_3[i++] = *(data++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;
      for (i = 0; i < 4; i++)
        ret += base64_chars[char_array_4[i]];
      i = 0;
    }
  }

  if (i) {
    for (int j = i; j < 3; j++)
      char_array_3[j] = 0;

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

    for (int j = 0; j < i + 1; j++)
      ret += base64_chars[char_array_4[j]];

    while (i++ < 3)
      ret += '=';
  }

  return ret;
}

// ====================== Streaming ======================

esp_err_t RTSPServer::stream_video_() {
  if (!streaming_active_)
    return ESP_OK;
  return encode_and_stream_frame_();
}

esp_err_t RTSPServer::encode_and_stream_frame_() {
  if (!camera_ || !h264_encoder_)
    return ESP_FAIL;

  mipi_dsi_cam::SimpleBufferElement *buffer = nullptr;
  uint8_t *frame_data = nullptr;
  int width = 0, height = 0;

  // IMPORTANT: this returns the raw format configured (YUYV)
  if (!camera_->get_current_rgb_frame(&buffer, &frame_data, &width, &height)) {
    ESP_LOGW(TAG, "Failed to get frame from camera");
    return ESP_FAIL;
  }

  if (!frame_data || !buffer) {
    ESP_LOGW(TAG, "Invalid frame data (ptr=%p, buf=%p)", frame_data, buffer);
    if (buffer) {
      camera_->release_buffer(buffer);
    }
    return ESP_FAIL;
  }

  if (frame_count_ == 0) {
    ESP_LOGI(TAG, "First YUYV frame: %dx%d, expected size: %d bytes",
             width, height, width * height * 2);
  }

  // Convert YUYV (YUV422) -> O_UYY_E_VYY (YUV420 packed) for hardware encoder
  convert_yuyv_to_o_uyy_e_vyy_(frame_data, yuv_buffer_, width, height);

  // Release camera buffer as soon as possible
  camera_->release_buffer(buffer);

  // Encode
  esp_h264_enc_in_frame_t in_frame{};
  in_frame.raw_data.buffer = yuv_buffer_;
  in_frame.raw_data.len = yuv_buffer_size_;
  in_frame.pts = frame_count_ * 90000 / 30;  // 30 fps

  esp_h264_enc_out_frame_t out_frame{};
  out_frame.raw_data.buffer = h264_buffer_;
  out_frame.raw_data.len = h264_buffer_size_;

  esp_h264_err_t ret = esp_h264_enc_process(h264_encoder_, &in_frame, &out_frame);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "H.264 encoding failed: %d (frame=%u, in=%u, out=%u)",
             ret, (unsigned) frame_count_,
             (unsigned) in_frame.raw_data.len,
             (unsigned) out_frame.raw_data.len);
    return ESP_FAIL;
  }

  if (out_frame.length == 0 || !out_frame.raw_data.buffer) {
    ESP_LOGE(TAG, "Invalid H.264 output (len=%u, buf=%p)",
             (unsigned) out_frame.length, out_frame.raw_data.buffer);
    return ESP_FAIL;
  }

  const char *frame_type_name = "Unknown";
  if (out_frame.frame_type == ESP_H264_FRAME_TYPE_IDR) frame_type_name = "IDR";
  else if (out_frame.frame_type == ESP_H264_FRAME_TYPE_I) frame_type_name = "I";
  else if (out_frame.frame_type == ESP_H264_FRAME_TYPE_P) frame_type_name = "P";

  ESP_LOGV(TAG, "Frame %u encoded: %u bytes, type=%s",
           (unsigned) frame_count_, (unsigned) out_frame.length, frame_type_name);

  // Cache SPS/PPS from IDR frame
  if (out_frame.frame_type == ESP_H264_FRAME_TYPE_IDR) {
    ESP_LOGI(TAG, "IDR frame, caching SPS/PPS");
    parse_and_cache_nal_units_(out_frame.raw_data.buffer, out_frame.length);
  }

  // Parse NAL units and send
  auto nal_units = parse_nal_units_(out_frame.raw_data.buffer, out_frame.length);
  ESP_LOGV(TAG, "Found %u NAL units", (unsigned) nal_units.size());

  for (size_t i = 0; i < nal_units.size(); i++) {
    const auto &nal = nal_units[i];
    uint8_t nal_type = nal.first[0] & 0x1F;
    const char *nal_type_name = "Unknown";
    if (nal_type == 1) nal_type_name = "P-slice";
    else if (nal_type == 5) nal_type_name = "IDR";
    else if (nal_type == 6) nal_type_name = "SEI";
    else if (nal_type == 7) nal_type_name = "SPS";
    else if (nal_type == 8) nal_type_name = "PPS";

    ESP_LOGV(TAG, "Sending NAL %u: type=%u (%s), len=%u",
             (unsigned) i, (unsigned) nal_type, nal_type_name, (unsigned) nal.second);

    // Marker bit true only on last NAL of the frame
    bool marker = (i == nal_units.size() - 1);
    send_h264_rtp_(nal.first, nal.second, marker);
  }

  frame_count_++;
  rtp_timestamp_ += 3000;  // 90kHz / 30fps

  return ESP_OK;
}

// ====================== YUYV / RGB565 conversion ======================

// YUYV 4:2:2 -> O_UYY_E_VYY (YUV420 packed)
esp_err_t RTSPServer::convert_yuyv_to_o_uyy_e_vyy_(const uint8_t *src,
                                                   uint8_t *dst,
                                                   uint16_t width,
                                                   uint16_t height) {
  if (!src || !dst || width == 0 || height == 0)
    return ESP_FAIL;

  // Each logical line in O_UYY_E_VYY = width/2 groups * 3 bytes = width*3/2
  uint32_t line_stride = (width * 3) / 2;

  for (uint16_t y = 0; y < height; y += 2) {
    uint8_t *u_line = dst + (y * line_stride);
    uint8_t *v_line = dst + ((y + 1) * line_stride);

    const uint8_t *line0 = src + (y * width * 2);
    const uint8_t *line1 = src + ((y + 1) * width * 2);

    for (uint16_t x = 0; x < width; x += 2) {
      // Line 0: Y0 U0 Y1 V0
      uint8_t y00 = line0[x * 2 + 0];
      uint8_t u00 = line0[x * 2 + 1];
      uint8_t y01 = line0[x * 2 + 2];
      uint8_t v00 = line0[x * 2 + 3];

      // Line 1: Y0 U1 Y1 V1
      uint8_t y10 = line1[x * 2 + 0];
      uint8_t u10 = line1[x * 2 + 1];
      uint8_t y11 = line1[x * 2 + 2];
      uint8_t v10 = line1[x * 2 + 3];

      uint8_t u_avg = (uint8_t) ((u00 + u10) >> 1);
      uint8_t v_avg = (uint8_t) ((v00 + v10) >> 1);

      int idx = (x / 2) * 3;

      // Line 0 => U Y Y
      u_line[idx + 0] = u_avg;
      u_line[idx + 1] = y00;
      u_line[idx + 2] = y01;

      // Line 1 => V Y Y
      v_line[idx + 0] = v_avg;
      v_line[idx + 1] = y10;
      v_line[idx + 2] = y11;
    }
  }

  return ESP_OK;
}

// RGB565 -> YUV420 (fallback, pas utilisé pour YUYV mais gardé)
void RTSPServer::init_yuv_lut_() {
  if (yuv_lut_initialized_)
    return;

  // BT.601 integer approx
  for (int i = 0; i < 32; i++) {
    int val_8 = (i << 3) | (i >> 2);
    y_r_lut_[i] = (66 * val_8) >> 8;
    y_b_lut_[i] = (25 * val_8) >> 8;
    u_r_lut_[i] = (-38 * val_8) >> 8;
    u_b_lut_[i] = (112 * val_8) >> 8;
    v_r_lut_[i] = (112 * val_8) >> 8;
    v_b_lut_[i] = (-18 * val_8) >> 8;
  }

  for (int i = 0; i < 64; i++) {
    int val_8 = (i << 2) | (i >> 4);
    y_g_lut_[i] = (129 * val_8) >> 8;
    u_g_lut_[i] = (-74 * val_8) >> 8;
    v_g_lut_[i] = (-94 * val_8) >> 8;
  }

  yuv_lut_initialized_ = true;
  ESP_LOGI(TAG, "YUV LUT initialized");
}

esp_err_t RTSPServer::convert_rgb565_to_yuv420_(const uint8_t *rgb565,
                                                uint8_t *yuv420,
                                                uint16_t width,
                                                uint16_t height) {
  if (!rgb565 || !yuv420 || width == 0 || height == 0)
    return ESP_FAIL;

  if (!yuv_lut_initialized_)
    init_yuv_lut_();

  const uint16_t *rgb = (const uint16_t *) rgb565;

  for (uint16_t row = 0; row < height; row += 2) {
    const uint16_t *row0 = rgb + (row * width);
    const uint16_t *row1 = rgb + ((row + 1) * width);
    uint8_t *odd_ptr = yuv420 + (row * width * 3 / 2);
    uint8_t *even_ptr = yuv420 + ((row + 1) * width * 3 / 2);

    for (uint16_t col = 0; col < width; col += 2,
                  row0 += 2, row1 += 2, odd_ptr += 3, even_ptr += 3) {
      uint16_t p00 = row0[0];
      uint16_t p01 = row0[1];
      uint16_t p10 = row1[0];
      uint16_t p11 = row1[1];

      uint8_t r0 = (p00 >> 11);
      uint8_t g0 = (p00 >> 5) & 0x3F;
      uint8_t b0 = p00 & 0x1F;

      uint8_t r1 = (p01 >> 11);
      uint8_t g1 = (p01 >> 5) & 0x3F;
      uint8_t b1 = p01 & 0x1F;

      uint8_t r2 = (p10 >> 11);
      uint8_t g2 = (p10 >> 5) & 0x3F;
      uint8_t b2 = p10 & 0x1F;

      uint8_t r3 = (p11 >> 11);
      uint8_t g3 = (p11 >> 5) & 0x3F;
      uint8_t b3 = p11 & 0x1F;

      uint8_t y0 = (uint8_t) (y_r_lut_[r0] + y_g_lut_[g0] + y_b_lut_[b0] + 16);
      uint8_t y1 = (uint8_t) (y_r_lut_[r1] + y_g_lut_[g1] + y_b_lut_[b1] + 16);
      uint8_t y2 = (uint8_t) (y_r_lut_[r2] + y_g_lut_[g2] + y_b_lut_[b2] + 16);
      uint8_t y3 = (uint8_t) (y_r_lut_[r3] + y_g_lut_[g3] + y_b_lut_[b3] + 16);

      uint8_t r_avg = (uint8_t) ((r0 + r1 + r2 + r3) >> 2);
      uint8_t g_avg = (uint8_t) ((g0 + g1 + g2 + g3) >> 2);
      uint8_t b_avg = (uint8_t) ((b0 + b1 + b2 + b3) >> 2);

      uint8_t u = (uint8_t) (u_r_lut_[r_avg] + u_g_lut_[g_avg] + u_b_lut_[b_avg] + 128);
      uint8_t v = (uint8_t) (v_r_lut_[r_avg] + v_g_lut_[g_avg] + v_b_lut_[b_avg] + 128);

      odd_ptr[0] = u;
      odd_ptr[1] = y0;
      odd_ptr[2] = y1;

      even_ptr[0] = v;
      even_ptr[1] = y2;
      even_ptr[2] = y3;
    }
  }

  return ESP_OK;
}

// ====================== NAL parsing / RTP send ======================

void RTSPServer::parse_and_cache_nal_units_(const uint8_t *data, size_t len) {
  auto nal_units = parse_nal_units_(data, len);
  for (const auto &nal : nal_units) {
    uint8_t nal_type = nal.first[0] & 0x1F;
    if (nal_type == 7) {  // SPS
      if (sps_data_) {
        free(sps_data_);
        sps_data_ = nullptr;
      }
      sps_size_ = nal.second;
      sps_data_ = (uint8_t *) malloc(sps_size_);
      memcpy(sps_data_, nal.first, sps_size_);
      ESP_LOGI(TAG, "Cached SPS (%u bytes)", (unsigned) sps_size_);
    } else if (nal_type == 8) {  // PPS
      if (pps_data_) {
        free(pps_data_);
        pps_data_ = nullptr;
      }
      pps_size_ = nal.second;
      pps_data_ = (uint8_t *) malloc(pps_size_);
      memcpy(pps_data_, nal.first, pps_size_);
      ESP_LOGI(TAG, "Cached PPS (%u bytes)", (unsigned) pps_size_);
    }
  }
}

std::vector<std::pair<const uint8_t *, size_t>>
RTSPServer::parse_nal_units_(const uint8_t *data, size_t len) {
  nal_units_cache_.clear();

  if (!data || len < 4)
    return nal_units_cache_;

  size_t i = 0;
  while (i < len - 3) {
    if (data[i] == 0x00 && data[i + 1] == 0x00) {
      size_t start_code_len = 0;
      if (data[i + 2] == 0x01) {
        start_code_len = 3;
      } else if (data[i + 2] == 0x00 && data[i + 3] == 0x01) {
        start_code_len = 4;
      }

      if (start_code_len > 0) {
        size_t j = i + start_code_len;
        while (j < len - 3) {
          if (data[j] == 0x00 && data[j + 1] == 0x00 &&
              (data[j + 2] == 0x01 ||
               (data[j + 2] == 0x00 && data[j + 3] == 0x01))) {
            break;
          }
          j++;
        }

        size_t nal_size = j - (i + start_code_len);
        if (nal_size > 0) {
          nal_units_cache_.push_back({data + i + start_code_len, nal_size});
        }

        i = j;
        continue;
      }
    }
    i++;
  }

  return nal_units_cache_;
}

esp_err_t RTSPServer::send_h264_rtp_(const uint8_t *data, size_t len, bool marker) {
  if (!data || len == 0)
    return ESP_FAIL;

  const size_t MAX_RTP_PAYLOAD = 1400;

  if (len <= MAX_RTP_PAYLOAD) {
    uint8_t *packet = rtp_packet_buffer_;
    RTPHeader *rtp = (RTPHeader *) packet;

    rtp->v = 2;
    rtp->p = 0;
    rtp->x = 0;
    rtp->cc = 0;
    rtp->m = marker ? 1 : 0;
    rtp->pt = 96;
    rtp->seq = htons(rtp_seq_num_++);
    rtp->timestamp = htonl(rtp_timestamp_);
    rtp->ssrc = htonl(rtp_ssrc_);

    memcpy(packet + sizeof(RTPHeader), data, len);

    size_t packet_size = sizeof(RTPHeader) + len;

    for (auto &session : sessions_) {
      if (!session.active || session.state != RTSPState::PLAYING)
        continue;

      sockaddr_in dest = session.client_addr;
      dest.sin_port = htons(session.client_rtp_port);

      sendto(rtp_socket_, packet, packet_size, 0, (sockaddr *) &dest, sizeof(dest));
    }

    return ESP_OK;
  }

  // Fragmentation FU-A
  ESP_LOGD(TAG, "Fragmenting NAL of %u bytes with FU-A", (unsigned) len);

  uint8_t nal_header = data[0];
  uint8_t nal_type = nal_header & 0x1F;
  uint8_t nri = nal_header & 0x60;

  uint8_t fu_indicator = nri | 28;  // Type 28 = FU-A

  const uint8_t *payload = data + 1;
  size_t payload_len = len - 1;
  size_t offset = 0;

  uint8_t *packet = rtp_packet_buffer_;

  while (offset < payload_len) {
    RTPHeader *rtp = (RTPHeader *) packet;
    rtp->v = 2;
    rtp->p = 0;
    rtp->x = 0;
    rtp->cc = 0;
    rtp->pt = 96;
    rtp->seq = htons(rtp_seq_num_++);
    rtp->timestamp = htonl(rtp_timestamp_);
    rtp->ssrc = htonl(rtp_ssrc_);

    uint8_t *fu_payload = packet + sizeof(RTPHeader);
    fu_payload[0] = fu_indicator;

    bool is_start = (offset == 0);
    size_t remaining = payload_len - offset;
    size_t chunk = (remaining > (MAX_RTP_PAYLOAD - 2)) ? (MAX_RTP_PAYLOAD - 2) : remaining;
    bool is_end = (offset + chunk >= payload_len);

    uint8_t fu_header = nal_type;
    if (is_start) fu_header |= 0x80;  // S
    if (is_end) fu_header |= 0x40;    // E

    fu_payload[1] = fu_header;

    memcpy(fu_payload + 2, payload + offset, chunk);

    rtp->m = (is_end && marker) ? 1 : 0;

    size_t packet_size = sizeof(RTPHeader) + 2 + chunk;

    for (auto &session : sessions_) {
      if (!session.active || session.state != RTSPState::PLAYING)
        continue;

      sockaddr_in dest = session.client_addr;
      dest.sin_port = htons(session.client_rtp_port);

      sendto(rtp_socket_, packet, packet_size, 0, (sockaddr *) &dest, sizeof(dest));
    }

    offset += chunk;
  }

  return ESP_OK;
}

// ====================== Sessions / Utils ======================

std::string RTSPServer::generate_session_id_() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%08X", esp_random());
  return std::string(buf);
}

RTSPSession *RTSPServer::find_session_(int socket_fd) {
  for (auto &s : sessions_) {
    if (s.socket_fd == socket_fd && s.active)
      return &s;
  }
  return nullptr;
}

RTSPSession *RTSPServer::find_session_by_id_(const std::string &session_id) {
  for (auto &s : sessions_) {
    if (s.session_id == session_id && s.active)
      return &s;
  }
  return nullptr;
}

void RTSPServer::remove_session_(int socket_fd) {
  for (auto &s : sessions_) {
    if (s.socket_fd == socket_fd) {
      close(s.socket_fd);
      s.active = false;
      ESP_LOGI(TAG, "Session %s removed", s.session_id.c_str());
      break;
    }
  }

  sessions_.erase(
      std::remove_if(sessions_.begin(), sessions_.end(),
                     [](const RTSPSession &s) { return !s.active; }),
      sessions_.end());
}

void RTSPServer::cleanup_inactive_sessions_() {
  uint32_t now = millis();
  const uint32_t timeout = 60000;  // 60s

  for (auto &s : sessions_) {
    if (s.active && (now - s.last_activity > timeout)) {
      ESP_LOGW(TAG, "Session %s timed out", s.session_id.c_str());
      remove_session_(s.socket_fd);
    }
  }
}

std::string RTSPServer::get_request_line_(const std::string &request, const std::string &field) {
  size_t pos = request.find(field + ":");
  if (pos == std::string::npos)
    return "";

  size_t start = pos + field.length() + 1;
  size_t end = request.find("\r\n", start);
  if (end == std::string::npos)
    return "";

  std::string value = request.substr(start, end - start);

  size_t first = value.find_first_not_of(" \t");
  if (first == std::string::npos)
    return "";
  size_t last = value.find_last_not_of(" \t");
  return value.substr(first, last - first + 1);
}

int RTSPServer::get_cseq_(const std::string &request) {
  std::string cseq_str = get_request_line_(request, "CSeq");
  if (cseq_str.empty())
    return 0;
  return std::atoi(cseq_str.c_str());
}

bool RTSPServer::check_authentication_(const std::string &request) {
  if (username_.empty() && password_.empty()) {
    ESP_LOGD(TAG, "Authentication disabled");
    return true;
  }

  std::string auth_header = get_request_line_(request, "Authorization");
  if (auth_header.empty()) {
    ESP_LOGW(TAG, "No Authorization header");
    return false;
  }

  if (auth_header.find("Basic ") != 0) {
    ESP_LOGW(TAG, "Unsupported auth type");
    return false;
  }

  std::string encoded = auth_header.substr(6);
  std::string decoded;
  int val = 0;
  int valb = -8;
  for (unsigned char c : encoded) {
    if (c == '=')
      break;
    const char *p = strchr(base64_chars, c);
    if (!p)
      continue;
    val = (val << 6) + (p - base64_chars);
    valb += 6;
    if (valb >= 0) {
      decoded.push_back(char((val >> valb) & 0xFF));
      valb -= 8;
    }
  }

  size_t colon = decoded.find(':');
  if (colon == std::string::npos) {
    ESP_LOGW(TAG, "Invalid auth format");
    return false;
  }

  std::string user = decoded.substr(0, colon);
  std::string pass = decoded.substr(colon + 1);

  bool valid = (user == username_ && pass == password_);
  if (!valid) {
    ESP_LOGW(TAG, "Invalid credentials");
  } else {
    ESP_LOGI(TAG, "Auth OK for user '%s'", user.c_str());
  }
  return valid;
}

// ====================== Streaming task ======================

void RTSPServer::streaming_task_wrapper_(void *param) {
  RTSPServer *server = static_cast<RTSPServer *>(param);

  ESP_LOGI(TAG, "Streaming task started");

  uint32_t frame_num = 0;
  uint32_t total_encode_time = 0;
  uint32_t start_time = millis();

  while (server->streaming_active_) {
    uint32_t t0 = millis();
    server->encode_and_stream_frame_();
    uint32_t dt = millis() - t0;

    total_encode_time += dt;
    frame_num++;

    if (frame_num % 30 == 0) {
      uint32_t elapsed = millis() - start_time;
      float fps = (frame_num * 1000.0f) / (float) elapsed;
      float avg = total_encode_time / (float) frame_num;
      ESP_LOGI(TAG, "Streaming stats: %.1f FPS, avg encode %.1f ms, last %u ms",
               fps, avg, (unsigned) dt);
    }

    // 30 FPS target
    if (dt < 33) {
      vTaskDelay(pdMS_TO_TICKS(33 - dt));
    } else {
      vTaskDelay(1);
    }
  }

  ESP_LOGI(TAG, "Streaming task ended");
  vTaskSuspend(nullptr);
}

}  // namespace rtsp_server
}  // namespace esphome

#endif  // USE_ESP_IDF


