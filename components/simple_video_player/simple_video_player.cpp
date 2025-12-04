#include "simple_video_player.h"


#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_http_client.h"  // For HTTP/HTTPS video streaming
#include <cstring>            // For strncmp
#include <vector>             // For dynamic buffer during HTTP download

#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace esphome {
namespace simple_video_player {

static const char *const TAG = "simple_video_player";

// Helper to read big-endian values
static uint32_t read_be32(FILE *f) {
  uint8_t buf[4];
  if (fread(buf, 1, 4, f) != 4) return 0;
  return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static uint16_t read_be16(FILE *f) {
  uint8_t buf[2];
  if (fread(buf, 1, 2, f) != 2) return 0;
  return (buf[0] << 8) | buf[1];
}

static uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (a << 24) | (b << 16) | (c << 8) | d;
}

void SimpleVideoPlayer::setup() {
  ESP_LOGI(TAG, "Setting up Simple Video Player...");
  ESP_LOGI(TAG, "  File: %s", this->file_path_.c_str());

  // Allocate input buffer
  this->input_buffer_ = (uint8_t *)heap_caps_malloc(this->buffer_size_,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->input_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate input buffer");
    this->mark_failed();
    return;
  }

  // Open video file
  if (!this->open_video_file_()) {
    ESP_LOGE(TAG, "Failed to open video file");
    this->mark_failed();
    return;
  }

  // If HTTP download is pending, defer initialization to loop()
  if (this->http_download_pending_) {
    ESP_LOGI(TAG, "HTTP source detected - initialization will complete in loop() after download");
    return;  // Exit setup early, complete initialization in loop()
  }

  // Detect format
  this->format_ = this->detect_format_();
  const char *format_str = "UNKNOWN";
  if (this->format_ == MediaFormat::MP4_H264) format_str = "MP4/H.264";
  else if (this->format_ == MediaFormat::MKV_H264) format_str = "MKV/H.264";
  else if (this->format_ == MediaFormat::MJPEG) format_str = "MJPEG";
  ESP_LOGI(TAG, "Detected format: %s", format_str);

  // Auto-detect resolution from video file
  if (this->format_ == MediaFormat::MJPEG) {
    if (this->detect_jpeg_resolution_(this->actual_width_, this->actual_height_)) {
      ESP_LOGI(TAG, "Auto-detected JPEG resolution: %dx%d", this->actual_width_, this->actual_height_);
    } else {
      ESP_LOGW(TAG, "Failed to auto-detect resolution, using configured: %dx%d", this->width_, this->height_);
      this->actual_width_ = this->width_;
      this->actual_height_ = this->height_;
    }

    // Auto-detect framerate from AVI header (if present and not overridden by user)
    if (!this->fps_override_) {
      if (!this->detect_avi_framerate_()) {
        ESP_LOGW(TAG, "Failed to detect AVI framerate, using default: 50 fps");
        // Keep default frame_interval_ = 20ms (50fps)
      }
    } else {
      ESP_LOGI(TAG, "Using user-configured framerate: %.2f fps (interval: %lu ms)",
               1000.0f / this->frame_interval_, (unsigned long)this->frame_interval_);
    }
  } else {
    // For MP4, use configured dimensions initially (will be updated during parsing)
    this->frame_interval_ = 20;  
    this->actual_width_ = this->width_;
    this->actual_height_ = this->height_;
  }

  // Calculate 16-byte aligned dimensions for decoder
  this->aligned_width_ = (this->actual_width_ + 15) & ~15;
  this->aligned_height_ = (this->actual_height_ + 15) & ~15;

  ESP_LOGI(TAG, "Video resolution: %dx%d (actual) -> %dx%d (aligned)",
           this->actual_width_, this->actual_height_,
           this->aligned_width_, this->aligned_height_);

  // Allocate RGB buffer with aligned dimensions
  this->rgb_buffer_size_ = this->aligned_width_ * this->aligned_height_ * 2;  // RGB565
  this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(64, this->rgb_buffer_size_,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->rgb_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate RGB buffer (%u bytes)", this->rgb_buffer_size_);
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "Allocated RGB buffer: %u bytes", this->rgb_buffer_size_);

  // Initialize appropriate decoder
  if (this->format_ == MediaFormat::MP4_H264) {
    // Parse MP4 file (this will extract resolution)
    if (!this->parse_mp4_()) {
      ESP_LOGE(TAG, "Failed to parse MP4 file");
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "MP4 parsed: %u video samples, %u audio samples",
             this->video_samples_.size(), this->audio_samples_.size());

    // Re-calculate dimensions if they were updated during parsing
    if (this->actual_width_ != this->aligned_width_ ||
        this->actual_height_ != ((this->aligned_height_ >> 4) << 4)) {
      int new_aligned_width = (this->actual_width_ + 15) & ~15;
      int new_aligned_height = (this->actual_height_ + 15) & ~15;

      if (new_aligned_width != this->aligned_width_ || new_aligned_height != this->aligned_height_) {
        this->aligned_width_ = new_aligned_width;
        this->aligned_height_ = new_aligned_height;

        ESP_LOGI(TAG, "Updated resolution after MP4 parsing: %dx%d (actual) -> %dx%d (aligned)",
                 this->actual_width_, this->actual_height_,
                 this->aligned_width_, this->aligned_height_);

        // Re-allocate RGB buffer with correct size
        heap_caps_free(this->rgb_buffer_);
        this->rgb_buffer_size_ = this->aligned_width_ * this->aligned_height_ * 2;
        this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(128, this->rgb_buffer_size_,
                                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (this->rgb_buffer_ == nullptr) {
          ESP_LOGE(TAG, "Failed to re-allocate RGB buffer (%u bytes)", this->rgb_buffer_size_);
          this->mark_failed();
          return;
        }
        ESP_LOGI(TAG, "Re-allocated RGB buffer: %u bytes", this->rgb_buffer_size_);
      }
    }

    // Initialize H.264 decoder
    if (!this->init_h264_decoder_()) {
      ESP_LOGE(TAG, "Failed to initialize H.264 decoder");
      this->mark_failed();
      return;
    }

    // Initialize audio decoder if speaker is configured
    if (this->speaker_ != nullptr && this->has_audio_) {
      if (!this->init_aac_decoder_()) {
        ESP_LOGW(TAG, "Failed to initialize audio decoder");
        // Continue without audio
      }
    }
  } else if (this->format_ == MediaFormat::MKV_H264) {
    // Parse MKV file (this will extract resolution)
    if (!this->parse_mkv_()) {
      ESP_LOGE(TAG, "Failed to parse MKV file");
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "MKV header parsed, video track: %u, audio track: %u",
             this->mkv_video_track_, this->mkv_audio_track_);

    // Parse clusters to build sample index
    if (!this->parse_mkv_clusters_()) {
      ESP_LOGE(TAG, "Failed to parse MKV clusters");
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "MKV clusters parsed: %u samples", this->mkv_samples_.size());

    // Re-calculate dimensions if they were updated during parsing
    if (this->actual_width_ != this->aligned_width_ ||
        this->actual_height_ != ((this->aligned_height_ >> 4) << 4)) {
      int new_aligned_width = (this->actual_width_ + 15) & ~15;
      int new_aligned_height = (this->actual_height_ + 15) & ~15;

      if (new_aligned_width != this->aligned_width_ || new_aligned_height != this->aligned_height_) {
        this->aligned_width_ = new_aligned_width;
        this->aligned_height_ = new_aligned_height;

        ESP_LOGI(TAG, "Updated resolution after MKV parsing: %dx%d (actual) -> %dx%d (aligned)",
                 this->actual_width_, this->actual_height_,
                 this->aligned_width_, this->aligned_height_);

        // Re-allocate RGB buffer with correct size
        heap_caps_free(this->rgb_buffer_);
        this->rgb_buffer_size_ = this->aligned_width_ * this->aligned_height_ * 2;
        this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(128, this->rgb_buffer_size_,
                                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (this->rgb_buffer_ == nullptr) {
          ESP_LOGE(TAG, "Failed to re-allocate RGB buffer (%u bytes)", this->rgb_buffer_size_);
          this->mark_failed();
          return;
        }
        ESP_LOGI(TAG, "Re-allocated RGB buffer: %u bytes", this->rgb_buffer_size_);
      }
    }

    // Initialize H.264 decoder
    if (!this->init_h264_decoder_()) {
      ESP_LOGE(TAG, "Failed to initialize H.264 decoder");
      this->mark_failed();
      return;
    }

    // Initialize audio decoder if speaker is configured
    if (this->speaker_ != nullptr && this->has_audio_) {
      if (!this->init_aac_decoder_()) {
        ESP_LOGW(TAG, "Failed to initialize audio decoder");
        // Continue without audio
      }
    }
  } else {
    // Initialize JPEG decoder
    if (!this->init_jpeg_decoder_()) {
      ESP_LOGE(TAG, "Failed to initialize JPEG decoder");
      this->mark_failed();
      return;
    }
  }

  // Create UI
  this->create_ui_();

  // Create playback timer
  this->playback_timer_ = lv_timer_create(timer_cb_, this->frame_interval_, this);
  lv_timer_pause(this->playback_timer_);

  if (this->auto_play_) {
    this->play();
  }

  ESP_LOGI(TAG, "Simple Video Player initialized");
}

// Complete initialization after HTTP download (called from loop())
void SimpleVideoPlayer::complete_video_initialization_() {
  // Detect format
  this->format_ = this->detect_format_();
  const char *format_str = "UNKNOWN";
  if (this->format_ == MediaFormat::MP4_H264) format_str = "MP4/H.264";
  else if (this->format_ == MediaFormat::MKV_H264) format_str = "MKV/H.264";
  else if (this->format_ == MediaFormat::MJPEG) format_str = "MJPEG";
  ESP_LOGI(TAG, "Detected format: %s", format_str);

  // Auto-detect resolution from video file
  if (this->format_ == MediaFormat::MJPEG) {
    if (this->detect_jpeg_resolution_(this->actual_width_, this->actual_height_)) {
      ESP_LOGI(TAG, "Auto-detected JPEG resolution: %dx%d", this->actual_width_, this->actual_height_);
    } else {
      ESP_LOGW(TAG, "Failed to auto-detect resolution, using configured: %dx%d", this->width_, this->height_);
      this->actual_width_ = this->width_;
      this->actual_height_ = this->height_;
    }

    // Auto-detect framerate from AVI header (if present and not overridden by user)
    if (!this->fps_override_) {
      if (!this->detect_avi_framerate_()) {
        ESP_LOGW(TAG, "Failed to detect AVI framerate, using default: 50 fps");
        // Keep default frame_interval_ = 20ms (50fps)
      }
    } else {
      ESP_LOGI(TAG, "Using user-configured framerate: %.2f fps (interval: %lu ms)",
               1000.0f / this->frame_interval_, (unsigned long)this->frame_interval_);
    }
  } else {
    // For MP4, use configured dimensions initially (will be updated during parsing)
    this->frame_interval_ = 20;
    this->actual_width_ = this->width_;
    this->actual_height_ = this->height_;
  }

  // Calculate 16-byte aligned dimensions for decoder
  this->aligned_width_ = (this->actual_width_ + 15) & ~15;
  this->aligned_height_ = (this->actual_height_ + 15) & ~15;

  ESP_LOGI(TAG, "Video resolution: %dx%d (actual) -> %dx%d (aligned)",
           this->actual_width_, this->actual_height_,
           this->aligned_width_, this->aligned_height_);

  // Allocate RGB buffer with aligned dimensions
  this->rgb_buffer_size_ = this->aligned_width_ * this->aligned_height_ * 2;  // RGB565
  this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(64, this->rgb_buffer_size_,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->rgb_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate RGB buffer (%u bytes)", this->rgb_buffer_size_);
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "Allocated RGB buffer: %u bytes", this->rgb_buffer_size_);

  // Initialize appropriate decoder
  if (this->format_ == MediaFormat::MP4_H264) {
    // Parse MP4 file (this will extract resolution)
    if (!this->parse_mp4_()) {
      ESP_LOGE(TAG, "Failed to parse MP4 file");
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "MP4 parsed: %u video samples, %u audio samples",
             this->video_samples_.size(), this->audio_samples_.size());

    // Re-calculate dimensions if they were updated during parsing
    if (this->actual_width_ != this->aligned_width_ ||
        this->actual_height_ != ((this->aligned_height_ >> 4) << 4)) {
      int new_aligned_width = (this->actual_width_ + 15) & ~15;
      int new_aligned_height = (this->actual_height_ + 15) & ~15;

      if (new_aligned_width != this->aligned_width_ || new_aligned_height != this->aligned_height_) {
        this->aligned_width_ = new_aligned_width;
        this->aligned_height_ = new_aligned_height;

        ESP_LOGI(TAG, "Updated resolution after MP4 parsing: %dx%d (actual) -> %dx%d (aligned)",
                 this->actual_width_, this->actual_height_,
                 this->aligned_width_, this->aligned_height_);

        // Re-allocate RGB buffer with correct size
        heap_caps_free(this->rgb_buffer_);
        this->rgb_buffer_size_ = this->aligned_width_ * this->aligned_height_ * 2;
        this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(128, this->rgb_buffer_size_,
                                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (this->rgb_buffer_ == nullptr) {
          ESP_LOGE(TAG, "Failed to re-allocate RGB buffer (%u bytes)", this->rgb_buffer_size_);
          this->mark_failed();
          return;
        }
        ESP_LOGI(TAG, "Re-allocated RGB buffer: %u bytes", this->rgb_buffer_size_);
      }
    }

    // Initialize H.264 decoder
    if (!this->init_h264_decoder_()) {
      ESP_LOGE(TAG, "Failed to initialize H.264 decoder");
      this->mark_failed();
      return;
    }

    // Initialize audio decoder if speaker is configured
    if (this->speaker_ != nullptr && this->has_audio_) {
      if (!this->init_aac_decoder_()) {
        ESP_LOGW(TAG, "Failed to initialize audio decoder");
        // Continue without audio
      }
    }
  } else if (this->format_ == MediaFormat::MKV_H264) {
    // Parse MKV file (this will extract resolution)
    if (!this->parse_mkv_()) {
      ESP_LOGE(TAG, "Failed to parse MKV file");
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "MKV header parsed, video track: %u, audio track: %u",
             this->mkv_video_track_, this->mkv_audio_track_);

    // Parse clusters to build sample index
    if (!this->parse_mkv_clusters_()) {
      ESP_LOGE(TAG, "Failed to parse MKV clusters");
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "MKV clusters parsed: %u samples", this->mkv_samples_.size());

    // Re-calculate dimensions if they were updated during parsing
    if (this->actual_width_ != this->aligned_width_ ||
        this->actual_height_ != ((this->aligned_height_ >> 4) << 4)) {
      int new_aligned_width = (this->actual_width_ + 15) & ~15;
      int new_aligned_height = (this->actual_height_ + 15) & ~15;

      if (new_aligned_width != this->aligned_width_ || new_aligned_height != this->aligned_height_) {
        this->aligned_width_ = new_aligned_width;
        this->aligned_height_ = new_aligned_height;

        ESP_LOGI(TAG, "Updated resolution after MKV parsing: %dx%d (actual) -> %dx%d (aligned)",
                 this->actual_width_, this->actual_height_,
                 this->aligned_width_, this->aligned_height_);

        // Re-allocate RGB buffer with correct size
        heap_caps_free(this->rgb_buffer_);
        this->rgb_buffer_size_ = this->aligned_width_ * this->aligned_height_ * 2;
        this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(128, this->rgb_buffer_size_,
                                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (this->rgb_buffer_ == nullptr) {
          ESP_LOGE(TAG, "Failed to re-allocate RGB buffer (%u bytes)", this->rgb_buffer_size_);
          this->mark_failed();
          return;
        }
        ESP_LOGI(TAG, "Re-allocated RGB buffer: %u bytes", this->rgb_buffer_size_);
      }
    }

    // Initialize H.264 decoder
    if (!this->init_h264_decoder_()) {
      ESP_LOGE(TAG, "Failed to initialize H.264 decoder");
      this->mark_failed();
      return;
    }

    // Initialize audio decoder if speaker is configured
    if (this->speaker_ != nullptr && this->has_audio_) {
      if (!this->init_aac_decoder_()) {
        ESP_LOGW(TAG, "Failed to initialize audio decoder");
        // Continue without audio
      }
    }
  } else {
    // Initialize JPEG decoder
    if (!this->init_jpeg_decoder_()) {
      ESP_LOGE(TAG, "Failed to initialize JPEG decoder");
      this->mark_failed();
      return;
    }
  }

  // Create UI
  this->create_ui_();

  // Create playback timer
  this->playback_timer_ = lv_timer_create(timer_cb_, this->frame_interval_, this);
  lv_timer_pause(this->playback_timer_);

  if (this->auto_play_) {
    this->play();
  }

  ESP_LOGI(TAG, "Video player fully initialized");
}

void SimpleVideoPlayer::loop() {
  // Handle HTTP download and delayed initialization
  if (this->http_download_pending_ && !this->initialization_complete_) {
#ifdef USE_WIFI
    // Check if WiFi is connected (non-blocking)
    if (wifi::global_wifi_component == nullptr) {
      ESP_LOGE(TAG, "WiFi component not available!");
      this->mark_failed();
      this->http_download_pending_ = false;
      return;
    }

    if (!wifi::global_wifi_component->is_connected()) {
      // WiFi not ready yet, just return and try again next loop
      static uint32_t last_log_time = 0;
      if (millis() - last_log_time > 5000) {
        ESP_LOGI(TAG, "Waiting for WiFi to connect before downloading video...");
        last_log_time = millis();
      }
      return;
    }

    // WiFi is connected, proceed with download
    ESP_LOGI(TAG, "WiFi connected! Starting HTTP download...");
#endif

    const char *url = this->file_path_.c_str();

    // Download file (this will still take time but won't wait for WiFi)
    if (!this->download_http_file_(url)) {
      ESP_LOGE(TAG, "Failed to download HTTP file");
      this->mark_failed();
      this->http_download_pending_ = false;
      return;
    }

    // Create FILE* from memory buffer
    this->file_ = fmemopen(this->http_buffer_, this->http_buffer_size_, "rb");
    if (this->file_ == nullptr) {
      ESP_LOGE(TAG, "Failed to create FILE* from HTTP buffer");
      heap_caps_free(this->http_buffer_);
      this->http_buffer_ = nullptr;
      this->mark_failed();
      this->http_download_pending_ = false;
      return;
    }

    this->file_size_ = this->http_buffer_size_;
    ESP_LOGI(TAG, "✓ HTTP video loaded into memory: %ld bytes", this->file_size_);

    // Now complete initialization (same code as setup() after open_video_file_())
    this->complete_video_initialization_();

    this->http_download_pending_ = false;
    this->initialization_complete_ = true;

    ESP_LOGI(TAG, "✓ HTTP video player fully initialized");

    // Auto-play if this was a re-download triggered by play()
    if (this->auto_play_after_download_) {
      ESP_LOGI(TAG, "Auto-starting playback after re-download");
      this->auto_play_after_download_ = false;
      this->play();  // Now play() will work since buffer is available
    }
  }

  // Main processing is done in LVGL timer callback
}

void SimpleVideoPlayer::dump_config() {
  ESP_LOGCONFIG(TAG, "Simple Video Player:");
  ESP_LOGCONFIG(TAG, "  File: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG, "  Configured resolution: %dx%d", this->width_, this->height_);
  if (this->actual_width_ > 0 && this->actual_height_ > 0) {
    ESP_LOGCONFIG(TAG, "  Detected resolution: %dx%d (aligned: %dx%d)",
                  this->actual_width_, this->actual_height_,
                  this->aligned_width_, this->aligned_height_);
  }
  const char *format_str = "UNKNOWN";
  if (this->format_ == MediaFormat::MP4_H264) format_str = "MP4/H.264";
  else if (this->format_ == MediaFormat::MKV_H264) format_str = "MKV/H.264";
  else if (this->format_ == MediaFormat::MJPEG) format_str = "MJPEG";
  ESP_LOGCONFIG(TAG, "  Format: %s", format_str);
  ESP_LOGCONFIG(TAG, "  Buffer size: %u bytes (RGB: %u bytes)", this->buffer_size_, this->rgb_buffer_size_);
  ESP_LOGCONFIG(TAG, "  Auto play: %s", this->auto_play_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Loop: %s", this->loop_ ? "yes" : "no");
  if (this->is_http_source_) {
    ESP_LOGCONFIG(TAG, "  Max HTTP file size: %zu MB", this->max_http_file_size_ / 1048576);
  }
}

// HTTP/HTTPS download helper
bool SimpleVideoPlayer::download_http_file_(const char *url) {
  ESP_LOGI(TAG, "Downloading from HTTP/HTTPS: %s", url);

  // Note: WiFi connection check is now done in loop() before calling this function
  // This function assumes WiFi is already connected

  // Configure HTTP client
  esp_http_client_config_t config = {};
  config.url = url;
  config.timeout_ms = 30000;  // 30 seconds timeout
  config.buffer_size = 4096;  // Read buffer size

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    return false;
  }

  // Open connection and get content length
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP connection failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);

  if (status_code != 200) {
    ESP_LOGE(TAG, "HTTP request failed with status %d", status_code);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  // Check maximum file size limit if Content-Length is provided
  if (content_length > 0) {
    if ((size_t)content_length > this->max_http_file_size_) {
      ESP_LOGE(TAG, "❌ HTTP file too large: %d bytes (%.2f MB)",
               content_length, content_length / 1048576.0f);
      ESP_LOGE(TAG, "   Maximum allowed: %zu bytes (%.2f MB)",
               this->max_http_file_size_, this->max_http_file_size_ / 1048576.0f);
      ESP_LOGE(TAG, "   Pour les grandes vidéos, utilisez un fichier local sur SD card!");
      ESP_LOGE(TAG, "   Ou augmentez 'max_http_file_size' dans votre YAML (risque de reboot!)");
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
  }

  if (content_length <= 0) {
    ESP_LOGW(TAG, "Content-Length not provided by server (got %d). Will download with dynamic buffering.", content_length);

    // Download without knowing size - use dynamic buffer
    std::vector<uint8_t> temp_buffer;
    temp_buffer.reserve(1024 * 1024);  // Reserve 1MB initially

    uint8_t chunk[4096];
    size_t total_downloaded = 0;

    while (true) {
      int read_len = esp_http_client_read(client, (char *)chunk, sizeof(chunk));
      if (read_len < 0) {
        ESP_LOGE(TAG, "HTTP read error");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
      }

      if (read_len == 0) {
        break;  // End of stream
      }

      // Append to buffer
      temp_buffer.insert(temp_buffer.end(), chunk, chunk + read_len);
      total_downloaded += read_len;

      // Check size limit during download
      if (total_downloaded > this->max_http_file_size_) {
        ESP_LOGE(TAG, "❌ Download exceeded maximum size: %zu bytes (%.2f MB)",
                 total_downloaded, total_downloaded / 1048576.0f);
        ESP_LOGE(TAG, "   Maximum allowed: %zu MB", this->max_http_file_size_ / 1048576);
        ESP_LOGE(TAG, "   Aborting download to prevent memory exhaustion!");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
      }

      // Log progress every 100KB
      if (total_downloaded % (100 * 1024) == 0 || total_downloaded < 4096) {
        ESP_LOGI(TAG, "Downloaded: %zu KB", total_downloaded / 1024);
      }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total_downloaded == 0) {
      ESP_LOGE(TAG, "No data downloaded from server");
      return false;
    }

    ESP_LOGI(TAG, "✓ Downloaded %zu bytes (%.2f MB) without Content-Length",
             total_downloaded, total_downloaded / 1048576.0f);

    // Allocate final buffer in SPIRAM
    this->http_buffer_ = (uint8_t *)heap_caps_malloc(total_downloaded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (this->http_buffer_ == nullptr) {
      ESP_LOGE(TAG, "Failed to allocate %zu bytes in SPIRAM", total_downloaded);
      return false;
    }

    // Copy data to SPIRAM
    memcpy(this->http_buffer_, temp_buffer.data(), total_downloaded);
    this->http_buffer_size_ = total_downloaded;

    ESP_LOGI(TAG, "✓ HTTP download complete: %zu bytes", total_downloaded);
    return true;
  }

  ESP_LOGI(TAG, "HTTP file size: %d bytes (%.2f MB)", content_length, content_length / 1048576.0f);

  // Allocate buffer in SPIRAM for the entire file
  this->http_buffer_ = (uint8_t *)heap_caps_malloc(content_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->http_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate %d bytes in SPIRAM for HTTP download", content_length);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  this->http_buffer_size_ = content_length;

  // Download file in chunks
  size_t downloaded = 0;
  uint8_t *write_ptr = this->http_buffer_;
  int last_progress = 0;

  while (downloaded < (size_t)content_length) {
    int read_len = esp_http_client_read(client, (char *)write_ptr, 4096);
    if (read_len < 0) {
      ESP_LOGE(TAG, "HTTP read error");
      heap_caps_free(this->http_buffer_);
      this->http_buffer_ = nullptr;
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }

    if (read_len == 0) {
      break;  // End of stream
    }

    downloaded += read_len;
    write_ptr += read_len;

    // Progress logging (every 10%)
    int progress = (downloaded * 100) / content_length;
    if (progress >= last_progress + 10) {
      ESP_LOGI(TAG, "Download progress: %d%% (%zu / %d bytes)", progress, downloaded, content_length);
      last_progress = progress;
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (downloaded != (size_t)content_length) {
    ESP_LOGW(TAG, "Downloaded %zu bytes, expected %d bytes", downloaded, content_length);
  }

  ESP_LOGI(TAG, "✓ HTTP download complete: %zu bytes", downloaded);
  return true;
}

bool SimpleVideoPlayer::open_video_file_() {
  // Cleanup previous HTTP buffer if any
  if (this->http_buffer_ != nullptr) {
    heap_caps_free(this->http_buffer_);
    this->http_buffer_ = nullptr;
    this->http_buffer_size_ = 0;
  }

  // Check if file_path is HTTP/HTTPS URL
  const char *path = this->file_path_.c_str();
  this->is_http_source_ = (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0);

  if (this->is_http_source_) {
    // HTTP/HTTPS: Mark for download in loop() (don't block setup())
    ESP_LOGI(TAG, "HTTP/HTTPS source detected: %s", path);
    ESP_LOGI(TAG, "Will download in loop() when WiFi is ready (non-blocking)");
    this->http_download_pending_ = true;
    return true;  // Return success, actual download will happen in loop()

  } else {
    // Local file: Use fopen
    ESP_LOGI(TAG, "Opening local file: %s", path);

    this->file_ = fopen(path, "rb");
    if (this->file_ == nullptr) {
      ESP_LOGE(TAG, "Failed to open file: %s", path);
      return false;
    }

    // Get file size
    fseek(this->file_, 0, SEEK_END);
    this->file_size_ = ftell(this->file_);
    fseek(this->file_, 0, SEEK_SET);

    ESP_LOGI(TAG, "✓ Local video file opened: %ld bytes", this->file_size_);
  }

  return true;
}

MediaFormat SimpleVideoPlayer::detect_format_() {
  if (this->file_ == nullptr) return MediaFormat::UNKNOWN;

  uint8_t header[8];
  if (fread(header, 1, 8, this->file_) != 8) {
    fseek(this->file_, 0, SEEK_SET);
    return MediaFormat::UNKNOWN;
  }
  fseek(this->file_, 0, SEEK_SET);

  // Check for Matroska/MKV EBML header (0x1A45DFA3)
  if (header[0] == 0x1A && header[1] == 0x45 && header[2] == 0xDF && header[3] == 0xA3) {
    return MediaFormat::MKV_H264;
  }

  // Check for MP4 box types
  uint32_t box_type = (header[4] << 24) | (header[5] << 16) | (header[6] << 8) | header[7];
  if (box_type == make_fourcc('f', 't', 'y', 'p') ||
      box_type == make_fourcc('m', 'o', 'o', 'v') ||
      box_type == make_fourcc('f', 'r', 'e', 'e') ||
      box_type == make_fourcc('m', 'd', 'a', 't')) {
    return MediaFormat::MP4_H264;
  }

  // Check for JPEG marker (FFD8)
  if (header[0] == 0xFF && header[1] == 0xD8) {
    return MediaFormat::MJPEG;
  }

  return MediaFormat::UNKNOWN;
}

bool SimpleVideoPlayer::detect_jpeg_resolution_(int &width, int &height) {
  if (this->file_ == nullptr) return false;

  // Save current file position
  long original_pos = ftell(this->file_);
  fseek(this->file_, 0, SEEK_SET);

  // Read first frame to get dimensions
  // Search for JPEG start marker (FFD8)
  int c1 = 0, c2 = 0;
  while ((c1 = fgetc(this->file_)) != EOF) {
    if (c1 == 0xFF) {
      c2 = fgetc(this->file_);
      if (c2 == 0xD8) {
        break;
      }
    }
  }

  if (c1 == EOF) {
    fseek(this->file_, original_pos, SEEK_SET);
    return false;
  }

  // Now parse JPEG markers to find SOF (Start of Frame)
  // SOF markers: FFC0-FFCF (we care about FFC0, FFC2 mainly)
  while (true) {
    // Find next marker
    c1 = fgetc(this->file_);
    if (c1 != 0xFF) continue;

    // Skip padding 0xFF bytes
    do {
      c2 = fgetc(this->file_);
    } while (c2 == 0xFF);

    if (c2 == EOF) break;

    // Check for SOF markers (C0-CF, except C4, C8, CC which are DHT, JPG, DAC)
    if ((c2 >= 0xC0 && c2 <= 0xC3) || (c2 >= 0xC5 && c2 <= 0xC7) ||
        (c2 >= 0xC9 && c2 <= 0xCB) || (c2 >= 0xCD && c2 <= 0xCF)) {
      // SOF marker found - read dimensions
      uint16_t length = read_be16(this->file_);
      uint8_t precision = fgetc(this->file_);
      height = read_be16(this->file_);
      width = read_be16(this->file_);

      // Restore file position
      fseek(this->file_, original_pos, SEEK_SET);
      return true;
    }

    // For other markers, skip their data
    if (c2 == 0xD8 || c2 == 0xD9 || c2 == 0x01 || (c2 >= 0xD0 && c2 <= 0xD7)) {
      // No length field for these markers
      continue;
    }

    // Read marker length and skip
    uint16_t marker_len = read_be16(this->file_);
    if (marker_len >= 2) {
      fseek(this->file_, marker_len - 2, SEEK_CUR);
    }

    // Safety check - don't parse too far
    if (ftell(this->file_) > 100000) break;
  }

  // Restore file position
  fseek(this->file_, original_pos, SEEK_SET);
  return false;
}

bool SimpleVideoPlayer::detect_avi_framerate_() {
  // This is now just a wrapper that calls parse_avi_header_()
  return this->parse_avi_header_();
}

bool SimpleVideoPlayer::parse_avi_header_() {
  if (this->file_ == nullptr) return false;

  // Save current file position
  long original_pos = ftell(this->file_);
  fseek(this->file_, 0, SEEK_SET);

  // Read AVI header
  uint8_t header[12];
  if (fread(header, 1, 12, this->file_) != 12) {
    fseek(this->file_, original_pos, SEEK_SET);
    this->is_avi_format_ = false;
    return false;
  }

  // Check for RIFF header
  if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F') {
    // Not an AVI file, might be raw MJPEG stream
    fseek(this->file_, original_pos, SEEK_SET);
    this->is_avi_format_ = false;
    ESP_LOGI(TAG, "Not an AVI file, treating as raw MJPEG stream");
    return false;
  }

  // Check for AVI marker
  if (header[8] != 'A' || header[9] != 'V' || header[10] != 'I' || header[11] != ' ') {
    fseek(this->file_, original_pos, SEEK_SET);
    this->is_avi_format_ = false;
    return false;
  }

  this->is_avi_format_ = true;
  ESP_LOGI(TAG, "AVI container detected, parsing header...");

  // Parse LIST hdrl to find avih (AVI main header)
  uint8_t list_header[12];
  if (fread(list_header, 1, 12, this->file_) != 12) {
    fseek(this->file_, original_pos, SEEK_SET);
    return false;
  }

  // Check for LIST hdrl
  if (list_header[0] != 'L' || list_header[1] != 'I' || list_header[2] != 'S' || list_header[3] != 'T') {
    fseek(this->file_, original_pos, SEEK_SET);
    return false;
  }

  if (list_header[8] != 'h' || list_header[9] != 'd' || list_header[10] != 'r' || list_header[11] != 'l') {
    fseek(this->file_, original_pos, SEEK_SET);
    return false;
  }

  // Get hdrl list size (bytes 4-7, little-endian)
  uint32_t hdrl_size = list_header[4] | (list_header[5] << 8) |
                       (list_header[6] << 16) | (list_header[7] << 24);

  // Read avih chunk header
  uint8_t avih_header[8];
  if (fread(avih_header, 1, 8, this->file_) != 8) {
    fseek(this->file_, original_pos, SEEK_SET);
    return false;
  }

  // Check for avih marker
  if (avih_header[0] != 'a' || avih_header[1] != 'v' || avih_header[2] != 'i' || avih_header[3] != 'h') {
    fseek(this->file_, original_pos, SEEK_SET);
    return false;
  }

  // Read avih data (56 bytes total)
  uint8_t avih_data[56];
  if (fread(avih_data, 1, 56, this->file_) != 56) {
    fseek(this->file_, original_pos, SEEK_SET);
    return false;
  }

  // Extract microseconds per frame (bytes 0-3, little-endian)
  uint32_t us_per_frame = avih_data[0] | (avih_data[1] << 8) |
                          (avih_data[2] << 16) | (avih_data[3] << 24);

  // Extract total frames (bytes 16-19, little-endian)
  this->avi_total_frames_ = avih_data[16] | (avih_data[17] << 8) |
                            (avih_data[18] << 16) | (avih_data[19] << 24);

  // AVI uses little-endian format, ESP32 is also little-endian
  if (us_per_frame > 0 && us_per_frame < 1000000) {  // Sanity check (1-1000 fps)
    // Calculate framerate and frame interval
    float fps = 1000000.0f / us_per_frame;
    this->frame_interval_ = us_per_frame / 1000;  // Convert microseconds to milliseconds

    ESP_LOGI(TAG, "AVI: framerate=%.2f fps, total_frames=%u",
             fps, this->avi_total_frames_);
  }

  // Now find the movi list offset
  // Skip to end of hdrl list (we're currently past avih, need to skip rest of hdrl)
  // Current position: 12 (RIFF+AVI) + 12 (LIST hdrl) + 8 (avih header) + 56 (avih data) = 88
  // hdrl ends at: 12 (RIFF+AVI) + 8 (LIST header) + hdrl_size
  long hdrl_end = 12 + 8 + hdrl_size;
  fseek(this->file_, hdrl_end, SEEK_SET);

  // Search for LIST movi
  while (!feof(this->file_)) {
    uint8_t chunk_header[8];
    if (fread(chunk_header, 1, 8, this->file_) != 8) break;

    // Check for LIST
    if (chunk_header[0] == 'L' && chunk_header[1] == 'I' &&
        chunk_header[2] == 'S' && chunk_header[3] == 'T') {

      // Read list type (4 bytes after size)
      uint8_t list_type[4];
      if (fread(list_type, 1, 4, this->file_) != 4) break;

      // Check for movi
      if (list_type[0] == 'm' && list_type[1] == 'o' &&
          list_type[2] == 'v' && list_type[3] == 'i') {
        this->avi_movi_offset_ = ftell(this->file_);
        ESP_LOGI(TAG, "AVI: movi list found at offset %ld", this->avi_movi_offset_);
        fseek(this->file_, original_pos, SEEK_SET);
        return true;
      }

      // Not movi, skip this list
      uint32_t list_size = chunk_header[4] | (chunk_header[5] << 8) |
                           (chunk_header[6] << 16) | (chunk_header[7] << 24);
      fseek(this->file_, list_size - 4, SEEK_CUR);  // -4 because we already read list type
    } else {
      // Not a LIST chunk, skip it
      uint32_t chunk_size = chunk_header[4] | (chunk_header[5] << 8) |
                            (chunk_header[6] << 16) | (chunk_header[7] << 24);
      fseek(this->file_, chunk_size, SEEK_CUR);
    }
  }

  ESP_LOGW(TAG, "AVI: movi list not found!");
  fseek(this->file_, original_pos, SEEK_SET);
  return false;
}

bool SimpleVideoPlayer::extract_mp4_resolution_() {
  // Resolution will be extracted during avc1 parsing
  // This is called after MP4 parsing completes to update dimensions
  if (this->actual_width_ > 0 && this->actual_height_ > 0) {
    return true;
  }
  return false;
}

// ==============================================
// JPEG/MJPEG DECODER
// ==============================================

bool SimpleVideoPlayer::init_jpeg_decoder_() {
  jpeg_decode_engine_cfg_t cfg = {
    .intr_priority = 0,
    .timeout_ms = 20,
  };

  esp_err_t ret = jpeg_new_decoder_engine(&cfg, &this->jpeg_decoder_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create JPEG decoder: %s", esp_err_to_name(ret));
    return false;
  }

  return true;
}

bool SimpleVideoPlayer::read_next_mjpeg_frame_() {
  if (this->file_ == nullptr) {
    return false;
  }

  if (this->is_avi_format_) {
    // AVI format: Read chunks with FourCC headers
    // First frame: seek to movi offset
    if (this->frame_count_ == 0 && this->avi_movi_offset_ > 0) {
      fseek(this->file_, this->avi_movi_offset_, SEEK_SET);
    }

    // Read chunk header (8 bytes: 4-byte FourCC + 4-byte size)
    while (!feof(this->file_)) {
      uint8_t chunk_header[8];
      if (fread(chunk_header, 1, 8, this->file_) != 8) {
        // End of file
        if (this->loop_) {
          fseek(this->file_, this->avi_movi_offset_, SEEK_SET);
          this->frame_count_ = 0;
          return this->read_next_mjpeg_frame_();
        }
        return false;
      }

      // Get chunk size (little-endian)
      uint32_t chunk_size = chunk_header[4] | (chunk_header[5] << 8) |
                            (chunk_header[6] << 16) | (chunk_header[7] << 24);

      // Check if this is a video chunk (00dc, 01dc, etc.)
      // FourCC format: stream_id (2 digits) + 'dc' for video
      bool is_video_chunk = (chunk_header[2] == 'd' && chunk_header[3] == 'c');

      if (is_video_chunk && chunk_size > 0 && chunk_size < this->buffer_size_) {
        // Read JPEG data
        size_t bytes_read = fread(this->input_buffer_, 1, chunk_size, this->file_);
        if (bytes_read != chunk_size) {
          ESP_LOGW(TAG, "AVI: Failed to read video chunk: got %u, expected %u", bytes_read, chunk_size);
          return false;
        }

        this->input_size_ = chunk_size;
        this->frame_count_++;

        // AVI chunks are word-aligned (2 bytes), skip padding byte if needed
        if (chunk_size % 2 != 0) {
          fgetc(this->file_);
        }

        return true;
      } else {
        // Skip this chunk (audio, index, or too large)
        fseek(this->file_, chunk_size + (chunk_size % 2), SEEK_CUR);
      }
    }

    // EOF reached
    if (this->loop_) {
      fseek(this->file_, this->avi_movi_offset_, SEEK_SET);
      this->frame_count_ = 0;
      return this->read_next_mjpeg_frame_();
    }
    return false;

  } else {
    // Raw MJPEG stream: Search for JPEG markers byte-by-byte
    int c1 = 0, c2 = 0;
    while ((c1 = fgetc(this->file_)) != EOF) {
      if (c1 == 0xFF) {
        c2 = fgetc(this->file_);
        if (c2 == 0xD8) {
          break;
        }
      }
    }

    if (c1 == EOF) {
      // End of file
      if (this->loop_) {
        fseek(this->file_, 0, SEEK_SET);
        this->frame_count_ = 0;
        return this->read_next_mjpeg_frame_();
      }
      return false;
    }

    // Start of JPEG found
    this->input_buffer_[0] = 0xFF;
    this->input_buffer_[1] = 0xD8;
    this->input_size_ = 2;

    // Read until end marker (FFD9)
    while (this->input_size_ < this->buffer_size_ - 1) {
      c1 = fgetc(this->file_);
      if (c1 == EOF) {
        break;
      }
      this->input_buffer_[this->input_size_++] = c1;

      if (c1 == 0xFF) {
        c2 = fgetc(this->file_);
        if (c2 == EOF) {
          break;
        }
        this->input_buffer_[this->input_size_++] = c2;
        if (c2 == 0xD9) {
          // End of JPEG
          break;
        }
      }
    }

    this->current_pos_ = ftell(this->file_);
    this->frame_count_++;

    return this->input_size_ > 2;
  }
}

bool SimpleVideoPlayer::decode_mjpeg_frame_() {
  if (this->input_size_ == 0 || this->jpeg_decoder_ == nullptr) {
    return false;
  }

  jpeg_decode_cfg_t decode_cfg = {
    .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
    .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
  };

  uint32_t out_size = 0;
  esp_err_t ret = jpeg_decoder_process(this->jpeg_decoder_, &decode_cfg,
                                        this->input_buffer_, this->input_size_,
                                        this->rgb_buffer_, this->rgb_buffer_size_,
                                        &out_size);

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "JPEG decode failed: %s", esp_err_to_name(ret));
    return false;
  }

  return true;
}

// ==============================================
// H.264/MP4 DECODER
// ==============================================

bool SimpleVideoPlayer::init_h264_decoder_() {
  // Create software H.264 decoder
  esp_h264_dec_cfg_sw_t cfg = {
    .pic_type = ESP_H264_RAW_FMT_I420
  };

  esp_h264_err_t err = esp_h264_dec_sw_new(&cfg, &this->h264_decoder_);
  if (err != ESP_H264_ERR_OK || this->h264_decoder_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create H.264 decoder: err=%d", err);
    return false;
  }

  // Open the decoder
  err = esp_h264_dec_open(this->h264_decoder_);
  if (err != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open H.264 decoder: err=%d", err);
    esp_h264_dec_del(this->h264_decoder_);
    this->h264_decoder_ = nullptr;
    return false;
  }

  // Allocate YUV buffer for decoded frames (using actual dimensions)
  size_t yuv_size = this->actual_width_ * this->actual_height_ * 3 / 2;  // I420
  this->yuv_buffer_.resize(yuv_size);

  // Initialize optimized YUV→RGB converter (BT.601 by default - most compatible)
  // Use BT.709 only if you're sure your videos are HD with BT.709 colorspace
  if (this->yuv_converter_ == nullptr) {
    this->yuv_converter_ = new YuvRgbConverter(YuvRgbConverter::Colorspace::BT601);
  }

  this->h264_decoder_ready_ = true;
  ESP_LOGI(TAG, "H.264 decoder initialized for %dx%d", this->actual_width_, this->actual_height_);

  return true;
}

bool SimpleVideoPlayer::parse_mp4_() {
  if (this->file_ == nullptr) return false;

  fseek(this->file_, 0, SEEK_SET);

  while (!feof(this->file_)) {
    uint32_t size, type;
    if (!this->read_mp4_box_(size, type)) break;

    if (type == make_fourcc('m', 'o', 'o', 'v')) {
      if (!this->parse_moov_(size - 8)) return false;
    } else {
      // Skip other boxes
      if (size > 8) {
        fseek(this->file_, size - 8, SEEK_CUR);
      }
    }
  }

  return !this->video_samples_.empty();
}

bool SimpleVideoPlayer::read_mp4_box_(uint32_t &size, uint32_t &type) {
  long pos = ftell(this->file_);
  size = read_be32(this->file_);
  type = read_be32(this->file_);

  if (size == 0 || feof(this->file_)) {
    ESP_LOGD(TAG, "read_mp4_box_ failed at pos=%ld: size=%u, eof=%d", pos, size, feof(this->file_));
    return false;
  }

  return true;
}

bool SimpleVideoPlayer::parse_moov_(uint32_t size) {
  long end_pos = ftell(this->file_) + size;

  while (ftell(this->file_) < end_pos) {
    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) break;

    if (box_type == make_fourcc('t', 'r', 'a', 'k')) {
      // We'll determine if it's video or audio inside parse_trak_
      long trak_start = ftell(this->file_);
      this->parse_trak_(box_size - 8, true);  // Try as video first
      fseek(this->file_, trak_start + box_size - 8, SEEK_SET);
    } else {
      if (box_size > 8) {
        fseek(this->file_, box_size - 8, SEEK_CUR);
      }
    }
  }

  return true;
}

bool SimpleVideoPlayer::parse_trak_(uint32_t size, bool is_video) {
  ESP_LOGD(TAG, "Parsing trak (is_video=%d)", is_video);
  long end_pos = ftell(this->file_) + size;

  while (ftell(this->file_) < end_pos) {
    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) break;

    if (box_type == make_fourcc('m', 'd', 'i', 'a')) {
      if (!this->parse_mdia_(box_size - 8, is_video)) return false;
    } else {
      if (box_size > 8) {
        fseek(this->file_, box_size - 8, SEEK_CUR);
      }
    }
  }

  return true;
}

bool SimpleVideoPlayer::parse_mdia_(uint32_t size, bool is_video) {
  long end_pos = ftell(this->file_) + size;

  while (ftell(this->file_) < end_pos) {
    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) break;

    if (box_type == make_fourcc('m', 'i', 'n', 'f')) {
      if (!this->parse_minf_(box_size - 8, is_video)) return false;
    } else if (box_type == make_fourcc('m', 'd', 'h', 'd')) {
      // Media header - get timescale
      fseek(this->file_, 12, SEEK_CUR);  // Skip version/flags and times
      uint32_t timescale = read_be32(this->file_);
      if (is_video) {
        this->video_timescale_ = timescale;
      } else {
        this->audio_timescale_ = timescale;
      }
      fseek(this->file_, box_size - 8 - 16, SEEK_CUR);
    } else {
      if (box_size > 8) {
        fseek(this->file_, box_size - 8, SEEK_CUR);
      }
    }
  }

  return true;
}

bool SimpleVideoPlayer::parse_minf_(uint32_t size, bool is_video) {
  long end_pos = ftell(this->file_) + size;

  while (ftell(this->file_) < end_pos) {
    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) break;

    if (box_type == make_fourcc('s', 't', 'b', 'l')) {
      if (!this->parse_stbl_(box_size - 8, is_video)) return false;
    } else {
      if (box_size > 8) {
        fseek(this->file_, box_size - 8, SEEK_CUR);
      }
    }
  }

  return true;
}

bool SimpleVideoPlayer::parse_stbl_(uint32_t size, bool is_video) {
  ESP_LOGD(TAG, "Parsing stbl (is_video=%d, size=%u)", is_video, size);
  long start_pos = ftell(this->file_);
  long end_pos = start_pos + size;

  // First pass - collect sample info
  std::vector<uint32_t> sample_sizes;
  std::vector<uint32_t> chunk_offsets;
  std::vector<uint32_t> sample_durations;
  std::vector<uint32_t> keyframes;

  // Sample-to-Chunk mapping (stsc)
  struct SampleToChunk {
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
    uint32_t sample_description_index;
  };
  std::vector<SampleToChunk> sample_to_chunk;

  while (ftell(this->file_) < end_pos) {
    long current_pos = ftell(this->file_);

    // Safety check - don't go past end
    if (current_pos >= end_pos) {
      ESP_LOGD(TAG, "  Reached end of stbl box");
      break;
    }

    ESP_LOGD(TAG, "  Loop iteration: pos=%ld, end_pos=%ld", current_pos, end_pos);

    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) {
      ESP_LOGD(TAG, "  read_mp4_box_ failed, breaking loop");
      break;
    }

    // Sanity check box size
    if (box_size < 8 || box_size > (end_pos - current_pos)) {
      ESP_LOGW(TAG, "  Invalid box size %u at pos %ld, breaking", box_size, current_pos);
      break;
    }

    // Log box type as 4-char string for debugging
    char fourcc[5] = {0};
    fourcc[0] = (box_type >> 24) & 0xFF;
    fourcc[1] = (box_type >> 16) & 0xFF;
    fourcc[2] = (box_type >> 8) & 0xFF;
    fourcc[3] = box_type & 0xFF;
    ESP_LOGD(TAG, "  stbl box: '%s' size=%u at pos=%ld", fourcc, box_size, current_pos);

    if (box_type == make_fourcc('s', 't', 's', 'd')) {
      this->parse_stsd_(box_size - 8, is_video);
    } else if (box_type == make_fourcc('s', 't', 's', 'z')) {
      // Sample sizes
      ESP_LOGD(TAG, "  Reading stsz...");
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t sample_size = read_be32(this->file_);
      uint32_t count = read_be32(this->file_);

      if (sample_size == 0) {
        sample_sizes.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
          sample_sizes.push_back(read_be32(this->file_));
        }
      } else {
        sample_sizes.assign(count, sample_size);
      }
      ESP_LOGD(TAG, "  stsz: %u samples", sample_sizes.size());
    } else if (box_type == make_fourcc('s', 't', 'c', 'o')) {
      // Chunk offsets
      ESP_LOGD(TAG, "  Reading stco...");
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t count = read_be32(this->file_);
      chunk_offsets.reserve(count);
      for (uint32_t i = 0; i < count; i++) {
        chunk_offsets.push_back(read_be32(this->file_));
      }
      ESP_LOGD(TAG, "  stco: %u offsets", chunk_offsets.size());
    } else if (box_type == make_fourcc('s', 't', 't', 's')) {
      // Sample durations
      ESP_LOGD(TAG, "  Reading stts...");
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t count = read_be32(this->file_);
      for (uint32_t i = 0; i < count; i++) {
        uint32_t sample_count = read_be32(this->file_);
        uint32_t duration = read_be32(this->file_);
        for (uint32_t j = 0; j < sample_count; j++) {
          sample_durations.push_back(duration);
        }
      }
      ESP_LOGD(TAG, "  stts: %u durations", sample_durations.size());
    } else if (box_type == make_fourcc('s', 't', 's', 's')) {
      // Sync samples (keyframes)
      ESP_LOGD(TAG, "  Reading stss...");
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t count = read_be32(this->file_);
      keyframes.reserve(count);
      for (uint32_t i = 0; i < count; i++) {
        keyframes.push_back(read_be32(this->file_));
      }
      ESP_LOGD(TAG, "  stss: %u keyframes", keyframes.size());
    } else if (box_type == make_fourcc('s', 't', 's', 'c')) {
      // Sample-to-Chunk table
      ESP_LOGI(TAG, "  Reading stsc (Sample-to-Chunk)...");
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t count = read_be32(this->file_);
      sample_to_chunk.reserve(count);
      ESP_LOGI(TAG, "  stsc has %u entries:", count);
      for (uint32_t i = 0; i < count; i++) {
        SampleToChunk entry;
        entry.first_chunk = read_be32(this->file_);
        entry.samples_per_chunk = read_be32(this->file_);
        entry.sample_description_index = read_be32(this->file_);
        sample_to_chunk.push_back(entry);
        ESP_LOGI(TAG, "    Entry %u: first_chunk=%u, samples_per_chunk=%u",
                 i, entry.first_chunk, entry.samples_per_chunk);
      }
    }

    // Position to next box
    long next_box_pos = current_pos + box_size;
    fseek(this->file_, next_box_pos, SEEK_SET);
    ESP_LOGD(TAG, "  Positioned to next box at %ld", next_box_pos);
  }

  // Build sample list using stsc (Sample-to-Chunk) table
  if (is_video && !sample_sizes.empty()) {
    ESP_LOGI(TAG, "Building video samples: sizes=%u, chunks=%u, durations=%u, keyframes=%u, stsc_entries=%u",
             sample_sizes.size(), chunk_offsets.size(), sample_durations.size(), keyframes.size(), sample_to_chunk.size());

    // If no stsc, assume 1 sample per chunk (fallback)
    if (sample_to_chunk.empty()) {
      ESP_LOGW(TAG, "No stsc table found, assuming 1 sample per chunk");
      sample_to_chunk.push_back({1, 1, 1});
    }

    // Calculate sample offsets using stsc table
    uint32_t sample_index = 0;
    uint32_t timestamp = 0;

    // Log chunk-to-sample mapping for first few chunks (diagnostic)
    bool log_chunks = chunk_offsets.size() <= 10;

    for (size_t chunk_idx = 0; chunk_idx < chunk_offsets.size() && sample_index < sample_sizes.size(); chunk_idx++) {
      uint32_t chunk_number = chunk_idx + 1;  // Chunks are 1-indexed in MP4
      uint32_t chunk_offset = chunk_offsets[chunk_idx];

      // Find how many samples are in this chunk
      uint32_t samples_in_this_chunk = 1;  // Default
      for (size_t stsc_idx = 0; stsc_idx < sample_to_chunk.size(); stsc_idx++) {
        if (chunk_number >= sample_to_chunk[stsc_idx].first_chunk) {
          // Check if this is the last entry or if next entry starts later
          if (stsc_idx + 1 >= sample_to_chunk.size() ||
              chunk_number < sample_to_chunk[stsc_idx + 1].first_chunk) {
            samples_in_this_chunk = sample_to_chunk[stsc_idx].samples_per_chunk;
            break;
          }
        }
      }

      if (log_chunks) {
        ESP_LOGI(TAG, "Chunk %u: offset=%u, samples_in_chunk=%u, starting_sample_idx=%u",
                 chunk_number, chunk_offset, samples_in_this_chunk, sample_index);
      }

      // Create samples for this chunk
      uint32_t sample_offset_in_chunk = chunk_offset;
      uint32_t samples_created_in_chunk = 0;
      for (uint32_t j = 0; j < samples_in_this_chunk && sample_index < sample_sizes.size(); j++) {
        Mp4Sample sample;
        sample.offset = sample_offset_in_chunk;
        sample.size = sample_sizes[sample_index];
        sample.duration = (sample_index < sample_durations.size()) ? sample_durations[sample_index] : 1000;
        sample.timestamp_ms = (timestamp * 1000) / this->video_timescale_;
        sample.is_keyframe = keyframes.empty() ||
                            std::find(keyframes.begin(), keyframes.end(), sample_index + 1) != keyframes.end();

        this->video_samples_.push_back(sample);

        // Next sample in this chunk starts after current sample
        sample_offset_in_chunk += sample.size;
        timestamp += sample.duration;
        sample_index++;
        samples_created_in_chunk++;
      }

      if (log_chunks && samples_created_in_chunk != samples_in_this_chunk) {
        ESP_LOGW(TAG, "  Warning: Created %u samples but expected %u (hit end of sample_sizes)",
                 samples_created_in_chunk, samples_in_this_chunk);
      }
    }
    this->total_frames_ = this->video_samples_.size();

    // Verify all samples were created
    if (sample_index < sample_sizes.size()) {
      ESP_LOGW(TAG, "⚠️  WARNING: Only created %u samples out of %u total!",
               sample_index, sample_sizes.size());
      ESP_LOGW(TAG, "   This means some samples are missing (chunks exhausted before samples)");
    } else {
      ESP_LOGI(TAG, "✅ Successfully created all %u samples from %zu chunks",
               sample_index, chunk_offsets.size());
    }

    // Diagnostic: Log first 10 sample offsets
    ESP_LOGI(TAG, "First 10 samples after stsc processing:");
    for (size_t i = 0; i < this->video_samples_.size() && i < 10; i++) {
      ESP_LOGI(TAG, "  Sample %zu: offset=%u, size=%u, keyframe=%d",
               i, this->video_samples_[i].offset, this->video_samples_[i].size,
               this->video_samples_[i].is_keyframe);
    }

    // Calculate total duration
    if (!this->video_samples_.empty()) {
      Mp4Sample &last_sample = this->video_samples_.back();
      this->total_duration_ms_ = last_sample.timestamp_ms + (last_sample.duration * 1000 / this->video_timescale_);
      ESP_LOGI(TAG, "Total video duration: %lu ms (%lu:%02lu)",
               (unsigned long)this->total_duration_ms_,
               (unsigned long)(this->total_duration_ms_ / 60000),
               (unsigned long)((this->total_duration_ms_ / 1000) % 60));

      // Calculate average framerate and adjust playback timer interval
      // This ensures smooth playback matching the video's actual framerate
      if (!this->fps_override_ && this->total_duration_ms_ > 0 && this->video_samples_.size() > 0) {
        float actual_fps = (this->video_samples_.size() * 1000.0f) / this->total_duration_ms_;
        this->frame_interval_ = (uint32_t)(1000.0f / actual_fps);

        ESP_LOGI(TAG, "Detected framerate: %.2f fps, base timer interval: %lu ms",
                 actual_fps, (unsigned long)this->frame_interval_);
      } else if (this->fps_override_) {
        ESP_LOGI(TAG, "Using user-configured framerate (overriding auto-detection)");
      }
    }

    ESP_LOGI(TAG, "Created %u video samples", this->video_samples_.size());
  } else if (is_video) {
    ESP_LOGW(TAG, "No video samples created: sample_sizes empty=%d", sample_sizes.empty());
  }

  return true;
}

bool SimpleVideoPlayer::parse_stsd_(uint32_t size, bool is_video) {
  fseek(this->file_, 4, SEEK_CUR);  // version/flags
  uint32_t entry_count = read_be32(this->file_);

  for (uint32_t i = 0; i < entry_count; i++) {
    uint32_t entry_size = read_be32(this->file_);
    uint32_t format = read_be32(this->file_);

    if (format == make_fourcc('a', 'v', 'c', '1')) {
      this->parse_avc1_(entry_size - 8);
    } else if (format == make_fourcc('m', 'p', '4', 'a')) {
      this->parse_mp4a_(entry_size - 8);
    } else {
      if (entry_size > 8) {
        fseek(this->file_, entry_size - 8, SEEK_CUR);
      }
    }
  }

  return true;
}

bool SimpleVideoPlayer::parse_avc1_(uint32_t size) {
  long start_pos = ftell(this->file_);
  long end_pos = start_pos + size;

  // Skip: 6 bytes reserved + 2 bytes data_reference_index + 16 bytes video pre-defined
  fseek(this->file_, 24, SEEK_CUR);

  // Read width and height (2 bytes each, big-endian)
  uint16_t vid_width = read_be16(this->file_);
  uint16_t vid_height = read_be16(this->file_);

  // Update actual dimensions if not already set
  if (this->actual_width_ == 0 || this->actual_width_ == this->width_) {
    this->actual_width_ = vid_width;
    this->actual_height_ = vid_height;
    ESP_LOGI(TAG, "Extracted video dimensions from avc1: %dx%d", vid_width, vid_height);
  }

  // Skip remaining fields to get to child boxes (78 - 24 - 4 = 50 bytes)
  fseek(this->file_, 50, SEEK_CUR);
  clearerr(this->file_);  // Clear any flags from previous operations

  // Parse child boxes to find avcC
  while (ftell(this->file_) < end_pos && !feof(this->file_)) {
    long current_pos = ftell(this->file_);
    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) {
      clearerr(this->file_);  // Clear EOF flag immediately
      break;
    }

    if (box_type == make_fourcc('a', 'v', 'c', 'C')) {
      this->parse_avcc_(box_size - 8);
      break;  // We found avcC, no need to continue
    } else {
      if (box_size > 8 && box_size < 1000000) {  // Sanity check
        long next_pos = current_pos + box_size;
        if (next_pos <= end_pos) {
          fseek(this->file_, next_pos, SEEK_SET);
        } else {
          break;
        }
      } else {
        break;
      }
    }
  }

  // Ensure we're at the end of this box
  fseek(this->file_, end_pos, SEEK_SET);
  clearerr(this->file_);  // Clear any EOF/error flags

  return true;
}

bool SimpleVideoPlayer::parse_avcc_(uint32_t size) {
  fseek(this->file_, 4, SEEK_CUR);  // configurationVersion, profile, compatibility, level

  uint8_t len_size_minus_one;
  fread(&len_size_minus_one, 1, 1, this->file_);
  this->nal_length_size_ = (len_size_minus_one & 0x03) + 1;

  // Read SPS
  uint8_t num_sps;
  fread(&num_sps, 1, 1, this->file_);
  num_sps &= 0x1F;

  for (int i = 0; i < num_sps; i++) {
    uint16_t sps_len = read_be16(this->file_);
    this->sps_.resize(sps_len);
    fread(this->sps_.data(), 1, sps_len, this->file_);
  }

  // Read PPS
  uint8_t num_pps;
  fread(&num_pps, 1, 1, this->file_);

  for (int i = 0; i < num_pps; i++) {
    uint16_t pps_len = read_be16(this->file_);
    this->pps_.resize(pps_len);
    fread(this->pps_.data(), 1, pps_len, this->file_);
  }

  ESP_LOGI(TAG, "avcC: NAL length size=%d, SPS=%d bytes, PPS=%d bytes",
           this->nal_length_size_, this->sps_.size(), this->pps_.size());

  return true;
}

bool SimpleVideoPlayer::parse_mp4a_(uint32_t size) {
  long start_pos = ftell(this->file_);
  long end_pos = start_pos + size;

  // Skip to esds
  fseek(this->file_, 28, SEEK_CUR);  // Skip fixed mp4a header
  clearerr(this->file_);  // Clear any flags from previous operations

  while (ftell(this->file_) < end_pos && !feof(this->file_)) {
    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) {
      clearerr(this->file_);  // Clear EOF flag immediately
      break;
    }

    if (box_type == make_fourcc('e', 's', 'd', 's')) {
      this->parse_esds_(box_size - 8);
      break;  // We found esds, no need to continue
    } else {
      if (box_size > 8 && box_size < 1000000) {  // Sanity check
        fseek(this->file_, box_size - 8, SEEK_CUR);
      }
    }
  }

