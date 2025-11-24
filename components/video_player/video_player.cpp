#include "video_player.h"
#include "esphome/core/log.h"

#include <algorithm>  // for std::find

#ifdef USE_ESP_IDF

namespace esphome {
namespace video_player {

static const char *const TAG = "video_player";

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

// --------------------------------------------------
// Setup / Loop
// --------------------------------------------------

void VideoPlayer::setup() {
  ESP_LOGI(TAG, "Setting up H.264 Video Player (source='%s', dev='%s')",
           this->source_path_.c_str(), this->device_path_.c_str());

  if (this->width_ <= 0 || this->height_ <= 0) {
    ESP_LOGE(TAG, "Invalid resolution %dx%d - please set width/height in YAML",
             this->width_, this->height_);
    this->mark_failed();
    return;
  }

  // Créer un widget LVGL (image) pour afficher la vidéo
  lv_obj_t *parent = this->parent_ ? this->parent_ : lv_scr_act();
  this->img_obj_ = lv_img_create(parent);

  // Préparer le buffer de frame (RGB565) pour LVGL
  size_t fb_size = static_cast<size_t>(this->width_) * this->height_ * 2;  // RGB565 (2 bytes/pixel)
  this->frame_buffer_.assign(fb_size, 0);

  memset(&this->img_dsc_, 0, sizeof(this->img_dsc_));
  this->img_dsc_.header.always_zero = 0;
  this->img_dsc_.header.w = this->width_;
  this->img_dsc_.header.h = this->height_;
  this->img_dsc_.header.cf = LV_IMG_CF_TRUE_COLOR;  // RGB565 supporté
  this->img_dsc_.data = this->frame_buffer_.data();
  this->img_dsc_.data_size = this->frame_buffer_.size();

  lv_img_set_src(this->img_obj_, &this->img_dsc_);
  lv_obj_center(this->img_obj_);

  // Hide initially until first frame is decoded
  lv_obj_add_flag(this->img_obj_, LV_OBJ_FLAG_HIDDEN);

  // Ouvrir le fichier vidéo (carte SD, SPIFFS, etc.)
  if (!this->source_path_.empty()) {
    this->file_ = fopen(this->source_path_.c_str(), "rb");
    if (this->file_ == nullptr) {
      ESP_LOGE(TAG, "Failed to open video source file: %s (errno=%d)",
               this->source_path_.c_str(), errno);
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "Video source opened: %s", this->source_path_.c_str());

    // Check if file is MP4 by looking at first bytes
    uint8_t header[8];
    if (fread(header, 1, 8, this->file_) == 8) {
      // MP4 files start with a box: size (4 bytes) + type (4 bytes)
      // Common first boxes: ftyp, moov, free, mdat
      uint32_t box_type = (header[4] << 24) | (header[5] << 16) | (header[6] << 8) | header[7];
      if (box_type == make_fourcc('f', 't', 'y', 'p') ||
          box_type == make_fourcc('m', 'o', 'o', 'v') ||
          box_type == make_fourcc('f', 'r', 'e', 'e') ||
          box_type == make_fourcc('m', 'd', 'a', 't')) {
        this->is_mp4_ = true;
        ESP_LOGI(TAG, "Detected MP4 container format");
      }
    }
    fseek(this->file_, 0, SEEK_SET);

    // Parse MP4 if detected
    if (this->is_mp4_) {
      if (!this->parse_mp4_()) {
        ESP_LOGE(TAG, "Failed to parse MP4 file");
        this->mark_failed();
        return;
      }
      ESP_LOGI(TAG, "MP4 parsed: %u samples, SPS=%u bytes, PPS=%u bytes",
               this->samples_.size(), this->sps_.size(), this->pps_.size());
    }
  } else {
    ESP_LOGW(TAG, "No video source path set");
    this->mark_failed();
    return;
  }

  // Initialiser le décodeur H.264 M2M (/dev/videoXX)
  if (this->init_decoder_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init H.264 decoder");
    this->mark_failed();
    return;
  }

  if (this->autoplay_) {
    this->play();
  }
}

void VideoPlayer::loop() {
  if (!this->decoder_ready_ || !this->playing_) {
    return;
  }

  // Décoder une frame à chaque boucle
  if (!this->feed_and_decode_one_frame_()) {
    if (this->eof_) {
      ESP_LOGI(TAG, "End of video reached");
      if (this->loop_) {
        ESP_LOGI(TAG, "Loop enabled, restarting file");
        if (this->file_) {
          fseek(this->file_, 0, SEEK_SET);
          this->eof_ = false;
          // Reset MP4 sample counter for looping
          if (this->is_mp4_) {
            this->current_sample_ = 0;
            this->sps_pps_sent_ = false;
          }
        }
      } else {
        this->stop();
      }
    }
  }
}

// --------------------------------------------------
// Control API
// --------------------------------------------------

void VideoPlayer::play() {
  if (!this->decoder_ready_) {
    ESP_LOGW(TAG, "Decoder not ready, cannot play");
    return;
  }
  if (this->file_ == nullptr) {
    ESP_LOGW(TAG, "No video file, cannot play");
    return;
  }
  if (this->playing_) return;

  ESP_LOGI(TAG, "Video playback started");
  this->playing_ = true;
}

void VideoPlayer::pause() {
  if (!this->playing_) return;
  ESP_LOGI(TAG, "Video playback paused");
  this->playing_ = false;
}

void VideoPlayer::stop() {
  if (!this->playing_) return;
  ESP_LOGI(TAG, "Video playback stopped");
  this->playing_ = false;
}

// --------------------------------------------------
// Decoder init / deinit (esp-h264 software decoder)
// --------------------------------------------------

esp_err_t VideoPlayer::init_decoder_() {
  if (this->decoder_ready_) return ESP_OK;

  // Create software H.264 decoder
  esp_h264_dec_cfg_sw_t cfg = {
    .pic_type = ESP_H264_RAW_FMT_I420
  };

  esp_h264_err_t err = esp_h264_dec_sw_new(&cfg, &this->decoder_);
  if (err != ESP_H264_ERR_OK || this->decoder_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create H.264 software decoder: err=%d", err);
    return ESP_FAIL;
  }

  // Open the decoder
  err = esp_h264_dec_open(this->decoder_);
  if (err != ESP_H264_ERR_OK) {
    ESP_LOGE(TAG, "Failed to open H.264 decoder: err=%d", err);
    esp_h264_dec_del(this->decoder_);
    this->decoder_ = nullptr;
    return ESP_FAIL;
  }

  // Allocate input buffer (max 256KB per frame should be enough)
  this->input_buffer_.resize(256 * 1024);

  // Allocate output buffer for I420 (Y + U/4 + V/4 = 1.5 bytes per pixel)
  size_t yuv_size = this->width_ * this->height_ * 3 / 2;
  this->yuv_buffer_.resize(yuv_size);

  this->decoder_ready_ = true;
  this->eof_ = false;
  ESP_LOGI(TAG, "H.264 software decoder initialized OK (%dx%d)", this->width_, this->height_);
  return ESP_OK;
}

void VideoPlayer::deinit_decoder_() {
  if (this->decoder_ != nullptr) {
    esp_h264_dec_close(this->decoder_);
    esp_h264_dec_del(this->decoder_);
    this->decoder_ = nullptr;
  }

  if (this->file_ != nullptr) {
    fclose(this->file_);
    this->file_ = nullptr;
  }

  this->decoder_ready_ = false;
}

// --------------------------------------------------
// I420 to RGB565 conversion
// --------------------------------------------------

void VideoPlayer::convert_i420_to_rgb565_(const uint8_t *yuv, uint8_t *rgb, int w, int h) {
  const uint8_t *y_plane = yuv;
  const uint8_t *u_plane = yuv + w * h;
  const uint8_t *v_plane = u_plane + (w * h / 4);

  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      int y_idx = j * w + i;
      int uv_idx = (j / 2) * (w / 2) + (i / 2);

      int y = y_plane[y_idx];
      int cb = u_plane[uv_idx] - 128;  // Cb (U)
      int cr = v_plane[uv_idx] - 128;  // Cr (V)

      // YCbCr to RGB conversion (BT.601)
      // R = Y + 1.402 * Cr
      // G = Y - 0.344 * Cb - 0.714 * Cr
      // B = Y + 1.772 * Cb
      int r = y + ((359 * cr) >> 8);
      int g = y - ((88 * cb + 183 * cr) >> 8);
      int b = y + ((454 * cb) >> 8);

      // Clamp values
      r = r < 0 ? 0 : (r > 255 ? 255 : r);
      g = g < 0 ? 0 : (g > 255 ? 255 : g);
      b = b < 0 ? 0 : (b > 255 ? 255 : b);

      // Convert to RGB565
      uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

      // Store in little-endian
      int rgb_idx = (j * w + i) * 2;
      rgb[rgb_idx] = rgb565 & 0xFF;
      rgb[rgb_idx + 1] = (rgb565 >> 8) & 0xFF;
    }
  }
}

