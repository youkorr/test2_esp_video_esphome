#include "rtsp_server.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include <cstring>
#include <sstream>
#include <algorithm>

#include <esp_random.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not set");
    this->mark_failed();
    return;
  }

  // Generate random SSRC
  this->rtp_ssrc_ = esp_random();

  // Initialize RTP/RTCP sockets
  if (this->init_rtp_sockets_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize RTP sockets");
    this->mark_failed();
    return;
  }

  // Initialize RTSP server
  if (this->init_rtsp_server_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize RTSP server");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "RTSP Server setup complete");
  ESP_LOGI(TAG, "Stream URL: rtsp://<IP>:%d%s", this->rtsp_port_, this->stream_path_.c_str());

  if (!this->username_.empty() && !this->password_.empty()) {
    ESP_LOGI(TAG, "Authentication: ENABLED (user='%s')", this->username_.c_str());
    ESP_LOGI(TAG, "Connect with: rtsp://%s:***@<IP>:%d%s",
             this->username_.c_str(), this->rtsp_port_, this->stream_path_.c_str());
  } else {
    ESP_LOGI(TAG, "Authentication: DISABLED");
  }

  ESP_LOGI(TAG, "Note: H.264 encoder will initialize when first client connects");
}

void RTSPServer::loop() {
  // Check if RTSP server is enabled by switch
  if (!this->enabled_) {
    // If streaming was active, stop it
    if (this->streaming_active_) {
      ESP_LOGI(TAG, "RTSP server disabled by switch, stopping streaming...");
      this->streaming_active_ = false;

      if (this->streaming_task_handle_ != nullptr) {
        for (int i = 0; i < 50; i++) {
          eTaskState task_state = eTaskGetState(this->streaming_task_handle_);
          if (task_state == eSuspended) {
            break;
          }
          vTaskDelay(pdMS_TO_TICKS(10));
        }
        vTaskDelete(this->streaming_task_handle_);
        this->streaming_task_handle_ = nullptr;
      }
    }
    return;  // Don't handle connections when disabled
  }

  // Handle incoming RTSP connections
  this->handle_rtsp_connections_();

  // Cleanup inactive sessions
  this->cleanup_inactive_sessions_();
}

void RTSPServer::dump_config() {
  ESP_LOGCONFIG(TAG, "RTSP Server:");
  ESP_LOGCONFIG(TAG, "  Status: %s (controlled by switch)", this->enabled_ ? "ENABLED" : "DISABLED");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->rtsp_port_);
  ESP_LOGCONFIG(TAG, "  Stream Path: %s", this->stream_path_.c_str());
  ESP_LOGCONFIG(TAG, "  RTP Port: %d", this->rtp_port_);
  ESP_LOGCONFIG(TAG, "  RTCP Port: %d", this->rtcp_port_);
  ESP_LOGCONFIG(TAG, "  Bitrate: %d bps", this->bitrate_);
  ESP_LOGCONFIG(TAG, "  GOP: %d", this->gop_);
  ESP_LOGCONFIG(TAG, "  QP Range: %d-%d", this->qp_min_, this->qp_max_);
  ESP_LOGCONFIG(TAG, "  Max Clients: %d", this->max_clients_);
  if (!this->username_.empty()) {
    ESP_LOGCONFIG(TAG, "  Authentication: Enabled (user: %s)", this->username_.c_str());
  } else {
    ESP_LOGCONFIG(TAG, "  Authentication: Disabled");
  }
}

