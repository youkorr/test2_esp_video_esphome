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

// Use esp_extractor API directly (no BSP dependency)
extern "C" {
#include "esp_extractor.h"
#include "esp_extractor_reg.h"
#include "esp_mp4_extractor.h"
#include "esp_avi_extractor.h"
#include "mem_pool.h"
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

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void play();
  void pause();
  void stop();
  bool is_playing() const { return state_ == PlayerState::PLAYING; }
  bool is_paused() const { return state_ == PlayerState::PAUSED; }

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

  // Configuration
  std::string file_path_;
  bool loop_{true};
  bool auto_play_{true};
  bool controls_enabled_{true};
  uint8_t volume_level_{80};

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

  bool controls_visible_{true};
  uint32_t hide_delay_ms_{5000};
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

}  // namespace mp4_player
}  // namespace esphome

#endif
