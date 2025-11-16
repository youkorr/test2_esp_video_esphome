#include "webrtc_camera.h"

#ifdef USE_ESP_IDF
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstring>
#include <esp_random.h>

namespace esphome {
namespace webrtc_camera {

static const char *const TAG = "webrtc_camera";

// HTML page for WebRTC client
static const char WEBRTC_HTML[] = R"html(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 WebRTC Camera</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }
        h1 { color: #4CAF50; }
        video { width: 100%; max-width: 1280px; background: #000; border: 2px solid #4CAF50; }
        button { padding: 10px 20px; margin: 5px; font-size: 16px; cursor: pointer; background: #4CAF50; color: white; border: none; border-radius: 4px; }
        button:hover { background: #45a049; }
        button:disabled { background: #666; cursor: not-allowed; }
        #status { margin: 20px 0; padding: 10px; background: #333; border-radius: 4px; }
        .info { color: #4CAF50; }
        .error { color: #f44336; }
    </style>
</head>
<body>
    <h1>ESP32-P4 WebRTC H.264 Stream</h1>
    <div id="status">Status: <span id="statusText">Ready</span></div>
    <video id="video" autoplay playsinline controls></video>
    <div>
        <button id="startBtn" onclick="start()">Start Stream</button>
        <button id="stopBtn" onclick="stop()" disabled>Stop Stream</button>
    </div>

    <script>
        const video = document.getElementById('video');
        const statusText = document.getElementById('statusText');
        const startBtn = document.getElementById('startBtn');
        const stopBtn = document.getElementById('stopBtn');

        let pc = null;
        let ws = null;

        function setStatus(msg, isError = false) {
            statusText.textContent = msg;
            statusText.className = isError ? 'error' : 'info';
            console.log(msg);
        }

        async function start() {
            try {
                setStatus('Connecting to signaling server...');

                // WebSocket signaling
                ws = new WebSocket(`ws://${window.location.hostname}:${window.location.port}/ws`);

                ws.onopen = async () => {
                    setStatus('Creating peer connection...');

                    // Create RTCPeerConnection
                    pc = new RTCPeerConnection({
                        iceServers: []  // Direct LAN connection
                    });

                    // Handle incoming tracks
                    pc.ontrack = (event) => {
                        setStatus('Receiving video stream...');
                        video.srcObject = event.streams[0];
                        startBtn.disabled = true;
                        stopBtn.disabled = false;
                    };

                    // Handle ICE candidates
                    pc.onicecandidate = (event) => {
                        if (event.candidate) {
                            ws.send(JSON.stringify({
                                type: 'candidate',
                                candidate: event.candidate
                            }));
                        }
                    };

                    pc.onconnectionstatechange = () => {
                        setStatus(`Connection: ${pc.connectionState}`);
                        if (pc.connectionState === 'failed' || pc.connectionState === 'closed') {
                            stop();
                        }
                    };

                    // Add transceiver for H.264 video
                    pc.addTransceiver('video', {
                        direction: 'recvonly'
                    });

                    // Create and send offer
                    const offer = await pc.createOffer();
                    await pc.setLocalDescription(offer);

                    ws.send(JSON.stringify({
                        type: 'offer',
                        sdp: offer.sdp
                    }));

                    setStatus('Waiting for answer...');
                };

                ws.onmessage = async (event) => {
                    const msg = JSON.parse(event.data);

                    if (msg.type === 'answer') {
                        setStatus('Received answer, connecting...');
                        await pc.setRemoteDescription(new RTCSessionDescription({
                            type: 'answer',
                            sdp: msg.sdp
                        }));
                    } else if (msg.type === 'candidate' && msg.candidate) {
                        await pc.addIceCandidate(new RTCIceCandidate(msg.candidate));
                    }
                };

                ws.onerror = (error) => {
                    setStatus('WebSocket error: ' + error, true);
                };

                ws.onclose = () => {
                    setStatus('Signaling connection closed');
                };

            } catch (error) {
                setStatus('Error: ' + error.message, true);
                console.error(error);
            }
        }

        function stop() {
            if (pc) {
                pc.close();
                pc = null;
            }
            if (ws) {
                ws.close();
                ws = null;
            }
            if (video.srcObject) {
                video.srcObject.getTracks().forEach(track => track.stop());
                video.srcObject = null;
            }
            startBtn.disabled = false;
            stopBtn.disabled = true;
            setStatus('Stopped');
        }
    </script>
</body>
</html>
)html";

void WebRTCCamera::setup() {
  ESP_LOGI(TAG, "Setting up WebRTC Camera...");

  // Generate random SSRC
  rtp_ssrc_ = esp_random();

  // Initialize H.264 encoder
  if (init_h264_encoder_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize H.264 encoder");
    mark_failed();
    return;
  }

  // Initialize RTP socket
  if (init_rtp_socket_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize RTP socket");
    mark_failed();
    return;
  }

  // Start signaling server
  if (start_signaling_server_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start signaling server");
    mark_failed();
    return;
  }

  ESP_LOGI(TAG, "WebRTC Camera setup complete");
  ESP_LOGI(TAG, "Signaling server: http://<IP>:%d", signaling_port_);
  ESP_LOGI(TAG, "RTP port: %d", rtp_port_);
}

void WebRTCCamera::loop() {
  if (!streaming_active_ || !client_connected_) {
    return;
  }

  // Encode and send frames
  if (encode_and_send_frame_() != ESP_OK) {
    ESP_LOGW(TAG, "Failed to encode/send frame");
  }

  // Small delay to control frame rate
  delay(33);  // ~30 FPS
}

void WebRTCCamera::dump_config() {
  ESP_LOGCONFIG(TAG, "WebRTC Camera:");
  ESP_LOGCONFIG(TAG, "  Signaling Port: %d", signaling_port_);
  ESP_LOGCONFIG(TAG, "  RTP Port: %d", rtp_port_);
  ESP_LOGCONFIG(TAG, "  Bitrate: %d bps", bitrate_);
  ESP_LOGCONFIG(TAG, "  GOP: %d", gop_);
  ESP_LOGCONFIG(TAG, "  QP Range: %d-%d", qp_min_, qp_max_);
}

esp_err_t WebRTCCamera::init_h264_encoder_() {
  ESP_LOGI(TAG, "Initializing H.264 hardware encoder...");

  if (!camera_) {
    ESP_LOGE(TAG, "Camera not set");
    return ESP_FAIL;
  }

  uint16_t width = camera_->get_image_width();
  uint16_t height = camera_->get_image_height();

  // Align dimensions to 16
  width = ((width + 15) >> 4) << 4;
  height = ((height + 15) >> 4) << 4;

  ESP_LOGI(TAG, "Resolution: %dx%d (aligned)", width, height);

  // Allocate YUV420 buffer
  yuv_buffer_size_ = width * height * 3 / 2;  // YUV420 format
  yuv_buffer_ = (uint8_t *)heap_caps_malloc(yuv_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!yuv_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate YUV buffer");
    return ESP_ERR_NO_MEM;
  }

  // Allocate H.264 output buffer (estimate 2x input size for worst case)
  h264_buffer_size_ = yuv_buffer_size_ * 2;
  h264_buffer_ = (uint8_t *)heap_caps_malloc(h264_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!h264_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate H.264 buffer");
    free(yuv_buffer_);
    return ESP_ERR_NO_MEM;
  }

  // Configure H.264 encoder
  esp_h264_enc_cfg_hw_t cfg = {
      .pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY,  // YUV420 format for HW encoder
      .gop = gop_,
      .fps = 30,
      .res = {.width = width, .height = height},
      .rc = {.bitrate = bitrate_, .qp_min = qp_min_, .qp_max = qp_max_}};

  esp_h264_err_t ret = esp_h264_enc_hw_new(&cfg, &h264_encoder_);
  if (ret != ESP_H264_ERR_OK || !h264_encoder_) {
    ESP_LOGE(TAG, "Failed to create H.264 encoder: %d", ret);
    cleanup_h264_encoder_();
    return ESP_FAIL;
  }

  // Open encoder
  ret = esp_h264_enc_open(h264_encoder_);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open H.264 encoder: %d", ret);
    cleanup_h264_encoder_();
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "H.264 encoder initialized successfully");
  return ESP_OK;
}

void WebRTCCamera::cleanup_h264_encoder_() {
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
}

esp_err_t WebRTCCamera::init_rtp_socket_() {
  ESP_LOGI(TAG, "Initializing RTP socket on port %d", rtp_port_);

  rtp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (rtp_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create RTP socket");
    return ESP_FAIL;
  }

  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(rtp_port_);

  if (bind(rtp_socket_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind RTP socket");
    close(rtp_socket_);
    rtp_socket_ = -1;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "RTP socket initialized");
  return ESP_OK;
}

void WebRTCCamera::cleanup_rtp_socket_() {
  if (rtp_socket_ >= 0) {
    close(rtp_socket_);
    rtp_socket_ = -1;
  }
  client_connected_ = false;
}

esp_err_t WebRTCCamera::convert_rgb565_to_yuv420_(const uint8_t *rgb565, uint8_t *yuv420,
                                                    uint16_t width, uint16_t height) {
  // Simple RGB565 to YUV420 conversion
  // This is a simplified version - for production, use optimized conversion

  uint8_t *y_plane = yuv420;
  uint8_t *u_plane = yuv420 + (width * height);
  uint8_t *v_plane = u_plane + (width * height / 4);

  for (uint16_t row = 0; row < height; row++) {
    for (uint16_t col = 0; col < width; col++) {
      uint16_t pixel = ((uint16_t *)rgb565)[row * width + col];

      // Extract RGB from RGB565
      uint8_t r = ((pixel >> 11) & 0x1F) << 3;
      uint8_t g = ((pixel >> 5) & 0x3F) << 2;
      uint8_t b = (pixel & 0x1F) << 3;

      // RGB to YUV conversion
      int y = (66 * r + 129 * g + 25 * b + 128) >> 8;
      int u = (-38 * r - 74 * g + 112 * b + 128) >> 8;
      int v = (112 * r - 94 * g - 18 * b + 128) >> 8;

      y_plane[row * width + col] = y + 16;

      // Subsample U and V (4:2:0)
      if (row % 2 == 0 && col % 2 == 0) {
        u_plane[(row / 2) * (width / 2) + (col / 2)] = u + 128;
        v_plane[(row / 2) * (width / 2) + (col / 2)] = v + 128;
      }
    }
  }

  return ESP_OK;
}

esp_err_t WebRTCCamera::encode_and_send_frame_() {
  if (!camera_ || !h264_encoder_) {
    return ESP_FAIL;
  }

  // Get current RGB565 frame (must be released after use)
  mipi_dsi_cam::SimpleBufferElement* buffer = nullptr;
  uint8_t* frame_data = nullptr;
  int width, height;

  if (!camera_->get_current_rgb_frame(&buffer, &frame_data, &width, &height)) {
    return ESP_FAIL;
  }

  // Convert RGB565 to YUV420
  if (convert_rgb565_to_yuv420_(frame_data, yuv_buffer_, width, height) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to convert RGB565 to YUV420");
    camera_->release_buffer(buffer);  // Release buffer before returning
    return ESP_FAIL;
  }

  // Prepare input frame
  esp_h264_enc_in_frame_t in_frame = {};
  in_frame.raw_data.buffer = yuv_buffer_;
  in_frame.raw_data.len = yuv_buffer_size_;
  in_frame.pts = frame_count_ * 90000 / 30;  // 90kHz clock, 30 FPS

  // Prepare output frame
  esp_h264_enc_out_frame_t out_frame = {};
  out_frame.raw_data.buffer = h264_buffer_;
  out_frame.raw_data.len = h264_buffer_size_;

  // Encode
  esp_h264_err_t ret = esp_h264_enc_process(h264_encoder_, &in_frame, &out_frame);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "H.264 encoding failed: %d", ret);
    camera_->release_buffer(buffer);  // Release buffer before returning
    return ESP_FAIL;
  }

  // Send over RTP
  if (send_h264_over_rtp_(out_frame.raw_data.buffer, out_frame.length,
                           out_frame.frame_type, out_frame.pts) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to send RTP packet");
    camera_->release_buffer(buffer);  // Release buffer before returning
    return ESP_FAIL;
  }

  frame_count_++;

  if (frame_count_ % 30 == 0) {
    ESP_LOGI(TAG, "Sent %d frames, type: %d, size: %d bytes",
             frame_count_, out_frame.frame_type, out_frame.length);
  }

  // Release buffer after processing
  camera_->release_buffer(buffer);

  return ESP_OK;
}

std::vector<std::pair<const uint8_t *, size_t>> WebRTCCamera::parse_nal_units_(const uint8_t *data, size_t len) {
  std::vector<std::pair<const uint8_t *, size_t>> nal_units;

  size_t i = 0;
  while (i < len - 3) {
    // Look for start code (0x00 0x00 0x00 0x01 or 0x00 0x00 0x01)
    if (data[i] == 0x00 && data[i + 1] == 0x00) {
      size_t start_code_len = 0;
      if (data[i + 2] == 0x01) {
        start_code_len = 3;
      } else if (data[i + 2] == 0x00 && data[i + 3] == 0x01) {
        start_code_len = 4;
      }

      if (start_code_len > 0) {
        // Find next start code
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

esp_err_t WebRTCCamera::send_h264_over_rtp_(const uint8_t *data, size_t len,
                                             esp_h264_frame_type_t frame_type, uint32_t timestamp) {
  if (!client_connected_ || rtp_socket_ < 0) {
    return ESP_FAIL;
  }

  // Parse NAL units
  auto nal_units = parse_nal_units_(data, len);

  for (const auto &nal : nal_units) {
    const uint8_t *nal_data = nal.first;
    size_t nal_size = nal.second;

    if (nal_size == 0) continue;

    // Get NAL unit type
    uint8_t nal_type = nal_data[0] & 0x1F;

    ESP_LOGD(TAG, "Sending NAL unit type %d, size %d", nal_type, nal_size);

    // For simplicity, send each NAL unit in a single RTP packet
    // In production, implement FU-A fragmentation for large NAL units
    const size_t MAX_RTP_PAYLOAD = 1400;

    if (nal_size <= MAX_RTP_PAYLOAD) {
      // Single NAL unit mode
      send_rtp_packet_(nal_data, nal_size, true);
    } else {
      // TODO: Implement FU-A fragmentation for large NAL units
      ESP_LOGW(TAG, "NAL unit too large (%d bytes), fragmentation not implemented", nal_size);
    }
  }

  rtp_timestamp_ += 3000;  // Increment for next frame (90kHz / 30fps)

  return ESP_OK;
}

esp_err_t WebRTCCamera::send_rtp_packet_(const uint8_t *payload, size_t len, bool marker) {
  if (rtp_socket_ < 0 || !client_connected_) {
    return ESP_FAIL;
  }

  uint8_t packet[2048];
  RTPHeader *rtp = (RTPHeader *)packet;

  // Fill RTP header
  rtp->v = 2;
  rtp->p = 0;
  rtp->x = 0;
  rtp->cc = 0;
  rtp->m = marker ? 1 : 0;
  rtp->pt = 96;  // Dynamic payload type for H.264
  rtp->seq = htons(rtp_seq_num_++);
  rtp->timestamp = htonl(rtp_timestamp_);
  rtp->ssrc = htonl(rtp_ssrc_);

  // Copy payload
  memcpy(packet + sizeof(RTPHeader), payload, len);

  // Send packet
  ssize_t sent = sendto(rtp_socket_, packet, sizeof(RTPHeader) + len, 0,
                        (struct sockaddr *)&client_addr_, sizeof(client_addr_));

  if (sent < 0) {
    ESP_LOGE(TAG, "Failed to send RTP packet: %d", errno);
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t WebRTCCamera::start_signaling_server_() {
  ESP_LOGI(TAG, "Starting signaling server on port %d", signaling_port_);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = signaling_port_;
  config.ctrl_port = signaling_port_ + 1;
  config.max_uri_handlers = 8;
  config.stack_size = 8192;

  if (httpd_start(&signaling_server_, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start signaling server");
    return ESP_FAIL;
  }

  // Register handlers
  httpd_uri_t index_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = index_handler_,
      .user_ctx = this};
  httpd_register_uri_handler(signaling_server_, &index_uri);

  httpd_uri_t ws_uri = {
      .uri = "/ws",
      .method = HTTP_GET,
      .handler = ws_handler_,
      .user_ctx = this,
      .is_websocket = true};
  httpd_register_uri_handler(signaling_server_, &ws_uri);

  ESP_LOGI(TAG, "Signaling server started");
  return ESP_OK;
}

void WebRTCCamera::stop_signaling_server_() {
  if (signaling_server_) {
    httpd_stop(signaling_server_);
    signaling_server_ = nullptr;
  }
}

WebRTCCamera *WebRTCCamera::get_instance_(httpd_req_t *req) {
  return static_cast<WebRTCCamera *>(req->user_ctx);
}

esp_err_t WebRTCCamera::index_handler_(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, WEBRTC_HTML, strlen(WEBRTC_HTML));
}

esp_err_t WebRTCCamera::ws_handler_(httpd_req_t *req) {
  WebRTCCamera *instance = get_instance_(req);
  if (!instance) {
    return ESP_FAIL;
  }

  if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "WebSocket handshake");
    return ESP_OK;
  }

  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

  // Receive frame
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  if (ws_pkt.len == 0) {
    return ESP_OK;
  }

  uint8_t *buf = (uint8_t *)malloc(ws_pkt.len + 1);
  if (!buf) {
    return ESP_ERR_NO_MEM;
  }

  ws_pkt.payload = buf;
  ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
  if (ret != ESP_OK) {
    free(buf);
    return ret;
  }

  buf[ws_pkt.len] = '\0';

  ESP_LOGI(TAG, "Received WebSocket message: %s", buf);

  // Parse JSON and handle SDP offer/answer
  // This is a simplified version - in production use a JSON library
  if (strstr((char *)buf, "\"type\":\"offer\"")) {
    ESP_LOGI(TAG, "Received SDP offer");

    // Extract client IP for RTP
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(httpd_req_to_sockfd(req), (struct sockaddr *)&addr, &addr_len) == 0) {
      instance->client_addr_ = addr;
      instance->client_addr_.sin_port = htons(instance->rtp_port_);
      instance->client_connected_ = true;
      instance->streaming_active_ = true;
      ESP_LOGI(TAG, "Client connected from %s", inet_ntoa(addr.sin_addr));
    }

    // Generate SDP answer
    char sdp_answer[1024];
    snprintf(sdp_answer, sizeof(sdp_answer),
             "{\"type\":\"answer\",\"sdp\":\"v=0\\r\\n"
             "o=- 0 0 IN IP4 0.0.0.0\\r\\n"
             "s=ESP32 WebRTC\\r\\n"
             "t=0 0\\r\\n"
             "m=video %d RTP/AVP 96\\r\\n"
             "a=rtpmap:96 H264/90000\\r\\n"
             "a=fmtp:96 packetization-mode=1\\r\\n"
             "a=recvonly\\r\\n\"}",
             instance->rtp_port_);

    // Send answer
    httpd_ws_frame_t ws_resp;
    memset(&ws_resp, 0, sizeof(httpd_ws_frame_t));
    ws_resp.payload = (uint8_t *)sdp_answer;
    ws_resp.len = strlen(sdp_answer);
    ws_resp.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame(req, &ws_resp);
    ESP_LOGI(TAG, "Sent SDP answer");
  }

  free(buf);
  return ESP_OK;
}

}  // namespace webrtc_camera
}  // namespace esphome

#endif  // USE_ESP_IDF