// --------------------------------------------------
// Lecture H.264 (fichier) + decode 1 frame
// --------------------------------------------------

size_t VideoPlayer::read_h264_chunk_(uint8_t *buf, size_t max_size) {
  if (this->file_ == nullptr || this->eof_)
    return 0;

  // For MP4 files, read samples; for raw H.264, read chunks
  if (this->is_mp4_) {
    return this->read_next_mp4_sample_(buf, max_size);
  }

  size_t n = fread(buf, 1, max_size, this->file_);
  if (n == 0) {
    this->eof_ = true;
  }
  return n;
}

// --------------------------------------------------
// MP4 Parsing
// --------------------------------------------------

bool VideoPlayer::read_mp4_box_(uint32_t &size, uint32_t &type) {
  size = read_be32(this->file_);
  type = read_be32(this->file_);
  return size > 0;
}

bool VideoPlayer::parse_mp4_() {
  fseek(this->file_, 0, SEEK_END);
  long file_size = ftell(this->file_);
  fseek(this->file_, 0, SEEK_SET);

  ESP_LOGI(TAG, "Parsing MP4 file, size=%ld bytes", file_size);

  // First pass: parse moov for codec info
  while (ftell(this->file_) < file_size) {
    uint32_t box_size, box_type;
    long box_start = ftell(this->file_);

    if (!this->read_mp4_box_(box_size, box_type)) {
      break;
    }

    if (box_size == 0) {
      box_size = file_size - box_start;
    }

    char type_str[5] = {
      (char)((box_type >> 24) & 0xFF),
      (char)((box_type >> 16) & 0xFF),
      (char)((box_type >> 8) & 0xFF),
      (char)(box_type & 0xFF),
      0
    };
    ESP_LOGD(TAG, "Box: %.4s size=%u at %ld", type_str, box_size, box_start);

    if (box_type == make_fourcc('m', 'o', 'o', 'v')) {
      // Parse moov but don't fail if stbl is empty (fragmented MP4)
      this->parse_moov_(box_size - 8);
    } else if (box_type == make_fourcc('m', 'o', 'o', 'f')) {
      // Parse movie fragment for fragmented MP4
      long moof_data_start = box_start + box_size;  // mdat follows moof
      this->parse_moof_(box_size - 8, moof_data_start);
    } else {
      // Skip this box
      fseek(this->file_, box_start + box_size, SEEK_SET);
    }
  }

  if (this->samples_.empty()) {
    ESP_LOGE(TAG, "No video samples found in MP4");
  } else {
    ESP_LOGI(TAG, "Found %u video samples", this->samples_.size());
  }
  return !this->samples_.empty();
}