esp_err_t RTSPServer::init_h264_encoder_() {
  ESP_LOGI(TAG, "Initializing H.264 HARDWARE encoder (ESP32-P4 accelerator)...");

  if (!this->camera_) {
    ESP_LOGE(TAG, "Camera not set");
    return ESP_FAIL;
  }

  // Ensure camera is streaming
  if (!this->camera_->is_streaming()) {
    ESP_LOGW(TAG, "Camera not streaming yet, starting stream...");
    if (!this->camera_->start_streaming()) {
      ESP_LOGE(TAG, "Failed to start camera streaming");
      return ESP_FAIL;
    }
    // Let camera pipeline stabilize
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (width == 0 || height == 0) {
    ESP_LOGE(TAG, "Invalid camera dimensions: %dx%d", width, height);
    return ESP_FAIL;
  }

  // Align to 16 (required by hardware encoder)
  width = ((width + 15) >> 4) << 4;
  height = ((height + 15) >> 4) << 4;

  ESP_LOGI(TAG, "Resolution used for encoder: %dx%d (camera: %dx%d)",
           width, height, this->camera_->get_image_width(), this->camera_->get_image_height());

  // Allocate YUV buffer (O_UYY_E_VYY packed YUV420)
  this->yuv_buffer_size_ = width * height * 3 / 2;
  this->yuv_buffer_ = (uint8_t *) heap_caps_aligned_alloc(64,
                                                          this->yuv_buffer_size_,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!this->yuv_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate YUV buffer (64-byte aligned)");
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "YUV buffer: %zu bytes @ %p", this->yuv_buffer_size_, this->yuv_buffer_);

  // Allocate H.264 output buffer (roughly 2x YUV as a safe upper bound)
  this->h264_buffer_size_ = this->yuv_buffer_size_ * 2;
  this->h264_buffer_ = (uint8_t *) heap_caps_aligned_alloc(64,
                                                           this->h264_buffer_size_,
                                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!this->h264_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate H.264 buffer (64-byte aligned)");
    free(this->yuv_buffer_);
    this->yuv_buffer_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "H.264 buffer: %zu bytes @ %p", this->h264_buffer_size_, this->h264_buffer_);

  // Allocate reusable RTP packet buffer
  this->rtp_packet_buffer_ = (uint8_t *) heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!this->rtp_packet_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate RTP packet buffer");
    free(this->yuv_buffer_);
    free(this->h264_buffer_);
    this->yuv_buffer_ = nullptr;
    this->h264_buffer_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  // Preallocate NAL units cache
  this->nal_units_cache_.reserve(20);

  // Configure hardware encoder
  esp_h264_enc_cfg_hw_t cfg = {
      .pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY,
      .gop = this->gop_,
      .fps = 30,
      .res = {.width = width, .height = height},
      .rc = {.bitrate = this->bitrate_, .qp_min = this->qp_min_, .qp_max = this->qp_max_},
  };

  ESP_LOGI(TAG,
           "Encoder config: %dx%d @ 30fps, GOP=%d, bitrate=%d, QP=%d-%d",
           width, height, this->gop_, this->bitrate_, this->qp_min_, this->qp_max_);

  esp_h264_err_t ret = esp_h264_enc_hw_new(&cfg, &this->h264_encoder_);
  if (ret != ESP_H264_ERR_OK || !this->h264_encoder_) {
    ESP_LOGE(TAG, "Failed to create H.264 hardware encoder: %d", ret);
    this->cleanup_h264_encoder_();
    return ESP_FAIL;
  }

  ret = esp_h264_enc_open(this->h264_encoder_);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open H.264 hardware encoder: %d", ret);
    this->cleanup_h264_encoder_();
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "H.264 HARDWARE encoder initialized successfully!");
  return ESP_OK;
}

void RTSPServer::cleanup_h264_encoder_() {
  if (this->h264_encoder_) {
    esp_h264_enc_close(this->h264_encoder_);
    esp_h264_enc_del(this->h264_encoder_);
    this->h264_encoder_ = nullptr;
  }
  if (this->yuv_buffer_) {
    free(this->yuv_buffer_);
    this->yuv_buffer_ = nullptr;
  }
  if (this->h264_buffer_) {
    free(this->h264_buffer_);
    this->h264_buffer_ = nullptr;
  }
  if (this->rtp_packet_buffer_) {
    free(this->rtp_packet_buffer_);
    this->rtp_packet_buffer_ = nullptr;
  }
  if (this->sps_data_) {
    free(this->sps_data_);
    this->sps_data_ = nullptr;
    this->sps_size_ = 0;
  }
  if (this->pps_data_) {
    free(this->pps_data_);
    this->pps_data_ = nullptr;
    this->pps_size_ = 0;
  }
}

