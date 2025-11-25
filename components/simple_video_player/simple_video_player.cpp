// simple_video_player.cpp
#include "simple_video_player.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace esphome {
namespace simple_video_player {

static const char *const TAG = "simple_video_player";

// Helper to read big-endian values
static uint32_t read_be32(FILE *f) {
  uint8_t buf[4];
  if (fread(buf, 1, 4, f) != 4) return 0;
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
}

static uint16_t read_be16(FILE *f) {
  uint8_t buf[2];
  if (fread(buf, 1, 2, f) != 2) return 0;
  return ((uint16_t)buf[0] << 8) | buf[1];
}

static uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d;
}

// ----------------------
// Setup / lifecycle
// ----------------------
void SimpleVideoPlayer::setup() {
  ESP_LOGI(TAG, "Setting up Simple Video Player...");
  ESP_LOGI(TAG, "  File: %s", this->file_path_.c_str());
  ESP_LOGI(TAG, "  Requested resolution: %dx%d", this->width_, this->height_);

  // Allocate input buffer in SPIRAM if available
  if (this->buffer_size_ < 65536) this->buffer_size_ = 256 * 1024;  // 256KB default
  this->input_buffer_ = (uint8_t *)heap_caps_malloc(this->buffer_size_,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!this->input_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate input buffer");
    this->mark_failed();
    return;
  }

  // Open video file
  if (!this->open_video_file_()) {
    this->mark_failed();
    return;
  }

  // Detect format
  this->format_ = this->detect_format_();
  ESP_LOGI(TAG, "Detected format: %s",
           this->format_ == MediaFormat::MP4_H264 ? "MP4/H.264" :
           (this->format_ == MediaFormat::MJPEG ? "MJPEG" : "UNKNOWN"));

  // Initialize decoders
  if (this->format_ == MediaFormat::MJPEG) {
    if (!this->init_jpeg_decoder_()) {
      ESP_LOGE(TAG, "Failed to initialize JPEG decoder");
      this->mark_failed();
      return;
    }

    // Read first frame to determine real resolution
    if (!this->read_next_mjpeg_frame_()) {
      ESP_LOGE(TAG, "Failed to read first MJPEG frame");
      this->mark_failed();
      return;
    }

    jpeg_dec_info_t info;
    if (jpeg_decoder_get_info(this->jpeg_decoder_, &info) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to get JPEG info");
      this->mark_failed();
      return;
    }

    size_t aligned_w = (info.image_width + 15) & ~15;
    size_t aligned_h = (info.image_height + 15) & ~15;
    this->width_ = aligned_w;
    this->height_ = aligned_h;

    this->rgb_buffer_size_ = aligned_w * aligned_h * 2;
    this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(
        64, this->rgb_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (!this->rgb_buffer_) {
      ESP_LOGE(TAG, "Failed to allocate RGB buffer for %zux%zu", aligned_w, aligned_h);
      this->mark_failed();
      return;
    }
    std::memset(this->rgb_buffer_, 0, this->rgb_buffer_size_);

    // Prepare LVGL descriptor
    std::memset(&this->frame_img_dsc_, 0, sizeof(this->frame_img_dsc_));
    this->frame_img_dsc_.header.cf = LV_IMG_CF_TRUE_COLOR;
    this->frame_img_dsc_.header.w = aligned_w;
    this->frame_img_dsc_.header.h = aligned_h;
    this->frame_img_dsc_.data = this->rgb_buffer_;
    this->frame_img_dsc_.data_size = this->rgb_buffer_size_;

    ESP_LOGI(TAG, "MJPEG resolution auto-detected: %zux%zu", aligned_w, aligned_h);

  } else if (this->format_ == MediaFormat::MP4_H264) {
    if (!this->parse_mp4_() || !this->init_h264_decoder_()) {
      ESP_LOGE(TAG, "Failed to initialize H.264 decoder");
      this->mark_failed();
      return;
    }

    if (this->speaker_ != nullptr && this->has_audio_) {
      if (!this->init_aac_decoder_()) {
        ESP_LOGW(TAG, "Failed to initialize AAC decoder - continuing without audio");
      }
    }

    this->rgb_buffer_size_ = (size_t)this->width_ * this->height_ * 2;
    this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(
        64, this->rgb_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (!this->rgb_buffer_) {
      ESP_LOGE(TAG, "Failed to allocate RGB buffer for H.264");
      this->mark_failed();
      return;
    }
    std::memset(this->rgb_buffer_, 0, this->rgb_buffer_size_);

    std::memset(&this->frame_img_dsc_, 0, sizeof(this->frame_img_dsc_));
    this->frame_img_dsc_.header.cf = LV_IMG_CF_TRUE_COLOR;
    this->frame_img_dsc_.header.w = this->width_;
    this->frame_img_dsc_.header.h = this->height_;
    this->frame_img_dsc_.data = this->rgb_buffer_;
    this->frame_img_dsc_.data_size = this->rgb_buffer_size_;
  } else {
    ESP_LOGE(TAG, "Unknown media format");
    this->mark_failed();
    return;
  }

  // Create UI
  this->create_ui_();

  // Timer: 1ms tick, non-blocking decode
  this->playback_timer_ = lv_timer_create(timer_cb_, 1, this);
  lv_timer_pause(this->playback_timer_);

  if (this->auto_play_) this->play();

  ESP_LOGI(TAG, "Simple Video Player initialized successfully");
}




void SimpleVideoPlayer::loop() {
  // Main processing handled in timer callback
}

void SimpleVideoPlayer::dump_config() {
  ESP_LOGCONFIG(TAG, "Simple Video Player:");
  ESP_LOGCONFIG(TAG, "  File: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG, "  Resolution: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Format: %s", this->format_ == MediaFormat::MP4_H264 ? "MP4/H.264" : "MJPEG");
  ESP_LOGCONFIG(TAG, "  Buffer size: %u", this->buffer_size_);
  ESP_LOGCONFIG(TAG, "  Auto play: %s", this->auto_play_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Loop: %s", this->loop_ ? "yes" : "no");
}

// ----------------------
// File open / detect
// ----------------------
bool SimpleVideoPlayer::open_video_file_() {
  this->file_ = fopen(this->file_path_.c_str(), "rb");
  if (this->file_ == nullptr) {
    ESP_LOGE(TAG, "Failed to open file: %s", this->file_path_.c_str());
    return false;
  }
  fseek(this->file_, 0, SEEK_END);
  this->file_size_ = ftell(this->file_);
  fseek(this->file_, 0, SEEK_SET);
  ESP_LOGI(TAG, "Video file opened: %ld bytes", this->file_size_);
  return true;
}

MediaFormat SimpleVideoPlayer::detect_format_() {
  if (this->file_ == nullptr) return MediaFormat::UNKNOWN;
  uint8_t header[8];
  if (fread(header, 1, sizeof(header), this->file_) != sizeof(header)) {
    fseek(this->file_, 0, SEEK_SET);
    return MediaFormat::UNKNOWN;
  }
  fseek(this->file_, 0, SEEK_SET);

  uint32_t box_type = ((uint32_t)header[4] << 24) | ((uint32_t)header[5] << 16) | ((uint32_t)header[6] << 8) | header[7];
  if (box_type == make_fourcc('f','t','y','p') || box_type == make_fourcc('m','o','o','v') ||
      box_type == make_fourcc('f','r','e','e') || box_type == make_fourcc('m','d','a','t')) {
    return MediaFormat::MP4_H264;
  }
  if (header[0] == 0xFF && header[1] == 0xD8) return MediaFormat::MJPEG;
  return MediaFormat::UNKNOWN;
}

// ==============================================
// JPEG/MJPEG
// ==============================================
bool SimpleVideoPlayer::init_jpeg_decoder_() {
  jpeg_decode_engine_cfg_t cfg = {
    .intr_priority = 0,
    .timeout_ms = 120,
  };
  esp_err_t ret = jpeg_new_decoder_engine(&cfg, &this->jpeg_decoder_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create JPEG decoder: %s", esp_err_to_name(ret));
    return false;
  }
  return true;
}

// Buffered MJPEG parser (no fgetc)
bool SimpleVideoPlayer::read_next_mjpeg_frame_() {
  if (this->file_ == nullptr) return false;

  const size_t SCAN_BUF = 8192;
  uint8_t *scan = (uint8_t *)heap_caps_malloc(SCAN_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!scan) {
    ESP_LOGE(TAG, "Failed to allocate scan buffer");
    return false;
  }

  bool soi_found = false;
  // search SOI (FFD8)
  while (!soi_found) {
    size_t n = fread(scan, 1, SCAN_BUF, this->file_);
    if (n == 0) break;
    for (size_t i = 0; i + 1 < n; ++i) {
      if (scan[i] == 0xFF && scan[i+1] == 0xD8) {
        long cur = ftell(this->file_);
        long found = cur - (long)(n - i);
        fseek(this->file_, found, SEEK_SET);
        soi_found = true;
        break;
      }
    }
  }
  heap_caps_free(scan);

  if (!soi_found) {
    if (this->loop_) {
      fseek(this->file_, 0, SEEK_SET);
      return this->read_next_mjpeg_frame_();
    }
    return false;
  }

  // Read up to buffer_size_ bytes containing the JPEG frame
  size_t got = fread(this->input_buffer_, 1, this->buffer_size_, this->file_);
  if (got < 4) return false;

  // find EOI (FFD9)
  for (size_t i = 2; i + 1 < got; ++i) {
    if (this->input_buffer_[i] == 0xFF && this->input_buffer_[i+1] == 0xD9) {
      this->input_size_ = i + 2;
      this->current_pos_ = ftell(this->file_);
      this->frame_count_++;
      return true;
    }
  }

  // EOI not found -> fail gracefully
  return false;
}

bool SimpleVideoPlayer::decode_mjpeg_frame_() {
  if (this->input_size_ == 0 || this->jpeg_decoder_ == nullptr) return false;

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
  return (out_size > 0);
}

// ==============================================
// MP4/H264 parsing and decode
// ==============================================
bool SimpleVideoPlayer::init_h264_decoder_() {
  esp_h264_dec_cfg_sw_t cfg = { .pic_type = ESP_H264_RAW_FMT_I420 };
  esp_h264_err_t err = esp_h264_dec_sw_new(&cfg, &this->h264_decoder_);
  if (err != ESP_H264_ERR_OK || this->h264_decoder_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create H.264 decoder: err=%d", err);
    return false;
  }
  err = esp_h264_dec_open(this->h264_decoder_);
  if (err != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open H.264 decoder: err=%d", err);
    esp_h264_dec_del(this->h264_decoder_);
    this->h264_decoder_ = nullptr;
    return false;
  }
  size_t yuv_size = (size_t)this->width_ * (size_t)this->height_ * 3 / 2;
  this->yuv_buffer_.resize(yuv_size);
  this->h264_decoder_ready_ = true;
  ESP_LOGI(TAG, "H.264 decoder initialized");
  return true;
}

bool SimpleVideoPlayer::parse_mp4_() {
  if (this->file_ == nullptr) return false;
  fseek(this->file_, 0, SEEK_SET);
  while (!feof(this->file_)) {
    uint32_t size = 0, type = 0;
    if (!this->read_mp4_box_(size, type)) break;
    if (type == make_fourcc('m','o','o','v')) {
      if (!this->parse_moov_(size - 8)) return false;
    } else {
      if (size > 8) fseek(this->file_, size - 8, SEEK_CUR);
    }
  }
  return !this->video_samples_.empty();
}

bool SimpleVideoPlayer::read_mp4_box_(uint32_t &size, uint32_t &type) {
  long pos = ftell(this->file_);
  size = read_be32(this->file_);
  type = read_be32(this->file_);
  if (size < 8 || feof(this->file_)) {
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
    if (box_type == make_fourcc('t','r','a','k')) {
      long trak_start = ftell(this->file_);
      this->parse_trak_(box_size - 8, true);  // attempt as video track
      fseek(this->file_, trak_start + box_size - 8, SEEK_SET);
    } else {
      if (box_size > 8) fseek(this->file_, box_size - 8, SEEK_CUR);
    }
  }
  return true;
}

bool SimpleVideoPlayer::parse_trak_(uint32_t size, bool is_video) {
  long end_pos = ftell(this->file_) + size;
  while (ftell(this->file_) < end_pos) {
    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) break;
    if (box_type == make_fourcc('m','d','i','a')) {
      if (!this->parse_mdia_(box_size - 8, is_video)) return false;
    } else {
      if (box_size > 8) fseek(this->file_, box_size - 8, SEEK_CUR);
    }
  }
  return true;
}

bool SimpleVideoPlayer::parse_mdia_(uint32_t size, bool is_video) {
  long end_pos = ftell(this->file_) + size;
  while (ftell(this->file_) < end_pos) {
    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) break;
    if (box_type == make_fourcc('m','i','n','f')) {
      if (!this->parse_minf_(box_size - 8, is_video)) return false;
    } else if (box_type == make_fourcc('m','d','h','d')) {
      fseek(this->file_, 12, SEEK_CUR);
      uint32_t timescale = read_be32(this->file_);
      if (is_video) this->video_timescale_ = timescale; else this->audio_timescale_ = timescale;
      fseek(this->file_, box_size - 8 - 16, SEEK_CUR);
    } else {
      if (box_size > 8) fseek(this->file_, box_size - 8, SEEK_CUR);
    }
  }
  return true;
}

bool SimpleVideoPlayer::parse_minf_(uint32_t size, bool is_video) {
  long end_pos = ftell(this->file_) + size;
  while (ftell(this->file_) < end_pos) {
    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) break;
    if (box_type == make_fourcc('s','t','b','l')) {
      if (!this->parse_stbl_(box_size - 8, is_video)) return false;
    } else {
      if (box_size > 8) fseek(this->file_, box_size - 8, SEEK_CUR);
    }
  }
  return true;
}

bool SimpleVideoPlayer::parse_stbl_(uint32_t size, bool is_video) {
  long end_pos = ftell(this->file_) + size;
  std::vector<uint32_t> sample_sizes;
  std::vector<uint32_t> chunk_offsets;
  std::vector<uint32_t> sample_durations;
  std::vector<uint32_t> keyframes;

  while (ftell(this->file_) < end_pos) {
    long current_pos = ftell(this->file_);
    uint32_t box_size, box_type;
    long box_start = current_pos;
    if (!this->read_mp4_box_(box_size, box_type)) break;

    if (box_type == make_fourcc('s','t','s','d')) {
      this->parse_stsd_(box_size - 8, is_video);
    } else if (box_type == make_fourcc('s','t','s','z')) {
      fseek(this->file_, 4, SEEK_CUR);
      uint32_t sample_size = read_be32(this->file_);
      uint32_t count = read_be32(this->file_);
      if (sample_size == 0) {
        sample_sizes.reserve(count);
        for (uint32_t i = 0; i < count; ++i) sample_sizes.push_back(read_be32(this->file_));
      } else sample_sizes.assign(count, sample_size);
    } else if (box_type == make_fourcc('s','t','c','o')) {
      fseek(this->file_, 4, SEEK_CUR);
      uint32_t count = read_be32(this->file_);
      chunk_offsets.reserve(count);
      for (uint32_t i = 0; i < count; ++i) chunk_offsets.push_back(read_be32(this->file_));
    } else if (box_type == make_fourcc('s','t','t','s')) {
      fseek(this->file_, 4, SEEK_CUR);
      uint32_t count = read_be32(this->file_);
      for (uint32_t i = 0; i < count; ++i) {
        uint32_t sample_count = read_be32(this->file_);
        uint32_t duration = read_be32(this->file_);
        for (uint32_t j = 0; j < sample_count; ++j) sample_durations.push_back(duration);
      }
    } else if (box_type == make_fourcc('s','t','s','s')) {
      fseek(this->file_, 4, SEEK_CUR);
      uint32_t count = read_be32(this->file_);
      for (uint32_t i = 0; i < count; ++i) keyframes.push_back(read_be32(this->file_));
    }

    fseek(this->file_, box_start + box_size, SEEK_SET);
    clearerr(this->file_);
  }

  // Build simple sample list (assume 1 sample per chunk for simplicity)
  if (is_video && !sample_sizes.empty() && !chunk_offsets.empty()) {
    uint32_t timestamp = 0;
    for (size_t i = 0; i < sample_sizes.size() && i < chunk_offsets.size(); ++i) {
      Mp4Sample sample;
      sample.offset = chunk_offsets[i];
      sample.size = sample_sizes[i];
      sample.duration = (i < sample_durations.size()) ? sample_durations[i] : 1000;
      sample.timestamp_ms = (uint32_t)((uint64_t)timestamp * 1000 / (this->video_timescale_ ? this->video_timescale_ : 1));
      sample.is_keyframe = keyframes.empty() || (std::find(keyframes.begin(), keyframes.end(), (uint32_t)(i+1)) != keyframes.end());
      this->video_samples_.push_back(sample);
      timestamp += sample.duration;
    }
    this->total_frames_ = this->video_samples_.size();
    ESP_LOGI(TAG, "Created %u video samples", (unsigned)this->video_samples_.size());
  }

  return true;
}

bool SimpleVideoPlayer::parse_stsd_(uint32_t size, bool is_video) {
  fseek(this->file_, 4, SEEK_CUR);  // version/flags
  uint32_t entry_count = read_be32(this->file_);
  for (uint32_t i = 0; i < entry_count; ++i) {
    uint32_t entry_size = read_be32(this->file_);
    uint32_t format = read_be32(this->file_);
    if (format == make_fourcc('a','v','c','1')) {
      this->parse_avc1_(entry_size - 8);
    } else if (format == make_fourcc('m','p','4','a')) {
      this->parse_mp4a_(entry_size - 8);
    } else {
      if (entry_size > 8) fseek(this->file_, entry_size - 8, SEEK_CUR);
    }
  }
  return true;
}

bool SimpleVideoPlayer::parse_avc1_(uint32_t size) {
  long start_pos = ftell(this->file_);
  long end_pos = start_pos + size;
  // skip reserved region to find avcC box
  fseek(this->file_, 78, SEEK_CUR);
  clearerr(this->file_);
  while (ftell(this->file_) < end_pos && !feof(this->file_)) {
    uint32_t box_size = 0, box_type = 0;
    if (!this->read_mp4_box_(box_size, box_type)) break;
    if (box_type == make_fourcc('a','v','c','C')) {
      this->parse_avcc_(box_size - 8);
      break;
    } else {
      if (box_size > 8 && box_size < 1000000) fseek(this->file_, box_size - 8, SEEK_CUR);
    }
  }
  fseek(this->file_, end_pos, SEEK_SET);
  clearerr(this->file_);
  return true;
}

bool SimpleVideoPlayer::parse_avcc_(uint32_t size) {
  fseek(this->file_, 4, SEEK_CUR);  // configurationVersion, profile, compatibility, level
  uint8_t len_size_minus_one = 0;
  if (fread(&len_size_minus_one, 1, 1, this->file_) != 1) return false;
  this->nal_length_size_ = (len_size_minus_one & 0x03) + 1;

  uint8_t num_sps = 0;
  if (fread(&num_sps, 1, 1, this->file_) != 1) return false;
  num_sps &= 0x1F;
  for (int i = 0; i < (int)num_sps; ++i) {
    uint16_t sps_len = read_be16(this->file_);
    this->sps_.resize(sps_len);
    fread(this->sps_.data(), 1, sps_len, this->file_);
  }

  uint8_t num_pps = 0;
  if (fread(&num_pps, 1, 1, this->file_) != 1) return false;
  for (int i = 0; i < (int)num_pps; ++i) {
    uint16_t pps_len = read_be16(this->file_);
    this->pps_.resize(pps_len);
    fread(this->pps_.data(), 1, pps_len, this->file_);
  }

  ESP_LOGI(TAG, "avcC: NAL length size=%d, SPS=%d bytes, PPS=%d bytes",
           this->nal_length_size_, (int)this->sps_.size(), (int)this->pps_.size());
  return true;
}

bool SimpleVideoPlayer::parse_mp4a_(uint32_t size) {
  long start_pos = ftell(this->file_);
  long end_pos = start_pos + size;
  fseek(this->file_, 28, SEEK_CUR);
  clearerr(this->file_);
  while (ftell(this->file_) < end_pos && !feof(this->file_)) {
    uint32_t box_size = 0, box_type = 0;
    if (!this->read_mp4_box_(box_size, box_type)) break;
    if (box_type == make_fourcc('e','s','d','s')) {
      this->parse_esds_(box_size - 8);
      break;
    } else {
      if (box_size > 8 && box_size < 1000000) fseek(this->file_, box_size - 8, SEEK_CUR);
    }
  }
  fseek(this->file_, end_pos, SEEK_SET);
  clearerr(this->file_);
  return true;
}

bool SimpleVideoPlayer::parse_esds_(uint32_t size) {
  long start_pos = ftell(this->file_);
  long end_pos = start_pos + size;
  fseek(this->file_, 4, SEEK_CUR);
  while (ftell(this->file_) < end_pos) {
    uint8_t tag = 0;
    if (fread(&tag, 1, 1, this->file_) != 1) break;
    uint32_t len = 0; uint8_t b = 0;
    do {
      if (fread(&b, 1, 1, this->file_) != 1) break;
      len = (len << 7) | (b & 0x7F);
    } while (b & 0x80);

    if (tag == 0x05) {  // DecoderSpecificInfo
      this->audio_config_.resize(len);
      fread(this->audio_config_.data(), 1, len, this->file_);
      this->has_audio_ = true;
      ESP_LOGI(TAG, "Found AAC config: %d bytes", len);
      break;
    } else {
      if (tag == 0x03) fseek(this->file_, 3, SEEK_CUR);
      else if (tag == 0x04) fseek(this->file_, 13, SEEK_CUR);
      else fseek(this->file_, len, SEEK_CUR);
    }
  }
  fseek(this->file_, end_pos, SEEK_SET);
  return true;
}

// ==============================================
// AUDIO (AAC) - optional (uses esp_audio_codec if available)
// ==============================================
bool SimpleVideoPlayer::init_aac_decoder_() {
#if USE_ESP_AUDIO_CODEC
  if (this->speaker_ == nullptr || !this->has_audio_) return false;
  esp_audio_dec_register_default();
  esp_aac_dec_cfg_t aac_cfg = { .aac_plus_enable = true };
  esp_audio_dec_cfg_t dec_cfg = { .type = ESP_AUDIO_TYPE_AAC, .cfg = &aac_cfg, .cfg_sz = sizeof(aac_cfg) };
  esp_audio_err_t ret = esp_audio_dec_open(&dec_cfg, &this->aac_decoder_);
  if (ret != ESP_AUDIO_ERR_OK || this->aac_decoder_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create AAC decoder: %d", ret);
    return false;
  }

  this->audio_input_buffer_ = (uint8_t *)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  this->audio_output_buffer_ = (uint8_t *)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!this->audio_input_buffer_ || !this->audio_output_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate audio buffers");
    esp_audio_dec_close(this->aac_decoder_);
    this->aac_decoder_ = nullptr;
    return false;
  }

  this->aac_decoder_ready_ = true;
  ESP_LOGI(TAG, "AAC decoder initialized: %d Hz, %d channels", this->audio_sample_rate_, this->audio_channels_);
  return true;
#else
  ESP_LOGW(TAG, "AAC decoder not available - esp_audio_codec not found");
  return false;
#endif
}

