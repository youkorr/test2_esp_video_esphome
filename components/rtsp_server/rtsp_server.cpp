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

  // Note: H.264 encoder initialization is deferred until first client connects
  // This is because camera dimensions are not available until streaming starts

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

  // Log authentication status
  if (!username_.empty() && !password_.empty()) {
    ESP_LOGI(TAG, "Authentication: ENABLED (user='%s')", username_.c_str());
    ESP_LOGI(TAG, "Connect with: rtsp://%s:***@<IP>:%d%s", username_.c_str(), rtsp_port_, stream_path_.c_str());
  } else {
    ESP_LOGI(TAG, "Authentication: DISABLED");
  }

  ESP_LOGI(TAG, "Note: H.264 encoder will initialize when first client connects");
}

void RTSPServer::loop() {
  // Handle incoming RTSP connections (lightweight - safe for loopTask's small stack)
  handle_rtsp_connections_();

  // NOTE: Video streaming now happens in separate task with larger stack
  // to avoid stack overflow in loopTask (which only has 1536 bytes)

  // Cleanup inactive sessions
  cleanup_inactive_sessions_();
}

void RTSPServer::dump_config() {
  ESP_LOGCONFIG(TAG, "RTSP Server:");
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

  // Check if camera is streaming and has valid dimensions
  if (!camera_->is_streaming()) {
    ESP_LOGW(TAG, "Camera not streaming yet, starting stream...");
    if (!camera_->start_streaming()) {
      ESP_LOGE(TAG, "Failed to start camera streaming");
      return ESP_FAIL;
    }
    // Give camera time to initialize
    delay(100);
  }

  uint16_t width = camera_->get_image_width();
  uint16_t height = camera_->get_image_height();

  // Verify dimensions are valid
  if (width == 0 || height == 0) {
    ESP_LOGE(TAG, "Invalid camera dimensions: %dx%d", width, height);
    return ESP_FAIL;
  }

  // Align to 16
  width = ((width + 15) >> 4) << 4;
  height = ((height + 15) >> 4) << 4;

  ESP_LOGI(TAG, "Resolution: %dx%d (aligned from %dx%d)", width, height,
           camera_->get_image_width(), camera_->get_image_height());

  // Allocate buffers with 64-byte alignment (required by hardware encoder DMA)
  yuv_buffer_size_ = width * height * 3 / 2;
  yuv_buffer_ = (uint8_t *)heap_caps_aligned_alloc(64, yuv_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!yuv_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate YUV buffer (64-byte aligned)");
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "YUV buffer allocated: %zu bytes @ %p (64-byte aligned)", yuv_buffer_size_, yuv_buffer_);

  h264_buffer_size_ = yuv_buffer_size_ * 2;
  h264_buffer_ = (uint8_t *)heap_caps_aligned_alloc(64, h264_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!h264_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate H.264 buffer (64-byte aligned)");
    free(yuv_buffer_);
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "H.264 buffer allocated: %zu bytes @ %p (64-byte aligned)", h264_buffer_size_, h264_buffer_);

  // Allocate reusable RTP packet buffer (2KB) to reduce stack usage
  rtp_packet_buffer_ = (uint8_t *)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!rtp_packet_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate RTP packet buffer");
    free(yuv_buffer_);
    free(h264_buffer_);
    return ESP_ERR_NO_MEM;
  }

  // Preallocate NAL units cache to reduce stack allocations
  nal_units_cache_.reserve(20);  // Typical IDR frame has 10-15 NAL units

  // Configure encoder (HARDWARE encoder - ESP32-P4 H.264 accelerator)
  esp_h264_enc_cfg_hw_t cfg = {
      .pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY,  // YUV420 packed (required by hardware encoder)
      .gop = gop_,
      .fps = 30,
      .res = {.width = width, .height = height},
      .rc = {.bitrate = bitrate_, .qp_min = qp_min_, .qp_max = qp_max_}};

  ESP_LOGI(TAG, "Encoder config: %dx%d @ 30fps, GOP=%d, bitrate=%d, QP=%d-%d",
           width, height, gop_, bitrate_, qp_min_, qp_max_);

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

  ESP_LOGI(TAG, "H.264 HARDWARE encoder initialized successfully!");
  ESP_LOGI(TAG, "Note: Using ESP32-P4 hardware H.264 accelerator");
  ESP_LOGI(TAG, "  Expected: 800x640 @ ~25-30 FPS (hardware acceleration)");
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

  // RTP socket
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
    return ESP_FAIL;
  }

  // RTCP socket
  rtcp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (rtcp_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create RTCP socket");
    close(rtp_socket_);
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

  // Set socket options
  int reuse = 1;
  setsockopt(rtsp_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(rtsp_port_);

  if (bind(rtsp_socket_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind RTSP socket");
    close(rtsp_socket_);
    return ESP_FAIL;
  }

  if (listen(rtsp_socket_, 5) < 0) {
    ESP_LOGE(TAG, "Failed to listen on RTSP socket");
    close(rtsp_socket_);
    return ESP_FAIL;
  }

  // Set non-blocking
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
  // Accept new connections
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);

  int client_fd = accept(rtsp_socket_, (struct sockaddr *)&client_addr, &addr_len);
  if (client_fd >= 0) {
    if (sessions_.size() < max_clients_) {
      ESP_LOGI(TAG, "New RTSP client connected from %s", inet_ntoa(client_addr.sin_addr));

      RTSPSession session = {};
      session.socket_fd = client_fd;
      session.state = RTSPState::INIT;
      session.client_addr = client_addr;
      session.last_activity = millis();
      session.active = true;

      // Set non-blocking
      int flags = fcntl(client_fd, F_GETFL, 0);
      fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

      sessions_.push_back(session);
    } else {
      ESP_LOGW(TAG, "Max clients reached, rejecting connection");
      close(client_fd);
    }
  }

  // Handle existing sessions
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

    // Vérifier l'authentification (sauf pour OPTIONS qui ne nécessite pas d'auth)
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
    // Connection closed
    ESP_LOGI(TAG, "Client disconnected");
    remove_session_(session.socket_fd);
  }
}