bool VideoPlayer::parse_moov_(uint32_t size) {
  long end = ftell(this->file_) + size;

  while (ftell(this->file_) < end) {
    uint32_t box_size, box_type;
    long box_start = ftell(this->file_);

    if (!this->read_mp4_box_(box_size, box_type)) {
      break;
    }

    if (box_type == make_fourcc('t', 'r', 'a', 'k')) {
      if (!this->parse_trak_(box_size - 8)) {
        // Continue to next track
      }
    }

    fseek(this->file_, box_start + box_size, SEEK_SET);
  }

  return true;
}

bool VideoPlayer::parse_trak_(uint32_t size) {
  long end = ftell(this->file_) + size;

  while (ftell(this->file_) < end) {
    uint32_t box_size, box_type;
    long box_start = ftell(this->file_);

    if (!this->read_mp4_box_(box_size, box_type)) {
      break;
    }

    if (box_type == make_fourcc('m', 'd', 'i', 'a')) {
      if (!this->parse_mdia_(box_size - 8)) {
        return false;
      }
    }

    fseek(this->file_, box_start + box_size, SEEK_SET);
  }

  return true;
}

bool VideoPlayer::parse_mdia_(uint32_t size) {
  long end = ftell(this->file_) + size;

  while (ftell(this->file_) < end) {
    uint32_t box_size, box_type;
    long box_start = ftell(this->file_);

    if (!this->read_mp4_box_(box_size, box_type)) {
      break;
    }

    if (box_type == make_fourcc('m', 'i', 'n', 'f')) {
      if (!this->parse_minf_(box_size - 8)) {
        return false;
      }
    }

    fseek(this->file_, box_start + box_size, SEEK_SET);
  }

  return true;
}

