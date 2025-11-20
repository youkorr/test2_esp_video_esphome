#include "rtsp_server.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include <cstring>
#include <cstdio>
#include <sstream>
#include <algorithm>

#include <esp_random.h>
#include <arpa/inet.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <fcntl.h>
#include <errno.h>
#include "esp_heap_caps.h"

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

  if (!camera_) {
    ESP_LOGE(TAG, "Camera not set");
    mark_failed();
    return;
  }

  // Optionnel : forcer le format pour OV5647 si besoin (sinon géré dans le YAML)
  // camera_->set_pixel_format(mipi_dsi_cam::PIXEL_FORMAT_RGB565);

  // Generate random SSRC
  rtp_ssrc_ = esp_random();

  // Initialize RTP/RTCP sockets
  if (init_rtp_sockets_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize RTP sockets");
    mark_failed();
    return;
  }

  // Initialize RTSP server
  if (init_rtsp_server_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize RTSP server");
    mark_failed();
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

  ESP_LOGI(TAG, "Note: H.264 encoder will initialize when first client connects (DESCRIBE/PLAY)");
}

void RTSPServer::loop() {
  // Géré par un switch dans ESPHome
  if (!enabled_) {
    if (streaming_active_) {
      ESP_LOGI(TAG, "RTSP server disabled by switch, stopping streaming...");
      streaming_active_ = false;

      if (streaming_task_handle_ != nullptr) {
        // Laisser la tâche se suspendre
        for (int i = 0; i < 50; i++) {
          eTaskState task_state = eTaskGetState(streaming_task_handle_);
          if (task_state == eSuspended) {
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

  // Gestion des connexions RTSP (léger, safe pour la stack de loopTask)
  handle_rtsp_connections_();

  // Nettoyage des sessions inactives
  cleanup_inactive_sessions_();
}

void RTSPServer::dump_config() {
  ESP_LOGCONFIG(TAG, "RTSP Server:");
  ESP_LOGCONFIG(TAG, "  Status: %s (controlled by switch)", enabled_ ? "ENABLED" : "DISABLED");
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

esp_err_t RTSPServer::init_h264_encoder_() {
  ESP_LOGI(TAG, "Initializing H.264 HARDWARE encoder (ESP32-P4 accelerator)...");

  if (!camera_) {
    ESP_LOGE(TAG, "Camera not set");
    return ESP_FAIL;
  }

  // S'assurer que la caméra stream
  if (!camera_->is_streaming()) {
    ESP_LOGW(TAG, "Camera not streaming yet, starting stream...");
    if (!camera_->start_streaming()) {
      ESP_LOGE(TAG, "Failed to start camera streaming");
      return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  uint16_t width = camera_->get_image_width();
  uint16_t height = camera_->get_image_height();

  if (width == 0 || height == 0) {
    ESP_LOGE(TAG, "Invalid camera dimensions: %dx%d", width, height);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Camera resolution: %dx%d (RGB565 expected)", width, height);

  // Pas de réalignement : on suppose des résolutions multiples de 16 (720,480, etc.)
  yuv_buffer_size_ = width * height * 3 / 2;
  yuv_buffer_ = (uint8_t *)heap_caps_aligned_alloc(64, yuv_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!yuv_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate YUV buffer (64-byte aligned)");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "YUV buffer allocated: %zu bytes @ %p", yuv_buffer_size_, yuv_buffer_);

  h264_buffer_size_ = yuv_buffer_size_ * 2;
  h264_buffer_ = (uint8_t *)heap_caps_aligned_alloc(64, h264_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!h264_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate H.264 buffer (64-byte aligned)");
    free(yuv_buffer_);
    yuv_buffer_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "H.264 buffer allocated: %zu bytes @ %p", h264_buffer_size_, h264_buffer_);

  // Buffer RTP réutilisable
  rtp_packet_buffer_ = (uint8_t *)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!rtp_packet_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate RTP packet buffer");
    free(yuv_buffer_);
    free(h264_buffer_);
    yuv_buffer_ = nullptr;
    h264_buffer_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  nal_units_cache_.reserve(20);

  // Configuration de l’encodeur HW
  esp_h264_enc_cfg_hw_t cfg = {};
  cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;  // YUV420 packed spécial P4
  cfg.gop = gop_;
  cfg.fps = 30;
  cfg.res.width = width;
  cfg.res.height = height;
  cfg.rc.bitrate = bitrate_;
  cfg.rc.qp_min = qp_min_;
  cfg.rc.qp_max = qp_max_;

  ESP_LOGI(TAG, "Encoder config: %dx%d @ %dfps, GOP=%d, bitrate=%d, QP=%d-%d",
           width, height, cfg.fps, gop_, bitrate_, qp_min_, qp_max_);

  esp_h264_err_t ret = esp_h264_enc_hw_new(&cfg, &h264_encoder_);
  if (ret != ESP_H264_ERR_OK || !h264_encoder_) {
    ESP_LOGE(TAG, "Failed to create H.264 hardware encoder: %d", ret);
    cleanup_h264_encoder_();
    return ESP_FAIL;
  }

  ret = esp_h264_enc_open(h264_encoder_);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open H.264 hardware encoder: %d", ret);
    cleanup_h264_encoder_();
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "H.264 HARDWARE encoder initialized successfully");
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
  }
  if (h264_buffer_) {
    free(h264_buffer_);
    h264_buffer_ = nullptr;
  }
  if (rtp_packet_buffer_) {
    free(rtp_packet_buffer_);
    rtp_packet_buffer_ = nullptr;
  }
  if (sps_data_) {
    free(sps_data_);
    sps_data_ = nullptr;
  }
  if (pps_data_) {
    free(pps_data_);
    pps_data_ = nullptr;
  }
}

esp_err_t RTSPServer::init_rtp_sockets_() {
  ESP_LOGI(TAG, "Initializing RTP/RTCP sockets...");

  // RTP
  rtp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (rtp_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create RTP socket");
    return ESP_FAIL;
  }

  struct sockaddr_in rtp_addr = {};
  rtp_addr.sin_family = AF_INET;
  rtp_addr.sin_addr.s_addr = INADDR_ANY;
  rtp_addr.sin_port = htons(rtp_port_);

  if (bind(rtp_socket_, (struct sockaddr *)&rtp_addr, sizeof(rtp_addr)) < 0) {
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

  struct sockaddr_in rtcp_addr = {};
  rtcp_addr.sin_family = AF_INET;
  rtcp_addr.sin_addr.s_addr = INADDR_ANY;
  rtcp_addr.sin_port = htons(rtcp_port_);

  if (bind(rtcp_socket_, (struct sockaddr *)&rtcp_addr, sizeof(rtcp_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind RTCP socket");
    close(rtp_socket_);
    close(rtcp_socket_);
    rtp_socket_ = -1;
    rtcp_socket_ = -1;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "RTP/RTCP sockets initialized");
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

  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(rtsp_port_);

  if (bind(rtsp_socket_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
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

void RTSPServer::handle_rtsp_connections_() {
  // Nouvelle connexion
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);

  int client_fd = accept(rtsp_socket_, (struct sockaddr *)&client_addr, &addr_len);
  if (client_fd >= 0) {
    if (sessions_.size() < max_clients_) {
      ESP_LOGI(TAG, "New RTSP client from %s", inet_ntoa(client_addr.sin_addr));

      RTSPSession session = {};
      session.socket_fd = client_fd;
      session.state = RTSPState::INIT;
      session.client_addr = client_addr;
      session.last_activity = millis();
      session.active = true;

      int flags = fcntl(client_fd, F_GETFL, 0);
      fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

      sessions_.push_back(session);
    } else {
      ESP_LOGW(TAG, "Max clients reached, rejecting connection");
      close(client_fd);
    }
  }

  // Sessions existantes
  for (auto &session : sessions_) {
    if (session.active) {
      handle_rtsp_request_(session);
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

    RTSPMethod method = parse_rtsp_method_(request);

    // Auth sauf OPTIONS
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
  if (request.find("OPTIONS") == 0) return RTSPMethod::OPTIONS;
  if (request.find("DESCRIBE") == 0) return RTSPMethod::DESCRIBE;
  if (request.find("SETUP") == 0) return RTSPMethod::SETUP;
  if (request.find("PLAY") == 0) return RTSPMethod::PLAY;
  if (request.find("PAUSE") == 0) return RTSPMethod::PAUSE;
  if (request.find("TEARDOWN") == 0) return RTSPMethod::TEARDOWN;
  return RTSPMethod::UNKNOWN;
}

void RTSPServer::send_rtsp_response_(int socket_fd, int code, const std::string &status,
                                     const std::map<std::string, std::string> &headers,
                                     const std::string &body) {
  std::ostringstream response;
  response << "RTSP/1.0 " << code << " " << status << "\r\n";

  for (const auto &header : headers) {
    response << header.first << ": " << header.second << "\r\n";
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
  int cseq = get_cseq_(request);

  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Public"] = "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN";

  send_rtsp_response_(session.socket_fd, 200, "OK", headers);
}

void RTSPServer::handle_describe_(RTSPSession &session, const std::string &request) {
  int cseq = get_cseq_(request);

  if (!h264_encoder_) {
    ESP_LOGI(TAG, "Initializing H.264 encoder for DESCRIBE...");
    if (init_h264_encoder_() != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize H.264 encoder");
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }

    if (sps_data_ == nullptr || pps_data_ == nullptr) {
      ESP_LOGI(TAG, "Attempting to extract SPS/PPS for SDP...");
      encode_and_stream_frame_();  // Essaie de récupérer SPS/PPS
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
  ESP_LOGD(TAG, "Transport header: '%s'", transport_line.c_str());

  if (transport_line.find("interleaved") != std::string::npos ||
      transport_line.find("RTP/AVP/TCP") != std::string::npos) {
    ESP_LOGW(TAG, "TCP interleaved not supported");
    std::map<std::string, std::string> headers;
    headers["CSeq"] = std::to_string(cseq);
    send_rtsp_response_(session.socket_fd, 461, "Unsupported Transport", headers);
    return;
  }

  size_t client_port_pos = transport_line.find("client_port=");
  if (client_port_pos != std::string::npos) {
    int rtp_port, rtcp_port;
    sscanf(transport_line.c_str() + client_port_pos, "client_port=%d-%d", &rtp_port, &rtcp_port);
    session.client_rtp_port = rtp_port;
    session.client_rtcp_port = rtcp_port;
  } else {
    ESP_LOGW(TAG, "No client_port found");
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
  headers["Transport"] = "RTP/AVP;unicast;client_port=" + std::to_string(session.client_rtp_port) +
                         "-" + std::to_string(session.client_rtcp_port) +
                         ";server_port=" + std::to_string(rtp_port_) + "-" + std::to_string(rtcp_port_);

  send_rtsp_response_(session.socket_fd, 200, "OK", headers);

  ESP_LOGI(TAG, "Session %s setup, client RTP port: %d", session.session_id.c_str(), session.client_rtp_port);
}

void RTSPServer::handle_play_(RTSPSession &session, const std::string &request) {
  int cseq = get_cseq_(request);

  if (!h264_encoder_) {
    ESP_LOGW(TAG, "Encoder not initialized (PLAY without DESCRIBE?)");
    if (init_h264_encoder_() != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize H.264 encoder");
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }
  }

  // S’assurer que la caméra stream
  if (!camera_->is_streaming()) {
    ESP_LOGI(TAG, "Starting camera streaming for RTSP PLAY");
    if (!camera_->start_streaming()) {
      ESP_LOGE(TAG, "Camera failed to start");
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  session.state = RTSPState::PLAYING;
  streaming_active_ = true;

  if (streaming_task_handle_ == nullptr) {
    BaseType_t result = xTaskCreatePinnedToCore(
        streaming_task_wrapper_,
        "rtsp_stream",
        16384,     // 16KB
        this,
        5,
        &streaming_task_handle_,
        1          // core 1
    );

    if (result != pdPASS || streaming_task_handle_ == nullptr) {
      ESP_LOGE(TAG, "Failed to create streaming task (res=%d)", result);
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

  ESP_LOGI(TAG, "Session %s started playing", session.session_id.c_str());
}

void RTSPServer::handle_teardown_(RTSPSession &session, const std::string &request) {
  int cseq = get_cseq_(request);

  std::map<std::string, std::string> headers;
  headers["CSeq"] = std::to_string(cseq);
  headers["Session"] = session.session_id;

  send_rtsp_response_(session.socket_fd, 200, "OK", headers);

  ESP_LOGI(TAG, "Session %s teardown", session.session_id.c_str());

  remove_session_(session.socket_fd);

  bool any_playing = false;
  for (const auto &s : sessions_) {
    if (s.active && s.state == RTSPState::PLAYING) {
      any_playing = true;
      break;
    }
  }

  if (!any_playing && streaming_active_) {
    ESP_LOGI(TAG, "Stopping streaming (no active clients)");
    streaming_active_ = false;

    if (streaming_task_handle_ != nullptr) {
      for (int i = 0; i < 50; i++) {
        eTaskState task_state = eTaskGetState(streaming_task_handle_);
        if (task_state == eSuspended) break;
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      vTaskDelete(streaming_task_handle_);
      streaming_task_handle_ = nullptr;
      ESP_LOGI(TAG, "Streaming task stopped");
    }
  }
}

std::string RTSPServer::generate_sdp_() {
  std::string local_ip = "0.0.0.0";  // éventuellement remplacé côté client

  uint16_t width = camera_->get_image_width();
  uint16_t height = camera_->get_image_height();

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
    ESP_LOGI(TAG, "SDP includes SPS/PPS (SPS=%d, PPS=%d)", (int)sps_size_, (int)pps_size_);
  } else {
    ESP_LOGW(TAG, "SDP WITHOUT SPS/PPS (client will parse from first IDR)");
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
  if (!streaming_active_)
    return ESP_OK;
  return encode_and_stream_frame_();
}

// YUYV → O_UYY_E_VYY (support éventuel d'autres capteurs, rarement utilisé avec OV5647)
esp_err_t RTSPServer::convert_yuyv_to_o_uyy_e_vyy_(const uint8_t *yuyv, uint8_t *o_uyy_e_vyy,
                                                   uint16_t width, uint16_t height) {
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

// LUT init pour RGB565 → YUV
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
  ESP_LOGI(TAG, "YUV LUT initialized");
}

// RGB565 → O_UYY_E_VYY (format HW encoder ESP32-P4)
esp_err_t RTSPServer::convert_rgb565_to_yuv420_(const uint8_t *rgb565, uint8_t *yuv420,
                                                uint16_t width, uint16_t height) {
  if (!yuv_lut_initialized_) {
    init_yuv_lut_();
  }

  const uint16_t *rgb = (const uint16_t *)rgb565;

  for (uint16_t row = 0; row < height; row += 2) {
    const uint16_t *row0 = rgb + (row * width);
    const uint16_t *row1 = rgb + ((row + 1) * width);
    uint8_t *odd_ptr = yuv420 + (row * width * 3 / 2);
    uint8_t *even_ptr = yuv420 + ((row + 1) * width * 3 / 2);

    for (uint16_t col = 0; col < width; col += 2, row0 += 2, row1 += 2, odd_ptr += 3, even_ptr += 3) {
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
  if (!camera_ || !h264_encoder_)
    return ESP_FAIL;

  // Même API que camera_web_server : capture_frame + get_image_data
  if (!camera_->capture_frame()) {
    ESP_LOGW(TAG, "Failed to capture frame from camera");
    return ESP_FAIL;
  }

  uint8_t *frame_data = camera_->get_image_data();
  size_t frame_size = camera_->get_image_size();
  int width = camera_->get_image_width();
  int height = camera_->get_image_height();

  if (!frame_data || frame_size == 0 || width == 0 || height == 0) {
    ESP_LOGW(TAG, "Invalid frame data: ptr=%p size=%u width=%d height=%d",
             frame_data, (unsigned)frame_size, width, height);
    return ESP_FAIL;
  }

  if (frame_count_ == 0) {
    ESP_LOGI(TAG, "First frame: %dx%d RGB565, size=%u bytes", width, height, (unsigned)frame_size);
    uint16_t *rgb = (uint16_t *)frame_data;
    ESP_LOGI(TAG, "First 4 pixels: %04X %04X %04X %04X", rgb[0], rgb[1], rgb[2], rgb[3]);
  }

  // Conversion RGB565 → O_UYY_E_VYY
  convert_rgb565_to_yuv420_(frame_data, yuv_buffer_, width, height);

  if (frame_count_ == 0) {
    ESP_LOGI(TAG, "First YUV420(O_UYY_E_VYY) bytes: "
                  "%02X %02X %02X %02X %02X %02X %02X %02X "
                  "%02X %02X %02X %02X %02X %02X %02X %02X",
             yuv_buffer_[0], yuv_buffer_[1], yuv_buffer_[2], yuv_buffer_[3],
             yuv_buffer_[4], yuv_buffer_[5], yuv_buffer_[6], yuv_buffer_[7],
             yuv_buffer_[8], yuv_buffer_[9], yuv_buffer_[10], yuv_buffer_[11],
             yuv_buffer_[12], yuv_buffer_[13], yuv_buffer_[14], yuv_buffer_[15]);
  }

  esp_h264_enc_in_frame_t in_frame = {};
  in_frame.raw_data.buffer = yuv_buffer_;
  in_frame.raw_data.len = yuv_buffer_size_;
  in_frame.pts = frame_count_ * 90000 / 30;

  esp_h264_enc_out_frame_t out_frame = {};
  out_frame.raw_data.buffer = h264_buffer_;
  out_frame.raw_data.len = h264_buffer_size_;

  esp_h264_err_t ret = esp_h264_enc_process(h264_encoder_, &in_frame, &out_frame);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "H.264 encoding failed: err=%d (in_len=%u out_len=%u)",
             ret, in_frame.raw_data.len, out_frame.raw_data.len);
    return ESP_FAIL;
  }

  const char *frame_type_name = "Unknown";
  if (out_frame.frame_type == ESP_H264_FRAME_TYPE_IDR) frame_type_name = "IDR";
  else if (out_frame.frame_type == ESP_H264_FRAME_TYPE_I) frame_type_name = "I";
  else if (out_frame.frame_type == ESP_H264_FRAME_TYPE_P) frame_type_name = "P";

  ESP_LOGV(TAG, "Frame %u encoded: %u bytes, type=%d (%s)",
           frame_count_, out_frame.length, out_frame.frame_type, frame_type_name);

  if (out_frame.length == 0 || out_frame.raw_data.buffer == nullptr) {
    ESP_LOGE(TAG, "Invalid H.264 output: len=%u buf=%p",
             out_frame.length, out_frame.raw_data.buffer);
    return ESP_FAIL;
  }

  if (out_frame.frame_type == ESP_H264_FRAME_TYPE_IDR) {
    ESP_LOGI(TAG, "IDR frame: caching SPS/PPS");
    parse_and_cache_nal_units_(out_frame.raw_data.buffer, out_frame.length);
  }

  auto nal_units = parse_nal_units_(out_frame.raw_data.buffer, out_frame.length);
  ESP_LOGV(TAG, "Found %d NAL units", (int)nal_units.size());

  for (size_t i = 0; i < nal_units.size(); i++) {
    const auto &nal = nal_units[i];
    uint8_t nal_type = nal.first[0] & 0x1F;
    const char *nal_type_name = "Unknown";
    if (nal_type == 1) nal_type_name = "P-slice";
    else if (nal_type == 5) nal_type_name = "IDR";
    else if (nal_type == 6) nal_type_name = "SEI";
    else if (nal_type == 7) nal_type_name = "SPS";
    else if (nal_type == 8) nal_type_name = "PPS";

    bool marker = (i == nal_units.size() - 1);
    ESP_LOGV(TAG, "Sending NAL %d: type=%d (%s), %u bytes, marker=%d",
             (int)i, nal_type, nal_type_name, (unsigned)nal.second, (int)marker);
    send_h264_rtp_(nal.first, nal.second, marker);
  }

  frame_count_++;
  rtp_timestamp_ += 3000;  // 90kHz / 30fps

  return ESP_OK;
}

void RTSPServer::parse_and_cache_nal_units_(const uint8_t *data, size_t len) {
  auto nal_units = parse_nal_units_(data, len);

  for (const auto &nal : nal_units) {
    uint8_t nal_type = nal.first[0] & 0x1F;

    if (nal_type == 7) {  // SPS
      if (sps_data_)
        free(sps_data_);
      sps_size_ = nal.second;
      sps_data_ = (uint8_t *)malloc(sps_size_);
      memcpy(sps_data_, nal.first, sps_size_);
      ESP_LOGI(TAG, "Cached SPS (%d bytes)", (int)sps_size_);
    } else if (nal_type == 8) {  // PPS
      if (pps_data_)
        free(pps_data_);
      pps_size_ = nal.second;
      pps_data_ = (uint8_t *)malloc(pps_size_);
      memcpy(pps_data_, nal.first, pps_size_);
      ESP_LOGI(TAG, "Cached PPS (%d bytes)", (int)pps_size_);
    }
  }
}

std::vector<std::pair<const uint8_t *, size_t>> RTSPServer::parse_nal_units_(const uint8_t *data, size_t len) {
  nal_units_cache_.clear();

  if (!data || len < 4) {
    ESP_LOGW(TAG, "parse_nal_units_: invalid input");
    return nal_units_cache_;
  }

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
              (data[j + 2] == 0x01 || (data[j + 2] == 0x00 && data[j + 3] == 0x01))) {
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
  if (!data || len == 0) {
    ESP_LOGW(TAG, "send_h264_rtp_: invalid data");
    return ESP_FAIL;
  }

  const size_t MAX_RTP_PAYLOAD = 1400;

  if (len <= MAX_RTP_PAYLOAD) {
    uint8_t *packet = rtp_packet_buffer_;
    RTPHeader *rtp = (RTPHeader *)packet;

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

    for (auto &session : sessions_) {
      if (session.active && session.state == RTSPState::PLAYING) {
        struct sockaddr_in dest_addr = session.client_addr;
        dest_addr.sin_port = htons(session.client_rtp_port);

        sendto(rtp_socket_, packet, sizeof(RTPHeader) + len, 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr));
      }
    }
    return ESP_OK;
  }

  ESP_LOGD(TAG, "Fragmenting NAL unit (%u bytes) with FU-A", (unsigned)len);

  uint8_t nal_header = data[0];
  uint8_t nal_type = nal_header & 0x1F;
  uint8_t nri = nal_header & 0x60;
  uint8_t fu_indicator = nri | 28;  // FU-A

  const uint8_t *payload = data + 1;
  size_t payload_len = len - 1;
  size_t offset = 0;
  size_t fragment_num = 0;

  uint8_t *packet = rtp_packet_buffer_;

  while (offset < payload_len) {
    RTPHeader *rtp = (RTPHeader *)packet;

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
    size_t chunk_size = (remaining > MAX_RTP_PAYLOAD - 2) ? (MAX_RTP_PAYLOAD - 2) : remaining;
    bool is_end = (offset + chunk_size >= payload_len);

    uint8_t fu_header = nal_type;
    if (is_start) fu_header |= 0x80;
    if (is_end) fu_header |= 0x40;

    fu_payload[1] = fu_header;
    memcpy(fu_payload + 2, payload + offset, chunk_size);

    rtp->m = (is_end && marker) ? 1 : 0;

    size_t packet_size = sizeof(RTPHeader) + 2 + chunk_size;

    for (auto &session : sessions_) {
      if (session.active && session.state == RTSPState::PLAYING) {
        struct sockaddr_in dest_addr = session.client_addr;
        dest_addr.sin_port = htons(session.client_rtp_port);

        sendto(rtp_socket_, packet, packet_size, 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr));
      }
    }

    offset += chunk_size;
    fragment_num++;
  }

  ESP_LOGV(TAG, "Sent NAL in %u FU-A fragments", (unsigned)fragment_num);
  return ESP_OK;
}

std::string RTSPServer::generate_session_id_() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%08X", esp_random());
  return std::string(buf);
}

RTSPSession *RTSPServer::find_session_(int socket_fd) {
  for (auto &session : sessions_) {
    if (session.socket_fd == socket_fd && session.active) {
      return &session;
    }
  }
  return nullptr;
}

RTSPSession *RTSPServer::find_session_by_id_(const std::string &session_id) {
  for (auto &session : sessions_) {
    if (session.session_id == session_id && session.active) {
      return &session;
    }
  }
  return nullptr;
}

void RTSPServer::remove_session_(int socket_fd) {
  for (auto &session : sessions_) {
    if (session.socket_fd == socket_fd) {
      close(session.socket_fd);
      session.active = false;
      ESP_LOGI(TAG, "Session %s removed", session.session_id.c_str());
      break;
    }
  }

  sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                 [](const RTSPSession &s) { return !s.active; }),
                  sessions_.end());
}

void RTSPServer::cleanup_inactive_sessions_() {
  uint32_t now = millis();
  const uint32_t timeout = 60000;  // 60s

  for (auto &session : sessions_) {
    if (session.active && (now - session.last_activity > timeout)) {
      ESP_LOGW(TAG, "Session %s timed out", session.session_id.c_str());
      remove_session_(session.socket_fd);
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
  return cseq_str.empty() ? 0 : std::stoi(cseq_str);
}

bool RTSPServer::check_authentication_(const std::string &request) {
  if (username_.empty() && password_.empty()) {
    ESP_LOGD(TAG, "Authentication disabled (no credentials)");
    return true;
  }

  ESP_LOGD(TAG, "Authentication required for user='%s'", username_.c_str());

  std::string auth_header = get_request_line_(request, "Authorization");
  if (auth_header.empty()) {
    ESP_LOGW(TAG, "No Authorization header");
    return false;
  }

  if (auth_header.find("Basic ") != 0) {
    ESP_LOGW(TAG, "Not Basic auth");
    return false;
  }

  std::string encoded = auth_header.substr(6);

  std::string decoded;
  int val = 0;
  int valb = -8;
  for (unsigned char c : encoded) {
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

  size_t colon_pos = decoded.find(':');
  if (colon_pos == std::string::npos) {
    ESP_LOGW(TAG, "Invalid auth format (no colon)");
    return false;
  }

  std::string received_username = decoded.substr(0, colon_pos);
  std::string received_password = decoded.substr(colon_pos + 1);

  bool valid = (received_username == username_ && received_password == password_);
  if (!valid) {
    ESP_LOGW(TAG, "Invalid credentials");
  } else {
    ESP_LOGI(TAG, "Authentication successful for '%s'", username_.c_str());
  }

  return valid;
}

// Tâche de streaming (séparée de loopTask)
void RTSPServer::streaming_task_wrapper_(void *param) {
  RTSPServer *server = static_cast<RTSPServer *>(param);

  ESP_LOGI(TAG, "Streaming task started");

  uint32_t frame_num = 0;
  uint32_t total_encode_time = 0;
  uint32_t start_time = millis();

  while (server->streaming_active_) {
    uint32_t encode_start = millis();

    server->encode_and_stream_frame_();

    uint32_t encode_time = millis() - encode_start;
    total_encode_time += encode_time;
    frame_num++;

    if (frame_num % 30 == 0) {
      uint32_t elapsed = millis() - start_time;
      float actual_fps = (frame_num * 1000.0f) / (float)elapsed;
      float avg_encode = total_encode_time / (float)frame_num;
      ESP_LOGI(TAG, "RTSP: %.1f FPS (avg encode=%.1f ms, last=%u ms)",
               actual_fps, avg_encode, (unsigned)encode_time);
    }

    if (encode_time < 33) {
      vTaskDelay(pdMS_TO_TICKS(33 - encode_time));
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