bool SimpleVideoPlayer::read_next_audio_sample_() {
  if (this->current_audio_sample_ >= this->audio_samples_.size()) return false;
  AudioSample &sample = this->audio_samples_[this->current_audio_sample_];
  fseek(this->file_, sample.offset, SEEK_SET);
  if (sample.size > 8192) {
    ESP_LOGW(TAG, "Audio sample too large: %u", sample.size);
    this->current_audio_sample_++;
    return false;
  }
  size_t bytes_read = fread(this->audio_input_buffer_, 1, sample.size, this->file_);
  if (bytes_read != sample.size) return false;
  this->audio_input_size_ = sample.size;
  this->current_audio_sample_++;
  return true;
}

bool SimpleVideoPlayer::decode_audio_frame_() {
#if USE_ESP_AUDIO_CODEC
  if (!this->aac_decoder_ready_ || this->speaker_ == nullptr || this->audio_input_size_ == 0) return false;
  esp_audio_dec_in_raw_t in_frame = { .buffer = this->audio_input_buffer_, .len = this->audio_input_size_, .consumed = 0 };
  esp_audio_dec_out_frame_t out_frame = { .buffer = this->audio_output_buffer_, .len = 16384, .decoded_size = 0 };
  esp_audio_err_t ret = esp_audio_dec_process(this->aac_decoder_, &in_frame, &out_frame);
  if (ret != ESP_AUDIO_ERR_OK) { ESP_LOGW(TAG, "AAC decode failed: %d", ret); return false; }
  if (out_frame.decoded_size > 0) {
    size_t bytes_written = this->speaker_->play(this->audio_output_buffer_, out_frame.decoded_size);
    if (bytes_written == 0) ESP_LOGW(TAG, "Failed to write audio to speaker");
  }
  return true;
#else
  return false;
#endif
}