bool VideoPlayer::parse_minf_(uint32_t size) {
  long end = ftell(this->file_) + size;

  while (ftell(this->file_) < end) {
    uint32_t box_size, box_type;
    long box_start = ftell(this->file_);

    if (!this->read_mp4_box_(box_size, box_type)) {
      break;
    }

    if (box_type == make_fourcc('s', 't', 'b', 'l')) {
      if (!this->parse_stbl_(box_size - 8)) {
        return false;
      }
    }

    fseek(this->file_, box_start + box_size, SEEK_SET);
  }

  return true;
}

bool VideoPlayer::parse_stbl_(uint32_t size) {
  long end = ftell(this->file_) + size;

  std::vector<uint32_t> sample_sizes;
  std::vector<uint32_t> chunk_offsets;
  std::vector<std::pair<uint32_t, uint32_t>> samples_per_chunk;  // first_chunk, samples_per_chunk
  std::vector<uint32_t> keyframes;

  while (ftell(this->file_) < end) {
    uint32_t box_size, box_type;
    long box_start = ftell(this->file_);

    if (!this->read_mp4_box_(box_size, box_type)) {
      break;
    }

    if (box_type == make_fourcc('s', 't', 's', 'd')) {
      // Sample description - contains avc1/avcC
      this->parse_stsd_(box_size - 8);
    } else if (box_type == make_fourcc('s', 't', 's', 'z')) {
      // Sample sizes
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t sample_size = read_be32(this->file_);
      uint32_t sample_count = read_be32(this->file_);

      if (sample_size == 0) {
        for (uint32_t i = 0; i < sample_count; i++) {
          sample_sizes.push_back(read_be32(this->file_));
        }
      } else {
        sample_sizes.assign(sample_count, sample_size);
      }
    } else if (box_type == make_fourcc('s', 't', 'c', 'o')) {
      // Chunk offsets
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t entry_count = read_be32(this->file_);
      for (uint32_t i = 0; i < entry_count; i++) {
        chunk_offsets.push_back(read_be32(this->file_));
      }
    } else if (box_type == make_fourcc('c', 'o', '6', '4')) {
      // 64-bit chunk offsets
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t entry_count = read_be32(this->file_);
      for (uint32_t i = 0; i < entry_count; i++) {
        fseek(this->file_, 4, SEEK_CUR);  // Skip high 32 bits
        chunk_offsets.push_back(read_be32(this->file_));
      }
    } else if (box_type == make_fourcc('s', 't', 's', 'c')) {
      // Sample-to-chunk
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t entry_count = read_be32(this->file_);
      for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t first_chunk = read_be32(this->file_);
        uint32_t spc = read_be32(this->file_);
        fseek(this->file_, 4, SEEK_CUR);  // sample_description_index
        samples_per_chunk.push_back({first_chunk, spc});
      }
    } else if (box_type == make_fourcc('s', 't', 's', 's')) {
      // Sync samples (keyframes)
      fseek(this->file_, 4, SEEK_CUR);  // version/flags
      uint32_t entry_count = read_be32(this->file_);
      for (uint32_t i = 0; i < entry_count; i++) {
        keyframes.push_back(read_be32(this->file_));
      }
    }

    fseek(this->file_, box_start + box_size, SEEK_SET);
  }

  // Build sample table from chunks
  if (sample_sizes.empty() || chunk_offsets.empty()) {
    ESP_LOGE(TAG, "MP4 stbl incomplete: sample_sizes=%u, chunk_offsets=%u",
             sample_sizes.size(), chunk_offsets.size());
    return false;
  }

  // Default: 1 sample per chunk if stsc is missing
  if (samples_per_chunk.empty()) {
    samples_per_chunk.push_back({1, 1});
  }

  size_t sample_idx = 0;
  size_t spc_idx = 0;
  uint32_t current_spc = samples_per_chunk[0].second;

  for (size_t chunk_idx = 0; chunk_idx < chunk_offsets.size(); chunk_idx++) {
    // Check if we need to update samples_per_chunk
    if (spc_idx + 1 < samples_per_chunk.size() &&
        chunk_idx + 1 >= samples_per_chunk[spc_idx + 1].first) {
      spc_idx++;
      current_spc = samples_per_chunk[spc_idx].second;
    }

    uint32_t offset = chunk_offsets[chunk_idx];

    for (uint32_t s = 0; s < current_spc && sample_idx < sample_sizes.size(); s++) {
      Mp4Sample sample;
      sample.offset = offset;
      sample.size = sample_sizes[sample_idx];
      sample.duration = 0;
      sample.is_keyframe = keyframes.empty() ||
                          std::find(keyframes.begin(), keyframes.end(), sample_idx + 1) != keyframes.end();

      this->samples_.push_back(sample);
      offset += sample.size;
      sample_idx++;
    }
  }

  ESP_LOGI(TAG, "Built sample table: %u samples from %u chunks",
           this->samples_.size(), chunk_offsets.size());
  return true;
}

