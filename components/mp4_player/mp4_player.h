#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/automation.h"

#ifdef USE_ESP_IDF

#include "lvgl.h"
#include "driver/jpeg_decode.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esphome/components/speaker/speaker.h"
#include "esphome/components/audio/audio.h"
#include <dirent.h>
#include <sys/stat.h>

// Use esp_extractor API directly (no BSP dependency)
extern "C" {
#include "esp_extractor.h"
#include "esp_extractor_reg.h"
#include "esp_mp4_extractor.h"
#include "esp_avi_extractor.h"
#include "mem_pool.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_reg.h"
#include "esp_aac_dec.h"
#include "esp_mp3_dec.h"
#include "esp_flac_dec.h"
#include "esp_pcm_dec.h"
}

namespace esphome {
namespace mp4_player {

enum class PlayerState {
  STOPPED,
  PLAYING,
  PAUSED
};

class Mp4Player : public Component {
 public:
  void set_file_path(const std::string &path) { file_path_ = path; }
  void set_parent(lv_obj_t *parent) { parent_ = parent; }
  void set_speaker(speaker::Speaker *spk) { speaker_ = spk; }
  void set_volume(int vol) { volume_level_ = (uint8_t)vol; }
  void set_loop(bool loop) { loop_ = loop; }
  void set_auto_play(bool auto_play) { auto_play_ = auto_play; }
  void set_show_controls(bool show) { controls_enabled_ = show; }
  void set_usb_storage(Component *usb_storage) { usb_storage_ = usb_storage; }
  void add_media_directory(const std::string &dir) { media_directories_.push_back(dir); }

  void add_on_play_callback(std::function<void()> &&callback) { on_play_callbacks_.add(std::move(callback)); }
  void add_on_stop_callback(std::function<void()> &&callback) { on_stop_callbacks_.add(std::move(callback)); }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void play();
  void pause();
  void stop();
  void play_file(const std::string &path);
  void show_file_browser();
  bool is_playing() const { return state_ == PlayerState::PLAYING; }
  bool is_paused() const { return state_ == PlayerState::PAUSED; }
  bool is_browser_active() const { return browser_active_; }

 protected:
  // UI
  void create_ui_();
  void create_controls_();
  void show_controls_();
  void hide_controls_();
  void update_progress_();
  void format_time_(char *buf, size_t buf_size, uint32_t time_ms);

  // JPEG processing
  static size_t strip_jpeg_com_markers_(uint8_t *data, size_t size);

  // Audio
  static esp_audio_simple_dec_type_t map_audio_format_(extractor_audio_format_t fmt);

  // Volume
  void apply_volume_to_pcm_(uint8_t *pcm_data, size_t size);

  // Static callbacks
  static void play_btn_cb_(lv_event_t *e);
  static void stop_btn_cb_(lv_event_t *e);
  static void progress_slider_cb_(lv_event_t *e);
  static void volume_slider_cb_(lv_event_t *e);
  static void hide_timer_cb_(lv_timer_t *timer);
  static void touch_cb_(lv_event_t *e);

  // Playback task
  static void playback_task_(void *arg);

  // File I/O callbacks for esp_extractor
  static void *file_open_cb_(char *url, void *ctx);
  static int file_read_cb_(void *data, uint32_t size, void *ctx);
  static int file_seek_cb_(uint32_t position, void *ctx);
  static int file_close_cb_(void *ctx);
  static uint32_t file_size_cb_(void *ctx);

  // File browser
  void create_file_browser_();
  void destroy_file_browser_();
  void scan_media_files_(const std::string &path);
  bool is_video_file_(const std::string &name);
  static void file_item_cb_(lv_event_t *e);
  static void back_btn_cb_(lv_event_t *e);
  static void refresh_btn_cb_(lv_event_t *e);

  struct FileEntry {
    std::string full_path;
    std::string name;
    size_t size;
    bool is_directory;
  };
  std::vector<FileEntry> file_entries_;
  std::vector<std::string> media_directories_;
  std::string current_browse_path_;
  bool browser_active_{false};
  lv_obj_t *browser_container_{nullptr};
  lv_obj_t *browser_list_{nullptr};
  lv_obj_t *browser_title_{nullptr};

  // Configuration
  std::string file_path_;
  bool loop_{true};
  bool auto_play_{true};
  bool controls_enabled_{true};
  uint8_t volume_level_{60};

  // USB storage (optional - when set, ensures USB is mounted before file access)
  Component *usb_storage_{nullptr};

