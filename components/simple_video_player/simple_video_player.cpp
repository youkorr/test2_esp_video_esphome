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
  ESP_LOGI(TAG, "  Resolution: %dx%d", this->width_, this->height_);

  // Allocate input buffer
  this->input_buffer_ = (uint8_t *)heap_caps_malloc(this->buffer_size_,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->input_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate input buffer");
    this->mark_failed();
    return;
  }

  // Allocate RGB buffer
  this->rgb_buffer_size_ = this->width_ * this->height_ * 2;  // RGB565
  this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(64, this->rgb_buffer_size_,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->rgb_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate RGB buffer");
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

  // Initialize appropriate decoder
  if (this->format_ == MediaFormat::MP4_H264) {
    // Parse MP4 file
    if (!this->parse_mp4_()) {
      ESP_LOGE(TAG, "Failed to parse MP4 file");
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "MP4 parsed: %u video samples, %u audio samples",
             this->video_samples_.size(), this->audio_samples_.size());

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
  ESP_LOGCONFIG(TAG, "  Resolution: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Format: %s",
                this->format_ == MediaFormat::MP4_H264 ? "MP4/H.264" : "MJPEG");
  ESP_LOGCONFIG(TAG, "  Buffer size: %u", this->buffer_size_);
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

  // Allocate YUV buffer for decoded frames
  size_t yuv_size = this->width_ * this->height_ * 3 / 2;  // I420
  this->yuv_buffer_.resize(yuv_size);

  this->h264_decoder_ready_ = true;
  ESP_LOGI(TAG, "H.264 decoder initialized");

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
  size = read_be32(this->file_);
  type = read_be32(this->file_);

  if (size == 0 || feof(this->file_)) return false;

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
  long end_pos = ftell(this->file_) + size;

  // First pass - collect sample info
  std::vector<uint32_t> sample_sizes;
  std::vector<uint32_t> chunk_offsets;
  std::vector<uint32_t> sample_durations;
  std::vector<uint32_t> keyframes;

  while (ftell(this->file_) < end_pos) {
    uint32_t box_size, box_type;
    long box_start = ftell(this->file_);
    if (!this->read_mp4_box_(box_size, box_type)) break;

    if (box_type == make_fourcc('s', 't', 's', 'd')) {
      this->parse_stsd_(box_size - 8, is_video);
    } else if (box_type == make_fourcc('s', 't', 's', 'z')) {
      // Sample sizes
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
    } else if (box_type == make_fourcc('s', 't', 'c', 'o')) {
      // Chunk offsets
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t count = read_be32(this->file_);
      chunk_offsets.reserve(count);
      for (uint32_t i = 0; i < count; i++) {
        chunk_offsets.push_back(read_be32(this->file_));
      }
    } else if (box_type == make_fourcc('s', 't', 't', 's')) {
      // Sample durations
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t count = read_be32(this->file_);
      for (uint32_t i = 0; i < count; i++) {
        uint32_t sample_count = read_be32(this->file_);
        uint32_t duration = read_be32(this->file_);
        for (uint32_t j = 0; j < sample_count; j++) {
          sample_durations.push_back(duration);
        }
      }
    } else if (box_type == make_fourcc('s', 't', 's', 's')) {
      // Sync samples (keyframes)
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t count = read_be32(this->file_);
      keyframes.reserve(count);
      for (uint32_t i = 0; i < count; i++) {
        keyframes.push_back(read_be32(this->file_));
      }
    }

    fseek(this->file_, box_start + box_size, SEEK_SET);
  }

  // Build sample list (simplified - assumes 1 sample per chunk)
  if (is_video && !sample_sizes.empty()) {
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
  long end_pos = ftell(this->file_) + size;

  // Skip to avcC box
  fseek(this->file_, 78, SEEK_CUR);  // Skip fixed avc1 header

  while (ftell(this->file_) < end_pos) {
    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) break;

    if (box_type == make_fourcc('a', 'v', 'c', 'C')) {
      this->parse_avcc_(box_size - 8);
    } else {
      if (box_size > 8) {
        fseek(this->file_, box_size - 8, SEEK_CUR);
      }
    }
  }

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
  // Skip to esds
  fseek(this->file_, 28, SEEK_CUR);  // Skip fixed mp4a header

  long end_pos = ftell(this->file_) + size - 28;

  while (ftell(this->file_) < end_pos) {
    uint32_t box_size, box_type;
    if (!this->read_mp4_box_(box_size, box_type)) break;

    if (box_type == make_fourcc('e', 's', 'd', 's')) {
      this->parse_esds_(box_size - 8);
    } else {
      if (box_size > 8) {
        fseek(this->file_, box_size - 8, SEEK_CUR);
      }
    }
  }

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
    // Convert I420 to RGB565
    this->convert_i420_to_rgb565_(out_frame.outbuf, this->rgb_buffer_,
                                   this->width_, this->height_);
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

void SimpleVideoPlayer::update_display_() {
  if (this->canvas_ == nullptr) {
    return;
  }

  lv_canvas_set_buffer(this->canvas_, this->rgb_buffer_,
                       this->width_, this->height_, LV_IMG_CF_TRUE_COLOR);
  lv_obj_invalidate(this->canvas_);

  // Update slider position
  if (this->slider_ != nullptr && this->total_frames_ > 0) {
    int progress = (this->frame_count_ * 100) / this->total_frames_;
    lv_slider_set_value(this->slider_, progress, LV_ANIM_OFF);
  }

  // Update time label
  if (this->time_label_ != nullptr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Frame: %lu", (unsigned long)this->frame_count_);
    lv_label_set_text(this->time_label_, buf);
  }
}

void SimpleVideoPlayer::create_ui_() {
  lv_obj_t *parent = this->parent_ != nullptr ? this->parent_ : lv_scr_act();

  // Create canvas for video display
  this->canvas_ = lv_canvas_create(parent);
  lv_canvas_set_buffer(this->canvas_, this->rgb_buffer_,
                       this->width_, this->height_, LV_IMG_CF_TRUE_COLOR);
  lv_obj_center(this->canvas_);

  // Create invisible touch layer over the canvas
  this->touch_layer_ = lv_obj_create(parent);
  lv_obj_remove_style_all(this->touch_layer_);
  lv_obj_set_size(this->touch_layer_, this->width_, this->height_);
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

  // Controls container at bottom
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

  // Progress slider
  this->slider_ = lv_slider_create(this->controls_container_);
  lv_obj_set_size(this->slider_, this->width_ - 300, 10);
  lv_obj_align(this->slider_, LV_ALIGN_LEFT_MID, 190, 0);
  lv_slider_set_range(this->slider_, 0, 100);
  lv_obj_add_event_cb(this->slider_, slider_cb_, LV_EVENT_VALUE_CHANGED, this);

  // Frame counter
  this->time_label_ = lv_label_create(this->controls_container_);
  lv_label_set_text(this->time_label_, "Frame: 0");
  lv_obj_align(this->time_label_, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_style_text_color(this->time_label_, lv_color_white(), 0);
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

  if (player->format_ == MediaFormat::MJPEG) {
    // For MJPEG, process multiple frames per callback for smooth playback
    // The hardware JPEG decoder is fast, process a few frames per timer tick
    int frames_processed = 0;
    const int max_frames_per_callback = 4;  // Process up to 4 frames per callback

    while (frames_processed < max_frames_per_callback) {
      if (player->read_next_mjpeg_frame_()) {
        if (player->decode_mjpeg_frame_()) {
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
        player->update_display_();
        got_frame = true;
      }
    }
    // Process audio
    player->process_audio_();
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
