#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/automation.h"

#ifdef USE_ESP_IDF

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "avi_player.h"
}

namespace esphome {
namespace avi_player {

enum class PlayerState {
  STOPPED,
  PLAYING
};

class AviPlayerComponent : public Component {
 public:
  void set_file_path(const std::string &path) { file_path_ = path; }
  void set_width(int w) { width_ = w; }
  void set_height(int h) { height_ = h; }
  void set_buffer_size(size_t size) { buffer_size_ = size; }
  void set_auto_play(bool b) { auto_play_ = b; }
  void set_loop(bool b) { loop_ = b; }
  void set_parent(lv_obj_t *parent) { parent_ = parent; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void play();
  void stop();

  PlayerState get_state() const { return state_; }

 protected:
  std::string file_path_;
  int width_{480};
  int height_{270};
  size_t buffer_size_{60 * 1024};
  bool auto_play_{true};
  bool loop_{false};
  lv_obj_t *parent_{nullptr};
  lv_obj_t *canvas_{nullptr};
  lv_obj_t *img_{nullptr};

  PlayerState state_{PlayerState::STOPPED};
  avi_player_handle_t avi_handle_{nullptr};

  lv_color_t *video_buffer_{nullptr};

  static void video_frame_callback(frame_data_t *data, void *arg);
  static void audio_frame_callback(frame_data_t *data, void *arg);
  static void audio_set_clock_callback(uint32_t rate, uint32_t bits_cfg, uint32_t ch, void *arg);
  static void play_end_callback(void *arg);

  void render_frame(frame_data_t *data);
};

// Actions
template<typename... Ts> class PlayAction : public Action<Ts...>, public Parented<AviPlayerComponent> {
 public:
  void play(Ts... x) override { this->parent_->play(); }
};

template<typename... Ts> class StopAction : public Action<Ts...>, public Parented<AviPlayerComponent> {
 public:
  void play(Ts... x) override { this->parent_->stop(); }
};

}  // namespace avi_player
}  // namespace esphome

#endif  // USE_ESP_IDF