esp_err_t RTSPServer::init_rtp_sockets_() {
  ESP_LOGI(TAG, "Initializing RTP/RTCP sockets...");

  // RTP socket
  this->rtp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (this->rtp_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create RTP socket");
    return ESP_FAIL;
  }

  struct sockaddr_in rtp_addr = {};
  rtp_addr.sin_family = AF_INET;
  rtp_addr.sin_addr.s_addr = INADDR_ANY;
  rtp_addr.sin_port = htons(this->rtp_port_);

  if (bind(this->rtp_socket_, (struct sockaddr *) &rtp_addr, sizeof(rtp_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind RTP socket");
    close(this->rtp_socket_);
    this->rtp_socket_ = -1;
    return ESP_FAIL;
  }

  // RTCP socket
  this->rtcp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (this->rtcp_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create RTCP socket");
    close(this->rtp_socket_);
    this->rtp_socket_ = -1;
    return ESP_FAIL;
  }

  struct sockaddr_in rtcp_addr = {};
  rtcp_addr.sin_family = AF_INET;
  rtcp_addr.sin_addr.s_addr = INADDR_ANY;
  rtcp_addr.sin_port = htons(this->rtcp_port_);

  if (bind(this->rtcp_socket_, (struct sockaddr *) &rtcp_addr, sizeof(rtcp_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind RTCP socket");
    close(this->rtp_socket_);
    close(this->rtcp_socket_);
    this->rtp_socket_ = -1;
    this->rtcp_socket_ = -1;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "RTP/RTCP sockets initialized");
  return ESP_OK;
}

esp_err_t RTSPServer::init_rtsp_server_() {
  ESP_LOGI(TAG, "Starting RTSP server on port %d", this->rtsp_port_);

  this->rtsp_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (this->rtsp_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create RTSP socket");
    return ESP_FAIL;
  }

  int reuse = 1;
  setsockopt(this->rtsp_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(this->rtsp_port_);

  if (bind(this->rtsp_socket_, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind RTSP socket");
    close(this->rtsp_socket_);
    this->rtsp_socket_ = -1;
    return ESP_FAIL;
  }

  if (listen(this->rtsp_socket_, 5) < 0) {
    ESP_LOGE(TAG, "Failed to listen on RTSP socket");
    close(this->rtsp_socket_);
    this->rtsp_socket_ = -1;
    return ESP_FAIL;
  }

  int flags = fcntl(this->rtsp_socket_, F_GETFL, 0);
  fcntl(this->rtsp_socket_, F_SETFL, flags | O_NONBLOCK);

  ESP_LOGI(TAG, "RTSP server started");
  return ESP_OK;
}

void RTSPServer::cleanup_sockets_() {
  if (this->rtsp_socket_ >= 0) {
    close(this->rtsp_socket_);
    this->rtsp_socket_ = -1;
  }
  if (this->rtp_socket_ >= 0) {
    close(this->rtp_socket_);
    this->rtp_socket_ = -1;
  }
  if (this->rtcp_socket_ >= 0) {
    close(this->rtcp_socket_);
    this->rtcp_socket_ = -1;
  }
}

void RTSPServer::handle_rtsp_connections_() {
  // Accept new clients
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);

  int client_fd = accept(this->rtsp_socket_, (struct sockaddr *) &client_addr, &addr_len);
  if (client_fd >= 0) {
    if (this->sessions_.size() < this->max_clients_) {
      ESP_LOGI(TAG, "New RTSP client from %s", inet_ntoa(client_addr.sin_addr));

      RTSPSession session = {};
      session.socket_fd = client_fd;
      session.state = RTSPState::INIT;
      session.client_addr = client_addr;
      session.last_activity = millis();
      session.active = true;

      int flags = fcntl(client_fd, F_GETFL, 0);
      fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

      this->sessions_.push_back(session);
    } else {
      ESP_LOGW(TAG, "Max clients reached, rejecting connection");
      close(client_fd);
    }
  }

  // Handle requests for existing sessions
  for (auto &session: this->sessions_) {
    if (session.active) {
      this->handle_rtsp_request_(session);
    }
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

    RTSPMethod method = this->parse_rtsp_method_(request);

    // Auth (except for OPTIONS)
    if (method != RTSPMethod::OPTIONS && !this->check_authentication_(request)) {
      ESP_LOGW(TAG, "Authentication failed");
      int cseq = this->get_cseq_(request);
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      headers["WWW-Authenticate"] = "Basic realm=\"RTSP Server\"";
      this->send_rtsp_response_(session.socket_fd, 401, "Unauthorized", headers);
      return;
    }

    switch (method) {
      case RTSPMethod::OPTIONS:
        this->handle_options_(session, request);
        break;
      case RTSPMethod::DESCRIBE:
        this->handle_describe_(session, request);
        break;
      case RTSPMethod::SETUP:
        this->handle_setup_(session, request);
        break;
      case RTSPMethod::PLAY:
        this->handle_play_(session, request);
        break;
      case RTSPMethod::TEARDOWN:
        this->handle_teardown_(session, request);
        break;
      default:
        ESP_LOGW(TAG, "Unknown RTSP method");
        break;
    }
  } else if (len == 0 || (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
    ESP_LOGI(TAG, "Client disconnected");
    this->remove_session_(session.socket_fd);
  }
}

RTSPMethod RTSPServer::parse_rtsp_method_(const std::string &request) {
  if (request.rfind("OPTIONS", 0) == 0)
    return RTSPMethod::OPTIONS;
  if (request.rfind("DESCRIBE", 0) == 0)
    return RTSPMethod::DESCRIBE;
  if (request.rfind("SETUP", 0) == 0)
    return RTSPMethod::SETUP;
  if (request.rfind("PLAY", 0) == 0)
    return RTSPMethod::PLAY;
  if (request.rfind("PAUSE", 0) == 0)
    return RTSPMethod::PAUSE;
  if (request.rfind("TEARDOWN", 0) == 0)
    return RTSPMethod::TEARDOWN;
  return RTSPMethod::UNKNOWN;
}

void RTSPServer::send_rtsp_response_(int socket_fd,
                                     int code,
                                     const std::string &status,
                                     const std::map<std::string, std::string> &headers,
                                     const std::string &body) {
  std::ostringstream response;
  response << "RTSP/1.0 " << code << " " << status << "\r\n";

  for (const auto &h: headers) {
    response << h.first << ": " << h.second << "\r\n";
  }

  if (!body.empty()) {
    response << "Content-Length: " << body.length() << "\r\n";
  }

  response << "\r\n";

  if (!body.empty()) {
    response << body;
  }

  std::string resp_str = response.str();
  send(socket_fd, resp_str.c_str(), resp_str.length(), 0);

  ESP_LOGD(TAG, "RTSP Response:\n%s", resp_str.c_str());
}

void RTSPServer::handle_options_(RTSPSession &session, const std::string &request) {
  int cseq = this->get_cseq_(request);
  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Public"] = "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN";
  this->send_rtsp_response_(session.socket_fd, 200, "OK", headers);
}

void RTSPServer::handle_describe_(RTSPSession &session, const std::string &request) {
  int cseq = this->get_cseq_(request);

  if (!this->h264_encoder_) {
    ESP_LOGI(TAG, "Initializing H.264 encoder for DESCRIBE...");
    if (this->init_h264_encoder_() != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize H.264 encoder");
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      this->send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }

    // Best effort to get SPS/PPS for SDP
    if (this->sps_data_ == nullptr || this->pps_data_ == nullptr) {
      ESP_LOGI(TAG, "Trying to extract SPS/PPS for SDP...");
      this->encode_and_stream_frame_();  // will cache SPS/PPS if frame is available
    }
  }

  std::string sdp = this->generate_sdp_();

  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Content-Type"] = "application/sdp";

  this->send_rtsp_response_(session.socket_fd, 200, "OK", headers, sdp);
}

void RTSPServer::handle_setup_(RTSPSession &session, const std::string &request) {
  int cseq = this->get_cseq_(request);

  std::string transport_line = this->get_request_line_(request, "Transport");
  ESP_LOGD(TAG, "Transport: '%s'", transport_line.c_str());

  if (transport_line.find("interleaved") != std::string::npos ||
      transport_line.find("RTP/AVP/TCP") != std::string::npos) {
    ESP_LOGW(TAG, "TCP interleaved not supported (use UDP)");
    std::map<std::string, std::string> headers;
    headers["CSeq"] = std::to_string(cseq);
    this->send_rtsp_response_(session.socket_fd, 461, "Unsupported Transport", headers);
    return;
  }

  size_t pos = transport_line.find("client_port=");
  if (pos != std::string::npos) {
    int rtp_port, rtcp_port;
    sscanf(transport_line.c_str() + pos, "client_port=%d-%d", &rtp_port, &rtcp_port);
    session.client_rtp_port = rtp_port;
    session.client_rtcp_port = rtcp_port;
  } else {
    ESP_LOGW(TAG, "No client_port found in Transport");
    std::map<std::string, std::string> headers;
    headers["CSeq"] = std::to_string(cseq);
    this->send_rtsp_response_(session.socket_fd, 461, "Unsupported Transport", headers);
    return;
  }

  if (session.session_id.empty()) {
    session.session_id = this->generate_session_id_();
  }
  session.state = RTSPState::READY;

  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Session"] = session.session_id;
  headers["Transport"] =
      "RTP/AVP;unicast;client_port=" + std::to_string(session.client_rtp_port) +
      "-" + std::to_string(session.client_rtcp_port) +
      ";server_port=" + std::to_string(this->rtp_port_) + "-" + std::to_string(this->rtcp_port_);

  this->send_rtsp_response_(session.socket_fd, 200, "OK", headers);

  ESP_LOGI(TAG, "Session %s setup, client RTP port: %d",
           session.session_id.c_str(), session.client_rtp_port);
}

void RTSPServer::handle_play_(RTSPSession &session, const std::string &request) {
  int cseq = this->get_cseq_(request);

  if (!this->h264_encoder_) {
    ESP_LOGW(TAG, "H.264 encoder not initialized (client skipped DESCRIBE?)");
    if (this->init_h264_encoder_() != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize H.264 encoder");
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      this->send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }
  }

  session.state = RTSPState::PLAYING;
  this->streaming_active_ = true;

  if (this->streaming_task_handle_ == nullptr) {
    BaseType_t result = xTaskCreatePinnedToCore(
        streaming_task_wrapper_,
        "rtsp_stream",
        16384,        // 16 KB stack
        this,
        5,
        &this->streaming_task_handle_,
        1
    );

    if (result != pdPASS || this->streaming_task_handle_ == nullptr) {
      ESP_LOGE(TAG, "Failed to create streaming task (result=%d)", result);
      this->streaming_active_ = false;
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      this->send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }

    ESP_LOGI(TAG, "Streaming task created with 16KB stack on core 1");
  }

  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Session"] = session.session_id;
  headers["RTP-Info"] = "url=" + this->stream_path_ + ";seq=" + std::to_string(this->rtp_seq_num_);

  this->send_rtsp_response_(session.socket_fd, 200, "OK", headers);

  ESP_LOGI(TAG, "Session %s started playing", session.session_id.c_str());
}

void RTSPServer::handle_teardown_(RTSPSession &session, const std::string &request) {
  int cseq = this->get_cseq_(request);

  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Session"] = session.session_id;

  this->send_rtsp_response_(session.socket_fd, 200, "OK", headers);

  ESP_LOGI(TAG, "Session %s teardown", session.session_id.c_str());
  this->remove_session_(session.socket_fd);

  bool any_playing = false;
  for (const auto &s: this->sessions_) {
    if (s.active && s.state == RTSPState::PLAYING) {
      any_playing = true;
      break;
    }
  }

  if (!any_playing && this->streaming_active_) {
    ESP_LOGI(TAG, "Stopping streaming (no active clients)...");
    this->streaming_active_ = false;

    if (this->streaming_task_handle_ != nullptr) {
      for (int i = 0; i < 50; i++) {
        eTaskState st = eTaskGetState(this->streaming_task_handle_);
        if (st == eSuspended) break;
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      vTaskDelete(this->streaming_task_handle_);
      this->streaming_task_handle_ = nullptr;
      ESP_LOGI(TAG, "Streaming task stopped");
    }
  }
}

std::string RTSPServer::generate_sdp_() {
  std::string local_ip = "0.0.0.0";  // could be replaced by actual IP if needed
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

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

  if (this->sps_data_ && this->sps_size_ > 0 &&
      this->pps_data_ && this->pps_size_ > 0) {
    std::string sps_b64 = this->base64_encode_(this->sps_data_, this->sps_size_);
    std::string pps_b64 = this->base64_encode_(this->pps_data_, this->pps_size_);
    sdp << ";sprop-parameter-sets=" << sps_b64 << "," << pps_b64;
    ESP_LOGI(TAG, "SDP includes SPS/PPS (SPS: %d bytes, PPS: %d bytes)",
             this->sps_size_, this->pps_size_);
  } else {
    ESP_LOGW(TAG, "SDP generated WITHOUT SPS/PPS - client will get them from RTP");
  }

  sdp << "\r\n";
  sdp << "a=control:track1\r\n";
  sdp << "a=framerate:30\r\n";
  sdp << "a=framesize:96 " << width << "-" << height << "\r\n";

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
      char_array_3[j] = '\0';

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

esp_err_t RTSPServer::stream_video_() {
  if (!this->streaming_active_)
    return ESP_OK;
  return this->encode_and_stream_frame_();
}

// (YUYV-conversion kept in case you passes YUYV someday – not used with OV5647 RGB565)
esp_err_t RTSPServer::convert_yuyv_to_o_uyy_e_vyy_(const uint8_t *yuyv,
                                                   uint8_t *o_uyy_e_vyy,
                                                   uint16_t width,
                                                   uint16_t height) {
  const uint8_t *src = yuyv;

  for (uint16_t row = 0; row < height; row += 2) {
    uint8_t *odd_line = o_uyy_e_vyy + (row * width * 3 / 2);
    uint8_t *even_line = o_uyy_e_vyy + ((row + 1) * width * 3 / 2);

    const uint8_t *src_row0 = src + (row * width * 2);
    const uint8_t *src_row1 = src + ((row + 1) * width * 2);

    for (uint16_t col = 0; col < width; col += 2) {
      uint8_t y0_r0 = src_row0[col * 2 + 0];
      uint8_t u0_r0 = src_row0[col * 2 + 1];
      uint8_t y1_r0 = src_row0[col * 2 + 2];
      uint8_t v0_r0 = src_row0[col * 2 + 3];

      uint8_t y0_r1 = src_row1[col * 2 + 0];
      uint8_t u0_r1 = src_row1[col * 2 + 1];
      uint8_t y1_r1 = src_row1[col * 2 + 2];
      uint8_t v0_r1 = src_row1[col * 2 + 3];

      uint8_t u_avg = (u0_r0 + u0_r1) >> 1;
      uint8_t v_avg = (v0_r0 + v0_r1) >> 1;

      size_t odd_idx = (col / 2) * 3;
      odd_line[odd_idx + 0] = u_avg;
      odd_line[odd_idx + 1] = y0_r0;
      odd_line[odd_idx + 2] = y1_r0;

      size_t even_idx = (col / 2) * 3;
      even_line[even_idx + 0] = v_avg;
      even_line[even_idx + 1] = y0_r1;
      even_line[even_idx + 2] = y1_r1;
    }
  }

  return ESP_OK;
}

void RTSPServer::init_yuv_lut_() {
  if (yuv_lut_initialized_)
    return;

  for (int i = 0; i < 32; i++) {
    int val_8bit = (i << 3) | (i >> 2);
    y_r_lut_[i] = (66 * val_8bit) >> 8;
    y_b_lut_[i] = (25 * val_8bit) >> 8;
    u_r_lut_[i] = (-38 * val_8bit) >> 8;
    u_b_lut_[i] = (112 * val_8bit) >> 8;
    v_r_lut_[i] = (112 * val_8bit) >> 8;
    v_b_lut_[i] = (-18 * val_8bit) >> 8;
  }

  for (int i = 0; i < 64; i++) {
    int val_8bit = (i << 2) | (i >> 4);
    y_g_lut_[i] = (129 * val_8bit) >> 8;
    u_g_lut_[i] = (-74 * val_8bit) >> 8;
    v_g_lut_[i] = (-94 * val_8bit) >> 8;
  }

  yuv_lut_initialized_ = true;
  ESP_LOGI(TAG, "YUV lookup tables initialized");
}

esp_err_t RTSPServer::convert_rgb565_to_yuv420_(const uint8_t *rgb565,
                                                uint8_t *yuv420,
                                                uint16_t width,
                                                uint16_t height) {
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

      uint8_t y0 = y_r_lut_[r0] + y_g_lut_[g0] + y_b_lut_[b0] + 16;
      uint8_t y1 = y_r_lut_[r1] + y_g_lut_[g1] + y_b_lut_[b1] + 16;
      uint8_t y2 = y_r_lut_[r2] + y_g_lut_[g2] + y_b_lut_[b2] + 16;
      uint8_t y3 = y_r_lut_[r3] + y_g_lut_[g3] + y_b_lut_[b3] + 16;

      uint8_t r_avg = (r0 + r1 + r2 + r3) >> 2;
      uint8_t g_avg = (g0 + g1 + g2 + g3) >> 2;
      uint8_t b_avg = (b0 + b1 + b2 + b3) >> 2;

      uint8_t u = u_r_lut_[r_avg] + u_g_lut_[g_avg] + u_b_lut_[b_avg] + 128;
      uint8_t v = v_r_lut_[r_avg] + v_g_lut_[g_avg] + v_b_lut_[b_avg] + 128;

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

esp_err_t RTSPServer::encode_and_stream_frame_() {
  if (!this->camera_ || !this->h264_encoder_)
    return ESP_FAIL;

  // 🔴 IMPORTANT : on utilise la même API que camera_web_server
  // capture_frame() + get_image_data() → RGB565
  if (!this->camera_->capture_frame()) {
    ESP_LOGW(TAG, "Failed to capture frame from camera");
    return ESP_FAIL;
  }

  uint8_t *frame_data = this->camera_->get_image_data();
  size_t frame_size = this->camera_->get_image_size();
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (frame_data == nullptr || frame_size == 0) {
    ESP_LOGW(TAG, "Invalid frame: ptr=%p size=%u", frame_data, (unsigned) frame_size);
    return ESP_FAIL;
  }

  if (this->frame_count_ == 0) {
    ESP_LOGI(TAG,
             "First RGB565 frame: %ux%u (%u bytes)",
             width, height, (unsigned) frame_size);
  }

  // Convert RGB565 -> O_UYY_E_VYY (YUV420 packed) for HW encoder
  this->convert_rgb565_to_yuv420_(frame_data, this->yuv_buffer_, width, height);

  // Encode from YUV buffer
  esp_h264_enc_in_frame_t in_frame = {};
  in_frame.raw_data.buffer = this->yuv_buffer_;
  in_frame.raw_data.len = this->yuv_buffer_size_;
  in_frame.pts = this->frame_count_ * 90000 / 30;  // 30 FPS

  esp_h264_enc_out_frame_t out_frame = {};
  out_frame.raw_data.buffer = this->h264_buffer_;
  out_frame.raw_data.len = this->h264_buffer_size_;

  esp_h264_err_t ret = esp_h264_enc_process(this->h264_encoder_, &in_frame, &out_frame);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG,
             "H.264 encoding failed: err=%d (frame=%u, in_len=%u, out_len=%u)",
             ret, this->frame_count_,
             in_frame.raw_data.len, out_frame.raw_data.len);
    return ESP_FAIL;
  }

  const char *frame_type_name = "Unknown";
  if (out_frame.frame_type == ESP_H264_FRAME_TYPE_IDR) frame_type_name = "IDR";
  else if (out_frame.frame_type == ESP_H264_FRAME_TYPE_I) frame_type_name = "I";
  else if (out_frame.frame_type == ESP_H264_FRAME_TYPE_P) frame_type_name = "P";

  ESP_LOGV(TAG,
           "Frame %u encoded: %u bytes, type=%d (%s)",
           this->frame_count_,
           out_frame.length,
           out_frame.frame_type,
           frame_type_name);

  if (out_frame.length == 0 || out_frame.raw_data.buffer == nullptr) {
    ESP_LOGE(TAG, "Invalid H.264 output: len=%u buf=%p",
             out_frame.length, out_frame.raw_data.buffer);
    return ESP_FAIL;
  }

  if (out_frame.frame_type == ESP_H264_FRAME_TYPE_IDR) {
    ESP_LOGI(TAG, "IDR frame - caching SPS/PPS");
    this->parse_and_cache_nal_units_(out_frame.raw_data.buffer, out_frame.length);
  }

  auto nal_units = this->parse_nal_units_(out_frame.raw_data.buffer, out_frame.length);
  ESP_LOGV(TAG, "Found %d NAL units", (int) nal_units.size());

  for (size_t i = 0; i < nal_units.size(); i++) {
    const auto &nal = nal_units[i];
    uint8_t nal_type = nal.first[0] & 0x1F;
    const char *nal_name = "Unknown";
    if (nal_type == 1) nal_name = "P-slice";
    else if (nal_type == 5) nal_name = "IDR";
    else if (nal_type == 6) nal_name = "SEI";
    else if (nal_type == 7) nal_name = "SPS";
    else if (nal_type == 8) nal_name = "PPS";

    ESP_LOGV(TAG,
             "Sending NAL %d: type=%d (%s), %u bytes",
             (int) i, nal_type, nal_name, (unsigned) nal.second);

    // marker = true pour chaque NAL (idéalement, seulement pour le dernier d'une frame)
    this->send_h264_rtp_(nal.first, nal.second, true);
  }

  this->frame_count_++;
  this->rtp_timestamp_ += 3000;  // 90kHz / 30fps

  return ESP_OK;
}

void RTSPServer::parse_and_cache_nal_units_(const uint8_t *data, size_t len) {
  auto nal_units = this->parse_nal_units_(data, len);

  for (const auto &nal : nal_units) {
    uint8_t nal_type = nal.first[0] & 0x1F;
    if (nal_type == 7) {  // SPS
      if (this->sps_data_)
        free(this->sps_data_);
      this->sps_size_ = nal.second;
      this->sps_data_ = (uint8_t *) malloc(this->sps_size_);
      memcpy(this->sps_data_, nal.first, this->sps_size_);
      ESP_LOGI(TAG, "Cached SPS (%d bytes)", this->sps_size_);
    } else if (nal_type == 8) {  // PPS
      if (this->pps_data_)
        free(this->pps_data_);
      this->pps_size_ = nal.second;
      this->pps_data_ = (uint8_t *) malloc(this->pps_size_);
      memcpy(this->pps_data_, nal.first, this->pps_size_);
      ESP_LOGI(TAG, "Cached PPS (%d bytes)", this->pps_size_);
    }
  }
}

std::vector<std::pair<const uint8_t *, size_t>>
RTSPServer::parse_nal_units_(const uint8_t *data, size_t len) {
  this->nal_units_cache_.clear();

  if (data == nullptr || len < 4) {
    ESP_LOGW(TAG, "parse_nal_units_: invalid input (ptr=%p, len=%u)", data, (unsigned) len);
    return this->nal_units_cache_;
  }

  size_t i = 0;
  while (i + 3 < len) {
    if (data[i] == 0x00 && data[i + 1] == 0x00) {
      size_t start_code_len = 0;
      if (data[i + 2] == 0x01) {
        start_code_len = 3;
      } else if (data[i + 2] == 0x00 && data[i + 3] == 0x01) {
        start_code_len = 4;
      }

      if (start_code_len > 0) {
        size_t j = i + start_code_len;
        while (j + 3 < len) {
          if (data[j] == 0x00 && data[j + 1] == 0x00 &&
              (data[j + 2] == 0x01 ||
               (data[j + 2] == 0x00 && data[j + 3] == 0x01))) {
            break;
          }
          j++;
        }

        size_t nal_size = j - (i + start_code_len);
        if (nal_size > 0) {
          this->nal_units_cache_.push_back({data + i + start_code_len, nal_size});
        }

        i = j;
        continue;
      }
    }
    i++;
  }

  return this->nal_units_cache_;
}

esp_err_t RTSPServer::send_h264_rtp_(const uint8_t *data, size_t len, bool marker) {
  if (!data || len == 0) {
    ESP_LOGW(TAG, "send_h264_rtp_: invalid data");
    return ESP_FAIL;
  }

  const size_t MAX_RTP_PAYLOAD = 1400;

  if (len <= MAX_RTP_PAYLOAD) {
    uint8_t *packet = this->rtp_packet_buffer_;
    RTPHeader *rtp = (RTPHeader *) packet;

    rtp->v = 2;
    rtp->p = 0;
    rtp->x = 0;
    rtp->cc = 0;
    rtp->m = marker ? 1 : 0;
    rtp->pt = 96;
    rtp->seq = htons(this->rtp_seq_num_++);
    rtp->timestamp = htonl(this->rtp_timestamp_);
    rtp->ssrc = htonl(this->rtp_ssrc_);

    memcpy(packet + sizeof(RTPHeader), data, len);

    for (auto &session: this->sessions_) {
      if (session.active && session.state == RTSPState::PLAYING) {
        struct sockaddr_in dest = session.client_addr;
        dest.sin_port = htons(session.client_rtp_port);
        sendto(this->rtp_socket_,
               packet,
               sizeof(RTPHeader) + len,
               0,
               (struct sockaddr *) &dest,
               sizeof(dest));
      }
    }

    return ESP_OK;
  }

  ESP_LOGD(TAG, "Fragmenting NAL (%u bytes) using FU-A", (unsigned) len);

  uint8_t nal_header = data[0];
  uint8_t nal_type = nal_header & 0x1F;
  uint8_t nri = nal_header & 0x60;

  uint8_t fu_indicator = nri | 28;  // FU-A
  const uint8_t *payload = data + 1;
  size_t payload_len = len - 1;
  size_t offset = 0;
  size_t fragments = 0;

  uint8_t *packet = this->rtp_packet_buffer_;

  while (offset < payload_len) {
    RTPHeader *rtp = (RTPHeader *) packet;

    rtp->v = 2;
    rtp->p = 0;
    rtp->x = 0;
    rtp->cc = 0;
    rtp->pt = 96;
    rtp->seq = htons(this->rtp_seq_num_++);
    rtp->timestamp = htonl(this->rtp_timestamp_);
    rtp->ssrc = htonl(this->rtp_ssrc_);

    uint8_t *fu_payload = packet + sizeof(RTPHeader);
    fu_payload[0] = fu_indicator;

    bool is_start = (offset == 0);
    size_t remaining = payload_len - offset;
    size_t chunk = remaining > (MAX_RTP_PAYLOAD - 2) ? (MAX_RTP_PAYLOAD - 2) : remaining;
    bool is_end = (offset + chunk >= payload_len);

    uint8_t fu_header = nal_type;
    if (is_start) fu_header |= 0x80;
    if (is_end) fu_header |= 0x40;

    fu_payload[1] = fu_header;

    memcpy(fu_payload + 2, payload + offset, chunk);

    rtp->m = (is_end && marker) ? 1 : 0;

    size_t packet_size = sizeof(RTPHeader) + 2 + chunk;

    for (auto &session: this->sessions_) {
      if (session.active && session.state == RTSPState::PLAYING) {
        struct sockaddr_in dest = session.client_addr;
        dest.sin_port = htons(session.client_rtp_port);
        sendto(this->rtp_socket_,
               packet,
               packet_size,
               0,
               (struct sockaddr *) &dest,
               sizeof(dest));
      }
    }

    offset += chunk;
    fragments++;
  }

  ESP_LOGV(TAG, "Sent NAL in %u fragments", (unsigned) fragments);
  return ESP_OK;
}

std::string RTSPServer::generate_session_id_() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%08X", esp_random());
  return std::string(buf);
}

RTSPSession *RTSPServer::find_session_(int socket_fd) {
  for (auto &s: this->sessions_) {
    if (s.socket_fd == socket_fd && s.active)
      return &s;
  }
  return nullptr;
}

RTSPSession *RTSPServer::find_session_by_id_(const std::string &session_id) {
  for (auto &s: this->sessions_) {
    if (s.session_id == session_id && s.active)
      return &s;
  }
  return nullptr;
}

void RTSPServer::remove_session_(int socket_fd) {
  for (auto &s: this->sessions_) {
    if (s.socket_fd == socket_fd) {
      close(s.socket_fd);
      s.active = false;
      ESP_LOGI(TAG, "Session %s removed", s.session_id.c_str());
      break;
    }
  }

  this->sessions_.erase(
      std::remove_if(this->sessions_.begin(),
                     this->sessions_.end(),
                     [](const RTSPSession &s) { return !s.active; }),
      this->sessions_.end());
}

void RTSPServer::cleanup_inactive_sessions_() {
  uint32_t now = millis();
  const uint32_t timeout = 60000;

  for (auto &s: this->sessions_) {
    if (s.active && (now - s.last_activity > timeout)) {
      ESP_LOGW(TAG, "Session %s timed out", s.session_id.c_str());
      this->remove_session_(s.socket_fd);
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
  std::string cseq_str = this->get_request_line_(request, "CSeq");
  return cseq_str.empty() ? 0 : std::stoi(cseq_str);
}

bool RTSPServer::check_authentication_(const std::string &request) {
  if (this->username_.empty() && this->password_.empty()) {
    ESP_LOGD(TAG, "Authentication disabled");
    return true;
  }

  std::string auth = this->get_request_line_(request, "Authorization");
  if (auth.empty()) {
    ESP_LOGW(TAG, "Auth failed: no Authorization header");
    return false;
  }

  if (auth.find("Basic ") != 0) {
    ESP_LOGW(TAG, "Auth failed: not Basic");
    return false;
  }

  std::string encoded = auth.substr(6);
  std::string decoded;
  int val = 0;
  int valb = -8;
  for (unsigned char c: encoded) {
    if (c == '=') break;
    const char *p = strchr(base64_chars, c);
    if (!p) continue;
    val = (val << 6) + (p - base64_chars);
    valb += 6;
    if (valb >= 0) {
      decoded.push_back(char((val >> valb) & 0xFF));
      valb -= 8;
    }
  }

  size_t colon = decoded.find(':');
  if (colon == std::string::npos) {
    ESP_LOGW(TAG, "Auth failed: invalid decoded format");
    return false;
  }

  std::string user = decoded.substr(0, colon);
  std::string pass = decoded.substr(colon + 1);

  bool ok = (user == this->username_ && pass == this->password_);
  if (!ok) {
    ESP_LOGW(TAG, "Auth failed: wrong credentials");
  } else {
    ESP_LOGI(TAG, "Authentication successful for user '%s'", user.c_str());
  }
  return ok;
}

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
      float fps = elapsed ? (frame_num * 1000.0f / elapsed) : 0.0f;
      float avg = frame_num ? (total_encode_time * 1.0f / frame_num) : 0.0f;
      ESP_LOGI(TAG,
               "Performance: %.1f FPS (avg encode: %.1f ms, last: %u ms)",
               fps, avg, dt);
    }

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



