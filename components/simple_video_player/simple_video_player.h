#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/automation.h"

#ifdef USE_ESP_IDF

#include "lvgl.h"
#include "driver/jpeg_decode.h"

namespace esphome {
namespace simple_video_player {

enum class PlayerState {
  STOPPED,
  PLAYING,
  PAUSED
};

class SimpleVideoPlayer : public Component {
 public:
  void set_file_path(const std::string &path) { file_path_ = path; }
  void set_width(int w) { width_ = w; }
  void set_height(int h) { height_ = h; }
  void set_buffer_size(size_t size) { buffer_size_ = size; }
  void set_auto_play(bool b) { auto_play_ = b; }
  void set_loop(bool b) { loop_ = b; }
  void set_show_controls(bool b) { show_controls_ = b; }

  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override {
    return setup_priority::LATE;
  }

  // Playback control
  void play();
  void pause();
  void stop();
  void resume();
  bool is_playing() const { return state_ == PlayerState::PLAYING; }
  bool is_paused() const { return state_ == PlayerState::PAUSED; }

 protected:
  bool init_decoder_();
  bool open_video_file_();
  bool read_next_frame_();
  bool decode_frame_();
  void update_display_();
  void create_ui_();
  void create_controls_();
  
  // LVGL callbacks
  static void play_btn_cb_(lv_event_t *e);
  static void pause_btn_cb_(lv_event_t *e);
  static void stop_btn_cb_(lv_event_t *e);
  static void slider_cb_(lv_event_t *e);
  static void timer_cb_(lv_timer_t *timer);

  // Configuration
  std::string file_path_;
  int width_{800};
  int height_{480};
  size_t buffer_size_{100000};
  bool auto_play_{true};
  bool loop_{true};
  bool show_controls_{true};

  // State
  PlayerState state_{PlayerState::STOPPED};
  FILE *file_{nullptr};
  long file_size_{0};
  long current_pos_{0};
  uint32_t frame_count_{0};
  uint32_t total_frames_{0};

  // Buffers
  uint8_t *jpeg_buffer_{nullptr};
  uint8_t *rgb_buffer_{nullptr};
  size_t jpeg_size_{0};
  size_t rgb_buffer_size_{0};

  // JPEG decoder
  jpeg_decoder_handle_t decoder_{nullptr};

  // LVGL objects
  lv_obj_t *canvas_{nullptr};
  lv_obj_t *play_btn_{nullptr};
  lv_obj_t *pause_btn_{nullptr};
  lv_obj_t *stop_btn_{nullptr};
  lv_obj_t *slider_{nullptr};
  lv_obj_t *time_label_{nullptr};
  lv_obj_t *controls_container_{nullptr};
  lv_timer_t *playback_timer_{nullptr};

  // Frame timing
  uint32_t last_frame_time_{0};
  uint32_t frame_interval_{33};  // ~30fps default
};

// Action templates for automation
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

#endif  // USE_ESP_IDF
