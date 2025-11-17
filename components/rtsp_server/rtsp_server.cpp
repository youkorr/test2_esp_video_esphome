#include "rtsp_server.h"

#ifdef USE_ESP_IDF
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstring>
#include <sstream>
#include <esp_random.h>
#include <arpa/inet.h>

namespace esphome {
namespace rtsp_server {

static const char *const TAG = "rtsp_server";

// Base64 encoding table
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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
  // Handle incoming RTSP connections
  handle_rtsp_connections_();

  // Stream video if any client is playing
  if (streaming_active_) {
    stream_video_();
  }

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
  ESP_LOGI(TAG, "Initializing H.264 software encoder (OpenH264)...");

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

  // Allocate buffers (standard allocation for software encoder)
  yuv_buffer_size_ = width * height * 3 / 2;
  yuv_buffer_ = (uint8_t *)heap_caps_malloc(yuv_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!yuv_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate YUV buffer");
    return ESP_ERR_NO_MEM;
  }

  h264_buffer_size_ = yuv_buffer_size_ * 2;
  h264_buffer_ = (uint8_t *)heap_caps_malloc(h264_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!h264_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate H.264 buffer");
    free(yuv_buffer_);
    return ESP_ERR_NO_MEM;
  }

  // Configure encoder (software encoder - OpenH264)
  esp_h264_enc_cfg_sw_t cfg = {
      .pic_type = ESP_H264_RAW_FMT_I420,  // YUV420 planar (I420) - compatible avec notre conversion
      .gop = gop_,
      .fps = 30,
      .res = {.width = width, .height = height},
      .rc = {.bitrate = bitrate_, .qp_min = qp_min_, .qp_max = qp_max_}};

  esp_h264_err_t ret = esp_h264_enc_sw_new(&cfg, &h264_encoder_);
  if (ret != ESP_H264_ERR_OK || !h264_encoder_) {
    ESP_LOGE(TAG, "Failed to create H.264 software encoder: %d", ret);
    cleanup_h264_encoder_();
    return ESP_FAIL;
  }

  ret = esp_h264_enc_open(h264_encoder_);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open H.264 encoder: %d", ret);
    cleanup_h264_encoder_();
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "H.264 software encoder initialized successfully (OpenH264)");
  ESP_LOGI(TAG, "Software encoder: up to 720p@15-20fps on ESP32-P4");
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

  // Lazy initialization of H.264 encoder on first PLAY
  if (!h264_encoder_) {
    ESP_LOGI(TAG, "Initializing H.264 encoder (first client)...");
    if (init_h264_encoder_() != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize H.264 encoder");
      std::map<std::string, std::string> headers;
      headers["CSeq"] = std::to_string(cseq);
      send_rtsp_response_(session.socket_fd, 500, "Internal Server Error", headers);
      return;
    }
    ESP_LOGI(TAG, "H.264 encoder initialized successfully");
  }

  session.state = RTSPState::PLAYING;
  streaming_active_ = true;

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
  streaming_active_ = any_playing;
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

esp_err_t RTSPServer::convert_rgb565_to_yuv420_(const uint8_t *rgb565, uint8_t *yuv420, uint16_t width,
                                                 uint16_t height) {
  uint8_t *y_plane = yuv420;
  uint8_t *u_plane = yuv420 + (width * height);
  uint8_t *v_plane = u_plane + (width * height / 4);

  for (uint16_t row = 0; row < height; row++) {
    for (uint16_t col = 0; col < width; col++) {
      uint16_t pixel = ((uint16_t *)rgb565)[row * width + col];

      uint8_t r = ((pixel >> 11) & 0x1F) << 3;
      uint8_t g = ((pixel >> 5) & 0x3F) << 2;
      uint8_t b = (pixel & 0x1F) << 3;

      int y = (66 * r + 129 * g + 25 * b + 128) >> 8;
      int u = (-38 * r - 74 * g + 112 * b + 128) >> 8;
      int v = (112 * r - 94 * g - 18 * b + 128) >> 8;

      y_plane[row * width + col] = y + 16;

      if (row % 2 == 0 && col % 2 == 0) {
        u_plane[(row / 2) * (width / 2) + (col / 2)] = u + 128;
        v_plane[(row / 2) * (width / 2) + (col / 2)] = v + 128;
      }
    }
  }

  return ESP_OK;
}

esp_err_t RTSPServer::encode_and_stream_frame_() {
  if (!camera_ || !h264_encoder_)
    return ESP_FAIL;

  // Use get_current_rgb_frame() which locks the buffer until release_buffer()
  // This prevents V4L2 from overwriting the buffer while we're encoding
  mipi_dsi_cam::SimpleBufferElement* buffer = nullptr;
  uint8_t* frame_data = nullptr;
  int width, height;

  if (!camera_->get_current_rgb_frame(&buffer, &frame_data, &width, &height)) {
    ESP_LOGW(TAG, "Failed to get RGB frame");
    return ESP_FAIL;
  }

  if (frame_data == nullptr || buffer == nullptr) {
    ESP_LOGW(TAG, "Invalid frame data: ptr=%p buffer=%p", frame_data, buffer);
    return ESP_FAIL;
  }

  // Convert RGB565 to YUV420 (buffer is locked, safe to read)
  convert_rgb565_to_yuv420_(frame_data, yuv_buffer_, width, height);

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
    ESP_LOGE(TAG, "H.264 encoding failed: %d", ret);
    return ESP_FAIL;
  }

  // Cache SPS/PPS from IDR frames
  if (out_frame.frame_type == ESP_H264_FRAME_TYPE_IDR) {
    parse_and_cache_nal_units_(out_frame.raw_data.buffer, out_frame.length);
  }

  // Send NAL units
  auto nal_units = parse_nal_units_(out_frame.raw_data.buffer, out_frame.length);
  for (const auto &nal : nal_units) {
    send_h264_rtp_(nal.first, nal.second, true);
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
  std::vector<std::pair<const uint8_t *, size_t>> nal_units;

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
          nal_units.push_back({data + i + start_code_len, nal_size});
        }

        i = j;
        continue;
      }
    }
    i++;
  }

  return nal_units;
}

esp_err_t RTSPServer::send_h264_rtp_(const uint8_t *data, size_t len, bool marker) {
  uint8_t packet[2048];
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

      sendto(rtp_socket_, packet, sizeof(RTPHeader) + len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }
  }

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

}  // namespace rtsp_server
}  // namespace esphome

#endif  // USE_ESP_IDF