void SimpleVideoPlayer::process_audio_() {
  if (!this->has_audio_ || this->speaker_ == nullptr) return;
  while (this->current_audio_sample_ < this->audio_samples_.size()) {
    AudioSample &sample = this->audio_samples_[this->current_audio_sample_];
    if (this->current_video_sample_ > 0) {
      Mp4Sample &video = this->video_samples_[this->current_video_sample_ - 1];
      if (sample.timestamp_ms > video.timestamp_ms + 100) break;
    }
    if (this->read_next_audio_sample_()) {
      this->decode_audio_frame_();
    } else break;
  }
}

// ==============================================
// MP4 sample read / h264 decode
// ==============================================
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
  fseek(this->file_, sample.offset, SEEK_SET);
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
  if (!this->h264_decoder_ready_ || this->input_size_ == 0) return false;

  std::vector<uint8_t> annexb_data;
  if (!this->sps_pps_sent_ && !this->sps_.empty() && !this->pps_.empty()) {
    annexb_data.insert(annexb_data.end(), {0x00,0x00,0x00,0x01});
    annexb_data.insert(annexb_data.end(), this->sps_.begin(), this->sps_.end());
    annexb_data.insert(annexb_data.end(), {0x00,0x00,0x00,0x01});
    annexb_data.insert(annexb_data.end(), this->pps_.begin(), this->pps_.end());
    this->sps_pps_sent_ = true;
  }

  size_t offset = 0;
  while (offset + this->nal_length_size_ <= this->input_size_) {
    uint32_t nalu_len = 0;
    for (int i = 0; i < this->nal_length_size_; ++i) nalu_len = (nalu_len << 8) | this->input_buffer_[offset + i];
    offset += this->nal_length_size_;
    if (offset + nalu_len > this->input_size_) break;
    annexb_data.insert(annexb_data.end(), {0x00,0x00,0x00,0x01});
    annexb_data.insert(annexb_data.end(), this->input_buffer_ + offset, this->input_buffer_ + offset + nalu_len);
    offset += nalu_len;
  }

  esp_h264_dec_in_frame_t in_frame = {};
  in_frame.raw_data.buffer = annexb_data.data();
  in_frame.raw_data.len = (uint32_t)annexb_data.size();
  in_frame.consume = 0;
  in_frame.dts = 0;
  in_frame.pts = 0;

  esp_h264_dec_out_frame_t out_frame = {};
  esp_h264_err_t err = esp_h264_dec_process(this->h264_decoder_, &in_frame, &out_frame);
  if (err != ESP_H264_ERR_OK) {
    ESP_LOGW(TAG, "H.264 decode error: %d", err);
    return false;
  }

  if (out_frame.out_size > 0 && out_frame.outbuf != nullptr) {
    this->convert_i420_to_rgb565_(out_frame.outbuf, this->rgb_buffer_, this->width_, this->height_);
    return true;
  }
  return false;
}