  // State
  PlayerState state_{PlayerState::STOPPED};
  uint32_t frame_count_{0};
  uint32_t total_duration_ms_{0};
  uint32_t current_time_ms_{0};
  uint32_t video_width_{0};
  uint32_t video_height_{0};
  uint32_t video_fps_{25};
  bool has_audio_{false};

  // JPEG decoder
  jpeg_decoder_handle_t jpeg_decoder_{nullptr};

  // Display buffers
  uint8_t *display_buffer_[2]{nullptr, nullptr};
  uint32_t display_buffer_size_{0};
  uint8_t current_display_buf_{0};
  uint8_t *jpeg_buffer_{nullptr};

  // LVGL UI
  lv_obj_t *parent_{nullptr};
  lv_obj_t *canvas_{nullptr};
  lv_obj_t *controls_container_{nullptr};
  lv_obj_t *touch_layer_{nullptr};
  lv_obj_t *play_btn_{nullptr};
  lv_obj_t *stop_btn_{nullptr};
  lv_obj_t *progress_slider_{nullptr};
  lv_obj_t *volume_slider_{nullptr};
  lv_obj_t *volume_icon_{nullptr};
  lv_obj_t *time_label_{nullptr};
  lv_obj_t *format_badge_{nullptr};
  lv_obj_t *resolution_label_{nullptr};
  lv_obj_t *loading_label_{nullptr};
  lv_timer_t *hide_timer_{nullptr};

  // Playback task
  TaskHandle_t playback_task_handle_{nullptr};
  EventGroupHandle_t playback_event_group_{nullptr};
  volatile bool frame_ready_{false};
  volatile bool stop_requested_{false};

  // Event bits
  static constexpr EventBits_t EVENT_START = (1 << 0);
  static constexpr EventBits_t EVENT_STOP = (1 << 1);
  static constexpr EventBits_t EVENT_TASK_EXIT = (1 << 2);

  // Speaker
  speaker::Speaker *speaker_{nullptr};

  // Audio decoder
  esp_audio_simple_dec_handle_t audio_decoder_{nullptr};
  uint8_t *audio_pcm_buffer_{nullptr};
  uint32_t audio_pcm_buffer_size_{0};
  extractor_audio_format_t audio_format_{};
  uint32_t audio_sample_rate_{0};
  uint8_t audio_channels_{0};
  uint8_t audio_bits_per_sample_{0};
  bool audio_decoder_ready_{false};

  // Audio ring buffer for decoupling decode from speaker output
  uint8_t *audio_ring_buffer_{nullptr};
  size_t audio_ring_size_{0};
  volatile size_t audio_ring_read_{0};
  volatile size_t audio_ring_write_{0};

  // Audio output task
  static void audio_output_task_(void *arg);
  TaskHandle_t audio_task_handle_{nullptr};
  volatile bool audio_task_running_{false};

  size_t audio_ring_available_() const;
  size_t audio_ring_free_() const;
  size_t audio_ring_push_(const uint8_t *data, size_t len);
  size_t audio_ring_pop_(uint8_t *data, size_t len);

  // JPEG error tracking
  bool jpeg_hw_error_logged_{false};
  uint32_t jpeg_hw_error_count_{0};

  // Flag set by playback task when dimensions change, consumed by loop() to update UI
  volatile bool dimensions_changed_{false};

  bool controls_visible_{true};
  uint32_t hide_delay_ms_{5000};

  // Automation triggers
  CallbackManager<void()> on_play_callbacks_;
  CallbackManager<void()> on_stop_callbacks_;
};

template<typename... Ts> class PlayAction : public Action<Ts...>, public Parented<Mp4Player> {
 public:
  void play(const Ts &...x) override { this->parent_->play(); }
};

template<typename... Ts> class PauseAction : public Action<Ts...>, public Parented<Mp4Player> {
 public:
  void play(const Ts &...x) override { this->parent_->pause(); }
};

template<typename... Ts> class StopAction : public Action<Ts...>, public Parented<Mp4Player> {
 public:
  void play(const Ts &...x) override { this->parent_->stop(); }
};

class PlayTrigger : public Trigger<> {
 public:
  explicit PlayTrigger(Mp4Player *player) { player->add_on_play_callback([this]() { this->trigger(); }); }
};

class StopTrigger : public Trigger<> {
 public:
  explicit StopTrigger(Mp4Player *player) { player->add_on_stop_callback([this]() { this->trigger(); }); }
};

}  // namespace mp4_player
}  // namespace esphome

#endif