bool VideoPlayer::parse_stsd_(uint32_t size) {
  long end = ftell(this->file_) + size;

  fseek(this->file_, 4, SEEK_CUR);  // version/flags
  uint32_t entry_count = read_be32(this->file_);

  for (uint32_t i = 0; i < entry_count && ftell(this->file_) < end; i++) {
    uint32_t entry_size = read_be32(this->file_);
    uint32_t entry_type = read_be32(this->file_);
    long entry_start = ftell(this->file_) - 8;

    if (entry_type == make_fourcc('a', 'v', 'c', '1') ||
        entry_type == make_fourcc('a', 'v', 'c', '3')) {
      this->parse_avc1_(entry_size - 8);
    }

    fseek(this->file_, entry_start + entry_size, SEEK_SET);
  }

  return true;
}

bool VideoPlayer::parse_avc1_(uint32_t size) {
  long end = ftell(this->file_) + size;

  // Skip visual sample entry header (78 bytes)
  fseek(this->file_, 78, SEEK_CUR);

  while (ftell(this->file_) < end) {
    uint32_t box_size, box_type;
    long box_start = ftell(this->file_);

    if (!this->read_mp4_box_(box_size, box_type)) {
      break;
    }

    if (box_type == make_fourcc('a', 'v', 'c', 'C')) {
      this->parse_avcc_(box_size - 8);
    }

    fseek(this->file_, box_start + box_size, SEEK_SET);
  }

  return true;
}

bool VideoPlayer::parse_avcc_(uint32_t size) {
  (void)fgetc(this->file_);  // config_version - unused
  fseek(this->file_, 3, SEEK_CUR);  // profile, compatibility, level

  uint8_t length_size_minus_one = fgetc(this->file_);
  this->nal_length_size_ = (length_size_minus_one & 0x03) + 1;

  // SPS
  uint8_t num_sps = fgetc(this->file_) & 0x1F;
  for (int i = 0; i < num_sps; i++) {
    uint16_t sps_len = read_be16(this->file_);
    this->sps_.resize(sps_len);
    fread(this->sps_.data(), 1, sps_len, this->file_);
  }

  // PPS
  uint8_t num_pps = fgetc(this->file_);
  for (int i = 0; i < num_pps; i++) {
    uint16_t pps_len = read_be16(this->file_);
    this->pps_.resize(pps_len);
    fread(this->pps_.data(), 1, pps_len, this->file_);
  }

  ESP_LOGI(TAG, "avcC: NAL length size=%u, SPS=%u bytes, PPS=%u bytes",
           this->nal_length_size_, this->sps_.size(), this->pps_.size());
  return true;
}