// YUV -> RGB565
void SimpleVideoPlayer::convert_i420_to_rgb565_(const uint8_t *yuv, uint8_t *rgb, int w, int h) {
  const uint8_t *y_plane = yuv;
  const uint8_t *u_plane = yuv + w * h;
  const uint8_t *v_plane = u_plane + (w * h / 4);
  uint16_t *rgb565 = (uint16_t *)rgb;

  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      int y = y_plane[j * w + i];
      int u = u_plane[(j / 2) * (w / 2) + (i / 2)];
      int v = v_plane[(j / 2) * (w / 2) + (i / 2)];
      int c = y - 16;
      int d = u - 128;
      int e = v - 128;
      int r = (298 * c + 409 * e + 128) >> 8;
      int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
      int b = (298 * c + 516 * d + 128) >> 8;
      r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
      g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
      b = (b < 0) ? 0 : ((b > 255) ? 255 : b);
      rgb565[j * w + i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
  }
}

// ==============================================
// UI / LVGL integration (zero-copy frame_img_dsc_)
// ==============================================
void SimpleVideoPlayer::update_display_() {
  if (this->canvas_ == nullptr) return;

  // frame_img_dsc_ data already points to rgb_buffer_
  this->frame_img_dsc_.data = this->rgb_buffer_;
  this->frame_img_dsc_.data_size = this->rgb_buffer_size_;
  lv_img_set_src(this->canvas_, &this->frame_img_dsc_);
  lv_obj_invalidate(this->canvas_);

  if (this->slider_ != nullptr && this->total_frames_ > 0) {
    int progress = (int)((this->frame_count_ * 100) / this->total_frames_);
    lv_slider_set_value(this->slider_, progress, LV_ANIM_OFF);
  }

  if (this->time_label_ != nullptr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Frame: %lu", (unsigned long)this->frame_count_);
    lv_label_set_text(this->time_label_, buf);
  }
}