  // Ensure we're at the end of this box
  fseek(this->file_, end_pos, SEEK_SET);
  clearerr(this->file_);  // Clear any EOF/error flags

  return true;
}

bool SimpleVideoPlayer::parse_esds_(uint32_t size) {
  long start_pos = ftell(this->file_);
  long end_pos = start_pos + size;

  // Skip version/flags
  fseek(this->file_, 4, SEEK_CUR);

  // Parse ES descriptor
  while (ftell(this->file_) < end_pos) {
    uint8_t tag;
    if (fread(&tag, 1, 1, this->file_) != 1) break;

    // Read descriptor length (variable length encoding)
    uint32_t len = 0;
    uint8_t b;
    do {
      if (fread(&b, 1, 1, this->file_) != 1) break;
      len = (len << 7) | (b & 0x7F);
    } while (b & 0x80);

    if (tag == 0x05) {  // DecoderSpecificInfo
      // This is the AAC config
      this->audio_config_.resize(len);
      fread(this->audio_config_.data(), 1, len, this->file_);
      this->has_audio_ = true;
      ESP_LOGI(TAG, "Found AAC config: %d bytes", len);
      break;
    } else {
      // Skip other descriptors (but parse nested ones)
      if (tag == 0x03 || tag == 0x04) {
        // ES_Descriptor or DecoderConfigDescriptor - continue parsing
        if (tag == 0x03) {
          fseek(this->file_, 3, SEEK_CUR);  // Skip ES_ID and flags
        } else if (tag == 0x04) {
          fseek(this->file_, 13, SEEK_CUR);  // Skip decoder config
        }
      } else {
        fseek(this->file_, len, SEEK_CUR);
      }
    }
  }

  fseek(this->file_, end_pos, SEEK_SET);
  return true;
}

