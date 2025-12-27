#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/automation.h"
#include "esphome/components/speaker/speaker.h"

#ifdef USE_ESP_IDF

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/jpeg_decode.h"
#include "esp_imgfx_rotate.h"

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
  void set_show_controls(bool b) { show_controls_ = b; }
  void set_show_slider(bool b) { show_slider_ = b; }
  void set_preload_to_memory(bool b) { preload_to_memory_ = b; }
  void set_fps(float fps) {
    if (fps > 0 && fps <= 120) {
      fps_ = fps;
      fps_override_ = true;
    }
  }
  void set_rotation(uint16_t rotation) { rotation_ = rotation; }
  void set_parent(lv_obj_t *parent) { parent_ = parent; }
  void set_speaker(speaker::Speaker *spk) { speaker_ = spk; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void play();
  void stop();

  PlayerState get_state() const { return state_; }

  // Triggers
  Trigger<> *get_on_play_trigger() const { return on_play_trigger_; }
  Trigger<> *get_on_stop_trigger() const { return on_stop_trigger_; }
  void set_on_play_trigger(Trigger<> *trigger) { on_play_trigger_ = trigger; }
  void set_on_stop_trigger(Trigger<> *trigger) { on_stop_trigger_ = trigger; }

 protected:
  std::string file_path_;
  int width_{480};
  int height_{270};
  size_t buffer_size_{256 * 1024};  // 256 KB default for high quality videos
  bool auto_play_{true};
  bool loop_{false};
  bool show_controls_{false};
  bool show_slider_{false};
  bool preload_to_memory_{false};
  float fps_{0};
  bool fps_override_{false};
  uint16_t rotation_{0};  // Rotation angle: 0, 90, 180, 270

  lv_obj_t *parent_{nullptr};
  lv_obj_t *canvas_{nullptr};
  lv_obj_t *img_{nullptr};
  lv_obj_t *controls_panel_{nullptr};
  lv_obj_t *play_btn_{nullptr};
  lv_obj_t *pause_btn_{nullptr};
  lv_obj_t *stop_btn_{nullptr};
  lv_obj_t *slider_{nullptr};

  PlayerState state_{PlayerState::STOPPED};
  avi_player_handle_t avi_handle_{nullptr};

  lv_color_t *video_buffer_{nullptr};
  uint8_t *memory_buffer_{nullptr};  // For preload_to_memory
  size_t memory_buffer_size_{0};

  jpeg_decoder_handle_t jpeg_decoder_{nullptr};
  jpeg_decode_cfg_t jpeg_decode_cfg_;  // JPEG decode configuration
  jpeg_decode_picture_info_t frame_info_;  // Actual frame dimensions from JPEG
  uint32_t actual_width_{0};   // 16-byte aligned width
  uint32_t actual_height_{0};  // 16-byte aligned height
  size_t video_buffer_size_{0};  // Actual buffer size
  lv_img_dsc_t lvgl_img_dsc_;  // LVGL image descriptor

  esp_imgfx_rotate_handle_t rotate_handle_{nullptr};  // Hardware rotation handle
  lv_color_t *rotate_buffer_{nullptr};  // Buffer for rotated frames
  size_t rotate_buffer_size_{0};
  bool rotation_initialized_{false};  // Track if rotation handle has been reinitialized with actual dimensions

  volatile bool frame_ready_{false};  // Flag for thread-safe LVGL update
  volatile bool need_resize_{false};  // Flag to resize object on first frame

  speaker::Speaker *speaker_{nullptr};  // Speaker for audio output
  uint32_t audio_sample_rate_{0};      // Audio sample rate from AVI
  uint8_t audio_bits_per_sample_{0};   // Audio bits per sample from AVI
  uint8_t audio_channels_{0};          // Audio channels from AVI

  Trigger<> *on_play_trigger_{new Trigger<>()};
  Trigger<> *on_stop_trigger_{new Trigger<>()};

  static void video_frame_callback(frame_data_t *data, void *arg);
  static void audio_frame_callback(frame_data_t *data, void *arg);
  static void audio_set_clock_callback(uint32_t rate, uint32_t bits_cfg, uint32_t ch, void *arg);
  static void play_end_callback(void *arg);

  void render_frame(frame_data_t *data);
  void create_controls();
  void update_slider_position();
  bool load_file_to_memory();
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
