#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/automation.h"

#ifdef USE_ESP_IDF

#include "lvgl.h"
#include "driver/jpeg_decode.h"
#include "driver/ppa.h"  // ESP32-P4 Pixel Processing Accelerator for hardware YUVRGB
#include "esp_timer.h"   // ESP32 native high-resolution timer for precise framerate control
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"  // Mutex for thread-safe LVGL access from ESP timer
#include "freertos/event_groups.h"  // Event groups for task synchronization
#include "freertos/task.h"  // FreeRTOS task for decode offloading
#include "esphome/components/speaker/speaker.h"
#include "yuv_rgb_convert.h"  // Software YUVRGB with lookup tables (fallback only)
#include "gmf_ppa_simple.h"   // ESP-GMF PPA wrapper for 2D-DMA + PPA hardware acceleration
#include "esp_imgfx_rotate.h"  // Hardware rotation support

// TODO: ESP-GMF full framework integration (currently using simplified wrapper)
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

// ESP audio codec (AAC decoder for MP4 audio)
#define USE_ESP_AUDIO_CODEC 1

#if USE_ESP_AUDIO_CODEC
extern "C" {
#include "decoder/impl/esp_aac_dec.h"
#include "decoder/esp_audio_dec.h"
#include "decoder/esp_audio_types.h"
}
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
  void set_preload_to_memory(bool enable) { use_file_cache_ = enable; }
  void set_rotation(uint16_t rotation) { rotation_ = rotation; }
  // Triple buffering is now mandatory for smooth playback (removed setter)

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
  void strip_jpeg_com_markers_();  // Strip COM markers that cause ESP32 JPEG decoder to fail
  bool decode_mjpeg_frame_();

  // PSRAM File Cache - Load entire file to memory to eliminate SD overhead
  bool load_file_to_cache_();

  // Cache-aware file I/O methods (work with both PSRAM cache and SD card)
  inline size_t cached_fread_(void* ptr, size_t size, size_t count) {
    if (file_cache_loaded_) {
      size_t bytes = size * count;
      size_t available = file_cache_size_ - file_cache_pos_;
      size_t to_read = std::min(bytes, available);
      if (to_read > 0) {
        memcpy(ptr, file_cache_buffer_ + file_cache_pos_, to_read);
        file_cache_pos_ += to_read;
      }
      return to_read / size;
    } else {
      return fread(ptr, size, count, file_);
    }
  }

  inline int cached_fseek_(long offset, int whence) {
    if (file_cache_loaded_) {
      if (whence == SEEK_SET) {
        file_cache_pos_ = std::min((size_t)std::max(0L, offset), file_cache_size_);
      } else if (whence == SEEK_CUR) {
        long new_pos = (long)file_cache_pos_ + offset;
        file_cache_pos_ = (size_t)std::max(0L, std::min((long)file_cache_size_, new_pos));
      } else if (whence == SEEK_END) {
        file_cache_pos_ = file_cache_size_;
      }
      return 0;
    } else {
      return fseek(file_, offset, whence);
    }
  }

  inline long cached_ftell_() {
    if (file_cache_loaded_) {
      return (long)file_cache_pos_;
    } else {
      return ftell(file_);
    }
  }

  inline int cached_feof_() {
    if (file_cache_loaded_) {
      return file_cache_pos_ >= file_cache_size_ ? 1 : 0;
    } else {
      return feof(file_);
    }
  }

  inline int cached_fgetc_() {
    if (file_cache_loaded_) {
      if (file_cache_pos_ >= file_cache_size_) {
        return EOF;
      }
      return (int)file_cache_buffer_[file_cache_pos_++];
    } else {
      return fgetc(file_);
    }
  }

  // Helper functions for reading big-endian values (cache-aware)
  inline uint32_t read_be32_() {
    uint8_t buf[4];
    if (cached_fread_(buf, 1, 4) != 4) return 0;
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
  }

  inline uint16_t read_be16_() {
    uint8_t buf[2];
    if (cached_fread_(buf, 1, 2) != 2) return 0;
    return (buf[0] << 8) | buf[1];
  }

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

  // Audio codec methods (AAC decoder)
  bool init_aac_decoder_();
  bool read_next_audio_sample_();
  bool decode_audio_frame_();
  void process_audio_();

  // PPA hardware YUVRGB conversion (replaces software converter)
  bool init_ppa_color_converter_();
  void cleanup_ppa_color_converter_();
  bool apply_ppa_color_convert_(const uint8_t *yuv, uint8_t *rgb, int w, int h);

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
  static void esp_timer_cb_(void *arg);  // ESP32 native timer callback (ultra-lightweight, just sets event bit)
  static void decode_task_(void *arg);  // FreeRTOS decode task (offloaded decoding from timer)
  static void hide_timer_cb_(lv_timer_t *timer);
  static void touch_cb_(lv_event_t *e);

  // Event bits for decode task synchronization (similar to avi_player)
  static constexpr EventBits_t EVENT_TIMER_TICK = (1 << 0);  // Timer fired, time to decode next frame
  static constexpr EventBits_t EVENT_STOP_PLAYBACK = (1 << 1);  // Stop requested
  static constexpr EventBits_t EVENT_TASK_EXIT = (1 << 2);  // Task should exit (cleanup)

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
  uint16_t rotation_{0};  // Rotation angle: 0, 90, 180, 270

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

  // PSRAM File Cache - Eliminate SD card overhead by loading entire file to RAM
  uint8_t *file_cache_buffer_{nullptr};  // Entire file loaded into PSRAM
  size_t file_cache_size_{0};            // Actual file size in cache
  size_t file_cache_pos_{0};             // Current read position in cache
  bool use_file_cache_{false};           // Enable PSRAM caching (for small files <32MB)
  bool file_cache_loaded_{false};        // true after file is loaded to PSRAM

  uint8_t *input_buffer_{nullptr};
  uint8_t *rgb_buffer_{nullptr};          // Buffer 0 - triple buffering
  uint8_t *rgb_buffer_back_{nullptr};     // Buffer 1 - triple buffering
  uint8_t *rgb_buffer_third_{nullptr};    // Buffer 2 - triple buffering (mandatory)
  size_t input_size_{0};
  size_t rgb_buffer_size_{0};
  bool use_triple_buffer_{false};         // TEMPORARY: Disabled to test memory issue (was true)
  uint8_t current_write_buffer_{0};       // 0/1/2 = write to rgb_buffer_/rgb_buffer_back_/rgb_buffer_third_

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

  // PPA hardware acceleration for YUVRGB conversion (primary method)
  // Falls back to software LUT conversion if PPA unavailable
  ppa_client_handle_t ppa_client_handle_{nullptr};
  bool ppa_color_convert_enabled_{false};

  // Hardware rotation support
  esp_imgfx_rotate_handle_t rotate_handle_{nullptr};  // Hardware rotation handle
  lv_color_t *rotate_buffer_{nullptr};  // Buffer for rotated frames
  size_t rotate_buffer_size_{0};
  bool rotation_initialized_{false};  // Track if rotation handle has been initialized with actual dimensions
  uint16_t rotated_width_{0};   // Cached rotated width (to avoid repeated queries)
  uint16_t rotated_height_{0};  // Cached rotated height (to avoid repeated queries)

  std::vector<Mp4Sample> video_samples_;
  std::vector<AudioSample> audio_samples_;
  size_t current_video_sample_{0};
  size_t current_audio_sample_{0};
  uint8_t nal_length_size_{4};
  std::vector<uint8_t> sps_;
  std::vector<uint8_t> pps_;
  std::vector<uint8_t> sps_patched_;  // Patched SPS for decoder compatibility (converts High profiles to Baseline)
  std::vector<uint8_t> pps_patched_;  // Patched PPS for decoder compatibility (CAVLC, no CABAC)
  bool sps_pps_sent_{false};

  // Helper to patch SPS for unsupported profiles (High 4:2:2, High 4:4:4, etc.)
  void patch_sps_for_decoder_compatibility_();

  // SPS/PPS reconstruction helpers
  void write_exp_golomb_ue_(std::vector<uint8_t>& data, size_t& bit_pos, uint32_t value);
  void write_exp_golomb_se_(std::vector<uint8_t>& data, size_t& bit_pos, int32_t value);
  void write_bits_(std::vector<uint8_t>& data, size_t& bit_pos, uint32_t value, int num_bits);
  std::vector<uint8_t> build_constrained_baseline_sps_(uint16_t width, uint16_t height, uint8_t level_idc);
  std::vector<uint8_t> build_constrained_baseline_pps_();
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
  // Audio codec variables (AAC decoder)
