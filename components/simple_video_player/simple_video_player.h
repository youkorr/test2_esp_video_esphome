#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/automation.h"

#ifdef USE_ESP_IDF

#include "lvgl.h"
#include "driver/jpeg_decode.h"
#include "esphome/components/speaker/speaker.h"
#include "yuv_rgb_convert.h"

// TODO: ESP-GMF will be added later
// #include "esp_gmf_element.h"
// #include "esp_gmf_pipeline.h"
// #include "esp_gmf_pool.h"
// #include "gmf_vid_dec.h"

extern "C" {
#include "esp_h264_dec.h"
#include "esp_h264_dec_sw.h"
#include "esp_h264_types.h"
#include "basetype.h"
#include "h264bsd_decoder.h"


}

#if __has_include("decoder/esp_audio_dec.h")
#define USE_ESP_AUDIO_CODEC 1
extern "C" {
#include "decoder/esp_audio_dec.h"
#include "decoder/esp_audio_dec_reg.h"
#include "decoder/impl/esp_aac_dec.h"
}
#else
#define USE_ESP_AUDIO_CODEC 0
#endif

namespace esphome {
namespace simple_video_player {

enum class PlayerState {
  STOPPED,
  PLAYING,
  PAUSED
};

enum class MediaFormat {
  UNKNOWN,
  MJPEG,
  MP4_H264,
  MKV_H264,
  GIF_ANIMATED
};

struct Mp4Sample {
  uint32_t offset;
  uint32_t size;
  uint32_t duration;
  uint32_t timestamp_ms;
  bool is_keyframe;
};

struct AudioSample {
  uint32_t offset;
  uint32_t size;
  uint32_t timestamp_ms;
};

struct MkvSample {
  uint64_t offset;
  uint32_t size;
  uint64_t timestamp_ns;  // Matroska uses nanoseconds
  uint16_t track_number;
  bool is_keyframe;
};

class SimpleVideoPlayer : public Component {
 public:
  void set_file_path(const std::string &path) { file_path_ = path; }
  void set_width(int w) { width_ = w; }
  void set_height(int h) { height_ = h; }
  void set_buffer_size(size_t size) { buffer_size_ = size; }
  void set_auto_play(bool b) { auto_play_ = b; }
  void set_loop(bool b) { loop_ = b; }
  void set_show_controls(bool b) { controls_enabled_ = b; }
  void set_parent(lv_obj_t *parent) { parent_ = parent; }
  void set_speaker(speaker::Speaker *spk) { speaker_ = spk; }
  void set_media_player_entity(const std::string &entity) { media_player_entity_ = entity; }
  void set_fps(float fps) {
    if (fps > 0 && fps <= 120) {
      frame_interval_ = (uint32_t)(1000.0f / fps);
      fps_override_ = true;
      ESP_LOGI("simple_video_player", "FPS configured: %.2f (interval: %u ms)", fps, frame_interval_);
    }
  }
  void set_max_http_file_size(size_t size) { max_http_file_size_ = size; }

  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void play();
  void pause();
  void stop();
  void resume();
  bool is_playing() const { return state_ == PlayerState::PLAYING; }
  bool is_paused() const { return state_ == PlayerState::PAUSED; }

 protected:
  MediaFormat detect_format_();
  bool detect_jpeg_resolution_(int &width, int &height);
  bool detect_avi_framerate_();
  bool parse_avi_header_();  // Parse AVI header and find movi offset
  bool extract_mp4_resolution_();

  bool init_jpeg_decoder_();
  bool read_next_mjpeg_frame_();
  bool decode_mjpeg_frame_();

  bool init_gif_decoder_();
  bool parse_gif_header_();
  bool read_next_gif_frame_();
  bool decode_gif_frame_();

  bool init_h264_decoder_();
  bool parse_mp4_();
  bool read_mp4_box_(uint32_t &size, uint32_t &type);
  bool parse_moov_(uint32_t size);
  bool parse_trak_(uint32_t size, bool is_video);
  bool parse_mdia_(uint32_t size, bool is_video);
  bool parse_minf_(uint32_t size, bool is_video);
  bool parse_stbl_(uint32_t size, bool is_video);
  bool parse_stsd_(uint32_t size, bool is_video);
  bool parse_avc1_(uint32_t size);
  bool parse_avcc_(uint32_t size);
  bool parse_mp4a_(uint32_t size);
  bool parse_esds_(uint32_t size);
  bool parse_stts_(uint32_t size, bool is_video);
  bool parse_stsc_(uint32_t size, bool is_video);
  bool parse_stsz_(uint32_t size, bool is_video);
  bool parse_stco_(uint32_t size, bool is_video);
  bool parse_stss_(uint32_t size);
  bool read_next_mp4_sample_();
  bool decode_h264_frame_();

