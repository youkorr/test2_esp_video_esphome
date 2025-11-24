#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/automation.h"

#ifdef USE_ESP_IDF

// LVGL
#include "lvgl.h"

// esp-h264 software decoder
extern "C" {
#include "esp_h264_dec.h"
#include "esp_h264_dec_sw.h"
#include "esp_h264_types.h"
}

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#endif  // USE_ESP_IDF

namespace esphome {
namespace video_player {

// MP4 sample entry
struct Mp4Sample {
  uint32_t offset;
  uint32_t size;
  uint32_t duration;
  bool is_keyframe;
};

class VideoPlayer : public Component {
 public:
  // --------- Configuration depuis YAML -------------
  void set_source_path(const std::string &path) { source_path_ = path; }
  void set_device_path(const std::string &path) { device_path_ = path; }

  void set_width(int w) { width_ = w; }
  void set_height(int h) { height_ = h; }
  void set_resolution(int w, int h) { width_ = w; height_ = h; }

  void set_autoplay(bool b) { autoplay_ = b; }
  void set_loop(bool b) { loop_ = b; }
#ifdef USE_ESP_IDF
  void set_parent(lv_obj_t *parent) { parent_ = parent; }
#endif

  // --------- ESPHome ----------
  void setup() override;
  void loop() override;

  // --------- Contrôle vidéo depuis automations ---------
  void play();
  void pause();
  void stop();
  bool is_playing() const { return playing_; }

  float get_setup_priority() const override {
    return setup_priority::LATE;  // comme le LVGL panel
  }

 protected:

  // ============================
  // H.264 SOFTWARE DECODER
  // ============================

  // --- init, deinit decoder ---
  esp_err_t init_decoder_();
  void deinit_decoder_();

  // --- YUV to RGB conversion ---
  void convert_i420_to_rgb565_(const uint8_t *yuv, uint8_t *rgb, int w, int h);

  // --- MP4 parsing ---
  bool parse_mp4_();
  bool read_mp4_box_(uint32_t &size, uint32_t &type);
  bool parse_moov_(uint32_t size);
  bool parse_trak_(uint32_t size);
  bool parse_mdia_(uint32_t size);
  bool parse_minf_(uint32_t size);
  bool parse_stbl_(uint32_t size);
  bool parse_stsd_(uint32_t size);
  bool parse_avc1_(uint32_t size);
  bool parse_avcc_(uint32_t size);
  bool parse_stts_(uint32_t size);
  bool parse_stsc_(uint32_t size);
  bool parse_stsz_(uint32_t size);
  bool parse_stco_(uint32_t size);
  bool parse_stss_(uint32_t size);
  // Fragmented MP4 support
  bool parse_moof_(uint32_t size, long mdat_start);
  bool parse_traf_(uint32_t size, long mdat_start);
  bool parse_trun_(uint32_t size, long mdat_start);

  // --- lecture du fichier H.264 ---
  size_t read_h264_chunk_(uint8_t *buf, size_t max_size);
  size_t read_next_mp4_sample_(uint8_t *buf, size_t max_size);

  // --- decode une frame ---
  bool feed_and_decode_one_frame_();

  // --- update LVGL ---
  void update_lvgl_frame_(const uint8_t *rgb565, size_t len);

  // =================================
  // Membres
  // =================================

  // Chemin du fichier vidéo (ex: "/sdcard/video.mp4")
  std::string source_path_;

  // Chemin du device H.264 (ex: "/dev/video30")
  std::string device_path_;

  // Résolution de sortie
  int width_{0};
  int height_{0};

  // Contrôle
  bool autoplay_{false};
  bool loop_{false};
  bool playing_{false};
  bool eof_{false};

  // MP4 parsing
  bool is_mp4_{false};
  std::vector<Mp4Sample> samples_;
  size_t current_sample_{0};
  uint8_t nal_length_size_{4};  // Usually 4 bytes for length prefix
  std::vector<uint8_t> sps_;
  std::vector<uint8_t> pps_;
  bool sps_pps_sent_{false};

#ifdef USE_ESP_IDF

  // Handle LVGL
  lv_obj_t *parent_{nullptr};
  lv_obj_t *img_obj_{nullptr};
  lv_img_dsc_t img_dsc_{};

  // Buffer RGB565 for LVGL
  std::vector<uint8_t> frame_buffer_;

  // esp-h264 software decoder
  esp_h264_dec_handle_t decoder_{nullptr};

  // Input buffer for encoded H.264 data
  std::vector<uint8_t> input_buffer_;

  // Output buffer for decoded I420 data
  std::vector<uint8_t> yuv_buffer_;

  // Fichier H.264
  FILE *file_{nullptr};

  bool decoder_ready_{false};

#endif  // USE_ESP_IDF
};

// Action templates for automation
template<typename... Ts> class PlayAction : public Action<Ts...>, public Parented<VideoPlayer> {
 public:
  void play(const Ts &...x) override { this->parent_->play(); }
};

template<typename... Ts> class PauseAction : public Action<Ts...>, public Parented<VideoPlayer> {
 public:
  void play(const Ts &...x) override { this->parent_->pause(); }
};

template<typename... Ts> class StopAction : public Action<Ts...>, public Parented<VideoPlayer> {
 public:
  void play(const Ts &...x) override { this->parent_->stop(); }
};

}  // namespace video_player
}  // namespace esphome

