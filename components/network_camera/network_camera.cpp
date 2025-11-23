#include "network_camera.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include <cstring>

namespace esphome {
namespace network_camera {

static const char *const TAG = "network_camera";

// Maximum JPEG buffer size (adjust based on resolution)
static const size_t MAX_JPEG_SIZE = 512 * 1024;  // 512KB for JPEG

void NetworkCamera::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Network Camera (MJPEG)...");
  ESP_LOGI(TAG, "  URL: %s", this->url_.c_str());
  ESP_LOGI(TAG, "  Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGI(TAG, "  Update interval: %u ms", this->update_interval_);

  if (!this->init_buffers_()) {
    ESP_LOGE(TAG, "Failed to allocate buffers");
    this->mark_failed();
    return;
  }

  if (!this->init_jpeg_decoder_()) {
    ESP_LOGE(TAG, "Failed to initialize JPEG decoder");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Network Camera initialized (not connected yet)");
}

void NetworkCamera::loop() {
  // Start timer when enabled
  if (this->enabled_ && this->lvgl_timer_ == nullptr) {
    ESP_LOGI(TAG, "Starting Network Camera display...");

    // Connect to stream
    if (!this->connect_stream_()) {
      ESP_LOGE(TAG, "Failed to connect to MJPEG stream");
      return;
    }

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
    this->disconnect_stream_();
    ESP_LOGI(TAG, "Network Camera display stopped");
  }
}

void NetworkCamera::lvgl_timer_callback_(lv_timer_t *timer) {
  NetworkCamera *cam = static_cast<NetworkCamera *>(timer->user_data);
  if (cam != nullptr && cam->stream_connected_) {
    if (cam->fetch_jpeg_frame_()) {
      if (cam->decode_jpeg_to_rgb565_()) {
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
      }
    }
  }
}

bool NetworkCamera::init_buffers_() {
  // RGB565 buffer size: width * height * 2 bytes
  this->rgb565_buffer_size_ = this->width_ * this->height_ * 2;

  // Allocate double buffers for RGB565
  this->rgb565_buffer_a_ = (uint8_t *)heap_caps_malloc(this->rgb565_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  this->rgb565_buffer_b_ = (uint8_t *)heap_caps_malloc(this->rgb565_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (this->rgb565_buffer_a_ == nullptr || this->rgb565_buffer_b_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate RGB565 buffers (%u bytes each)", this->rgb565_buffer_size_);
    return false;
  }

  this->current_display_buffer_ = this->rgb565_buffer_a_;
  this->current_decode_buffer_ = this->rgb565_buffer_b_;

  // Allocate JPEG receive buffer
  this->jpeg_buffer_size_ = MAX_JPEG_SIZE;
  this->jpeg_buffer_ = (uint8_t *)heap_caps_malloc(this->jpeg_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (this->jpeg_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate JPEG buffer (%u bytes)", this->jpeg_buffer_size_);
    return false;
  }

  ESP_LOGI(TAG, "Buffers allocated:");
  ESP_LOGI(TAG, "  RGB565: 2x %u bytes", this->rgb565_buffer_size_);
  ESP_LOGI(TAG, "  JPEG: %u bytes", this->jpeg_buffer_size_);

  return true;
}

bool NetworkCamera::init_jpeg_decoder_() {
  jpeg_decode_engine_cfg_t decode_eng_cfg = {
      .intr_priority = 0,
      .timeout_ms = 40,
  };

  esp_err_t ret = jpeg_new_decoder_engine(&decode_eng_cfg, &this->jpeg_decoder_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create JPEG decoder: %s", esp_err_to_name(ret));
    return false;
  }

  ESP_LOGI(TAG, "JPEG hardware decoder initialized");
  return true;
}

bool NetworkCamera::connect_stream_() {
  if (this->stream_connected_) {
    return true;
  }

  esp_http_client_config_t config = {};
  config.url = this->url_.c_str();
  config.timeout_ms = 5000;
  config.buffer_size = 4096;
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

  ESP_LOGI(TAG, "Connected to stream - Status: %d, Content-Length: %d", status_code, content_length);

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

void NetworkCamera::disconnect_stream_() {
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

  // Read data from stream
  uint8_t temp_buffer[4096];
  static uint8_t parse_buffer[8192];
  static size_t parse_buffer_len = 0;

  int read_len = esp_http_client_read(this->http_client_, (char *)temp_buffer, sizeof(temp_buffer));
  if (read_len < 0) {
    ESP_LOGE(TAG, "Stream read error");
    this->disconnect_stream_();
    return false;
  }
  if (read_len == 0) {
    return false;  // No data available
  }

  // Append to parse buffer
  if (parse_buffer_len + read_len > sizeof(parse_buffer)) {
    parse_buffer_len = 0;  // Reset on overflow
  }
  memcpy(parse_buffer + parse_buffer_len, temp_buffer, read_len);
  parse_buffer_len += read_len;

  // Parse MJPEG stream
  // Look for JPEG markers (FFD8 = start, FFD9 = end)
  size_t i = 0;
  while (i < parse_buffer_len - 1) {
    if (this->mjpeg_state_ == MjpegState::SEARCHING_BOUNDARY) {
      // Look for JPEG start marker (FFD8)
      if (parse_buffer[i] == 0xFF && parse_buffer[i + 1] == 0xD8) {
        this->jpeg_data_len_ = 0;
        this->mjpeg_state_ = MjpegState::READING_CONTENT;
        // Copy start marker
        this->jpeg_buffer_[this->jpeg_data_len_++] = 0xFF;
        this->jpeg_buffer_[this->jpeg_data_len_++] = 0xD8;
        i += 2;
        continue;
      }
      i++;
    } else if (this->mjpeg_state_ == MjpegState::READING_CONTENT) {
      // Copy data and look for end marker (FFD9)
      if (parse_buffer[i] == 0xFF && parse_buffer[i + 1] == 0xD9) {
        // End of JPEG
        this->jpeg_buffer_[this->jpeg_data_len_++] = 0xFF;
        this->jpeg_buffer_[this->jpeg_data_len_++] = 0xD9;

        // Move remaining data to start of buffer
        size_t remaining = parse_buffer_len - i - 2;
        if (remaining > 0) {
          memmove(parse_buffer, parse_buffer + i + 2, remaining);
        }
        parse_buffer_len = remaining;

        this->mjpeg_state_ = MjpegState::SEARCHING_BOUNDARY;

        if (this->first_update_) {
          ESP_LOGI(TAG, "First JPEG frame received: %u bytes", this->jpeg_data_len_);
          this->first_update_ = false;
        }

        return true;  // Complete frame received
      }

      // Copy byte to JPEG buffer
      if (this->jpeg_data_len_ < this->jpeg_buffer_size_) {
        this->jpeg_buffer_[this->jpeg_data_len_++] = parse_buffer[i];
      } else {
        // Buffer overflow, reset
        ESP_LOGW(TAG, "JPEG buffer overflow, resetting");
        this->mjpeg_state_ = MjpegState::SEARCHING_BOUNDARY;
        this->jpeg_data_len_ = 0;
      }
      i++;
    }
  }

  // Keep unparsed data
  if (i < parse_buffer_len && this->mjpeg_state_ == MjpegState::SEARCHING_BOUNDARY) {
    size_t remaining = parse_buffer_len - i;
    memmove(parse_buffer, parse_buffer + i, remaining);
    parse_buffer_len = remaining;
  }

  return false;  // No complete frame yet
}

bool NetworkCamera::decode_jpeg_to_rgb565_() {
  if (this->jpeg_data_len_ == 0 || this->jpeg_decoder_ == nullptr) {
    return false;
  }

  jpeg_decode_picture_info_t pic_info = {};

  // Get JPEG info first
  esp_err_t ret = jpeg_decoder_get_info(this->jpeg_buffer_, this->jpeg_data_len_, &pic_info);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get JPEG info: %s", esp_err_to_name(ret));
    return false;
  }

  // Configure decode
  jpeg_decode_cfg_t decode_cfg = {
      .output_format = JPEG_DECODE_OUT_FORMAT_RGB565_BIG_ENDIAN,
      .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
  };

  uint32_t out_size = 0;
  ret = jpeg_decoder_process(this->jpeg_decoder_, &decode_cfg,
                             this->jpeg_buffer_, this->jpeg_data_len_,
                             this->current_decode_buffer_, this->rgb565_buffer_size_,
                             &out_size);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "JPEG decode failed: %s", esp_err_to_name(ret));
    return false;
  }

  return true;
}

void NetworkCamera::update_canvas_() {
  if (this->canvas_obj_ == nullptr) {
    if (!this->canvas_warning_shown_) {
      ESP_LOGW(TAG, "Canvas not configured yet");
      this->canvas_warning_shown_ = true;
    }
    return;
  }

  lv_canvas_set_buffer(this->canvas_obj_, this->current_decode_buffer_,
                       this->width_, this->height_, LV_IMG_CF_TRUE_COLOR);
  lv_obj_invalidate(this->canvas_obj_);
}

void NetworkCamera::swap_buffers_() {
  // Swap decode and display buffers
  uint8_t *temp = this->current_display_buffer_;
  this->current_display_buffer_ = this->current_decode_buffer_;
  this->current_decode_buffer_ = temp;
}

void NetworkCamera::configure_canvas(lv_obj_t *canvas) {
  this->canvas_obj_ = canvas;
  ESP_LOGI(TAG, "Canvas configured: %p", canvas);

  if (canvas != nullptr) {
    lv_coord_t w = lv_obj_get_width(canvas);
    lv_coord_t h = lv_obj_get_height(canvas);
    ESP_LOGI(TAG, "  Canvas size: %dx%d", w, h);
  }
}

void NetworkCamera::dump_config() {
  ESP_LOGCONFIG(TAG, "Network Camera (MJPEG):");
  ESP_LOGCONFIG(TAG, "  URL: %s", this->url_.c_str());
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->update_interval_);
  ESP_LOGCONFIG(TAG, "  Canvas configured: %s", this->canvas_obj_ ? "YES" : "NO");
}

}  // namespace network_camera
}  // namespace esphome