// Fragmented MP4 parsing
bool VideoPlayer::parse_moof_(uint32_t size, long mdat_start) {
  long end = ftell(this->file_) + size;

  while (ftell(this->file_) < end) {
    uint32_t box_size, box_type;
    long box_start = ftell(this->file_);

    if (!this->read_mp4_box_(box_size, box_type)) {
      break;
    }

    if (box_type == make_fourcc('t', 'r', 'a', 'f')) {
      this->parse_traf_(box_size - 8, mdat_start);
    }

    fseek(this->file_, box_start + box_size, SEEK_SET);
  }

  return true;
}

bool VideoPlayer::parse_traf_(uint32_t size, long mdat_start) {
  long end = ftell(this->file_) + size;

  while (ftell(this->file_) < end) {
    uint32_t box_size, box_type;
    long box_start = ftell(this->file_);

    if (!this->read_mp4_box_(box_size, box_type)) {
      break;
    }

    if (box_type == make_fourcc('t', 'r', 'u', 'n')) {
      this->parse_trun_(box_size - 8, mdat_start);
    }

    fseek(this->file_, box_start + box_size, SEEK_SET);
  }

  return true;
}

bool VideoPlayer::parse_trun_(uint32_t size, long mdat_start) {
  uint8_t version = fgetc(this->file_);
  uint32_t flags = 0;
  flags |= fgetc(this->file_) << 16;
  flags |= fgetc(this->file_) << 8;
  flags |= fgetc(this->file_);

  uint32_t sample_count = read_be32(this->file_);

  // Data offset (relative to mdat start)
  int32_t data_offset = 0;
  if (flags & 0x000001) {
    data_offset = (int32_t)read_be32(this->file_);
  }

  // First sample flags
  if (flags & 0x000004) {
    read_be32(this->file_);  // skip first_sample_flags
  }

  // Calculate base offset
  long offset = mdat_start + 8 + data_offset;  // +8 for mdat header

  ESP_LOGD(TAG, "trun: %u samples, offset=%ld", sample_count, offset);

  for (uint32_t i = 0; i < sample_count; i++) {
    Mp4Sample sample;
    sample.duration = 0;
    sample.is_keyframe = (i == 0);  // Assume first sample in fragment is keyframe

    // Sample duration
    if (flags & 0x000100) {
      sample.duration = read_be32(this->file_);
    }

    // Sample size
    if (flags & 0x000200) {
      sample.size = read_be32(this->file_);
    } else {
      sample.size = 0;  // Should have default from tfhd
    }

    // Sample flags
    if (flags & 0x000400) {
      uint32_t sample_flags = read_be32(this->file_);
      // Check if this is a non-sync sample (bit 16)
      sample.is_keyframe = !(sample_flags & 0x00010000);
    }

    // Composition time offset
    if (flags & 0x000800) {
      if (version == 0) {
        read_be32(this->file_);  // unsigned
      } else {
        read_be32(this->file_);  // signed
      }
    }

    sample.offset = offset;
    offset += sample.size;

    this->samples_.push_back(sample);
  }

  return true;
}

size_t VideoPlayer::read_next_mp4_sample_(uint8_t *buf, size_t max_size) {
  if (this->file_ == nullptr || this->eof_) {
    return 0;
  }

  size_t written = 0;

  // Send SPS/PPS first (as Annex-B NAL units)
  if (!this->sps_pps_sent_) {
    // Start code + SPS
    if (!this->sps_.empty() && written + 4 + this->sps_.size() <= max_size) {
      buf[written++] = 0x00;
      buf[written++] = 0x00;
      buf[written++] = 0x00;
      buf[written++] = 0x01;
      memcpy(buf + written, this->sps_.data(), this->sps_.size());
      written += this->sps_.size();
    }

    // Start code + PPS
    if (!this->pps_.empty() && written + 4 + this->pps_.size() <= max_size) {
      buf[written++] = 0x00;
      buf[written++] = 0x00;
      buf[written++] = 0x00;
      buf[written++] = 0x01;
      memcpy(buf + written, this->pps_.data(), this->pps_.size());
      written += this->pps_.size();
    }

    this->sps_pps_sent_ = true;
    if (written > 0) {
      return written;
    }
  }

  // Read next sample
  if (this->current_sample_ >= this->samples_.size()) {
    this->eof_ = true;
    return 0;
  }

  Mp4Sample &sample = this->samples_[this->current_sample_];
  fseek(this->file_, sample.offset, SEEK_SET);

  // Read sample and convert from AVCC to Annex-B
  size_t remaining = sample.size;
  while (remaining > 0 && written < max_size) {
    // Read NAL length
    uint32_t nal_len = 0;
    for (int i = 0; i < this->nal_length_size_; i++) {
      nal_len = (nal_len << 8) | fgetc(this->file_);
    }
    remaining -= this->nal_length_size_;

    if (nal_len > remaining || written + 4 + nal_len > max_size) {
      break;
    }

    // Write Annex-B start code
    buf[written++] = 0x00;
    buf[written++] = 0x00;
    buf[written++] = 0x00;
    buf[written++] = 0x01;

    // Read NAL data
    fread(buf + written, 1, nal_len, this->file_);
    written += nal_len;
    remaining -= nal_len;
  }

  this->current_sample_++;
  return written;
}