// ==============================================
// AUDIO DECODER
// ==============================================

bool SimpleVideoPlayer::init_aac_decoder_() {
#if USE_ESP_AUDIO_CODEC
  if (this->speaker_ == nullptr || !this->has_audio_) {
    return false;
  }

  // Register AAC decoder
  esp_aac_dec_register();

  // Configure AAC decoder
  esp_aac_dec_cfg_t aac_cfg = {
    .aac_plus_enable = true,
  };

  esp_audio_dec_cfg_t dec_cfg = {
    .type = ESP_AUDIO_TYPE_AAC,
    .cfg = &aac_cfg,
    .cfg_sz = sizeof(aac_cfg),
  };

  // Create decoder instance
  esp_audio_err_t ret = esp_audio_dec_open(&dec_cfg, &this->aac_decoder_);
  if (ret != ESP_AUDIO_ERR_OK || this->aac_decoder_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create AAC decoder: %d", ret);
    return false;
  }

  // Allocate audio buffers (larger for decoded PCM)
  this->audio_input_buffer_ = (uint8_t *)heap_caps_malloc(8192,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  this->audio_output_buffer_ = (uint8_t *)heap_caps_malloc(16384,
                                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (!this->audio_input_buffer_ || !this->audio_output_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate audio buffers");
    esp_audio_dec_close(this->aac_decoder_);
    this->aac_decoder_ = nullptr;
    return false;
  }

  this->aac_decoder_ready_ = true;
  ESP_LOGI(TAG, "AAC decoder initialized: %d Hz, %d channels",
           this->audio_sample_rate_, this->audio_channels_);

  return true;
#else
  ESP_LOGW(TAG, "AAC decoder not available - esp_audio_codec not found");
  return false;
#endif
}

bool SimpleVideoPlayer::read_next_audio_sample_() {
  if (this->current_audio_sample_ >= this->audio_samples_.size()) {
    return false;
  }

  AudioSample &sample = this->audio_samples_[this->current_audio_sample_];

  // Seek to sample position
  fseek(this->file_, sample.offset, SEEK_SET);

  // Read sample data
  if (sample.size > 8192) {
    ESP_LOGW(TAG, "Audio sample too large: %u", sample.size);
    this->current_audio_sample_++;
    return false;
  }

  size_t bytes_read = fread(this->audio_input_buffer_, 1, sample.size, this->file_);
  if (bytes_read != sample.size) {
    return false;
  }

  this->audio_input_size_ = sample.size;
  this->current_audio_sample_++;

  return true;
}

bool SimpleVideoPlayer::decode_audio_frame_() {
#if USE_ESP_AUDIO_CODEC
  if (!this->aac_decoder_ready_ || this->speaker_ == nullptr || this->audio_input_size_ == 0) {
    return false;
  }

  // Prepare input frame
  esp_audio_dec_in_raw_t in_frame = {
    .buffer = this->audio_input_buffer_,
    .len = this->audio_input_size_,
    .consumed = 0,
  };

  // Prepare output frame
  esp_audio_dec_out_frame_t out_frame = {
    .buffer = this->audio_output_buffer_,
    .len = 16384,
    .decoded_size = 0,
  };

  // Decode AAC to PCM
  esp_audio_err_t ret = esp_audio_dec_process(this->aac_decoder_, &in_frame, &out_frame);
  if (ret != ESP_AUDIO_ERR_OK) {
    ESP_LOGW(TAG, "AAC decode failed: %d", ret);
    return false;
  }

  // Send decoded PCM to speaker
  if (out_frame.decoded_size > 0) {
    size_t bytes_written = this->speaker_->play(this->audio_output_buffer_, out_frame.decoded_size);
    if (bytes_written == 0) {
      ESP_LOGW(TAG, "Failed to write audio to speaker");
    }
  }

  return true;
#else
  return false;
#endif
}

void SimpleVideoPlayer::process_audio_() {
  if (!this->has_audio_ || this->speaker_ == nullptr) {
    return;
  }

  // Process audio samples to keep in sync with video
  // This is a simplified implementation
  while (this->current_audio_sample_ < this->audio_samples_.size()) {
    AudioSample &sample = this->audio_samples_[this->current_audio_sample_];

    // Check if this audio sample should be played based on video position
    if (this->current_video_sample_ > 0) {
      Mp4Sample &video = this->video_samples_[this->current_video_sample_ - 1];
      if (sample.timestamp_ms > video.timestamp_ms + 100) {
        break;  // Audio is ahead, wait
      }
    }

    if (this->read_next_audio_sample_()) {
      this->decode_audio_frame_();
    } else {
      break;
    }
  }
}

bool SimpleVideoPlayer::read_next_mp4_sample_() {
  if (this->current_video_sample_ >= this->video_samples_.size()) {
    if (this->loop_) {
      this->current_video_sample_ = 0;
      this->sps_pps_sent_ = false;
      this->frame_count_ = 0;
    } else {
      return false;
    }
  }

  Mp4Sample &sample = this->video_samples_[this->current_video_sample_];

  // Mark if we need to send SPS/PPS before this keyframe
  if (sample.is_keyframe) {
    this->sps_pps_sent_ = false;  // Reset flag to prepend SPS/PPS to keyframe
  }

  // Seek to sample position
  fseek(this->file_, sample.offset, SEEK_SET);

  // Read sample data
  if (sample.size > this->buffer_size_) {
    ESP_LOGW(TAG, "Sample too large: %u > %u", sample.size, this->buffer_size_);
    this->current_video_sample_++;
    return false;
  }

  size_t bytes_read = fread(this->input_buffer_, 1, sample.size, this->file_);
  if (bytes_read != sample.size) {
    ESP_LOGW(TAG, "Failed to read sample: got %u, expected %u", bytes_read, sample.size);
    return false;
  }

  this->input_size_ = sample.size;
  this->current_video_sample_++;
  this->frame_count_++;

  return true;
}

bool SimpleVideoPlayer::decode_h264_frame_() {
  if (!this->h264_decoder_ready_) {
    ESP_LOGW(TAG, "decode_h264_frame_: decoder not ready!");
    return false;
  }

  if (this->input_size_ == 0) {
    ESP_LOGW(TAG, "decode_h264_frame_: input_size is 0!");
    return false;
  }

  // Convert AVCC to Annex-B format
  std::vector<uint8_t> annexb_data;

  // Add SPS/PPS before first frame or keyframes
  if (!this->sps_pps_sent_ && !this->sps_.empty() && !this->pps_.empty()) {
    // Start code
    annexb_data.push_back(0x00);
    annexb_data.push_back(0x00);
    annexb_data.push_back(0x00);
    annexb_data.push_back(0x01);
    annexb_data.insert(annexb_data.end(), this->sps_.begin(), this->sps_.end());

    annexb_data.push_back(0x00);
    annexb_data.push_back(0x00);
    annexb_data.push_back(0x00);
    annexb_data.push_back(0x01);
    annexb_data.insert(annexb_data.end(), this->pps_.begin(), this->pps_.end());

    this->sps_pps_sent_ = true;
  }

  // Convert NALUs
  size_t offset = 0;
  int nalu_count = 0;
  while (offset + this->nal_length_size_ <= this->input_size_) {
    uint32_t nalu_len = 0;
    for (int i = 0; i < this->nal_length_size_; i++) {
      nalu_len = (nalu_len << 8) | this->input_buffer_[offset + i];
    }
    offset += this->nal_length_size_;

    if (offset + nalu_len > this->input_size_) {
      ESP_LOGE(TAG, "❌ CORRUPT SAMPLE DETECTED!");
      ESP_LOGE(TAG, "   NALU length %u exceeds remaining data (%u bytes)",
               nalu_len, this->input_size_ - offset);
      ESP_LOGE(TAG, "   This usually means MP4 sample offset is WRONG!");
      ESP_LOGE(TAG, "   Sample #%u: offset=%u, size=%u",
               this->current_video_sample_ - 1,
               this->video_samples_[this->current_video_sample_ - 1].offset,
               this->video_samples_[this->current_video_sample_ - 1].size);

      // Check if we're reading from a valid position
      if (nalu_len > 100000000) {  // > 100MB is definitely corrupt
        ESP_LOGE(TAG, "   NALU length is impossibly large (%u bytes = %.1f MB)",
                 nalu_len, nalu_len / 1048576.0f);
        ESP_LOGE(TAG, "   First 16 bytes of sample data:");
        for (int i = 0; i < 16 && i < this->input_size_; i++) {
          ESP_LOGE(TAG, "     [%d] = 0x%02X", i, this->input_buffer_[i]);
        }
      }
      break;
    }

    // Add start code
    annexb_data.push_back(0x00);
    annexb_data.push_back(0x00);
    annexb_data.push_back(0x00);
    annexb_data.push_back(0x01);
    annexb_data.insert(annexb_data.end(),
                       this->input_buffer_ + offset,
                       this->input_buffer_ + offset + nalu_len);
    offset += nalu_len;
    nalu_count++;
  }

  if (annexb_data.empty()) {
    ESP_LOGW(TAG, "AVCC conversion failed: no NALUs found in %u bytes", this->input_size_);
    return false;
  }

  // Decode
  esp_h264_dec_in_frame_t in_frame = {
    .raw_data = {
      .buffer = annexb_data.data(),
      .len = (uint32_t)annexb_data.size()
    },
    .consume = 0,
    .dts = 0,
    .pts = 0
  };

  esp_h264_dec_out_frame_t out_frame = {};

  esp_h264_err_t err = esp_h264_dec_process(this->h264_decoder_, &in_frame, &out_frame);

  if (err != ESP_H264_ERR_OK) {
    ESP_LOGW(TAG, "H.264 decode error: %d", err);
    return false;
  }

  if (out_frame.out_size > 0 && out_frame.outbuf != nullptr) {
    // Convert I420 to RGB565 (use actual dimensions for conversion, aligned for output)
    this->convert_i420_to_rgb565_(out_frame.outbuf, this->rgb_buffer_,
                                   this->actual_width_, this->actual_height_);
    return true;
  }

  return false;
}

void SimpleVideoPlayer::convert_i420_to_rgb565_(const uint8_t *yuv, uint8_t *rgb, int w, int h) {
  // Use optimized converter with BT.709 colorspace (5-10x faster than naive loop)
  if (this->yuv_converter_ != nullptr) {
    this->yuv_converter_->convert_i420_to_rgb565(yuv, rgb, w, h);
  } else {
    ESP_LOGE(TAG, "YUV converter not initialized!");
  }
}

// ==============================================
// MKV/MATROSKA PARSER
// ==============================================

// EBML/Matroska element IDs
#define EBML_ID_HEADER        0x1A45DFA3
#define EBML_ID_SEGMENT       0x18538067
#define EBML_ID_INFO          0x1549A966
#define EBML_ID_TIMECODE_SCALE 0x2AD7B1
#define EBML_ID_DURATION      0x4489
#define EBML_ID_TRACKS        0x1654AE6B
#define EBML_ID_TRACK_ENTRY   0xAE
#define EBML_ID_TRACK_NUMBER  0xD7
#define EBML_ID_TRACK_TYPE    0x83
#define EBML_ID_CODEC_ID      0x86
#define EBML_ID_CODEC_PRIVATE 0x63A2
#define EBML_ID_VIDEO         0xE0
#define EBML_ID_PIXEL_WIDTH   0xB0
#define EBML_ID_PIXEL_HEIGHT  0xBA
#define EBML_ID_AUDIO         0xE1
#define EBML_ID_SAMPLING_FREQ 0xB5
#define EBML_ID_CHANNELS      0x9F
#define EBML_ID_CLUSTER       0x1F43B675
#define EBML_ID_TIMECODE      0xE7
#define EBML_ID_SIMPLE_BLOCK  0xA3
#define EBML_ID_BLOCK_GROUP   0xA0
#define EBML_ID_BLOCK         0xA1
#define EBML_ID_BLOCK_DURATION 0x9B

uint64_t SimpleVideoPlayer::read_ebml_id_() {
  uint8_t first_byte;
  if (fread(&first_byte, 1, 1, this->file_) != 1) {
    return 0;
  }

  // Determine ID length from leading zeros
  int len = 0;
  if (first_byte & 0x80) len = 1;
  else if (first_byte & 0x40) len = 2;
  else if (first_byte & 0x20) len = 3;
  else if (first_byte & 0x10) len = 4;
  else return 0;

  uint64_t id = first_byte;
  for (int i = 1; i < len; i++) {
    uint8_t byte;
    if (fread(&byte, 1, 1, this->file_) != 1) return 0;
    id = (id << 8) | byte;
  }

  return id;
}

uint64_t SimpleVideoPlayer::read_ebml_size_() {
  uint8_t first_byte;
  if (fread(&first_byte, 1, 1, this->file_) != 1) {
    return 0;
  }

  // Determine size length from leading zeros
  int len = 0;
  uint8_t mask = 0;
  if (first_byte & 0x80) { len = 1; mask = 0x7F; }
  else if (first_byte & 0x40) { len = 2; mask = 0x3F; }
  else if (first_byte & 0x20) { len = 3; mask = 0x1F; }
  else if (first_byte & 0x10) { len = 4; mask = 0x0F; }
  else if (first_byte & 0x08) { len = 5; mask = 0x07; }
  else if (first_byte & 0x04) { len = 6; mask = 0x03; }
  else if (first_byte & 0x02) { len = 7; mask = 0x01; }
  else if (first_byte & 0x01) { len = 8; mask = 0x00; }
  else return 0;

  uint64_t size = first_byte & mask;
  for (int i = 1; i < len; i++) {
    uint8_t byte;
    if (fread(&byte, 1, 1, this->file_) != 1) return 0;
    size = (size << 8) | byte;
  }

  return size;
}

uint64_t SimpleVideoPlayer::read_ebml_vint_() {
  uint8_t first_byte;
  if (fread(&first_byte, 1, 1, this->file_) != 1) {
    return 0;
  }

  // Determine length from leading zeros (same as size encoding)
  int len = 0;
  uint8_t mask = 0;
  if (first_byte & 0x80) { len = 1; mask = 0x7F; }
  else if (first_byte & 0x40) { len = 2; mask = 0x3F; }
  else if (first_byte & 0x20) { len = 3; mask = 0x1F; }
  else if (first_byte & 0x10) { len = 4; mask = 0x0F; }
  else if (first_byte & 0x08) { len = 5; mask = 0x07; }
  else if (first_byte & 0x04) { len = 6; mask = 0x03; }
  else if (first_byte & 0x02) { len = 7; mask = 0x01; }
  else if (first_byte & 0x01) { len = 8; mask = 0x00; }
  else return 0;

  uint64_t value = first_byte & mask;
  for (int i = 1; i < len; i++) {
    uint8_t byte;
    if (fread(&byte, 1, 1, this->file_) != 1) return 0;
    value = (value << 8) | byte;
  }

  return value;
}

bool SimpleVideoPlayer::read_ebml_uint_(uint64_t size, uint64_t &value) {
  value = 0;
  for (uint64_t i = 0; i < size && i < 8; i++) {
    uint8_t byte;
    if (fread(&byte, 1, 1, this->file_) != 1) return false;
    value = (value << 8) | byte;
  }
  return true;
}

bool SimpleVideoPlayer::read_ebml_string_(uint64_t size, std::string &value) {
  if (size > 1024) return false;  // Sanity check
  value.resize(size);
  return fread(&value[0], 1, size, this->file_) == size;
}

bool SimpleVideoPlayer::parse_mkv_() {
  ESP_LOGI(TAG, "Parsing MKV file...");

  // Read EBML header
  uint64_t id = read_ebml_id_();
  if (id != EBML_ID_HEADER) {
    ESP_LOGE(TAG, "Invalid MKV: EBML header not found");
    return false;
  }

  uint64_t header_size = read_ebml_size_();
  fseek(this->file_, header_size, SEEK_CUR);  // Skip EBML header content

  // Read Segment
  id = read_ebml_id_();
  if (id != EBML_ID_SEGMENT) {
    ESP_LOGE(TAG, "Invalid MKV: Segment not found");
    return false;
  }

  uint64_t segment_size = read_ebml_size_();
  this->mkv_segment_start_ = ftell(this->file_);

  return parse_mkv_segment_(segment_size);
}

bool SimpleVideoPlayer::parse_mkv_segment_(uint64_t size) {
  uint64_t end_pos = this->mkv_segment_start_ + size;

  while (ftell(this->file_) < (long)end_pos) {
    uint64_t id = read_ebml_id_();
    if (id == 0) break;

    uint64_t elem_size = read_ebml_size_();
    long elem_start = ftell(this->file_);

    if (id == EBML_ID_INFO) {
      if (!parse_mkv_info_(elem_size)) {
        ESP_LOGW(TAG, "Failed to parse Info");
      }
    } else if (id == EBML_ID_TRACKS) {
      if (!parse_mkv_tracks_(elem_size)) {
        ESP_LOGW(TAG, "Failed to parse Tracks");
      }
    } else if (id == EBML_ID_CLUSTER) {
      // Found first cluster - stop parsing metadata
      this->mkv_cluster_start_ = elem_start;
      ESP_LOGI(TAG, "Found first Cluster at offset %ld", elem_start);
      break;
    } else {
      // Skip unknown elements
      fseek(this->file_, elem_start + elem_size, SEEK_SET);
    }
  }

  return this->mkv_video_track_ > 0;
}

bool SimpleVideoPlayer::parse_mkv_info_(uint64_t size) {
  long end_pos = ftell(this->file_) + size;

  while (ftell(this->file_) < end_pos) {
    uint64_t id = read_ebml_id_();
    if (id == 0) break;

    uint64_t elem_size = read_ebml_size_();

    if (id == EBML_ID_TIMECODE_SCALE) {
      read_ebml_uint_(elem_size, this->mkv_timecode_scale_);
      ESP_LOGI(TAG, "TimecodeScale: %llu ns", (unsigned long long)this->mkv_timecode_scale_);
    } else if (id == EBML_ID_DURATION) {
      // Duration is a float in Matroska timecode units
      // For simplicity, we'll skip it and calculate from samples
      fseek(this->file_, elem_size, SEEK_CUR);
    } else {
      fseek(this->file_, elem_size, SEEK_CUR);
    }
  }

  return true;
}

bool SimpleVideoPlayer::parse_mkv_tracks_(uint64_t size) {
  long end_pos = ftell(this->file_) + size;

  while (ftell(this->file_) < end_pos) {
    uint64_t id = read_ebml_id_();
    if (id == 0) break;

    uint64_t elem_size = read_ebml_size_();

    if (id == EBML_ID_TRACK_ENTRY) {
      if (!parse_mkv_track_entry_(elem_size)) {
        ESP_LOGW(TAG, "Failed to parse TrackEntry");
      }
    } else {
      fseek(this->file_, elem_size, SEEK_CUR);
    }
  }

  ESP_LOGI(TAG, "Video track: %u, Audio track: %u", this->mkv_video_track_, this->mkv_audio_track_);
  return this->mkv_video_track_ > 0;
}

bool SimpleVideoPlayer::parse_mkv_track_entry_(uint64_t size) {
  long end_pos = ftell(this->file_) + size;
  uint16_t track_number = 0;
  uint64_t track_type = 0;
  std::string codec_id;
  int width = 0, height = 0;

  while (ftell(this->file_) < end_pos) {
    uint64_t id = read_ebml_id_();
    if (id == 0) break;

    uint64_t elem_size = read_ebml_size_();
    long elem_start = ftell(this->file_);

    if (id == EBML_ID_TRACK_NUMBER) {
      uint64_t num;
      read_ebml_uint_(elem_size, num);
      track_number = (uint16_t)num;
    } else if (id == EBML_ID_TRACK_TYPE) {
      read_ebml_uint_(elem_size, track_type);
    } else if (id == EBML_ID_CODEC_ID) {
      read_ebml_string_(elem_size, codec_id);
    } else if (id == EBML_ID_VIDEO) {
      // Parse video dimensions
      long video_end = elem_start + elem_size;
      while (ftell(this->file_) < video_end) {
        uint64_t vid_id = read_ebml_id_();
        uint64_t vid_size = read_ebml_size_();
        if (vid_id == EBML_ID_PIXEL_WIDTH) {
          uint64_t w;
          read_ebml_uint_(vid_size, w);
          width = (int)w;
        } else if (vid_id == EBML_ID_PIXEL_HEIGHT) {
          uint64_t h;
          read_ebml_uint_(vid_size, h);
          height = (int)h;
        } else {
          fseek(this->file_, vid_size, SEEK_CUR);
        }
      }
    } else if (id == EBML_ID_CODEC_PRIVATE) {
      // For H.264, this contains SPS/PPS in AVCC format (same as MP4 avcC box)
      if (codec_id == "V_MPEG4/ISO/AVC" || codec_id == "V_AVC") {
        std::vector<uint8_t> codec_private(elem_size);
        fread(codec_private.data(), 1, elem_size, this->file_);

        // Parse AVCC format to extract SPS/PPS
        if (elem_size > 7) {
          // Byte 4: NAL length size - 1
          this->nal_length_size_ = (codec_private[4] & 0x03) + 1;

          // Byte 5: Number of SPS (lower 5 bits)
          uint8_t num_sps = codec_private[5] & 0x1F;
          size_t offset = 6;

          // Read SPS
          for (int i = 0; i < num_sps && offset + 2 <= elem_size; i++) {
            uint16_t sps_len = (codec_private[offset] << 8) | codec_private[offset + 1];
            offset += 2;
            if (offset + sps_len <= elem_size) {
              this->sps_.resize(sps_len);
              memcpy(this->sps_.data(), &codec_private[offset], sps_len);
              offset += sps_len;
            }
          }

          // Read number of PPS
          if (offset < elem_size) {
            uint8_t num_pps = codec_private[offset];
            offset++;

            // Read PPS
            for (int i = 0; i < num_pps && offset + 2 <= elem_size; i++) {
              uint16_t pps_len = (codec_private[offset] << 8) | codec_private[offset + 1];
              offset += 2;
              if (offset + pps_len <= elem_size) {
                this->pps_.resize(pps_len);
                memcpy(this->pps_.data(), &codec_private[offset], pps_len);
                offset += pps_len;
              }
            }
          }

          ESP_LOGI(TAG, "MKV avcC: NAL length size=%d, SPS=%d bytes, PPS=%d bytes",
                   this->nal_length_size_, this->sps_.size(), this->pps_.size());
        }
      } else {
        fseek(this->file_, elem_size, SEEK_CUR);
      }
    } else {
      fseek(this->file_, elem_start + elem_size, SEEK_SET);
    }
  }

  // Store track info
  if (track_type == 1 && (codec_id == "V_MPEG4/ISO/AVC" || codec_id == "V_AVC")) {
    // Video track
    this->mkv_video_track_ = track_number;
    if (width > 0 && height > 0) {
      this->actual_width_ = width;
      this->actual_height_ = height;
      ESP_LOGI(TAG, "Found H.264 video track %u: %dx%d", track_number, width, height);
    }
  } else if (track_type == 2 && (codec_id == "A_AAC" || codec_id.find("AAC") != std::string::npos)) {
    // Audio track
    this->mkv_audio_track_ = track_number;
    this->has_audio_ = true;
    ESP_LOGI(TAG, "Found AAC audio track %u", track_number);
  }

  return true;
}

bool SimpleVideoPlayer::parse_mkv_clusters_() {
  // Seek to first cluster
  fseek(this->file_, this->mkv_cluster_start_, SEEK_SET);

  uint64_t cluster_timecode = 0;
  int sample_count = 0;
  const int max_samples = 300;  // Limit pre-parsing to avoid memory issues

  ESP_LOGI(TAG, "Pre-parsing MKV clusters (max %d samples)...", max_samples);

  while (sample_count < max_samples && !feof(this->file_)) {
    uint64_t id = read_ebml_id_();
    if (id == 0) break;

    uint64_t elem_size = read_ebml_size_();
    long elem_start = ftell(this->file_);
    long elem_end = elem_start + elem_size;

    if (id == EBML_ID_CLUSTER) {
      // Parse cluster
      cluster_timecode = 0;

      while (ftell(this->file_) < elem_end && sample_count < max_samples) {
        uint64_t cid = read_ebml_id_();
        if (cid == 0) break;

        uint64_t csize = read_ebml_size_();
        long cstart = ftell(this->file_);

        if (cid == EBML_ID_TIMECODE) {
          read_ebml_uint_(csize, cluster_timecode);
        } else if (cid == EBML_ID_SIMPLE_BLOCK) {
          // Parse SimpleBlock
          MkvSample sample;
          sample.offset = cstart;
          sample.size = csize;

          // Read track number (EBML variable-length integer)
          uint64_t track_num = this->read_ebml_vint_();
          sample.track_number = (uint16_t)track_num;

          // Read relative timecode (int16 big-endian)
          int16_t relative_tc;
          fread(&relative_tc, 2, 1, this->file_);
          relative_tc = (relative_tc >> 8) | ((relative_tc & 0xFF) << 8);

          // Read flags
          uint8_t flags;
          fread(&flags, 1, 1, this->file_);
          sample.is_keyframe = (flags & 0x80) != 0;

          // Calculate absolute timestamp
          sample.timestamp_ns = (cluster_timecode + relative_tc) * this->mkv_timecode_scale_;

          if (sample.track_number == this->mkv_video_track_) {
            // Debug log for first few samples
            if (sample_count < 5) {
              ESP_LOGD(TAG, "Sample %d: track=%u, flags=0x%02X, keyframe=%d, offset=%ld, size=%llu",
                       sample_count, sample.track_number, flags, sample.is_keyframe,
                       cstart, (unsigned long long)csize);
            }
            this->mkv_samples_.push_back(sample);
            sample_count++;
          }

          fseek(this->file_, cstart + csize, SEEK_SET);
        } else {
          fseek(this->file_, cstart + csize, SEEK_SET);
        }
      }

      fseek(this->file_, elem_end, SEEK_SET);
    } else {
      fseek(this->file_, elem_end, SEEK_SET);
    }
  }

  this->total_frames_ = this->mkv_samples_.size();
  if (!this->mkv_samples_.empty()) {
    this->total_duration_ms_ = this->mkv_samples_.back().timestamp_ns / 1000000;
  }

  // Count and log keyframes
  int keyframe_count = 0;
  ESP_LOGI(TAG, "MKV Keyframe analysis (first 10 samples):");
  for (size_t i = 0; i < this->mkv_samples_.size() && i < 10; i++) {
    if (this->mkv_samples_[i].is_keyframe) {
      keyframe_count++;
      ESP_LOGI(TAG, "  Sample %zu: KEYFRAME, offset=%llu, size=%u",
               i, (unsigned long long)this->mkv_samples_[i].offset, this->mkv_samples_[i].size);
    } else {
      ESP_LOGD(TAG, "  Sample %zu: P-frame, offset=%llu, size=%u",
               i, (unsigned long long)this->mkv_samples_[i].offset, this->mkv_samples_[i].size);
    }
  }
  for (size_t i = 10; i < this->mkv_samples_.size(); i++) {
    if (this->mkv_samples_[i].is_keyframe) keyframe_count++;
  }

  ESP_LOGI(TAG, "Pre-parsed %u MKV samples, duration: %lu ms, keyframes: %d",
           this->total_frames_, (unsigned long)this->total_duration_ms_, keyframe_count);

  // Reset to first cluster for playback
  fseek(this->file_, this->mkv_cluster_start_, SEEK_SET);
  return this->total_frames_ > 0;
}

bool SimpleVideoPlayer::read_next_mkv_sample_() {
  if (this->current_mkv_sample_ >= this->mkv_samples_.size()) {
    if (this->loop_) {
      this->current_mkv_sample_ = 0;
      this->frame_count_ = 0;
      this->sps_pps_sent_ = false;
      fseek(this->file_, this->mkv_cluster_start_, SEEK_SET);
    } else {
      return false;
    }
  }

  MkvSample &sample = this->mkv_samples_[this->current_mkv_sample_];

  // Mark if we need SPS/PPS before keyframe
  if (sample.is_keyframe) {
    this->sps_pps_sent_ = false;
  }

  // Seek to sample
  fseek(this->file_, sample.offset, SEEK_SET);

  // Save position before reading header
  long header_start = ftell(this->file_);

  // Skip SimpleBlock header (track number, timecode, flags)
  // Track number is EBML variable-length integer (1-8 bytes)
  uint64_t track_num = this->read_ebml_vint_();

  // Skip relative timecode (2 bytes)
  fseek(this->file_, 2, SEEK_CUR);

  // Skip flags (1 byte)
  fseek(this->file_, 1, SEEK_CUR);

  // Calculate header size and frame size
  long frame_start = ftell(this->file_);
  uint32_t header_size = frame_start - header_start;
  uint32_t frame_size = sample.size - header_size;

  if (frame_size > this->buffer_size_) {
    ESP_LOGW(TAG, "MKV sample too large: %u", frame_size);
    this->current_mkv_sample_++;
    return false;
  }

  if (frame_size == 0) {
    ESP_LOGW(TAG, "MKV sample has zero frame size!");
    this->current_mkv_sample_++;
    return false;
  }

  size_t bytes_read = fread(this->input_buffer_, 1, frame_size, this->file_);
  if (bytes_read != frame_size) {
    ESP_LOGW(TAG, "Failed to read MKV sample: expected %u bytes, got %zu", frame_size, bytes_read);
    return false;
  }

  this->input_size_ = frame_size;
  this->current_time_ms_ = sample.timestamp_ns / 1000000;
  this->current_mkv_sample_++;
  this->frame_count_++;

  return true;
}

// ==============================================
// COMMON FUNCTIONS
// ==============================================

void SimpleVideoPlayer::format_time_(char *buf, size_t buf_size, uint32_t time_ms) {
  uint32_t total_seconds = time_ms / 1000;
  uint32_t minutes = total_seconds / 60;
  uint32_t seconds = total_seconds % 60;
  snprintf(buf, buf_size, "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
}

void SimpleVideoPlayer::update_display_() {
  if (this->canvas_ == nullptr) {
    return;
  }

  // CRITICAL: Reset canvas buffer pointer EVERY frame to force LVGL refresh
  // This is the pattern from lvgl_camera_display that ensures proper PSRAM access
  // Without this, LVGL may cache the buffer and not detect content changes
  lv_canvas_set_buffer(this->canvas_, this->rgb_buffer_,
                       this->actual_width_, this->actual_height_, LV_IMG_CF_TRUE_COLOR);

  // Invalidate to trigger redraw
  lv_obj_invalidate(this->canvas_);

  // Update UI controls only every 15 frames (~0.5 second at 30fps) to reduce overhead
  // This significantly improves video playback performance on ESP32-P4
  if (this->frame_count_ % 15 == 0) {
    // Update slider position
    if (this->slider_ != nullptr && this->total_frames_ > 0) {
      int progress = (this->frame_count_ * 100) / this->total_frames_;
      lv_slider_set_value(this->slider_, progress, LV_ANIM_OFF);
    }

    // Update time label
    if (this->time_label_ != nullptr) {
      char current_time[16], total_time[16];
      char buf[40];

      this->format_time_(current_time, sizeof(current_time), this->current_time_ms_);
      this->format_time_(total_time, sizeof(total_time), this->total_duration_ms_);

      snprintf(buf, sizeof(buf), "%s / %s", current_time, total_time);
      lv_label_set_text(this->time_label_, buf);
    }
  }
}

void SimpleVideoPlayer::create_ui_() {
  lv_obj_t *parent = this->parent_ != nullptr ? this->parent_ : lv_scr_act();

  // Create canvas for video display (use actual dimensions)
  this->canvas_ = lv_canvas_create(parent);
  lv_canvas_set_buffer(this->canvas_, this->rgb_buffer_,
                       this->actual_width_, this->actual_height_, LV_IMG_CF_TRUE_COLOR);
  lv_obj_center(this->canvas_);

  // Create loading indicator (shown during initial load, hidden after first frame)
  this->loading_spinner_ = lv_label_create(parent);
  lv_label_set_text(this->loading_spinner_, "Loading...");
  lv_obj_center(this->loading_spinner_);
  lv_obj_set_style_text_color(this->loading_spinner_, lv_color_hex(0x00A8FF), 0);
  lv_obj_set_style_text_font(this->loading_spinner_, &lv_font_montserrat_16, 0);

  // Create invisible touch layer over the canvas
  this->touch_layer_ = lv_obj_create(parent);
  lv_obj_remove_style_all(this->touch_layer_);
  lv_obj_set_size(this->touch_layer_, this->actual_width_, this->actual_height_);
  lv_obj_center(this->touch_layer_);
  lv_obj_add_flag(this->touch_layer_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(this->touch_layer_, touch_cb_, LV_EVENT_CLICKED, this);

  if (this->controls_enabled_) {
    this->create_controls_();
    // Create hide timer
    this->hide_timer_ = lv_timer_create(hide_timer_cb_, this->hide_delay_ms_, this);
    lv_timer_pause(this->hide_timer_);
  }
}

void SimpleVideoPlayer::create_controls_() {
  lv_obj_t *parent = this->parent_ != nullptr ? this->parent_ : lv_scr_act();

  // Controls container at bottom (use actual video width) - made taller for badges
  this->controls_container_ = lv_obj_create(parent);
  lv_obj_set_size(this->controls_container_, this->actual_width_, 90);
  lv_obj_align(this->controls_container_, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_opa(this->controls_container_, LV_OPA_70, 0);
  lv_obj_set_style_bg_color(this->controls_container_, lv_color_black(), 0);

  // Play button
  this->play_btn_ = lv_btn_create(this->controls_container_);
  lv_obj_set_size(this->play_btn_, 50, 40);
  lv_obj_align(this->play_btn_, LV_ALIGN_LEFT_MID, 10, 0);
  lv_obj_t *play_label = lv_label_create(this->play_btn_);
  lv_label_set_text(play_label, LV_SYMBOL_PLAY);
  lv_obj_center(play_label);
  lv_obj_add_event_cb(this->play_btn_, play_btn_cb_, LV_EVENT_CLICKED, this);

  // Pause button
  this->pause_btn_ = lv_btn_create(this->controls_container_);
  lv_obj_set_size(this->pause_btn_, 50, 40);
  lv_obj_align(this->pause_btn_, LV_ALIGN_LEFT_MID, 70, 0);
  lv_obj_t *pause_label = lv_label_create(this->pause_btn_);
  lv_label_set_text(pause_label, LV_SYMBOL_PAUSE);
  lv_obj_center(pause_label);
  lv_obj_add_event_cb(this->pause_btn_, pause_btn_cb_, LV_EVENT_CLICKED, this);

  // Stop button
  this->stop_btn_ = lv_btn_create(this->controls_container_);
  lv_obj_set_size(this->stop_btn_, 50, 40);
  lv_obj_align(this->stop_btn_, LV_ALIGN_LEFT_MID, 130, 0);
  lv_obj_t *stop_label = lv_label_create(this->stop_btn_);
  lv_label_set_text(stop_label, LV_SYMBOL_STOP);
  lv_obj_center(stop_label);
  lv_obj_add_event_cb(this->stop_btn_, stop_btn_cb_, LV_EVENT_CLICKED, this);

  // Progress slider (enhanced style)
  this->slider_ = lv_slider_create(this->controls_container_);
  lv_obj_set_size(this->slider_, this->actual_width_ - 300, 12);
  lv_obj_align(this->slider_, LV_ALIGN_LEFT_MID, 190, 0);
  lv_slider_set_range(this->slider_, 0, 100);

  // Style the slider for better visibility
  lv_obj_set_style_bg_color(this->slider_, lv_color_hex(0x404040), LV_PART_MAIN);  // Dark gray background
  lv_obj_set_style_bg_opa(this->slider_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(this->slider_, lv_color_hex(0x00A8FF), LV_PART_INDICATOR);  // Blue indicator
  lv_obj_set_style_bg_opa(this->slider_, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(this->slider_, lv_color_hex(0xFFFFFF), LV_PART_KNOB);  // White knob
  lv_obj_set_style_pad_all(this->slider_, 0, LV_PART_MAIN);  // No padding

  lv_obj_add_event_cb(this->slider_, slider_cb_, LV_EVENT_VALUE_CHANGED, this);

  // Time counter (top row, right side)
  this->time_label_ = lv_label_create(this->controls_container_);
  lv_label_set_text(this->time_label_, "00:00 / 00:00");
  lv_obj_align(this->time_label_, LV_ALIGN_TOP_RIGHT, -10, 5);
  lv_obj_set_style_text_color(this->time_label_, lv_color_white(), 0);

  // Format badge (bottom row, left side)
  this->format_badge_ = lv_label_create(this->controls_container_);
  const char *format_text = this->format_ == MediaFormat::MP4_H264 ? "MP4" : "MJPEG";
  lv_label_set_text(this->format_badge_, format_text);
  lv_obj_align(this->format_badge_, LV_ALIGN_BOTTOM_LEFT, 10, -5);
  lv_obj_set_style_text_color(this->format_badge_, lv_color_hex(0x00FF00), 0);  // Green
  lv_obj_set_style_text_font(this->format_badge_, &lv_font_montserrat_14, 0);

  // Resolution label (bottom row, next to format)
  this->resolution_label_ = lv_label_create(this->controls_container_);
  char res_text[32];
  snprintf(res_text, sizeof(res_text), "%dx%d", this->actual_width_, this->actual_height_);
  lv_label_set_text(this->resolution_label_, res_text);
  lv_obj_align(this->resolution_label_, LV_ALIGN_BOTTOM_LEFT, 80, -5);
  lv_obj_set_style_text_color(this->resolution_label_, lv_color_hex(0xFFFFFF), 0);  // White
  lv_obj_set_style_text_font(this->resolution_label_, &lv_font_montserrat_14, 0);
}

void SimpleVideoPlayer::play() {
  if (this->state_ == PlayerState::PLAYING) {
    return;
  }

  // Check if HTTP buffer was freed (e.g., after stop()) and needs re-download
  if (this->is_http_source_ && this->http_buffer_ == nullptr) {
    ESP_LOGI(TAG, "HTTP buffer was freed, will re-download video in loop()");
    this->http_download_pending_ = true;
    this->initialization_complete_ = false;
    this->auto_play_after_download_ = true;  // Auto-play after re-download
    return;  // Download will happen in loop(), then play will resume automatically
  }

  // Check if decoder buffers were freed (after stop()) and need re-initialization
  if (this->input_buffer_ == nullptr || this->rgb_buffer_ == nullptr) {
    ESP_LOGI(TAG, "Decoder buffers were freed, re-initializing...");

    // Re-allocate input buffer
    if (this->input_buffer_ == nullptr) {
      this->input_buffer_ = (uint8_t *)heap_caps_malloc(this->buffer_size_,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (this->input_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to re-allocate input buffer");
        return;
      }
      ESP_LOGD(TAG, "Re-allocated input_buffer_: %zu bytes", this->buffer_size_);
    }

    // Re-allocate RGB buffer
    if (this->rgb_buffer_ == nullptr) {
      this->rgb_buffer_size_ = this->aligned_width_ * this->aligned_height_ * 2;
      this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(64, this->rgb_buffer_size_,
                                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (this->rgb_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to re-allocate RGB buffer");
        return;
      }
      ESP_LOGD(TAG, "Re-allocated rgb_buffer_: %zu bytes", this->rgb_buffer_size_);
    }

    // Re-initialize H.264 decoder if needed
    if (this->format_ == MediaFormat::MP4_H264 || this->format_ == MediaFormat::MKV_H264) {
      if (!this->h264_decoder_ready_) {
        ESP_LOGI(TAG, "Re-initializing H.264 decoder...");
        if (!this->init_h264_decoder_()) {
          ESP_LOGE(TAG, "Failed to re-initialize H.264 decoder");
          return;
        }
      }
    }

    // Re-initialize audio decoder if needed
#if USE_ESP_AUDIO_CODEC
    if (this->has_audio_ && !this->aac_decoder_ready_) {
      if (!this->init_aac_decoder_()) {
        ESP_LOGW(TAG, "Failed to re-initialize AAC decoder");
      }
    }
#endif
  }

  if (this->state_ == PlayerState::STOPPED) {
    if (this->format_ == MediaFormat::MJPEG && this->file_ != nullptr) {
      fseek(this->file_, 0, SEEK_SET);
    } else if (this->format_ == MediaFormat::MP4_H264) {
      this->current_video_sample_ = 0;
      this->sps_pps_sent_ = false;
    } else if (this->format_ == MediaFormat::MKV_H264) {
      // Find first keyframe to start playback
      size_t first_keyframe = 0;
      for (size_t i = 0; i < this->mkv_samples_.size(); i++) {
        if (this->mkv_samples_[i].is_keyframe) {
          first_keyframe = i;
          ESP_LOGI(TAG, "Starting playback from first keyframe at sample %zu", first_keyframe);
          break;
        }
      }
      this->current_mkv_sample_ = first_keyframe;
      this->sps_pps_sent_ = false;
    }
    this->frame_count_ = 0;
  }

  this->state_ = PlayerState::PLAYING;
  if (this->playback_timer_ != nullptr) {
    lv_timer_resume(this->playback_timer_);
  }

  // Show loading spinner when starting playback (it will be hidden on first successful frame)
  if (this->loading_spinner_ != nullptr) {
    lv_obj_clear_flag(this->loading_spinner_, LV_OBJ_FLAG_HIDDEN);
  }

  // Start auto-hide timer for controls
  if (this->hide_timer_ != nullptr && this->controls_visible_) {
    lv_timer_reset(this->hide_timer_);
    lv_timer_resume(this->hide_timer_);
  }

  ESP_LOGI(TAG, "Playback started");
}

void SimpleVideoPlayer::pause() {
  if (this->state_ != PlayerState::PLAYING) {
    return;
  }

  this->state_ = PlayerState::PAUSED;
  if (this->playback_timer_ != nullptr) {
    lv_timer_pause(this->playback_timer_);
  }

  // Show controls when paused
  this->show_controls_();
  if (this->hide_timer_ != nullptr) {
    lv_timer_pause(this->hide_timer_);
  }

  ESP_LOGI(TAG, "Playback paused");
}

void SimpleVideoPlayer::resume() {
  if (this->state_ != PlayerState::PAUSED) {
    return;
  }

  this->state_ = PlayerState::PLAYING;
  if (this->playback_timer_ != nullptr) {
    lv_timer_resume(this->playback_timer_);
  }

  // Start auto-hide timer
  if (this->hide_timer_ != nullptr && this->controls_visible_) {
    lv_timer_reset(this->hide_timer_);
    lv_timer_resume(this->hide_timer_);
  }

  ESP_LOGI(TAG, "Playback resumed");
}

void SimpleVideoPlayer::stop() {
  this->state_ = PlayerState::STOPPED;
  if (this->playback_timer_ != nullptr) {
    lv_timer_pause(this->playback_timer_);
  }

  if (this->format_ == MediaFormat::MJPEG && this->file_ != nullptr) {
    fseek(this->file_, 0, SEEK_SET);
  } else if (this->format_ == MediaFormat::MP4_H264) {
    this->current_video_sample_ = 0;
    this->sps_pps_sent_ = false;
  }
  this->frame_count_ = 0;
  this->current_pos_ = 0;
  this->current_time_ms_ = 0;

  // Update slider
  if (this->slider_ != nullptr) {
    lv_slider_set_value(this->slider_, 0, LV_ANIM_OFF);
  }

  // Show controls when stopped
  this->show_controls_();
  if (this->hide_timer_ != nullptr) {
    lv_timer_pause(this->hide_timer_);
  }

  // Free HTTP buffer to reclaim SPIRAM when stopping
  if (this->is_http_source_ && this->http_buffer_ != nullptr) {
    ESP_LOGI(TAG, "Freeing HTTP buffer (%zu bytes from SPIRAM)", this->http_buffer_size_);

    // Close FILE* first since it points to the buffer
    if (this->file_ != nullptr) {
      fclose(this->file_);
      this->file_ = nullptr;
    }

    // Free the buffer
    heap_caps_free(this->http_buffer_);
    this->http_buffer_ = nullptr;
    this->http_buffer_size_ = 0;
  }

  // Free decoder buffers to reclaim SPIRAM
  ESP_LOGI(TAG, "Freeing decoder buffers from SPIRAM...");
  size_t total_freed = 0;

  // Free H.264 input buffer
  if (this->input_buffer_ != nullptr) {
    total_freed += this->buffer_size_;
    heap_caps_free(this->input_buffer_);
    this->input_buffer_ = nullptr;
    ESP_LOGD(TAG, "  Freed input_buffer_: %zu bytes", this->buffer_size_);
  }

  // Free RGB output buffer
  if (this->rgb_buffer_ != nullptr) {
    total_freed += this->rgb_buffer_size_;
    heap_caps_free(this->rgb_buffer_);
    this->rgb_buffer_ = nullptr;
    this->rgb_buffer_size_ = 0;
    ESP_LOGD(TAG, "  Freed rgb_buffer_: %zu bytes", this->rgb_buffer_size_);
  }

  // Free YUV buffer (vector will auto-free, but clear to reclaim immediately)
  if (!this->yuv_buffer_.empty()) {
    size_t yuv_size = this->yuv_buffer_.size();
    total_freed += yuv_size;
    this->yuv_buffer_.clear();
    this->yuv_buffer_.shrink_to_fit();  // Force memory release
    ESP_LOGD(TAG, "  Freed yuv_buffer_: %zu bytes", yuv_size);
  }

  // Free audio buffers
  if (this->audio_input_buffer_ != nullptr) {
    total_freed += 8192;
    heap_caps_free(this->audio_input_buffer_);
    this->audio_input_buffer_ = nullptr;
    ESP_LOGD(TAG, "  Freed audio_input_buffer_: 8192 bytes");
  }

  if (this->audio_output_buffer_ != nullptr) {
    total_freed += 16384;
    heap_caps_free(this->audio_output_buffer_);
    this->audio_output_buffer_ = nullptr;
    ESP_LOGD(TAG, "  Freed audio_output_buffer_: 16384 bytes");
  }

  // Close AAC decoder
#if USE_ESP_AUDIO_CODEC
  if (this->aac_decoder_ != nullptr) {
    esp_audio_dec_close(this->aac_decoder_);
    this->aac_decoder_ = nullptr;
    this->aac_decoder_ready_ = false;
    ESP_LOGD(TAG, "  Closed AAC decoder");
  }
#endif

  // Close H.264 decoder (CRITICAL for SPIRAM release!)
  if (this->h264_decoder_ != nullptr) {
    ESP_LOGI(TAG, "Closing H.264 decoder to free internal SPIRAM buffers...");
    esp_h264_dec_close(this->h264_decoder_);
    esp_h264_dec_del(this->h264_decoder_);
    this->h264_decoder_ = nullptr;
    this->h264_decoder_ready_ = false;
    ESP_LOGD(TAG, "  Closed and deleted H.264 decoder");
  }

  ESP_LOGI(TAG, "Playback stopped - freed %zu bytes (%.2f MB) from SPIRAM",
           total_freed, total_freed / (1024.0 * 1024.0));
  ESP_LOGI(TAG, "NOTE: H.264 decoder also freed internal SPIRAM (amount not tracked)");
}

// Static callbacks
void SimpleVideoPlayer::play_btn_cb_(lv_event_t *e) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(lv_event_get_user_data(e));
  if (player->is_paused()) {
    player->resume();
  } else {
    player->play();
  }
}

void SimpleVideoPlayer::pause_btn_cb_(lv_event_t *e) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(lv_event_get_user_data(e));
  player->pause();
}

void SimpleVideoPlayer::stop_btn_cb_(lv_event_t *e) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(lv_event_get_user_data(e));
  player->stop();
}

void SimpleVideoPlayer::slider_cb_(lv_event_t *e) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(lv_event_get_user_data(e));
  lv_obj_t *slider = lv_event_get_target(e);

  int value = lv_slider_get_value(slider);

  if (player->format_ == MediaFormat::MJPEG) {
    long new_pos = (player->file_size_ * value) / 100;
    if (player->file_ != nullptr) {
      fseek(player->file_, new_pos, SEEK_SET);
      player->current_pos_ = new_pos;
    }
  } else if (player->format_ == MediaFormat::MP4_H264) {
    size_t new_sample = (player->video_samples_.size() * value) / 100;
    player->current_video_sample_ = new_sample;
    player->sps_pps_sent_ = false;  // Resend SPS/PPS
  }
}

void SimpleVideoPlayer::timer_cb_(lv_timer_t *timer) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(timer->user_data);

  if (player->state_ != PlayerState::PLAYING) {
    return;
  }

  static uint32_t last_callback_time = 0;
  uint32_t current_time = esp_timer_get_time() / 1000;  // microseconds to milliseconds

  // Log timing for performance debugging (only every 30 frames to avoid spam)
  static int callback_count = 0;
  static uint32_t fps_measure_start = 0;
  static int fps_frame_count = 0;

  if (callback_count++ % 30 == 0) {
    uint32_t actual_interval = current_time - last_callback_time;

    // Calculate actual FPS over last 30 frames
    if (fps_measure_start > 0) {
      uint32_t time_elapsed = current_time - fps_measure_start;
      float actual_fps = (fps_frame_count * 1000.0f) / time_elapsed;
      ESP_LOGI(TAG, "📊 Performance: %.2f FPS | decode time shown below | target: %.0f FPS",
               actual_fps, 1000.0f / player->frame_interval_);
    }

    // Reset FPS measurement
    fps_measure_start = current_time;
    fps_frame_count = 0;
  }
  fps_frame_count++;
  last_callback_time = current_time;

  bool got_frame = false;
  bool end_of_stream = false;  // True only when we've exhausted all samples

  if (player->format_ == MediaFormat::MJPEG) {
    uint32_t decode_start = esp_timer_get_time() / 1000;

    // For MJPEG, process ONE frame per callback for precise timing
    // The multi-frame approach was causing timing issues
    if (player->read_next_mjpeg_frame_()) {
      if (player->decode_mjpeg_frame_()) {
        // Update current time (estimate based on frames and frame interval)
        player->current_time_ms_ = player->frame_count_ * player->frame_interval_;

        // Estimate total duration for MJPEG if not set
        if (player->total_duration_ms_ == 0 && player->file_size_ > 0) {
          // Rough estimate: assume average frame size and continue from current position
          uint32_t avg_frame_size = player->input_size_ > 0 ? player->input_size_ : 50000;
          uint32_t estimated_total_frames = player->file_size_ / avg_frame_size;
          player->total_duration_ms_ = estimated_total_frames * player->frame_interval_;
        }

        player->update_display_();
        got_frame = true;

        uint32_t decode_time = (esp_timer_get_time() / 1000) - decode_start;
        if (callback_count % 30 == 0) {
          ESP_LOGI(TAG, "MJPEG decode time: %lu ms", (unsigned long)decode_time);
        }
      }
      // If decode fails, we just skip this frame and try next one
    } else {
      // read_next_mjpeg_frame_() returned false = reached end
      end_of_stream = true;
    }
  } else if (player->format_ == MediaFormat::MP4_H264) {
    uint32_t decode_start = esp_timer_get_time() / 1000;

    if (player->read_next_mp4_sample_()) {
      // Sample read successfully, try to decode
      if (player->decode_h264_frame_()) {
        // Update current time from video sample timestamp
        if (player->current_video_sample_ > 0 && player->current_video_sample_ <= player->video_samples_.size()) {
          player->current_time_ms_ = player->video_samples_[player->current_video_sample_ - 1].timestamp_ms;
        }
        player->update_display_();
        got_frame = true;

        // Reset error counter on success
        static int consecutive_decode_errors = 0;
        consecutive_decode_errors = 0;

        uint32_t decode_time = (esp_timer_get_time() / 1000) - decode_start;
        if (callback_count % 30 == 0) {
          ESP_LOGI(TAG, "H.264 decode time: %lu ms (software decoder)", (unsigned long)decode_time);
        }
      } else {
        // Decode failed, but we can continue to next frame
        static int consecutive_decode_errors = 0;
        consecutive_decode_errors++;

        if (consecutive_decode_errors <= 5) {
          ESP_LOGE(TAG, "❌ Frame %u decode FAILED (consecutive errors: %d) - skipping to next frame",
                   player->current_video_sample_ - 1, consecutive_decode_errors);
        } else if (consecutive_decode_errors == 10) {
          ESP_LOGE(TAG, "❌ 10 consecutive decode failures! Video may be corrupted or AVCC conversion broken");
        } else if (consecutive_decode_errors % 30 == 0) {
          ESP_LOGE(TAG, "❌ Still failing: %d consecutive decode errors", consecutive_decode_errors);
        }
        // IMPORTANT: Don't set end_of_stream here! We just skip this bad frame and continue
      }
    } else {
      // read_next_mp4_sample_() returned false = reached end of video
      end_of_stream = true;
    }
    // Process audio
    player->process_audio_();
  } else if (player->format_ == MediaFormat::MKV_H264) {
    uint32_t decode_start = esp_timer_get_time() / 1000;

    if (player->read_next_mkv_sample_()) {
      if (player->decode_h264_frame_()) {
        // Update current time from MKV sample timestamp
        if (player->current_mkv_sample_ > 0 && player->current_mkv_sample_ <= player->mkv_samples_.size()) {
          player->current_time_ms_ = player->mkv_samples_[player->current_mkv_sample_ - 1].timestamp_ns / 1000000;
        }
        player->update_display_();
        got_frame = true;

        uint32_t decode_time = (esp_timer_get_time() / 1000) - decode_start;
        if (callback_count % 30 == 0) {
          ESP_LOGI(TAG, "MKV H.264 decode time: %lu ms (software decoder)", (unsigned long)decode_time);
        }
      } else {
        ESP_LOGW(TAG, "MKV H.264 decode failed for sample %zu - skipping", player->current_mkv_sample_ - 1);
        // Decode failed, but we can continue to next frame
      }
    } else {
      // read_next_mkv_sample_() returned false = reached end of video
      ESP_LOGD(TAG, "MKV read_next_sample returned false - end of stream");
      end_of_stream = true;
    }
    // Process audio
    player->process_audio_();
  }

  // Hide loading spinner after first frame
  // Use player member variable instead of static to properly reset on replay
  if (got_frame && player->loading_spinner_ != nullptr) {
    // Hide spinner on any successful frame display
    lv_obj_add_flag(player->loading_spinner_, LV_OBJ_FLAG_HIDDEN);
  }

  // Only stop if we've reached the actual end of the video stream
  // Not just because a single frame failed to decode!
  if (end_of_stream) {
    // End of video
    if (!player->loop_) {
      ESP_LOGI(TAG, "Reached end of video, stopping playback");
      player->stop();

      // NOTE: We do NOT free the HTTP buffer here to allow replay
      // Buffer will be freed when opening a new video or when component is destroyed
      // If you want to free memory immediately after playback, call stop() manually
    }
  }
}

void SimpleVideoPlayer::hide_timer_cb_(lv_timer_t *timer) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(timer->user_data);
  player->hide_controls_();
  lv_timer_pause(timer);
}

void SimpleVideoPlayer::touch_cb_(lv_event_t *e) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(lv_event_get_user_data(e));
  if (player->controls_visible_) {
    player->hide_controls_();
  } else {
    player->show_controls_();
  }
}

void SimpleVideoPlayer::show_controls_() {
  if (this->controls_container_ == nullptr) {
    return;
  }

  lv_obj_clear_flag(this->controls_container_, LV_OBJ_FLAG_HIDDEN);
  this->controls_visible_ = true;

  // Start timer to auto-hide during playback
  if (this->state_ == PlayerState::PLAYING && this->hide_timer_ != nullptr) {
    lv_timer_reset(this->hide_timer_);
    lv_timer_resume(this->hide_timer_);
  }
}

void SimpleVideoPlayer::hide_controls_() {
  if (this->controls_container_ == nullptr) {
    return;
  }

  lv_obj_add_flag(this->controls_container_, LV_OBJ_FLAG_HIDDEN);
  this->controls_visible_ = false;

  if (this->hide_timer_ != nullptr) {
    lv_timer_pause(this->hide_timer_);
  }
}

}  // namespace simple_video_player
}  // namespace esphome

#else  // !USE_ESP_IDF

namespace esphome {
namespace simple_video_player {

void SimpleVideoPlayer::setup() {
  ESP_LOGE("simple_video_player", "Requires ESP-IDF");
  this->mark_failed();
}

void SimpleVideoPlayer::loop() {}
void SimpleVideoPlayer::dump_config() {}
void SimpleVideoPlayer::play() {}
void SimpleVideoPlayer::pause() {}
void SimpleVideoPlayer::stop() {}
void SimpleVideoPlayer::resume() {}

}  // namespace simple_video_player
}  // namespace esphome

#endif  // USE_ESP_IDF



	






	



