#include "network_camera.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/wifi/wifi_component.h"

#include <cstring>
#include <algorithm>
#include <cctype>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "mbedtls/base64.h"
#include "esp_task_wdt.h"

namespace esphome {
namespace network_camera {

static const char *const TAG = "network_camera";

// Maximum buffer sizes
static const size_t MAX_JPEG_SIZE = 512 * 1024;  // 512KB for JPEG
static const size_t MAX_H264_SIZE = 256 * 1024;  // 256KB for H264 NAL units

void NetworkCamera::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Network Camera...");
  ESP_LOGI(TAG, "  URL: %s", this->url_.c_str());
  ESP_LOGI(TAG, "  Protocol: %s", this->protocol_ == Protocol::RTSP ? "RTSP/H264" : "MJPEG");
  ESP_LOGI(TAG, "  Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGI(TAG, "  Update interval: %u ms", this->update_interval_);

  if (!this->init_buffers_()) {
    ESP_LOGE(TAG, "Failed to allocate buffers");
    this->mark_failed();
    return;
  }

  if (this->protocol_ == Protocol::MJPEG) {
    if (!this->init_jpeg_decoder_()) {
      ESP_LOGE(TAG, "Failed to initialize JPEG decoder");
      this->mark_failed();
      return;
    }
  } else {
    if (!this->init_h264_decoder_()) {
      ESP_LOGE(TAG, "Failed to initialize H264 decoder");
      this->mark_failed();
      return;
    }
  }

  ESP_LOGI(TAG, "Network Camera initialized");
}

void NetworkCamera::loop() {
  // Start timer when enabled
  if (this->enabled_ && this->lvgl_timer_ == nullptr) {
    uint32_t now = millis();

    // CRITICAL: Check WiFi FIRST before any connection attempt
    auto wifi_component = wifi::global_wifi_component;
    if (wifi_component == nullptr || !wifi_component->is_connected()) {
      // Log only on first attempt or every 30 seconds to avoid spam
      static uint32_t last_wifi_log = 0;
      if (this->connection_attempts_ == 0 || (now - last_wifi_log) > 30000) {
        ESP_LOGW(TAG, "⏳ WiFi not ready yet, waiting for connection...");
        last_wifi_log = now;
      }
      this->last_connection_attempt_ = now;
      // Keep trying - don't disable camera
      return;
    }

    // Additional check: Verify WiFi has valid IP address
    if (!wifi_component->has_sta()) {
      static uint32_t last_ip_log = 0;
      if ((now - last_ip_log) > 30000) {
        ESP_LOGW(TAG, "⏳ WiFi connected but no STA interface yet, waiting...");
        last_ip_log = now;
      }
      this->last_connection_attempt_ = now;
      return;
    }

    // Check if we need to wait before attempting connection (retry delay)
    if (this->last_connection_attempt_ > 0 &&
        (now - this->last_connection_attempt_) < this->connection_retry_delay_) {
      // Still within retry delay period, skip this attempt
      return;
    }

    // WiFi is READY - proceed with connection
    ESP_LOGI(TAG, "✓ WiFi ready, starting camera...");
    ESP_LOGI(TAG, "Starting Network Camera display...");
    this->connection_attempts_++;
    this->last_connection_attempt_ = now;

    bool connected = false;
    if (this->protocol_ == Protocol::MJPEG) {
      connected = this->connect_mjpeg_stream_();
    } else {
      connected = this->connect_rtsp_stream_();
    }

    if (!connected) {
      ESP_LOGE(TAG, "Failed to connect to stream (attempt %u, will retry in %u seconds)",
               this->connection_attempts_, this->connection_retry_delay_ / 1000);
      // Don't disable - will retry automatically after delay
      return;
    }

    // Connection successful! Reset retry counter
    ESP_LOGI(TAG, "Connection established after %u attempt(s)", this->connection_attempts_);
    this->connection_attempts_ = 0;

    this->lvgl_timer_ = lv_timer_create(lvgl_timer_callback_, this->update_interval_, this);
    if (this->lvgl_timer_ == nullptr) {
      ESP_LOGE(TAG, "Failed to create LVGL timer");
    } else {
      ESP_LOGI(TAG, "Network Camera display started");
    }
  }

  // Stop timer when disabled
  if (!this->enabled_ && this->lvgl_timer_ != nullptr) {
    ESP_LOGI(TAG, "Stopping Network Camera display...");
    lv_timer_del(this->lvgl_timer_);
    this->lvgl_timer_ = nullptr;

    if (this->protocol_ == Protocol::MJPEG) {
      this->disconnect_mjpeg_stream_();
    } else {
      this->disconnect_rtsp_stream_();
    }

    ESP_LOGI(TAG, "Network Camera display stopped");
  }
}

void NetworkCamera::lvgl_timer_callback_(lv_timer_t *timer) {
  NetworkCamera *cam = static_cast<NetworkCamera *>(timer->user_data);
  if (cam == nullptr || !cam->stream_connected_) {
    return;
  }

  bool frame_ready = false;

  if (cam->protocol_ == Protocol::MJPEG) {
    if (cam->fetch_jpeg_frame_()) {
      frame_ready = cam->decode_jpeg_to_rgb565_();
    }
  } else {
    if (cam->fetch_rtp_frame_()) {
      if (cam->decode_h264_to_yuv_()) {
        cam->convert_yuv420_to_rgb565_(cam->yuv_buffer_, cam->current_decode_buffer_,
                                       cam->width_, cam->height_);
        frame_ready = true;
      }
    }
  }

  if (frame_ready) {
    cam->update_canvas_();
    cam->swap_buffers_();
    cam->frame_count_++;

    // Log FPS every 100 frames
    if (cam->frame_count_ % 100 == 0) {
      static uint32_t last_time = 0;
      uint32_t now = millis();
      if (last_time > 0) {
        float fps = 100000.0f / (now - last_time);
        ESP_LOGI(TAG, "Frames: %u - FPS: %.1f", cam->frame_count_, fps);
      }
      last_time = now;
    }
  } else {
    // Debug: Log when no frame is ready (only every 100 attempts)
    static uint32_t no_frame_count = 0;
    no_frame_count++;
    if (no_frame_count == 100 || no_frame_count % 500 == 0) {
      if (cam->protocol_ == Protocol::RTSP) {
        ESP_LOGW(TAG, "No H264 frames decoded yet (%u attempts)", no_frame_count);
      } else {
        ESP_LOGW(TAG, "No JPEG frames decoded yet (%u attempts)", no_frame_count);
      }
    }
  }
}

bool NetworkCamera::init_buffers_() {
  // ESP32-P4 JPEG decoder requires dimensions to be 16-byte aligned
  // Round up to nearest multiple of 16
  uint32_t aligned_width = (this->width_ + 15) & ~15;
  uint32_t aligned_height = (this->height_ + 15) & ~15;

  ESP_LOGI(TAG, "Image dimensions: %ux%u (configured) → %ux%u (16-byte aligned)",
           this->width_, this->height_, aligned_width, aligned_height);

  // RGB565 buffer size: aligned_width * aligned_height * 2 bytes
  this->rgb565_buffer_size_ = aligned_width * aligned_height * 2;

  // Allocate double buffers for RGB565 with 64-byte alignment for JPEG decoder
  // Using heap_caps_aligned_alloc instead of jpeg_alloc_decoder_mem to avoid initialization issues
  this->rgb565_buffer_a_ = (uint8_t *)heap_caps_aligned_alloc(64, this->rgb565_buffer_size_,
                                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  this->rgb565_buffer_b_ = (uint8_t *)heap_caps_aligned_alloc(64, this->rgb565_buffer_size_,
                                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (this->rgb565_buffer_a_ == nullptr || this->rgb565_buffer_b_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate aligned RGB565 buffers (%u bytes each)", this->rgb565_buffer_size_);
    return false;
  }

  ESP_LOGI(TAG, "Allocated 64-byte aligned RGB565 buffers in SPIRAM: %u bytes each", this->rgb565_buffer_size_);

  this->current_display_buffer_ = this->rgb565_buffer_a_;
  this->current_decode_buffer_ = this->rgb565_buffer_b_;

  if (this->protocol_ == Protocol::MJPEG) {
    // Allocate JPEG receive buffer
    this->jpeg_buffer_size_ = MAX_JPEG_SIZE;
    this->jpeg_buffer_ = (uint8_t *)heap_caps_malloc(this->jpeg_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (this->jpeg_buffer_ == nullptr) {
      ESP_LOGE(TAG, "Failed to allocate JPEG buffer (%u bytes)", this->jpeg_buffer_size_);
      return false;
    }
  } else {
    // Allocate H264 and YUV buffers
    this->h264_buffer_size_ = MAX_H264_SIZE;
    this->h264_buffer_ = (uint8_t *)heap_caps_malloc(this->h264_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    // YUV420: width * height * 1.5 bytes
    this->yuv_buffer_size_ = this->width_ * this->height_ * 3 / 2;
    this->yuv_buffer_ = (uint8_t *)heap_caps_malloc(this->yuv_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (this->h264_buffer_ == nullptr || this->yuv_buffer_ == nullptr) {
      ESP_LOGE(TAG, "Failed to allocate H264/YUV buffers");
      return false;
    }
  }

  ESP_LOGI(TAG, "Buffers allocated successfully");
  return true;
}

bool NetworkCamera::init_jpeg_decoder_() {
  jpeg_decode_engine_cfg_t decode_eng_cfg = {
      .intr_priority = 0,
      .timeout_ms = 200,  // 200ms timeout - increased for network streams with variable latency
  };

  esp_err_t ret = jpeg_new_decoder_engine(&decode_eng_cfg, &this->jpeg_decoder_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create JPEG decoder: %s", esp_err_to_name(ret));
    return false;
  }

  ESP_LOGI(TAG, "JPEG hardware decoder initialized (timeout=200ms, optimized for network streams)");
  return true;
}

bool NetworkCamera::init_h264_decoder_() {
  esp_h264_dec_cfg_sw_t dec_cfg = {};
  dec_cfg.pic_type = ESP_H264_RAW_FMT_I420;
  dec_cfg.profile_idc = ESP_H264_PROFILE_AUTO;  // openh264 supports all profiles - auto-detect

  esp_h264_err_t ret = esp_h264_dec_sw_new(&dec_cfg, &this->h264_decoder_);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "Failed to create H264 decoder: %d", ret);
    return false;
  }

  ret = esp_h264_dec_open(this->h264_decoder_);
  if (ret != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open H264 decoder: %d", ret);
    return false;
  }

  ESP_LOGI(TAG, "H264 decoder initialized (openh264 supports Baseline/Main/High profiles)");
  return true;
}

// ============================================================================
// MJPEG Methods
// ============================================================================

bool NetworkCamera::connect_mjpeg_stream_() {
  if (this->stream_connected_) {
    return true;
  }

  esp_http_client_config_t config = {};
  config.url = this->url_.c_str();
  config.timeout_ms = 5000;
  config.buffer_size = 8192;  // Increased from 4096 to reduce HTTP round-trips
  config.buffer_size_tx = 1024;

  this->http_client_ = esp_http_client_init(&config);
  if (this->http_client_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create HTTP client");
    return false;
  }

  esp_err_t err = esp_http_client_open(this->http_client_, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open connection: %s", esp_err_to_name(err));
    esp_http_client_cleanup(this->http_client_);
    this->http_client_ = nullptr;
    return false;
  }

  int content_length = esp_http_client_fetch_headers(this->http_client_);
  int status_code = esp_http_client_get_status_code(this->http_client_);

  ESP_LOGI(TAG, "MJPEG connected - Status: %d", status_code);

  if (status_code != 200) {
    ESP_LOGE(TAG, "HTTP error: %d", status_code);
    esp_http_client_cleanup(this->http_client_);
    this->http_client_ = nullptr;
    return false;
  }

  this->stream_connected_ = true;
  this->mjpeg_state_ = MjpegState::SEARCHING_BOUNDARY;

  return true;
}

void NetworkCamera::disconnect_mjpeg_stream_() {
  if (this->http_client_ != nullptr) {
    esp_http_client_close(this->http_client_);
    esp_http_client_cleanup(this->http_client_);
    this->http_client_ = nullptr;
  }
  this->stream_connected_ = false;
}

bool NetworkCamera::fetch_jpeg_frame_() {
  if (!this->stream_connected_ || this->http_client_ == nullptr) {
    return false;
  }

  uint8_t temp_buffer[8192];  // Increased from 4096 for better throughput
  static uint8_t parse_buffer[16384];  // Increased from 8192 to handle larger frames
  static size_t parse_buffer_len = 0;

  int read_len = esp_http_client_read(this->http_client_, (char *)temp_buffer, sizeof(temp_buffer));
  if (read_len < 0) {
    ESP_LOGE(TAG, "Stream read error");
    this->disconnect_mjpeg_stream_();
    return false;
  }
  if (read_len == 0) {
    return false;
  }

  // Append to parse buffer
  if (parse_buffer_len + read_len > sizeof(parse_buffer)) {
    parse_buffer_len = 0;
  }
  memcpy(parse_buffer + parse_buffer_len, temp_buffer, read_len);
  parse_buffer_len += read_len;

  // Parse MJPEG stream - look for JPEG markers
  size_t i = 0;
  while (i < parse_buffer_len - 1) {
    if (this->mjpeg_state_ == MjpegState::SEARCHING_BOUNDARY) {
      // Look for JPEG start marker (FFD8)
      if (parse_buffer[i] == 0xFF && parse_buffer[i + 1] == 0xD8) {
        this->jpeg_data_len_ = 0;
        this->mjpeg_state_ = MjpegState::READING_CONTENT;
        this->jpeg_buffer_[this->jpeg_data_len_++] = 0xFF;
        this->jpeg_buffer_[this->jpeg_data_len_++] = 0xD8;
        i += 2;
        continue;
      }
      i++;
    } else if (this->mjpeg_state_ == MjpegState::READING_CONTENT) {
      // Copy data and look for end marker (FFD9)
      if (parse_buffer[i] == 0xFF && parse_buffer[i + 1] == 0xD9) {
        this->jpeg_buffer_[this->jpeg_data_len_++] = 0xFF;
        this->jpeg_buffer_[this->jpeg_data_len_++] = 0xD9;

        size_t remaining = parse_buffer_len - i - 2;
        if (remaining > 0) {
          memmove(parse_buffer, parse_buffer + i + 2, remaining);
        }
        parse_buffer_len = remaining;

        this->mjpeg_state_ = MjpegState::SEARCHING_BOUNDARY;

        if (this->first_update_) {
          ESP_LOGI(TAG, "First JPEG frame: %u bytes", this->jpeg_data_len_);
          this->first_update_ = false;
        }

        return true;
      }

      if (this->jpeg_data_len_ < this->jpeg_buffer_size_) {
        this->jpeg_buffer_[this->jpeg_data_len_++] = parse_buffer[i];
      } else {
        ESP_LOGW(TAG, "JPEG buffer overflow");
        this->mjpeg_state_ = MjpegState::SEARCHING_BOUNDARY;
        this->jpeg_data_len_ = 0;
      }
      i++;
    }
  }

  if (i < parse_buffer_len && this->mjpeg_state_ == MjpegState::SEARCHING_BOUNDARY) {
    size_t remaining = parse_buffer_len - i;
    memmove(parse_buffer, parse_buffer + i, remaining);
    parse_buffer_len = remaining;
  }

  return false;
}

// Strip ALL unsupported markers from JPEG for ESP32-P4 hardware decoder
// ESP32-P4 JPEG hardware decoder only supports these markers:
// - SOI (FF D8), DQT (FF DB), DHT (FF C4), SOF0 (FF C0), SOS (FF DA), EOI (FF D9)
// - Scan data (between SOS and EOI)
// ALL other markers must be removed: APP0-15, COM, DRI, RST, etc.
size_t NetworkCamera::strip_jpeg_com_markers_(uint8_t *data, size_t len) {
  if (len < 4) return len;

  size_t write_pos = 0;
  size_t read_pos = 0;

  // Keep SOI
  if (data[0] == 0xFF && data[1] == 0xD8) {
    data[write_pos++] = 0xFF;
    data[write_pos++] = 0xD8;
    read_pos = 2;
  }

  bool in_scan_data = false;  // Track if we're in compressed scan data

  while (read_pos < len - 1) {
    if (data[read_pos] == 0xFF) {
      uint8_t marker = data[read_pos + 1];

      // Check for byte stuffing (FF 00) - keep it if in scan data
      if (marker == 0x00) {
        if (in_scan_data) {
          data[write_pos++] = 0xFF;
          data[write_pos++] = 0x00;
        }
        read_pos += 2;
        continue;
      }

      // SOS marker - start of scan data (keep it and everything after until next marker)
      if (marker == 0xDA) {
        in_scan_data = true;
        // Copy SOS marker and its length
        if (read_pos + 3 >= len) break;
        uint16_t marker_len = (data[read_pos + 2] << 8) | data[read_pos + 3];
        size_t total_len = 2 + marker_len;
        if (read_pos + total_len > len) break;

        memcpy(data + write_pos, data + read_pos, total_len);
        write_pos += total_len;
        read_pos += total_len;
        continue;
      }

      // EOI marker - end of image (keep it)
      if (marker == 0xD9) {
        data[write_pos++] = 0xFF;
        data[write_pos++] = 0xD9;
        break;  // Done!
      }

      // RST markers (FF D0-D7) - REMOVE (not supported by hardware)
      if (marker >= 0xD0 && marker <= 0xD7) {
        read_pos += 2;
        continue;
      }

      // Essential markers to KEEP: DQT, DHT, SOF0, SOF2
      if (marker == 0xDB || marker == 0xC4 || marker == 0xC0 || marker == 0xC2) {
        if (read_pos + 3 >= len) break;
        uint16_t marker_len = (data[read_pos + 2] << 8) | data[read_pos + 3];
        size_t total_len = 2 + marker_len;
        if (read_pos + total_len > len) break;

        memcpy(data + write_pos, data + read_pos, total_len);
        write_pos += total_len;
        read_pos += total_len;
        continue;
      }

      // ALL other markers: APP0-15 (E0-EF), COM (FE), DRI (DD), DNL (DC), etc. - REMOVE
      if ((marker >= 0xE0 && marker <= 0xEF) ||  // APP markers
          marker == 0xFE ||                        // COM
          marker == 0xDD ||                        // DRI
          marker == 0xDC ||                        // DNL
          (marker >= 0xC0 && marker <= 0xCF && marker != 0xC0 && marker != 0xC2 && marker != 0xC4)) {  // Other SOF
        if (read_pos + 3 >= len) break;
        uint16_t marker_len = (data[read_pos + 2] << 8) | data[read_pos + 3];
        read_pos += 2 + marker_len;  // Skip entire marker
        continue;
      }

      // Unknown marker - skip it
      read_pos += 2;
    } else {
      // Regular data (scan data or other)
      data[write_pos++] = data[read_pos++];
    }
  }

  // Copy remaining bytes
  while (read_pos < len) {
    data[write_pos++] = data[read_pos++];
  }

  return write_pos;
}

size_t NetworkCamera::strip_jpeg_restart_markers_(uint8_t *data, size_t len) {
  // This function is now integrated into strip_jpeg_com_markers_
  // Keep for compatibility but it does nothing
  return len;
}

bool NetworkCamera::decode_jpeg_to_rgb565_() {
  if (this->jpeg_data_len_ == 0 || this->jpeg_decoder_ == nullptr) {
    return false;
  }

  // CRITICAL: Validate JPEG markers BEFORE decoding
  if (this->jpeg_data_len_ < 4 ||
      this->jpeg_buffer_[0] != 0xFF || this->jpeg_buffer_[1] != 0xD8) {
    static uint32_t validation_errors = 0;
    if (validation_errors++ < 5) {
      ESP_LOGW(TAG, "Invalid JPEG header: size=%u, markers=0x%02X%02X (expected FF D8)",
               this->jpeg_data_len_,
               this->jpeg_data_len_ > 0 ? this->jpeg_buffer_[0] : 0,
               this->jpeg_data_len_ > 1 ? this->jpeg_buffer_[1] : 0);
    }
    return false;
  }

  // DEBUG: Track sizes through processing pipeline
  static uint32_t debug_count = 0;
  size_t original_len = this->jpeg_data_len_;
  bool debug_this_frame = (debug_count++ < 5);  // Debug first 5 frames

  if (debug_this_frame) {
    ESP_LOGI(TAG, "=== JPEG Processing Debug ===");
    ESP_LOGI(TAG, "Original size: %u bytes", original_len);
  }

  // CRITICAL FIX: Truncate at first EOI (FF D9) BEFORE stripping markers
  // go2rtc concatenates multiple JPEGs - we need only the first one
  bool eoi_found = false;
  for (size_t i = 0; i < this->jpeg_data_len_ - 1; i++) {
    if (this->jpeg_buffer_[i] == 0xFF) {
      uint8_t next = this->jpeg_buffer_[i + 1];
      if (next == 0xD9) {  // EOI marker found
        eoi_found = true;
        this->jpeg_data_len_ = i + 2;  // Truncate here
        break;
      } else if (next == 0x00) {
        i++;  // Skip byte stuffing (FF 00)
      }
    }
  }

  if (debug_this_frame) {
    if (!eoi_found) {
      ESP_LOGW(TAG, "⚠️ NO EOI MARKER FOUND!");
    }
    ESP_LOGI(TAG, "After EOI truncation: %u bytes", this->jpeg_data_len_);
  }

  // Strip ALL unsupported markers (APP, COM, DRI, RST, etc.)
  // This is the CRITICAL FIX for ESP32-P4 hardware decoder
  size_t cleaned_len = this->strip_jpeg_com_markers_(this->jpeg_buffer_, this->jpeg_data_len_);

  if (debug_this_frame) {
    ESP_LOGI(TAG, "After marker cleanup: %u bytes (removed %d bytes)",
             cleaned_len, (int)(this->jpeg_data_len_ - cleaned_len));
    ESP_LOGI(TAG, "Final: SOI=%02X%02X EOI=%02X%02X",
             this->jpeg_buffer_[0], this->jpeg_buffer_[1],
             this->jpeg_buffer_[cleaned_len - 2], this->jpeg_buffer_[cleaned_len - 1]);
    ESP_LOGI(TAG, "=============================");
  }

  this->jpeg_data_len_ = cleaned_len;

  // Validate we still have a valid JPEG after cleanup
  if (this->jpeg_data_len_ < 4 ||
      this->jpeg_buffer_[0] != 0xFF || this->jpeg_buffer_[1] != 0xD8 ||
      this->jpeg_buffer_[this->jpeg_data_len_ - 2] != 0xFF ||
      this->jpeg_buffer_[this->jpeg_data_len_ - 1] != 0xD9) {
    ESP_LOGW(TAG, "JPEG corrupted after cleanup");
    return false;
  }

  jpeg_decode_cfg_t decode_cfg = {
      .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
      .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
  };

  uint32_t out_size = 0;
  esp_err_t ret = jpeg_decoder_process(this->jpeg_decoder_, &decode_cfg,
                                       this->jpeg_buffer_, this->jpeg_data_len_,
                                       this->current_decode_buffer_, this->rgb565_buffer_size_,
                                       &out_size);

  if (ret != ESP_OK) {
    // Log decode errors for first 10 frames, then only every 100th error
    static uint32_t decode_errors = 0;
    decode_errors++;
    if (decode_errors <= 10 || decode_errors % 100 == 0) {
      ESP_LOGE(TAG, "❌ JPEG decode failed (error #%u): %s",
               decode_errors, esp_err_to_name(ret));
      ESP_LOGE(TAG, "   JPEG size: %u bytes, RGB buffer: %u bytes",
               this->jpeg_data_len_, this->rgb565_buffer_size_);

      // Provide helpful diagnostic info
      if (decode_errors <= 3) {
        ESP_LOGE(TAG, "   💡 If errors persist, the JPEG may contain unsupported features");
        ESP_LOGE(TAG, "   💡 Try adjusting camera JPEG quality or resolution");
      }
    }
    return false;
  }

  // Log first successful decode only
  static bool first_success = false;
  if (!first_success) {
    ESP_LOGI(TAG, "✅ JPEG decoder working! %ux%u resolution", this->width_, this->height_);
    ESP_LOGI(TAG, "✅ First frame: %u bytes JPEG → %u bytes RGB565", this->jpeg_data_len_, out_size);
    first_success = true;
  }

  return true;
}

// ============================================================================
// RTSP Methods
// ============================================================================

bool NetworkCamera::connect_rtsp_stream_() {
  if (this->stream_connected_) {
    return true;
  }

  // Parse RTSP URL: rtsp://[user:pass@]host:port/path
  std::string url = this->url_;
  if (url.find("rtsp://") != 0) {
    ESP_LOGE(TAG, "Invalid RTSP URL");
    return false;
  }

  url = url.substr(7);  // Remove "rtsp://"

  // Check for credentials (user:pass@)
  std::string credentials;
  size_t at_pos = url.find('@');
  if (at_pos != std::string::npos) {
    credentials = url.substr(0, at_pos);
    url = url.substr(at_pos + 1);  // Remove credentials from URL

    // Encode credentials to Base64 for Basic auth
    size_t out_len = 0;
    unsigned char base64_buf[256];
    if (mbedtls_base64_encode(base64_buf, sizeof(base64_buf), &out_len,
                              (const unsigned char *)credentials.c_str(), credentials.length()) == 0) {
      this->rtsp_auth_ = std::string((char *)base64_buf, out_len);
      ESP_LOGI(TAG, "RTSP credentials encoded for Basic auth");
    }
  }

  // Now parse host:port/path
  size_t path_pos = url.find('/');
  size_t port_pos = url.find(':');

  std::string host;
  uint16_t port = 554;
  std::string path = "/";

  if (port_pos != std::string::npos && (path_pos == std::string::npos || port_pos < path_pos)) {
    host = url.substr(0, port_pos);
    if (path_pos != std::string::npos) {
      port = atoi(url.substr(port_pos + 1, path_pos - port_pos - 1).c_str());
    } else {
      port = atoi(url.substr(port_pos + 1).c_str());
    }
  } else if (path_pos != std::string::npos) {
    host = url.substr(0, path_pos);
  } else {
    host = url;
  }

  if (path_pos != std::string::npos) {
    path = url.substr(path_pos);
  }

  ESP_LOGI(TAG, "Connecting to RTSP: %s:%u%s", host.c_str(), port, path.c_str());

  // Create TCP socket for RTSP
  this->rtsp_socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (this->rtsp_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create socket");
    return false;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);

  struct hostent *he = gethostbyname(host.c_str());
  if (he == nullptr) {
    ESP_LOGE(TAG, "DNS resolution failed for %s", host.c_str());
    close(this->rtsp_socket_);
    this->rtsp_socket_ = -1;
    return false;
  }
  memcpy(&server_addr.sin_addr, he->h_addr, he->h_length);

  ESP_LOGI(TAG, "Attempting TCP connection to %s:%u...", host.c_str(), port);

  // Set socket to non-blocking for connect
  int flags = fcntl(this->rtsp_socket_, F_GETFL, 0);
  fcntl(this->rtsp_socket_, F_SETFL, flags | O_NONBLOCK);

  // Start non-blocking connect
  int ret = connect(this->rtsp_socket_, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (ret < 0 && errno != EINPROGRESS) {
    ESP_LOGE(TAG, "Failed to start connect: %s (errno %d)", strerror(errno), errno);
    close(this->rtsp_socket_);
    this->rtsp_socket_ = -1;
    return false;
  }

  // Wait for connection with timeout, feeding watchdog periodically
  bool connected = false;
  for (int i = 0; i < 10; i++) {  // 10 x 500ms = 5 seconds max
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(this->rtsp_socket_, &write_fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;  // 500ms

    int sel_ret = select(this->rtsp_socket_ + 1, nullptr, &write_fds, nullptr, &tv);

    if (sel_ret > 0) {
      // Check if connection succeeded
      int so_error;
      socklen_t len = sizeof(so_error);
      getsockopt(this->rtsp_socket_, SOL_SOCKET, SO_ERROR, &so_error, &len);

      if (so_error == 0) {
        connected = true;
        break;
      } else {
        ESP_LOGE(TAG, "Connection failed: %s (errno %d)", strerror(so_error), so_error);
        break;
      }
    } else if (sel_ret < 0) {
      ESP_LOGE(TAG, "Select error: %s", strerror(errno));
      break;
    }

    // Feed watchdog and yield
    esp_task_wdt_reset();
    vTaskDelay(1);
  }

  if (!connected) {
    ESP_LOGE(TAG, "Connection timeout");
    close(this->rtsp_socket_);
    this->rtsp_socket_ = -1;
    return false;
  }

  // Set back to blocking for RTSP commands
  fcntl(this->rtsp_socket_, F_SETFL, flags);

  // Set read/write timeout
  struct timeval tv;
  tv.tv_sec = 5;
  tv.tv_usec = 0;
  setsockopt(this->rtsp_socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(this->rtsp_socket_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  ESP_LOGI(TAG, "TCP connection established");

  std::string full_url = "rtsp://" + host + ":" + std::to_string(port) + path;

  // OPTIONS
  if (!this->send_rtsp_request_("OPTIONS", full_url)) {
    this->disconnect_rtsp_stream_();
    return false;
  }

  // DESCRIBE
  std::string sdp_response;
  if (!this->send_rtsp_request_("DESCRIBE", full_url, "Accept: application/sdp\r\n", &sdp_response)) {
    this->disconnect_rtsp_stream_();
    return false;
  }

  // Parse SDP to get control URL for video track
  std::string control_url = full_url;
  size_t control_pos = sdp_response.find("a=control:");
  if (control_pos != std::string::npos) {
    size_t start = control_pos + 10; // Length of "a=control:"
    size_t end = sdp_response.find('\r', start);
    if (end == std::string::npos) {
      end = sdp_response.find('\n', start);
    }
    if (end != std::string::npos) {
      std::string control = sdp_response.substr(start, end - start);

      // Remove ALL whitespace characters (spaces, tabs, newlines) from the string
      control.erase(std::remove_if(control.begin(), control.end(),
                                   [](unsigned char c) { return std::isspace(c); }),
                   control.end());

      ESP_LOGI(TAG, "SDP control attribute (cleaned): '%s'", control.c_str());

      // If control is a relative URL, append to base URL
      if (control.empty()) {
        control_url = full_url;
      } else if (control.find("://") != std::string::npos) {
        // Absolute URL
        control_url = control;
      } else if (control[0] == '/') {
        // Relative to server root
        size_t scheme_end = full_url.find("://");
        if (scheme_end != std::string::npos) {
          size_t path_start = full_url.find('/', scheme_end + 3);
          if (path_start != std::string::npos) {
            control_url = full_url.substr(0, path_start) + control;
          } else {
            control_url = full_url + control;
          }
        }
      } else {
        // Relative to current path - append to full_url
        if (full_url.back() == '/') {
          control_url = full_url + control;
        } else {
          control_url = full_url + "/" + control;
        }
      }
      ESP_LOGI(TAG, "Using control URL from SDP: %s", control_url.c_str());
    }
  } else {
    // Try common fallbacks
    ESP_LOGW(TAG, "No control URL in SDP, trying common patterns...");
    control_url = full_url; // Some cameras use the base URL
  }

  // SETUP with TCP interleaved transport
  if (!this->send_rtsp_request_("SETUP", control_url, "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n")) {
    this->disconnect_rtsp_stream_();
    return false;
  }

  // PLAY
  char session_header[128];
  snprintf(session_header, sizeof(session_header), "Session: %s\r\n", this->rtsp_session_.c_str());
  if (!this->send_rtsp_request_("PLAY", full_url, session_header)) {
    this->disconnect_rtsp_stream_();
    return false;
  }

  // Set socket to non-blocking for reading interleaved data
  flags = fcntl(this->rtsp_socket_, F_GETFL, 0);
  fcntl(this->rtsp_socket_, F_SETFL, flags | O_NONBLOCK);

  this->stream_connected_ = true;
  ESP_LOGI(TAG, "RTSP stream connected (TCP interleaved)");

  return true;
}

void NetworkCamera::disconnect_rtsp_stream_() {
  if (this->rtsp_socket_ >= 0) {
    // Send TEARDOWN
    if (!this->rtsp_session_.empty()) {
      // Set blocking for TEARDOWN
      int flags = fcntl(this->rtsp_socket_, F_GETFL, 0);
      fcntl(this->rtsp_socket_, F_SETFL, flags & ~O_NONBLOCK);

      char session_header[128];
      snprintf(session_header, sizeof(session_header), "Session: %s\r\n", this->rtsp_session_.c_str());
      this->send_rtsp_request_("TEARDOWN", this->url_, session_header);
    }
    close(this->rtsp_socket_);
    this->rtsp_socket_ = -1;
  }
  this->stream_connected_ = false;
  this->rtsp_session_.clear();

  // CRITICAL: Reset SPS/PPS sent flags so they get sent again on reconnect
  this->param_sets_sent_ = false;
  this->param_sets_sent_fua_ = false;
  this->has_sps_ = false;
  this->has_pps_ = false;
  this->h264_data_len_ = 0;
}

bool NetworkCamera::send_rtsp_request_(const std::string &method, const std::string &url,
                                       const std::string &extra_headers, std::string *response_body) {
  char request[768];

  // Build Authorization header if credentials available
  std::string auth_header;
  if (!this->rtsp_auth_.empty()) {
    auth_header = "Authorization: Basic " + this->rtsp_auth_ + "\r\n";
  }

  snprintf(request, sizeof(request),
           "%s %s RTSP/1.0\r\n"
           "CSeq: %d\r\n"
           "%s"
           "%s"
           "\r\n",
           method.c_str(), url.c_str(), this->cseq_++, auth_header.c_str(), extra_headers.c_str());

  if (send(this->rtsp_socket_, request, strlen(request), 0) < 0) {
    ESP_LOGE(TAG, "Failed to send RTSP %s", method.c_str());
    return false;
  }

  // Receive response
  char response[4096];  // Increased size for SDP content
  int len = recv(this->rtsp_socket_, response, sizeof(response) - 1, 0);
  if (len <= 0) {
    ESP_LOGE(TAG, "Failed to receive RTSP response");
    return false;
  }
  response[len] = '\0';

  // Check status
  if (strstr(response, "200 OK") == nullptr) {
    ESP_LOGE(TAG, "RTSP %s failed: %s", method.c_str(), response);
    return false;
  }

  // Extract Session ID from SETUP response
  if (method == "SETUP") {
    char *session = strstr(response, "Session: ");
    if (session) {
      session += 9;
      char *end = strpbrk(session, ";\r\n");
      if (end) {
        this->rtsp_session_ = std::string(session, end - session);
        ESP_LOGI(TAG, "RTSP Session: %s", this->rtsp_session_.c_str());
      }
    }
  }

  // If caller wants the response body (for SDP parsing)
  if (response_body != nullptr) {
    *response_body = std::string(response);
  }

  ESP_LOGI(TAG, "RTSP %s OK", method.c_str());
  return true;
}

bool NetworkCamera::fetch_rtp_frame_() {
  if (this->rtsp_socket_ < 0) {
    return false;
  }

  // TCP interleaved format:
  // $ (0x24), channel (1 byte), length (2 bytes big endian), RTP data
  uint8_t header[4];
  uint8_t rtp_packet[1500];

  // Accumulate NAL units into h264_buffer_
  bool frame_complete = false;

  while (!frame_complete) {
    // Read interleaved header
    ssize_t len = recv(this->rtsp_socket_, header, 4, MSG_PEEK);
    if (len <= 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;  // No more data available
      }
      return false;
    }

    if (len < 4) {
      break;  // Not enough data yet
    }

    // Check for interleaved marker
    if (header[0] != '$') {
      // Skip non-interleaved data (could be RTSP response)
      char skip[1];
      recv(this->rtsp_socket_, skip, 1, 0);
      continue;
    }

    uint8_t channel = header[1];
    uint16_t rtp_len = (header[2] << 8) | header[3];

    if (rtp_len > sizeof(rtp_packet)) {
      ESP_LOGW(TAG, "RTP packet too large: %u", rtp_len);
      // Consume the header and skip the packet
      recv(this->rtsp_socket_, header, 4, 0);
      while (rtp_len > 0) {
        ssize_t skip = recv(this->rtsp_socket_, rtp_packet,
                           rtp_len > sizeof(rtp_packet) ? sizeof(rtp_packet) : rtp_len, 0);
        if (skip <= 0) break;
        rtp_len -= skip;
      }
      continue;
    }

    // Consume the header
    recv(this->rtsp_socket_, header, 4, 0);

    // Read RTP packet
    ssize_t received = 0;
    while (received < rtp_len) {
      len = recv(this->rtsp_socket_, rtp_packet + received, rtp_len - received, 0);
      if (len <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        }
        return false;
      }
      received += len;
    }

    if (received < rtp_len) {
      break;  // Incomplete packet
    }

    // Skip RTCP packets (channel 1)
    if (channel != 0) {
      continue;
    }

    if (rtp_len < 12) {
      continue;  // Invalid RTP packet
    }

    // RTP header
    uint8_t marker = (rtp_packet[1] >> 7) & 0x01;

    int header_len = 12;  // Basic RTP header

    // H264 NAL unit starts after RTP header
    uint8_t *nal_data = rtp_packet + header_len;
    int nal_len = rtp_len - header_len;

    if (nal_len <= 0) {
      continue;
    }

    // Check NAL unit type
    uint8_t nal_type = nal_data[0] & 0x1F;

    // H.264 NAL unit types:
    // 7 = SPS (Sequence Parameter Set)
    // 8 = PPS (Picture Parameter Set)
    // 5 = IDR (I-frame)
    // 1 = Non-IDR (P-frame)

    if (nal_type == 7) {
      // SPS - cache it (with start code)
      if (nal_len + 4 <= sizeof(this->sps_cache_)) {
        this->sps_len_ = 0;
        this->sps_cache_[this->sps_len_++] = 0x00;
        this->sps_cache_[this->sps_len_++] = 0x00;
        this->sps_cache_[this->sps_len_++] = 0x00;
        this->sps_cache_[this->sps_len_++] = 0x01;
        memcpy(this->sps_cache_ + this->sps_len_, nal_data, nal_len);
        this->sps_len_ += nal_len;
        this->has_sps_ = true;
        ESP_LOGI(TAG, "Cached SPS: %u bytes", this->sps_len_);
      }
      // Don't add SPS to main buffer - it will be prepended to I-frames
    } else if (nal_type == 8) {
      // PPS - cache it (with start code)
      if (nal_len + 4 <= sizeof(this->pps_cache_)) {
        this->pps_len_ = 0;
        this->pps_cache_[this->pps_len_++] = 0x00;
        this->pps_cache_[this->pps_len_++] = 0x00;
        this->pps_cache_[this->pps_len_++] = 0x00;
        this->pps_cache_[this->pps_len_++] = 0x01;
        memcpy(this->pps_cache_ + this->pps_len_, nal_data, nal_len);
        this->pps_len_ += nal_len;
        this->has_pps_ = true;
        ESP_LOGI(TAG, "Cached PPS: %u bytes", this->pps_len_);
      }
      // Don't add PPS to main buffer - it will be prepended to I-frames
    } else if (nal_type >= 1 && nal_type <= 23) {
      // Picture NAL unit (I-frame, P-frame, etc.)

      // CRITICAL FIX: Send SPS/PPS with the FIRST frame received (not just I-frames)
      // Without this, if stream starts with P-frames, decoder never gets param sets
      if (!this->param_sets_sent_ && this->has_sps_ && this->has_pps_) {
        // Send SPS/PPS with first frame (I-frame OR P-frame)
        if (this->h264_data_len_ + this->sps_len_ + this->pps_len_ + nal_len + 4 < this->h264_buffer_size_) {
          // Add SPS
          memcpy(this->h264_buffer_ + this->h264_data_len_, this->sps_cache_, this->sps_len_);
          this->h264_data_len_ += this->sps_len_;
          // Add PPS
          memcpy(this->h264_buffer_ + this->h264_data_len_, this->pps_cache_, this->pps_len_);
          this->h264_data_len_ += this->pps_len_;

          ESP_LOGI(TAG, "✓ Sent SPS+PPS (%u+%u bytes) with FIRST frame (NAL type %u)",
                   this->sps_len_, this->pps_len_, nal_type);
          this->param_sets_sent_ = true;
        }
      }

      // Also prepend SPS/PPS to each I-frame for recovery after packet loss
      if (nal_type == 5 && this->has_sps_ && this->has_pps_ && this->param_sets_sent_) {
        if (this->h264_data_len_ + this->sps_len_ + this->pps_len_ + nal_len + 4 < this->h264_buffer_size_) {
          // Add SPS
          memcpy(this->h264_buffer_ + this->h264_data_len_, this->sps_cache_, this->sps_len_);
          this->h264_data_len_ += this->sps_len_;
          // Add PPS
          memcpy(this->h264_buffer_ + this->h264_data_len_, this->pps_cache_, this->pps_len_);
          this->h264_data_len_ += this->pps_len_;
          ESP_LOGI(TAG, "Prepended SPS+PPS (%u+%u bytes) to I-frame for recovery", this->sps_len_, this->pps_len_);
        }
      }

      // Add the picture NAL unit itself
      if (this->h264_data_len_ + nal_len + 4 < this->h264_buffer_size_) {
        // Add start code
        this->h264_buffer_[this->h264_data_len_++] = 0x00;
        this->h264_buffer_[this->h264_data_len_++] = 0x00;
        this->h264_buffer_[this->h264_data_len_++] = 0x00;
        this->h264_buffer_[this->h264_data_len_++] = 0x01;
        memcpy(this->h264_buffer_ + this->h264_data_len_, nal_data, nal_len);
        this->h264_data_len_ += nal_len;

        // Log first 10 frames for debugging
        static uint32_t frame_count = 0;
        if (frame_count++ < 10) {
          const char *frame_type = nal_type == 5 ? "I-frame (IDR)" :
                                   nal_type == 1 ? "P-frame" :
                                   nal_type == 2 ? "P-frame (partition A)" :
                                   nal_type == 3 ? "P-frame (partition B)" :
                                   nal_type == 4 ? "P-frame (partition C)" : "Other";
          ESP_LOGI(TAG, "Frame #%u: NAL type %u (%s), size %u bytes",
                   frame_count, nal_type, frame_type, nal_len);
        }
      }
    } else if (nal_type == 28) {
      // FU-A (Fragmentation Unit)
      if (nal_len < 2) continue;

      uint8_t fu_header = nal_data[1];
      bool start = (fu_header >> 7) & 0x01;
      uint8_t fu_type = fu_header & 0x1F;

      if (start) {
        // Start of fragmented NAL
        uint8_t reconstructed = (nal_data[0] & 0xE0) | fu_type;

        // CRITICAL FIX: Send SPS/PPS with first fragmented frame (but not twice!)
        // Only prepend SPS/PPS if we haven't sent them yet
        // This handles both first frame AND subsequent I-frames
        if (!this->param_sets_sent_fua_ && this->has_sps_ && this->has_pps_ && (fu_type >= 1 && fu_type <= 23)) {
          // Send SPS/PPS with first fragmented picture frame
          if (this->h264_data_len_ + this->sps_len_ + this->pps_len_ + nal_len + 3 < this->h264_buffer_size_) {
            // Add SPS
            memcpy(this->h264_buffer_ + this->h264_data_len_, this->sps_cache_, this->sps_len_);
            this->h264_data_len_ += this->sps_len_;
            // Add PPS
            memcpy(this->h264_buffer_ + this->h264_data_len_, this->pps_cache_, this->pps_len_);
            this->h264_data_len_ += this->pps_len_;
            ESP_LOGI(TAG, "✓ Sent SPS+PPS (%u+%u bytes) with FIRST fragmented frame (FU type %u)",
                     this->sps_len_, this->pps_len_, fu_type);
            this->param_sets_sent_fua_ = true;
          } else {
            ESP_LOGW(TAG, "⚠ Buffer too small to add SPS+PPS (need %u bytes, buffer has %u free)",
                     this->sps_len_ + this->pps_len_ + nal_len + 3,
                     this->h264_buffer_size_ - this->h264_data_len_);
          }
        }
        // Note: We removed the second SPS/PPS prepending block to avoid double prepending
        // I-frames will get SPS/PPS from the first block when param_sets_sent_fua is false

        if (this->h264_data_len_ + nal_len + 3 < this->h264_buffer_size_) {
          this->h264_buffer_[this->h264_data_len_++] = 0x00;
          this->h264_buffer_[this->h264_data_len_++] = 0x00;
          this->h264_buffer_[this->h264_data_len_++] = 0x00;
          this->h264_buffer_[this->h264_data_len_++] = 0x01;
          this->h264_buffer_[this->h264_data_len_++] = reconstructed;
          memcpy(this->h264_buffer_ + this->h264_data_len_, nal_data + 2, nal_len - 2);
          this->h264_data_len_ += nal_len - 2;

          // Log first 10 fragmented frames for debugging
          static uint32_t frag_count = 0;
          if (frag_count++ < 10) {
            const char *frame_type = fu_type == 5 ? "I-frame (IDR)" :
                                     fu_type == 1 ? "P-frame" : "Other";
            ESP_LOGI(TAG, "Fragmented frame #%u: FU type %u (%s), fragment size %u bytes",
                     frag_count, fu_type, frame_type, nal_len - 2);
          }
        }
      } else {
        // Continuation
        if (this->h264_data_len_ + nal_len - 2 < this->h264_buffer_size_) {
          memcpy(this->h264_buffer_ + this->h264_data_len_, nal_data + 2, nal_len - 2);
          this->h264_data_len_ += nal_len - 2;
        }
      }
    }

    // Marker bit indicates end of frame
    // Only set frame_complete for actual picture data (not SPS/PPS)
    if (marker && nal_type != 7 && nal_type != 8) {
      frame_complete = true;
    }
  }

  if (frame_complete && this->h264_data_len_ > 0) {
    if (this->first_update_) {
      ESP_LOGI(TAG, "First H264 frame: %u bytes", this->h264_data_len_);
      this->first_update_ = false;
    }
    return true;
  }

  return false;
}

bool NetworkCamera::decode_h264_to_yuv_() {
  if (this->h264_data_len_ == 0 || this->h264_decoder_ == nullptr) {
    return false;
  }

  esp_h264_dec_in_frame_t in_frame = {};
  in_frame.raw_data.buffer = this->h264_buffer_;
  in_frame.raw_data.len = this->h264_data_len_;

  esp_h264_dec_out_frame_t out_frame = {};

  // Process all NAL units in the buffer
  bool frame_decoded = false;
  static bool first_decode_success = false;

  while (in_frame.raw_data.len > 0) {
    esp_h264_err_t ret = esp_h264_dec_process(this->h264_decoder_, &in_frame, &out_frame);
    if (ret != ESP_H264_ERR_OK) {
      // Log decode error for debugging
      static uint32_t error_count = 0;
      error_count++;
      if (error_count <= 10 || error_count % 100 == 0) {
        ESP_LOGE(TAG, "H264 decode error: %d (NAL size: %u bytes, error #%u)",
                 ret, in_frame.raw_data.len, error_count);

        // Explain error code
        if (ret == -1) ESP_LOGE(TAG, "  → ESP_H264_ERR_FAIL (general decode failure)");
        if (ret == -2) ESP_LOGE(TAG, "  → ESP_H264_ERR_ARG (invalid arguments)");
        if (ret == -3) ESP_LOGE(TAG, "  → ESP_H264_ERR_MEM (out of memory)");
        if (ret == -5) ESP_LOGE(TAG, "  → ESP_H264_ERR_UNSUPPORTED (profile incompatible or feature not supported)");
        if (ret == -6) ESP_LOGE(TAG, "  → ESP_H264_ERR_TIMEOUT");
        if (ret == -7) ESP_LOGE(TAG, "  → ESP_H264_ERR_OVERFLOW");

        if (!first_decode_success) {
          ESP_LOGE(TAG, "  ⚠ No frames decoded yet - check if SPS/PPS were sent with first frame");
          ESP_LOGE(TAG, "  ⚠ If error = -5, H264 profile may be incompatible (High Profile not fully supported)");
        }
      }
      break;
    }

    // Check if we got a decoded frame
    if (out_frame.out_size > 0 && out_frame.outbuf != nullptr) {
      // Copy decoded YUV data to our buffer
      size_t copy_size = out_frame.out_size;
      if (copy_size > this->yuv_buffer_size_) {
        copy_size = this->yuv_buffer_size_;
      }
      memcpy(this->yuv_buffer_, out_frame.outbuf, copy_size);
      frame_decoded = true;

      // Log first successful decode
      if (!first_decode_success) {
        ESP_LOGI(TAG, "✓ First frame decoded successfully! Decoder initialized and working.");
        ESP_LOGI(TAG, "  Decoded YUV size: %u bytes (expected: %u bytes)",
                 out_frame.out_size, this->yuv_buffer_size_);
        first_decode_success = true;
      }
    }

    // Move to next NAL unit
    in_frame.raw_data.buffer += in_frame.consume;
    in_frame.raw_data.len -= in_frame.consume;
  }

  // Reset buffer for next frame
  this->h264_data_len_ = 0;

  return frame_decoded;
}

void NetworkCamera::convert_yuv420_to_rgb565_(uint8_t *yuv, uint8_t *rgb565, int width, int height) {
  // YUV420 (I420) layout:
  // Y plane: width * height bytes
  // U plane: (width/2) * (height/2) bytes
  // V plane: (width/2) * (height/2) bytes

  uint8_t *y_plane = yuv;
  uint8_t *u_plane = yuv + width * height;
  uint8_t *v_plane = u_plane + (width / 2) * (height / 2);

  uint16_t *rgb = (uint16_t *)rgb565;

  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      int y_idx = j * width + i;
      int uv_idx = (j / 2) * (width / 2) + (i / 2);

      int y = y_plane[y_idx];
      int u = u_plane[uv_idx] - 128;
      int v = v_plane[uv_idx] - 128;

      // YUV to RGB conversion
      int r = y + ((v * 359) >> 8);
      int g = y - ((u * 88 + v * 183) >> 8);
      int b = y + ((u * 454) >> 8);

      // Clamp
      r = r < 0 ? 0 : (r > 255 ? 255 : r);
      g = g < 0 ? 0 : (g > 255 ? 255 : g);
      b = b < 0 ? 0 : (b > 255 ? 255 : b);

      // RGB565: RRRRR GGGGGG BBBBB
      rgb[y_idx] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
  }
}

// ============================================================================
// Common Methods
// ============================================================================

void NetworkCamera::update_canvas_() {
  if (this->canvas_obj_ == nullptr) {
    if (!this->canvas_warning_shown_) {
      ESP_LOGW(TAG, "Canvas not configured");
      this->canvas_warning_shown_ = true;
    }
    return;
  }

  lv_canvas_set_buffer(this->canvas_obj_, this->current_decode_buffer_,
                       this->width_, this->height_, LV_IMG_CF_TRUE_COLOR);
  lv_obj_invalidate(this->canvas_obj_);
}

void NetworkCamera::swap_buffers_() {
  uint8_t *temp = this->current_display_buffer_;
  this->current_display_buffer_ = this->current_decode_buffer_;
  this->current_decode_buffer_ = temp;
}

void NetworkCamera::configure_canvas(lv_obj_t *canvas) {
  this->canvas_obj_ = canvas;
  ESP_LOGD(TAG, "Canvas configured: %p (will render at %ux%u)", canvas, this->width_, this->height_);
}

void NetworkCamera::dump_config() {
  ESP_LOGCONFIG(TAG, "Network Camera:");
  ESP_LOGCONFIG(TAG, "  URL: %s", this->url_.c_str());
  ESP_LOGCONFIG(TAG, "  Protocol: %s", this->protocol_ == Protocol::RTSP ? "RTSP/H264" : "MJPEG");
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->update_interval_);
}

}  // namespace network_camera
}  // namespace esphome
