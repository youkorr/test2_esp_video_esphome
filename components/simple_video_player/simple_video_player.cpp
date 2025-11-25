#include "simple_video_player.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esp_heap_caps.h"

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

  // Detect format
  this->format_ = this->detect_format_();
  ESP_LOGI(TAG, "Detected format: %s",
           this->format_ == MediaFormat::MP4_H264 ? "MP4/H.264" : "MJPEG");

  // Auto-detect resolution from video file
  if (this->format_ == MediaFormat::MJPEG) {
    if (this->detect_jpeg_resolution_(this->actual_width_, this->actual_height_)) {
      ESP_LOGI(TAG, "Auto-detected JPEG resolution: %dx%d", this->actual_width_, this->actual_height_);
    } else {
      ESP_LOGW(TAG, "Failed to auto-detect resolution, using configured: %dx%d", this->width_, this->height_);
      this->actual_width_ = this->width_;
      this->actual_height_ = this->height_;
    }
  } else {
    // For MP4, use configured dimensions initially (will be updated during parsing)
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
        this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(64, this->rgb_buffer_size_,
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

void SimpleVideoPlayer::loop() {
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
  ESP_LOGCONFIG(TAG, "  Format: %s",
                this->format_ == MediaFormat::MP4_H264 ? "MP4/H.264" : "MJPEG");
  ESP_LOGCONFIG(TAG, "  Buffer size: %u bytes (RGB: %u bytes)", this->buffer_size_, this->rgb_buffer_size_);
  ESP_LOGCONFIG(TAG, "  Auto play: %s", this->auto_play_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Loop: %s", this->loop_ ? "yes" : "no");
}

bool SimpleVideoPlayer::open_video_file_() {
  this->file_ = fopen(this->file_path_.c_str(), "rb");
  if (this->file_ == nullptr) {
    ESP_LOGE(TAG, "Failed to open file: %s", this->file_path_.c_str());
    return false;
  }

  // Get file size
  fseek(this->file_, 0, SEEK_END);
  this->file_size_ = ftell(this->file_);
  fseek(this->file_, 0, SEEK_SET);

  ESP_LOGI(TAG, "Video file opened: %ld bytes", this->file_size_);
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
    .timeout_ms = 40,
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
    }

    // Position to next box
    long next_box_pos = current_pos + box_size;
    fseek(this->file_, next_box_pos, SEEK_SET);
    ESP_LOGD(TAG, "  Positioned to next box at %ld", next_box_pos);
  }

  // Build sample list (simplified - assumes 1 sample per chunk)
  if (is_video && !sample_sizes.empty()) {
    ESP_LOGI(TAG, "Building video samples: sizes=%u, offsets=%u, durations=%u, keyframes=%u",
             sample_sizes.size(), chunk_offsets.size(), sample_durations.size(), keyframes.size());

    uint32_t timestamp = 0;
    for (size_t i = 0; i < sample_sizes.size() && i < chunk_offsets.size(); i++) {
      Mp4Sample sample;
      sample.offset = chunk_offsets[i];
      sample.size = sample_sizes[i];
      sample.duration = (i < sample_durations.size()) ? sample_durations[i] : 1000;
      sample.timestamp_ms = (timestamp * 1000) / this->video_timescale_;
      sample.is_keyframe = keyframes.empty() ||
                          std::find(keyframes.begin(), keyframes.end(), i + 1) != keyframes.end();

      this->video_samples_.push_back(sample);
      timestamp += sample.duration;
    }
    this->total_frames_ = this->video_samples_.size();

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
      if (this->total_duration_ms_ > 0 && this->video_samples_.size() > 0) {
        float actual_fps = (this->video_samples_.size() * 1000.0f) / this->total_duration_ms_;
        this->frame_interval_ = (uint32_t)(1000.0f / actual_fps);

        ESP_LOGI(TAG, "Detected framerate: %.2f fps, base timer interval: %lu ms",
                 actual_fps, (unsigned long)this->frame_interval_);
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
  esp_audio_dec_register_default();

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
  if (!this->h264_decoder_ready_ || this->input_size_ == 0) {
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
  while (offset + this->nal_length_size_ <= this->input_size_) {
    uint32_t nalu_len = 0;
    for (int i = 0; i < this->nal_length_size_; i++) {
      nalu_len = (nalu_len << 8) | this->input_buffer_[offset + i];
    }
    offset += this->nal_length_size_;

    if (offset + nalu_len > this->input_size_) break;

    // Add start code
    annexb_data.push_back(0x00);
    annexb_data.push_back(0x00);
    annexb_data.push_back(0x00);
    annexb_data.push_back(0x01);
    annexb_data.insert(annexb_data.end(),
                       this->input_buffer_ + offset,
                       this->input_buffer_ + offset + nalu_len);
    offset += nalu_len;
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
  const uint8_t *y_plane = yuv;
  const uint8_t *u_plane = yuv + w * h;
  const uint8_t *v_plane = u_plane + (w * h / 4);

  uint16_t *rgb565 = (uint16_t *)rgb;

  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      int y = y_plane[j * w + i];
      int u = u_plane[(j / 2) * (w / 2) + (i / 2)];
      int v = v_plane[(j / 2) * (w / 2) + (i / 2)];

      // YUV to RGB (BT.601)
      int c = y - 16;
      int d = u - 128;
      int e = v - 128;

      int r = (298 * c + 409 * e + 128) >> 8;
      int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
      int b = (298 * c + 516 * d + 128) >> 8;

      // Clamp
      r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
      g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
      b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

      // Convert to RGB565
      rgb565[j * w + i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
  }
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

  // Only invalidate canvas to trigger redraw (buffer is already set in create_ui_)
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

  if (this->state_ == PlayerState::STOPPED) {
    if (this->format_ == MediaFormat::MJPEG && this->file_ != nullptr) {
      fseek(this->file_, 0, SEEK_SET);
    } else if (this->format_ == MediaFormat::MP4_H264) {
      this->current_video_sample_ = 0;
      this->sps_pps_sent_ = false;
    }
    this->frame_count_ = 0;
  }

  this->state_ = PlayerState::PLAYING;
  if (this->playback_timer_ != nullptr) {
    lv_timer_resume(this->playback_timer_);
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

  ESP_LOGI(TAG, "Playback stopped");
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

  bool got_frame = false;
  static bool first_frame_received = false;

  if (player->format_ == MediaFormat::MJPEG) {
    // For MJPEG, process multiple frames per callback for smooth playback
    // The hardware JPEG decoder is fast, process a few frames per timer tick
    int frames_processed = 0;
    const int max_frames_per_callback = 4;  // Process up to 4 frames per callback

    while (frames_processed < max_frames_per_callback) {
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
          frames_processed++;
        } else {
          break;  // Decode failed, stop processing
        }
      } else {
        break;  // No more frames available
      }
    }
  } else if (player->format_ == MediaFormat::MP4_H264) {
    if (player->read_next_mp4_sample_()) {
      if (player->decode_h264_frame_()) {
        // Update current time from video sample timestamp
        if (player->current_video_sample_ > 0 && player->current_video_sample_ <= player->video_samples_.size()) {
          player->current_time_ms_ = player->video_samples_[player->current_video_sample_ - 1].timestamp_ms;
        }
        player->update_display_();
        got_frame = true;
      }
    }
    // Process audio
    player->process_audio_();
  }

  // Hide loading spinner after first frame
  if (got_frame && !first_frame_received) {
    first_frame_received = true;
    if (player->loading_spinner_ != nullptr) {
      lv_obj_add_flag(player->loading_spinner_, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (!got_frame) {
    // End of video
    if (!player->loop_) {
      player->stop();
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