bool VideoPlayer::feed_and_decode_one_frame_() {
  if (!this->decoder_ready_ || this->decoder_ == nullptr || this->file_ == nullptr)
    return false;

  // Read next H.264 sample (Annex-B format)
  size_t n = this->read_h264_chunk_(this->input_buffer_.data(), this->input_buffer_.size());
  if (n == 0) {
    // EOF or no data
    return false;
  }

  // Prepare input frame
  esp_h264_dec_in_frame_t in_frame = {};
  in_frame.raw_data.buffer = this->input_buffer_.data();
  in_frame.raw_data.len = n;
  in_frame.pts = 0;
  in_frame.dts = 0;

  // Prepare output frame - set buffer capacity
  esp_h264_dec_out_frame_t out_frame = {};
  out_frame.outbuf = this->yuv_buffer_.data();
  out_frame.out_size = this->yuv_buffer_.size();  // Tell decoder buffer capacity

  // Decode
  esp_h264_err_t err = esp_h264_dec_process(this->decoder_, &in_frame, &out_frame);
  if (err != ESP_H264_ERR_OK) {
    ESP_LOGW(TAG, "Decode failed: err=%d, input_size=%u", err, n);
    return true;  // Continue trying
  }

  // Check if we got a decoded frame
  if (out_frame.out_size > 0) {
    ESP_LOGD(TAG, "Decoded frame: %u bytes (expected %u)",
             out_frame.out_size, this->yuv_buffer_.size());

    // Convert I420 to RGB565
    this->convert_i420_to_rgb565_(this->yuv_buffer_.data(),
                                   this->frame_buffer_.data(),
                                   this->width_, this->height_);

    // Update LVGL display
    this->update_lvgl_frame_(this->frame_buffer_.data(), this->frame_buffer_.size());
  }

  return true;
}

// --------------------------------------------------
// LVGL : mise à jour de l'image
// --------------------------------------------------

void VideoPlayer::update_lvgl_frame_(const uint8_t *rgb565, size_t len) {
  if (this->img_obj_ == nullptr || rgb565 == nullptr)
    return;
  if (len > this->frame_buffer_.size())
    len = this->frame_buffer_.size();

  memcpy(this->frame_buffer_.data(), rgb565, len);

  // Actualiser descripteur LVGL
  this->img_dsc_.data = this->frame_buffer_.data();
  this->img_dsc_.data_size = this->frame_buffer_.size();

  lv_img_set_src(this->img_obj_, &this->img_dsc_);
  lv_obj_invalidate(this->img_obj_);

  // Show image if it was hidden
  if (lv_obj_has_flag(this->img_obj_, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_clear_flag(this->img_obj_, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace video_player
}  // namespace esphome

#else  // !USE_ESP_IDF

namespace esphome {
namespace video_player {

void VideoPlayer::setup() {
  ESP_LOGE("video_player", "VideoPlayer requires ESP-IDF");
  this->mark_failed();
}

void VideoPlayer::loop() {}

void VideoPlayer::play() {}
void VideoPlayer::pause() {}
void VideoPlayer::stop() {}

}  // namespace video_player
}  // namespace esphome

#endif  // USE_ESP_IDF