  // MKV/Matroska parsing
  bool parse_mkv_();
  uint64_t read_ebml_id_();
  uint64_t read_ebml_size_();
  uint64_t read_ebml_vint_();  // Read EBML variable-length integer (for data values)
  bool read_ebml_uint_(uint64_t size, uint64_t &value);
  bool read_ebml_string_(uint64_t size, std::string &value);
  bool parse_mkv_segment_(uint64_t size);
  bool parse_mkv_info_(uint64_t size);
  bool parse_mkv_tracks_(uint64_t size);
  bool parse_mkv_track_entry_(uint64_t size);
  bool parse_mkv_clusters_();
  bool read_next_mkv_sample_();

  bool init_aac_decoder_();
  bool read_next_audio_sample_();
  bool decode_audio_frame_();
  void process_audio_();

  void convert_i420_to_rgb565_(const uint8_t *yuv, uint8_t *rgb, int w, int h);

  bool download_http_file_(const char *url);  // Download HTTP/HTTPS file to SPIRAM
  bool open_video_file_();
  void complete_video_initialization_();  // Complete initialization after HTTP download
  void update_display_();
  void create_ui_();
  void create_controls_();
  void show_controls_();
  void hide_controls_();
  void format_time_(char *buf, size_t buf_size, uint32_t time_ms);

  static void play_btn_cb_(lv_event_t *e);
  static void pause_btn_cb_(lv_event_t *e);
  static void stop_btn_cb_(lv_event_t *e);
  static void slider_cb_(lv_event_t *e);
  static void timer_cb_(lv_timer_t *timer);
  static void hide_timer_cb_(lv_timer_t *timer);
  static void touch_cb_(lv_event_t *e);

  std::string file_path_;
  int width_{800};   // Default/configured width (used if auto-detection fails)
  int height_{480};  // Default/configured height (used if auto-detection fails)
  int actual_width_{0};   // Detected actual video width
  int actual_height_{0};  // Detected actual video height
  int aligned_width_{0};  // 16-byte aligned width for decoder
  int aligned_height_{0}; // 16-byte aligned height for decoder
  size_t buffer_size_{300000};
  bool auto_play_{true};
  bool loop_{false};
  bool controls_enabled_{true};
  bool fps_override_{false};

  PlayerState state_{PlayerState::STOPPED};
  MediaFormat format_{MediaFormat::UNKNOWN};
  FILE *file_{nullptr};
  long file_size_{0};
  long current_pos_{0};
  uint32_t frame_count_{0};
  uint32_t total_frames_{0};

  // HTTP/HTTPS streaming support
  uint8_t *http_buffer_{nullptr};  // Buffer for downloaded HTTP content
  size_t http_buffer_size_{0};     // Size of HTTP buffer
  size_t max_http_file_size_{40 * 1024 * 1024};  // Maximum HTTP file size (40MB default)
  bool is_http_source_{false};     // true if file_path_ is http:// or https://
  bool http_download_pending_{false};  // true if HTTP download needs to happen in loop()
  bool initialization_complete_{false};  // true if video player is fully initialized
  bool auto_play_after_download_{false};  // true if should auto-play after re-downloading

  uint8_t *input_buffer_{nullptr};
  uint8_t *rgb_buffer_{nullptr};
  size_t input_size_{0};
  size_t rgb_buffer_size_{0};

  lv_img_dsc_t frame_img_dsc_{};

  // JPEG decoder (hardware)
  jpeg_decoder_handle_t jpeg_decoder_{nullptr};