void SimpleVideoPlayer::create_ui_() {
  lv_obj_t *parent = this->parent_ != nullptr ? this->parent_ : lv_scr_act();

  // Create LVGL image object (canvas_) and set zero-copy descriptor
  this->canvas_ = lv_img_create(parent);
  lv_obj_set_size(this->canvas_, this->width_, this->height_);
  lv_obj_center(this->canvas_);
  lv_img_set_src(this->canvas_, &this->frame_img_dsc_);

  // Touch layer (invisible) to show/hide controls
  this->touch_layer_ = lv_obj_create(parent);
  lv_obj_remove_style_all(this->touch_layer_);
  lv_obj_set_size(this->touch_layer_, this->width_, this->height_);
  lv_obj_center(this->touch_layer_);
  lv_obj_add_flag(this->touch_layer_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(this->touch_layer_, touch_cb_, LV_EVENT_CLICKED, this);

  if (this->controls_enabled_) {
    this->create_controls_();
    this->hide_timer_ = lv_timer_create(hide_timer_cb_, this->hide_delay_ms_, this);
    lv_timer_pause(this->hide_timer_);
  }
}

void SimpleVideoPlayer::create_controls_() {
  lv_obj_t *parent = this->parent_ != nullptr ? this->parent_ : lv_scr_act();

  this->controls_container_ = lv_obj_create(parent);
  lv_obj_set_size(this->controls_container_, this->width_, 60);
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

  // Slider
  this->slider_ = lv_slider_create(this->controls_container_);
  lv_obj_set_size(this->slider_, this->width_ - 300, 10);
  lv_obj_align(this->slider_, LV_ALIGN_LEFT_MID, 190, 0);
  lv_slider_set_range(this->slider_, 0, 100);
  lv_obj_add_event_cb(this->slider_, slider_cb_, LV_EVENT_VALUE_CHANGED, this);

  // Time label
  this->time_label_ = lv_label_create(this->controls_container_);
  lv_label_set_text(this->time_label_, "Frame: 0");
  lv_obj_align(this->time_label_, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_style_text_color(this->time_label_, lv_color_white(), 0);
}

// Playback control
void SimpleVideoPlayer::play() {
  if (this->state_ == PlayerState::PLAYING) return;
  if (this->state_ == PlayerState::STOPPED) {
    if (this->format_ == MediaFormat::MJPEG && this->file_ != nullptr) fseek(this->file_, 0, SEEK_SET);
    else if (this->format_ == MediaFormat::MP4_H264) {
      this->current_video_sample_ = 0;
      this->sps_pps_sent_ = false;
    }
    this->frame_count_ = 0;
  }
  this->state_ = PlayerState::PLAYING;
  if (this->playback_timer_ != nullptr) lv_timer_resume(this->playback_timer_);
  if (this->hide_timer_ != nullptr && this->controls_visible_) { lv_timer_reset(this->hide_timer_); lv_timer_resume(this->hide_timer_); }
  ESP_LOGI(TAG, "Playback started");
}

void SimpleVideoPlayer::pause() {
  if (this->state_ != PlayerState::PLAYING) return;
  this->state_ = PlayerState::PAUSED;
  if (this->playback_timer_ != nullptr) lv_timer_pause(this->playback_timer_);
  this->show_controls_();
  if (this->hide_timer_ != nullptr) lv_timer_pause(this->hide_timer_);
  ESP_LOGI(TAG, "Playback paused");
}

void SimpleVideoPlayer::resume() {
  if (this->state_ != PlayerState::PAUSED) return;
  this->state_ = PlayerState::PLAYING;
  if (this->playback_timer_ != nullptr) lv_timer_resume(this->playback_timer_);
  if (this->hide_timer_ != nullptr && this->controls_visible_) { lv_timer_reset(this->hide_timer_); lv_timer_resume(this->hide_timer_); }
  ESP_LOGI(TAG, "Playback resumed");
}

void SimpleVideoPlayer::stop() {
  this->state_ = PlayerState::STOPPED;
  if (this->playback_timer_ != nullptr) lv_timer_pause(this->playback_timer_);
  if (this->format_ == MediaFormat::MJPEG && this->file_ != nullptr) fseek(this->file_, 0, SEEK_SET);
  else if (this->format_ == MediaFormat::MP4_H264) { this->current_video_sample_ = 0; this->sps_pps_sent_ = false; }
  this->frame_count_ = 0;
  this->current_pos_ = 0;
  if (this->slider_ != nullptr) lv_slider_set_value(this->slider_, 0, LV_ANIM_OFF);
  this->show_controls_();
  if (this->hide_timer_ != nullptr) lv_timer_pause(this->hide_timer_);
  ESP_LOGI(TAG, "Playback stopped");
}

// LVGL callbacks (static)
void SimpleVideoPlayer::play_btn_cb_(lv_event_t *e) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(lv_event_get_user_data(e));
  if (player->is_paused()) player->resume(); else player->play();
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
    if (player->file_ != nullptr) { fseek(player->file_, new_pos, SEEK_SET); player->current_pos_ = new_pos; }
  } else if (player->format_ == MediaFormat::MP4_H264) {
    if (!player->video_samples_.empty()) {
      size_t new_sample = (player->video_samples_.size() * value) / 100;
      if (new_sample >= player->video_samples_.size()) new_sample = player->video_samples_.size() - 1;
      player->current_video_sample_ = new_sample;
      player->sps_pps_sent_ = false;
    }
  }
}