#if USE_ESP_AUDIO_CODEC
  esp_audio_dec_handle_t aac_decoder_{nullptr};
  uint8_t *audio_input_buffer_{nullptr};
  uint8_t *audio_output_buffer_{nullptr};
  size_t audio_input_size_{0};
  size_t audio_output_size_{0};
  bool has_audio_{false};
  bool aac_decoder_ready_{false};
#endif

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
  esp_timer_handle_t playback_timer_{nullptr};  // ESP32 native timer for precise framerate
  lv_timer_t *hide_timer_{nullptr};
  SemaphoreHandle_t lvgl_mutex_{nullptr};  // Mutex for thread-safe LVGL access from timer callback
  EventGroupHandle_t decode_event_group_{nullptr};  // Event group for decode task synchronization
  TaskHandle_t decode_task_handle_{nullptr};  // FreeRTOS task for video decoding (offloaded from timer)
  volatile bool frame_ready_{false};  // Flag set by decode task, checked by loop() for LVGL update
  volatile bool stop_pending_{false};  // Flag to request stop from timer (processed in loop())
  volatile uint32_t frames_dropped_{0};  // Counter for dropped frames (timer skipped due to slow processing)

  uint32_t last_frame_time_{0};
  uint32_t frame_interval_{10};
  uint32_t current_time_ms_{0};
  uint32_t total_duration_ms_{0};

  bool controls_visible_{true};
  uint32_t hide_delay_ms_{5000};  // Auto-hide controls after 5 seconds
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