RTSPMethod RTSPServer::parse_rtsp_method_(const std::string &request) {
  if (request.find("OPTIONS") == 0)
    return RTSPMethod::OPTIONS;
  if (request.find("DESCRIBE") == 0)
    return RTSPMethod::DESCRIBE;
  if (request.find("SETUP") == 0)
    return RTSPMethod::SETUP;
  if (request.find("PLAY") == 0)
    return RTSPMethod::PLAY;
  if (request.find("PAUSE") == 0)
    return RTSPMethod::PAUSE;
  if (request.find("TEARDOWN") == 0)
    return RTSPMethod::TEARDOWN;
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

  // Initialize H.264 encoder if not already done (needed to get SPS/PPS for SDP)
  if (!h264_encoder_) {
    ESP_LOGI(TAG, "Initializing H.264 encoder for DESCRIBE...");
    if (init_h264_encoder_() != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize H.264 encoder");
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }

    // Try to extract SPS/PPS by encoding first available frame
    // Don't wait if camera is not ready yet - SDP will work without SPS/PPS
    // (client can get them from first IDR frame via RTP)
    if (sps_data_ == nullptr || pps_data_ == nullptr) {
      ESP_LOGI(TAG, "Attempting to extract SPS/PPS for SDP...");
      encode_and_stream_frame_();  // Best effort - will cache SPS/PPS if frame available
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

  // Parse Transport header
  std::string transport_line = get_request_line_(request, "Transport");
  ESP_LOGD(TAG, "Transport header: '%s'", transport_line.c_str());

  // Check if client requests TCP interleaved (not supported yet)
  if (transport_line.find("interleaved") != std::string::npos ||
      transport_line.find("RTP/AVP/TCP") != std::string::npos) {
    ESP_LOGW(TAG, "Client requested TCP interleaved transport (not supported)");
    ESP_LOGW(TAG, "Please configure client to use UDP transport");
    std::map<std::string, std::string> headers;
    headers["CSeq"] = std::to_string(cseq);
    send_rtsp_response_(session.socket_fd, 461, "Unsupported Transport", headers);
    return;
  }

  // Extract client ports (UDP mode)
  size_t client_port_pos = transport_line.find("client_port=");
  if (client_port_pos != std::string::npos) {
    int rtp_port, rtcp_port;
    sscanf(transport_line.c_str() + client_port_pos, "client_port=%d-%d", &rtp_port, &rtcp_port);
    session.client_rtp_port = rtp_port;
    session.client_rtcp_port = rtcp_port;
  } else {
    ESP_LOGW(TAG, "No client_port found in Transport header");
    std::map<std::string, std::string> headers;
    headers["CSeq"] = std::to_string(cseq);
    send_rtsp_response_(session.socket_fd, 461, "Unsupported Transport", headers);
    return;
  }

  // Generate session ID
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

  // Encoder should already be initialized during DESCRIBE
  // But check just in case client skipped DESCRIBE
  if (!h264_encoder_) {
    ESP_LOGW(TAG, "H.264 encoder not initialized (client skipped DESCRIBE?)");
    if (init_h264_encoder_() != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize H.264 encoder");
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }
  }

  session.state = RTSPState::PLAYING;
  streaming_active_ = true;

  // Create streaming task if not already running
  // Note: On ESP32-P4, stack_size appears to be in BYTES not WORDS
  // With preallocated buffers, we need 16KB stack
  if (streaming_task_handle_ == nullptr) {
    BaseType_t result = xTaskCreatePinnedToCore(
        streaming_task_wrapper_,
        "rtsp_stream",        // Task name
        16384,                // Stack size in BYTES (16KB)
        this,                 // Parameter passed to task
        5,                    // Priority (same as loopTask)
        &streaming_task_handle_,
        1                     // Pin to core 1 (loopTask runs on core 1)
    );

    if (result != pdPASS || streaming_task_handle_ == nullptr) {
      ESP_LOGE(TAG, "Failed to create streaming task (result=%d)", result);
      streaming_active_ = false;
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }
    ESP_LOGI(TAG, "Streaming task created with 32KB stack on core 1");
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

  // Check if any sessions are still playing
  bool any_playing = false;
  for (const auto &s : sessions_) {
    if (s.active && s.state == RTSPState::PLAYING) {
      any_playing = true;
      break;
    }
  }

  // Stop streaming if no more clients
  if (!any_playing && streaming_active_) {
    ESP_LOGI(TAG, "Stopping streaming (no active clients)...");
    streaming_active_ = false;

    // Wait for streaming task to finish gracefully
    // The task checks streaming_active_ in its loop and will suspend itself
    if (streaming_task_handle_ != nullptr) {
      ESP_LOGD(TAG, "Waiting for streaming task to terminate...");

      // Wait up to 500ms for task to suspend itself
      for (int i = 0; i < 50; i++) {
        eTaskState task_state = eTaskGetState(streaming_task_handle_);
        if (task_state == eSuspended) {
          ESP_LOGD(TAG, "Streaming task suspended, safe to delete");
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // Wait 10ms
      }

      // Delete the task
      vTaskDelete(streaming_task_handle_);
      streaming_task_handle_ = nullptr;
      ESP_LOGI(TAG, "Streaming task stopped successfully");
    }
  }
}

std::string RTSPServer::generate_sdp_() {
  // Get local IP
  std::string local_ip = "0.0.0.0";  // Will be replaced with actual IP

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

  // Add SPS/PPS if available
  if (sps_data_ && sps_size_ > 0 && pps_data_ && pps_size_ > 0) {
    std::string sps_b64 = base64_encode_(sps_data_, sps_size_);
    std::string pps_b64 = base64_encode_(pps_data_, pps_size_);
    sdp << ";sprop-parameter-sets=" << sps_b64 << "," << pps_b64;
    ESP_LOGI(TAG, "SDP includes SPS/PPS (SPS: %d bytes, PPS: %d bytes)", sps_size_, pps_size_);
  } else {
    ESP_LOGW(TAG, "SDP generated WITHOUT SPS/PPS - client must extract from RTP stream");
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

// Convert YUYV (YUV422) to O_UYY_E_VYY format (YUV420 for ESP32-P4 hardware encoder)
// YUYV input: Y0 U0 Y1 V0 Y2 U1 Y3 V1... (4 bytes for 2 pixels)
// O_UYY_E_VYY output: odd lines = U Y Y U Y Y..., even lines = V Y Y V Y Y...
// This is MUCH faster than RGB565 conversion (no color space conversion, just rearrangement)
esp_err_t RTSPServer::convert_yuyv_to_o_uyy_e_vyy_(const uint8_t *yuyv, uint8_t *o_uyy_e_vyy, uint16_t width,
                                                    uint16_t height) {
  const uint8_t *src = yuyv;

  // Process 2 lines at a time for YUV420 (vertical subsampling)
  for (uint16_t row = 0; row < height; row += 2) {
    uint8_t *odd_line = o_uyy_e_vyy + (row * width * 3 / 2);      // Odd line: U Y Y U Y Y...
    uint8_t *even_line = o_uyy_e_vyy + ((row + 1) * width * 3 / 2); // Even line: V Y Y V Y Y...

    const uint8_t *src_row0 = src + (row * width * 2);       // YUYV row 0
    const uint8_t *src_row1 = src + ((row + 1) * width * 2); // YUYV row 1

    // Process 2 pixels at a time (YUYV contains Y0 U0 Y1 V0 for each 2 pixels)
    for (uint16_t col = 0; col < width; col += 2) {
      // Read from row 0: Y0 U0 Y1 V0
      uint8_t y0_r0 = src_row0[col * 2 + 0];
      uint8_t u0_r0 = src_row0[col * 2 + 1];
      uint8_t y1_r0 = src_row0[col * 2 + 2];
      uint8_t v0_r0 = src_row0[col * 2 + 3];

      // Read from row 1: Y0 U0 Y1 V0
      uint8_t y0_r1 = src_row1[col * 2 + 0];
      uint8_t u0_r1 = src_row1[col * 2 + 1];
      uint8_t y1_r1 = src_row1[col * 2 + 2];
      uint8_t v0_r1 = src_row1[col * 2 + 3];

      // Average U and V vertically for YUV420 subsampling
      uint8_t u_avg = (u0_r0 + u0_r1) >> 1;
      uint8_t v_avg = (v0_r0 + v0_r1) >> 1;

      // Write to line at row index (0,2,4...): U Y0 Y1
      // According to O_UYY_E_VYY format: line 1,3,5... = U Y Y
      size_t odd_idx = (col / 2) * 3;
      odd_line[odd_idx + 0] = u_avg;
      odd_line[odd_idx + 1] = y0_r0;
      odd_line[odd_idx + 2] = y1_r0;

      // Write to line at row index (1,3,5...): V Y0 Y1
      // According to O_UYY_E_VYY format: line 2,4,6... = V Y Y
      size_t even_idx = (col / 2) * 3;
      even_line[even_idx + 0] = v_avg;
      even_line[even_idx + 1] = y0_r1;
      even_line[even_idx + 2] = y1_r1;
    }
  }

  return ESP_OK;
}

// Initialize YUV lookup tables for fast RGB565 to YUV conversion
void RTSPServer::init_yuv_lut_() {
  if (yuv_lut_initialized_)
    return;

  // RGB565 uses 5 bits for R, 6 bits for G, 5 bits for B
  // Expand to 8-bit values: RGB565 value -> 8-bit RGB using proper bit expansion
  // Formula: value_8bit = (value_5or6 << 3) | (value_5or6 >> 2) for 5-bit
  //          value_8bit = (value_6 << 2) | (value_6 >> 4) for 6-bit

  // BT.601 coefficients for RGB to YUV conversion:
  // Y  =  0.257*R + 0.504*G + 0.098*B + 16
  // U  = -0.148*R - 0.291*G + 0.439*B + 128
  // V  =  0.439*R - 0.368*G - 0.071*B + 128
  //
  // Scaled by 256 for integer math:
  // Y  = ( 66*R + 129*G +  25*B) >> 8 + 16
  // U  = (-38*R -  74*G + 112*B) >> 8 + 128
  // V  = (112*R -  94*G -  18*B) >> 8 + 128

  // Precompute Y contributions for each RGB565 component
  for (int i = 0; i < 32; i++) {
    int val_8bit = (i << 3) | (i >> 2);  // 5-bit to 8-bit expansion
    y_r_lut_[i] = (66 * val_8bit) >> 8;
    y_b_lut_[i] = (25 * val_8bit) >> 8;
    u_r_lut_[i] = (-38 * val_8bit) >> 8;
    u_b_lut_[i] = (112 * val_8bit) >> 8;
    v_r_lut_[i] = (112 * val_8bit) >> 8;
    v_b_lut_[i] = (-18 * val_8bit) >> 8;
  }

  for (int i = 0; i < 64; i++) {
    int val_8bit = (i << 2) | (i >> 4);  // 6-bit to 8-bit expansion
    y_g_lut_[i] = (129 * val_8bit) >> 8;
    u_g_lut_[i] = (-74 * val_8bit) >> 8;
    v_g_lut_[i] = (-94 * val_8bit) >> 8;
  }

  yuv_lut_initialized_ = true;
  ESP_LOGI(TAG, "YUV lookup tables initialized");
}

// Convert RGB565 to O_UYY_E_VYY format (required by ESP32-P4 hardware encoder)
// Format: odd lines = u y y u y y..., even lines = v y y v y y...
// Optimized version using lookup tables
esp_err_t RTSPServer::convert_rgb565_to_yuv420_(const uint8_t *rgb565, uint8_t *yuv420, uint16_t width,
                                                 uint16_t height) {
  // Initialize lookup tables on first call
  if (!yuv_lut_initialized_) {
    init_yuv_lut_();
  }
  const uint16_t *rgb = (const uint16_t *)rgb565;

  // Process 2 lines at a time (odd line gets U, even line gets V)
  for (uint16_t row = 0; row < height; row += 2) {
    const uint16_t *row0 = rgb + (row * width);
    const uint16_t *row1 = rgb + ((row + 1) * width);
    uint8_t *odd_line = yuv420 + (row * width * 3 / 2);      // Odd line: u y y u y y...
    uint8_t *even_line = yuv420 + ((row + 1) * width * 3 / 2); // Even line: v y y v y y...

    size_t out_idx = 0;

    // Process 2 pixels at a time (UV subsampling) - optimized with lookup tables
    for (uint16_t col = 0; col < width; col += 2) {
      // Get 4 pixels (2x2 block) and extract RGB565 components
      uint16_t p00 = row0[col];
      uint16_t p01 = row0[col + 1];
      uint16_t p10 = row1[col];
      uint16_t p11 = row1[col + 1];

      // Extract RGB565 components (5-bit R, 6-bit G, 5-bit B)
      uint8_t r0 = (p00 >> 11) & 0x1F;
      uint8_t g0 = (p00 >> 5) & 0x3F;
      uint8_t b0 = p00 & 0x1F;

      uint8_t r1 = (p01 >> 11) & 0x1F;
      uint8_t g1 = (p01 >> 5) & 0x3F;
      uint8_t b1 = p01 & 0x1F;

      uint8_t r2 = (p10 >> 11) & 0x1F;
      uint8_t g2 = (p10 >> 5) & 0x3F;
      uint8_t b2 = p10 & 0x1F;

      uint8_t r3 = (p11 >> 11) & 0x1F;
      uint8_t g3 = (p11 >> 5) & 0x3F;
      uint8_t b3 = p11 & 0x1F;

      // Calculate Y for all 4 pixels using lookup tables (no multiplications!)
      int y0 = y_r_lut_[r0] + y_g_lut_[g0] + y_b_lut_[b0] + 16;
      int y1 = y_r_lut_[r1] + y_g_lut_[g1] + y_b_lut_[b1] + 16;
      int y2 = y_r_lut_[r2] + y_g_lut_[g2] + y_b_lut_[b2] + 16;
      int y3 = y_r_lut_[r3] + y_g_lut_[g3] + y_b_lut_[b3] + 16;

      // Average RGB565 components for U/V (using shifts for speed)
      int r_avg = (r0 + r1 + r2 + r3) >> 2;
      int g_avg = (g0 + g1 + g2 + g3) >> 2;
      int b_avg = (b0 + b1 + b2 + b3) >> 2;

      // Calculate U and V using lookup tables
      int u = u_r_lut_[r_avg] + u_g_lut_[g_avg] + u_b_lut_[b_avg] + 128;
      int v = v_r_lut_[r_avg] + v_g_lut_[g_avg] + v_b_lut_[b_avg] + 128;

      // Clamp to valid range [0, 255]
      y0 = (y0 < 0) ? 0 : (y0 > 255) ? 255 : y0;
      y1 = (y1 < 0) ? 0 : (y1 > 255) ? 255 : y1;
      y2 = (y2 < 0) ? 0 : (y2 > 255) ? 255 : y2;
      y3 = (y3 < 0) ? 0 : (y3 > 255) ? 255 : y3;
      u = (u < 0) ? 0 : (u > 255) ? 255 : u;
      v = (v < 0) ? 0 : (v > 255) ? 255 : v;

      // Write to odd line: u y0 y1
      odd_line[out_idx + 0] = u;
      odd_line[out_idx + 1] = y0;
      odd_line[out_idx + 2] = y1;

      // Write to even line: v y2 y3
      even_line[out_idx + 0] = v;
      even_line[out_idx + 1] = y2;
      even_line[out_idx + 2] = y3;

      out_idx += 3;
    }
  }

  return ESP_OK;
}

esp_err_t RTSPServer::encode_and_stream_frame_() {
  if (!camera_ || !h264_encoder_)
    return ESP_FAIL;

  // Use get_current_rgb_frame() which locks the buffer until release_buffer()
  // This prevents V4L2 from overwriting the buffer while we're encoding
  // Note: Despite the name, this returns the format configured in pixel_format (YUYV in our case)
  mipi_dsi_cam::SimpleBufferElement* buffer = nullptr;
  uint8_t* frame_data = nullptr;
  int width, height;

  if (!camera_->get_current_rgb_frame(&buffer, &frame_data, &width, &height)) {
    ESP_LOGW(TAG, "Failed to get frame from camera");
    return ESP_FAIL;
  }

  if (frame_data == nullptr || buffer == nullptr) {
    ESP_LOGW(TAG, "Invalid frame data: ptr=%p buffer=%p", frame_data, buffer);
    return ESP_FAIL;
  }

  // Debug: Log first frame info
  if (frame_count_ == 0) {
    ESP_LOGI(TAG, "First RGB565 frame: %dx%d, expected size: %d bytes", width, height, width * height * 2);
    uint16_t *rgb = (uint16_t *)frame_data;
    ESP_LOGI(TAG, "First 4 RGB565 pixels: %04X %04X %04X %04X", rgb[0], rgb[1], rgb[2], rgb[3]);
  }

  // Convert RGB565 to O_UYY_E_VYY (YUV420) for hardware encoder
  // Note: YUYV from OV5647 has incorrect U/V values (all 0x10), so using RGB565 instead
  convert_rgb565_to_yuv420_(frame_data, yuv_buffer_, width, height);

  // Debug: Log converted YUV buffer
  if (frame_count_ == 0) {
    ESP_LOGI(TAG, "Converted O_UYY_E_VYY buffer size: %zu bytes", yuv_buffer_size_);
    ESP_LOGI(TAG, "First 16 bytes of O_UYY_E_VYY: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             yuv_buffer_[0], yuv_buffer_[1], yuv_buffer_[2], yuv_buffer_[3],
             yuv_buffer_[4], yuv_buffer_[5], yuv_buffer_[6], yuv_buffer_[7],
             yuv_buffer_[8], yuv_buffer_[9], yuv_buffer_[10], yuv_buffer_[11],
             yuv_buffer_[12], yuv_buffer_[13], yuv_buffer_[14], yuv_buffer_[15]);
  }

  // Release the camera buffer ASAP (we've copied to yuv_buffer_)
  camera_->release_buffer(buffer);

  // Encode from our YUV buffer
  esp_h264_enc_in_frame_t in_frame = {};
  in_frame.raw_data.buffer = yuv_buffer_;
  in_frame.raw_data.len = yuv_buffer_size_;
  in_frame.pts = frame_count_ * 90000 / 30;

  esp_h264_enc_out_frame_t out_frame = {};
  out_frame.raw_data.buffer = h264_buffer_;
  out_frame.raw_data.len = h264_buffer_size_;

  esp_h264_err_t ret = esp_h264_enc_process(h264_encoder_, &in_frame, &out_frame);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "H.264 encoding failed: error code %d (frame %u, in_len=%u, out_len=%u)",
             ret, frame_count_, in_frame.raw_data.len, out_frame.raw_data.len);
    if (frame_count_ == 0) {
      ESP_LOGE(TAG, "First frame encoding failed - check YUV format conversion!");
    }
    return ESP_FAIL;
  }

  const char* frame_type_name = "Unknown";
  if (out_frame.frame_type == ESP_H264_FRAME_TYPE_IDR) frame_type_name = "IDR";
  else if (out_frame.frame_type == ESP_H264_FRAME_TYPE_I) frame_type_name = "I";
  else if (out_frame.frame_type == ESP_H264_FRAME_TYPE_P) frame_type_name = "P";
  // Note: Hardware encoder only supports IDR, I, and P frames (no B-frames)

  ESP_LOGD(TAG, "Frame %u encoded: %u bytes, type=%d (%s)",
           frame_count_, out_frame.length, out_frame.frame_type, frame_type_name);

  // Validate output
  if (out_frame.length == 0 || out_frame.raw_data.buffer == nullptr) {
    ESP_LOGE(TAG, "Invalid H.264 output: len=%u buf=%p", out_frame.length, out_frame.raw_data.buffer);
    return ESP_FAIL;
  }

  // Cache SPS/PPS from IDR frames
  if (out_frame.frame_type == ESP_H264_FRAME_TYPE_IDR) {
    ESP_LOGD(TAG, "IDR frame - caching SPS/PPS");
    parse_and_cache_nal_units_(out_frame.raw_data.buffer, out_frame.length);
  }

  // Send NAL units
  ESP_LOGD(TAG, "Parsing NAL units from %u bytes", out_frame.length);
  auto nal_units = parse_nal_units_(out_frame.raw_data.buffer, out_frame.length);
  ESP_LOGD(TAG, "Found %d NAL units", nal_units.size());

  for (size_t i = 0; i < nal_units.size(); i++) {
    const auto &nal = nal_units[i];
    uint8_t nal_type = nal.first[0] & 0x1F;
    const char* nal_type_name = "Unknown";
    if (nal_type == 1) nal_type_name = "P-slice";
    else if (nal_type == 5) nal_type_name = "IDR";
    else if (nal_type == 6) nal_type_name = "SEI";
    else if (nal_type == 7) nal_type_name = "SPS";
    else if (nal_type == 8) nal_type_name = "PPS";

    ESP_LOGD(TAG, "Sending NAL unit %d: type=%d (%s), %u bytes", i, nal_type, nal_type_name, nal.second);
    send_h264_rtp_(nal.first, nal.second, true);
  }

  frame_count_++;
  rtp_timestamp_ += 3000;  // 90kHz / 30fps

  ESP_LOGD(TAG, "Frame %u sent successfully", frame_count_);
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
      ESP_LOGI(TAG, "Cached SPS (%d bytes)", sps_size_);
    } else if (nal_type == 8) {  // PPS
      if (pps_data_)
        free(pps_data_);
      pps_size_ = nal.second;
      pps_data_ = (uint8_t *)malloc(pps_size_);
      memcpy(pps_data_, nal.first, pps_size_);
      ESP_LOGI(TAG, "Cached PPS (%d bytes)", pps_size_);
    }
  }
}

std::vector<std::pair<const uint8_t *, size_t>> RTSPServer::parse_nal_units_(const uint8_t *data, size_t len) {
  // Reuse cached vector to reduce stack allocations
  nal_units_cache_.clear();

  if (data == nullptr || len < 4) {
    ESP_LOGW(TAG, "parse_nal_units_: invalid input (ptr=%p, len=%u)", data, len);
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
  // Validate input
  if (data == nullptr || len == 0) {
    ESP_LOGW(TAG, "send_h264_rtp_: invalid data (ptr=%p, len=%u)", data, len);
    return ESP_FAIL;
  }

  const size_t MAX_RTP_PAYLOAD = 1400;  // Safe MTU size

  // Small NAL units: send directly (Single NAL Unit Mode)
  if (len <= MAX_RTP_PAYLOAD) {
    // Use preallocated buffer to reduce heap fragmentation
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

    // Send to all playing clients
    for (auto &session : sessions_) {
      if (session.active && session.state == RTSPState::PLAYING) {
        struct sockaddr_in dest_addr = session.client_addr;
        dest_addr.sin_port = htons(session.client_rtp_port);

        sendto(rtp_socket_, packet, sizeof(RTPHeader) + len, 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr));
      }
    }

    // No need to free - using preallocated buffer
    return ESP_OK;
  }

  // Large NAL units: Fragment using FU-A (RFC 6184)
  ESP_LOGD(TAG, "Fragmenting NAL unit (%u bytes) with FU-A", len);

  uint8_t nal_header = data[0];
  uint8_t nal_type = nal_header & 0x1F;
  uint8_t nri = nal_header & 0x60;

  // FU indicator: F=0, NRI=from original, Type=28 (FU-A)
  uint8_t fu_indicator = nri | 28;

  const uint8_t *payload = data + 1;  // Skip original NAL header
  size_t payload_len = len - 1;
  size_t offset = 0;
  size_t fragment_num = 0;

  // Use preallocated buffer to reduce heap fragmentation
  uint8_t *packet = rtp_packet_buffer_;

  while (offset < payload_len) {
    RTPHeader *rtp = (RTPHeader *)packet;

    // RTP header
    rtp->v = 2;
    rtp->p = 0;
    rtp->x = 0;
    rtp->cc = 0;
    rtp->pt = 96;
    rtp->seq = htons(rtp_seq_num_++);
    rtp->timestamp = htonl(rtp_timestamp_);
    rtp->ssrc = htonl(rtp_ssrc_);

    // FU-A header
    uint8_t *fu_payload = packet + sizeof(RTPHeader);
    fu_payload[0] = fu_indicator;

    // FU header: S, E, R, Type
    bool is_start = (offset == 0);
    size_t remaining = payload_len - offset;
    size_t chunk_size = (remaining > MAX_RTP_PAYLOAD - 2) ? (MAX_RTP_PAYLOAD - 2) : remaining;
    bool is_end = (offset + chunk_size >= payload_len);

    uint8_t fu_header = nal_type;
    if (is_start) fu_header |= 0x80;  // S bit
    if (is_end) fu_header |= 0x40;    // E bit

    fu_payload[1] = fu_header;

    // Copy fragment data
    memcpy(fu_payload + 2, payload + offset, chunk_size);

    // Set marker bit on last fragment
    rtp->m = (is_end && marker) ? 1 : 0;

    size_t packet_size = sizeof(RTPHeader) + 2 + chunk_size;

    // Send to all playing clients
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

  // No need to free - using preallocated buffer

  ESP_LOGD(TAG, "Sent NAL unit in %u fragments", fragment_num);

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

  // Remove inactive sessions
  sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                  [](const RTSPSession &s) { return !s.active; }),
                  sessions_.end());
}

void RTSPServer::cleanup_inactive_sessions_() {
  uint32_t now = millis();
  const uint32_t timeout = 60000;  // 60 seconds

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

  // Trim whitespace
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
  // Si pas d'authentification configurée, autoriser toutes les requêtes
  if (username_.empty() && password_.empty()) {
    ESP_LOGD(TAG, "Authentication: disabled (no credentials configured)");
    return true;
  }

  ESP_LOGD(TAG, "Authentication: required for user='%s'", username_.c_str());

  // Extraire le header Authorization
  std::string auth_header = get_request_line_(request, "Authorization");
  if (auth_header.empty()) {
    ESP_LOGW(TAG, "Authentication failed: no Authorization header");
    return false;
  }

  ESP_LOGD(TAG, "Authorization header: '%s'", auth_header.c_str());

  // Vérifier que c'est Basic auth
  if (auth_header.find("Basic ") != 0) {
    ESP_LOGW(TAG, "Authentication failed: not Basic auth");
    return false;
  }

  // Extraire les credentials encodés en base64
  std::string encoded = auth_header.substr(6);  // Skip "Basic "

  // Décoder base64
  std::string decoded;
  int val = 0;
  int valb = -8;
  for (unsigned char c : encoded) {
    if (c == '=') break;

    // Trouver l'index dans la table base64
    const char *p = strchr(base64_chars, c);
    if (p == nullptr) continue;

    val = (val << 6) + (p - base64_chars);
    valb += 6;

    if (valb >= 0) {
      decoded.push_back(char((val >> valb) & 0xFF));
      valb -= 8;
    }
  }

  ESP_LOGD(TAG, "Decoded credentials: '%s'", decoded.c_str());

  // Le format décodé doit être "username:password"
  size_t colon_pos = decoded.find(':');
  if (colon_pos == std::string::npos) {
    ESP_LOGW(TAG, "Authentication failed: invalid format (no colon)");
    return false;
  }

  std::string received_username = decoded.substr(0, colon_pos);
  std::string received_password = decoded.substr(colon_pos + 1);

  ESP_LOGD(TAG, "Received user='%s', expected user='%s'", received_username.c_str(), username_.c_str());

  // Comparer avec les credentials configurés
  bool valid = (received_username == username_ && received_password == password_);
  if (!valid) {
    ESP_LOGW(TAG, "Authentication failed: invalid credentials");
  } else {
    ESP_LOGI(TAG, "Authentication successful for user '%s'", username_.c_str());
  }

  return valid;
}

// Streaming task function (runs in separate task with 8KB stack)
void RTSPServer::streaming_task_wrapper_(void *param) {
  RTSPServer *server = static_cast<RTSPServer *>(param);

  ESP_LOGI(TAG, "Streaming task started");

  uint32_t frame_num = 0;
  uint32_t total_encode_time = 0;
  uint32_t start_time = millis();

  while (server->streaming_active_) {
    // Measure encoding time
    uint32_t encode_start = millis();

    // Encode and stream one frame
    server->encode_and_stream_frame_();

    uint32_t encode_time = millis() - encode_start;
    total_encode_time += encode_time;
    frame_num++;

    // Log performance every 30 frames (every ~1 second at 30 FPS)
    if (frame_num % 30 == 0) {
      uint32_t elapsed = millis() - start_time;
      float actual_fps = (frame_num * 1000.0f) / elapsed;
      float avg_encode = total_encode_time / (float)frame_num;
      ESP_LOGI(TAG, "Performance: %.1f FPS (avg encode: %.1f ms/frame, last: %u ms)",
               actual_fps, avg_encode, encode_time);
    }

    // Target 30 FPS = 33.3ms per frame
    // Only delay if encoding was fast enough
    if (encode_time < 33) {
      vTaskDelay(pdMS_TO_TICKS(33 - encode_time));
    } else {
      // Encoding took longer than target - skip delay but yield to other tasks
      vTaskDelay(1);
    }
  }

  ESP_LOGI(TAG, "Streaming task ended");

  // Task will be deleted by vTaskDelete() in handle_teardown_()
  vTaskSuspend(nullptr);  // Suspend until deleted
}

}  // namespace rtsp_server
}  // namespace esphome

#endif  // USE_ESP_IDF