// Timer: non-blocking decode loop
void SimpleVideoPlayer::timer_cb_(lv_timer_t *timer) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(timer->user_data);
  if (player->state_ != PlayerState::PLAYING) return;

  bool got_frame = false;

  if (player->format_ == MediaFormat::MJPEG) {
    int frames_processed = 0;
    const int max_frames_per_callback = 3; // tuned small for P4 reliability
    while (frames_processed < max_frames_per_callback) {
      if (player->read_next_mjpeg_frame_()) {
        if (player->decode_mjpeg_frame_()) {
          player->update_display_();
          got_frame = true;
          frames_processed++;
        } else break;
      } else break;
    }
  } else if (player->format_ == MediaFormat::MP4_H264) {
    if (player->read_next_mp4_sample_()) {
      if (player->decode_h264_frame_()) {
        player->update_display_();
        got_frame = true;
      }
    }
    // audio processing
    player->process_audio_();
  }

  if (!got_frame) {
    if (!player->loop_) player->stop();
  }
}

void SimpleVideoPlayer::hide_timer_cb_(lv_timer_t *timer) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(timer->user_data);
  player->hide_controls_();
  lv_timer_pause(timer);
}

void SimpleVideoPlayer::touch_cb_(lv_event_t *e) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(lv_event_get_user_data(e));
  if (player->controls_visible_) player->hide_controls_(); else player->show_controls_();
}

void SimpleVideoPlayer::show_controls_() {
  if (this->controls_container_ == nullptr) return;
  lv_obj_clear_flag(this->controls_container_, LV_OBJ_FLAG_HIDDEN);
  this->controls_visible_ = true;
  if (this->state_ == PlayerState::PLAYING && this->hide_timer_ != nullptr) {
    lv_timer_reset(this->hide_timer_);
    lv_timer_resume(this->hide_timer_);
  }
}

void SimpleVideoPlayer::hide_controls_() {
  if (this->controls_container_ == nullptr) return;
  lv_obj_add_flag(this->controls_container_, LV_OBJ_FLAG_HIDDEN);
  this->controls_visible_ = false;
  if (this->hide_timer_ != nullptr) lv_timer_pause(this->hide_timer_);
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