  // GIF decoder data
  struct GifFrame {
    uint32_t offset;         // File offset of frame data
    uint32_t size;           // Size of compressed data
    uint16_t delay_ms;       // Delay in milliseconds
    uint16_t left, top;      // Frame position
    uint16_t width, height;  // Frame dimensions
    bool has_transparency;   // Has transparent color
    uint8_t transparent_idx; // Transparent color index
    uint8_t disposal_method; // Frame disposal method
  };
  std::vector<GifFrame> gif_frames_;
  size_t current_gif_frame_{0};
  uint16_t gif_width_{0};
  uint16_t gif_height_{0};
  std::vector<uint8_t> gif_global_palette_;  // 256 colors × 3 bytes (RGB)
  std::vector<uint8_t> gif_frame_buffer_;    // Decoded frame data (indexes)
  uint16_t gif_background_color_{0};
  bool gif_has_global_palette_{false};
  bool gif_decoder_ready_{false};

  esp_h264_dec_handle_t h264_decoder_{nullptr};
  std::vector<uint8_t> yuv_buffer_;
  bool h264_decoder_ready_{false};
  YuvRgbConverter *yuv_converter_{nullptr};  // Optimized YUV→RGB with BT.709 support

  std::vector<Mp4Sample> video_samples_;
  std::vector<AudioSample> audio_samples_;
  size_t current_video_sample_{0};
  size_t current_audio_sample_{0};
  uint8_t nal_length_size_{4};
  std::vector<uint8_t> sps_;
  std::vector<uint8_t> pps_;
  bool sps_pps_sent_{false};
  uint32_t video_timescale_{1000};
  uint32_t audio_timescale_{44100};

  // MKV/Matroska data
  std::vector<MkvSample> mkv_samples_;
  size_t current_mkv_sample_{0};
  uint64_t mkv_timecode_scale_{1000000};  // Default 1ms in nanoseconds
  uint16_t mkv_video_track_{0};
  uint16_t mkv_audio_track_{0};
  uint64_t mkv_segment_start_{0};
  uint64_t mkv_cluster_start_{0};

  // AVI/MJPEG data
  bool is_avi_format_{false};  // true if AVI container, false if raw MJPEG
  long avi_movi_offset_{0};    // File offset where movi list starts
  uint32_t avi_total_frames_{0};

  uint32_t audio_sample_rate_{44100};
  uint8_t audio_channels_{2};
  std::vector<uint8_t> audio_config_;

  speaker::Speaker *speaker_{nullptr};
  std::string media_player_entity_;
#if USE_ESP_AUDIO_CODEC
  esp_audio_dec_handle_t aac_decoder_{nullptr};
#else
  void *aac_decoder_{nullptr};
#endif
  uint8_t *audio_input_buffer_{nullptr};
  uint8_t *audio_output_buffer_{nullptr};
  size_t audio_input_size_{0};
  size_t audio_output_size_{0};
  bool has_audio_{false};
  bool aac_decoder_ready_{false};

  lv_obj_t *parent_{nullptr};
  lv_obj_t *canvas_{nullptr};
  lv_obj_t *play_btn_{nullptr};
  lv_obj_t *pause_btn_{nullptr};
  lv_obj_t *stop_btn_{nullptr};
  lv_obj_t *slider_{nullptr};
  lv_obj_t *time_label_{nullptr};
  lv_obj_t *format_badge_{nullptr};
  lv_obj_t *resolution_label_{nullptr};
  lv_obj_t *loading_spinner_{nullptr};
  lv_obj_t *controls_container_{nullptr};
  lv_obj_t *touch_layer_{nullptr};
  lv_timer_t *playback_timer_{nullptr};
  lv_timer_t *hide_timer_{nullptr};

  uint32_t last_frame_time_{0};
  uint32_t frame_interval_{10};
  uint32_t current_time_ms_{0};
  uint32_t total_duration_ms_{0};

  bool controls_visible_{true};
  uint32_t hide_delay_ms_{3000};
};

template<typename... Ts> class PlayAction : public Action<Ts...>, public Parented<SimpleVideoPlayer> {
 public:
  void play(const Ts &...x) override { this->parent_->play(); }
};

template<typename... Ts> class PauseAction : public Action<Ts...>, public Parented<SimpleVideoPlayer> {
 public:
  void play(const Ts &...x) override { this->parent_->pause(); }
};

template<typename... Ts> class StopAction : public Action<Ts...>, public Parented<SimpleVideoPlayer> {
 public:
  void play(const Ts &...x) override { this->parent_->stop(); }
};

template<typename... Ts> class ResumeAction : public Action<Ts...>, public Parented<SimpleVideoPlayer> {
 public:
  void play(const Ts &...x) override { this->parent_->resume(); }
};

}  // namespace simple_video_player
}  // namespace esphome

#endif

