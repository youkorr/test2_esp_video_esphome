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
#include "freertos/queue.h"
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
#include "esp_audio_es_extractor.h"
#include "esp_wav_extractor.h"
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

enum class FileType {
  UNKNOWN,
  DIRECTORY,
  VIDEO,
  IMAGE,
  AUDIO,
  LOTTIE,
  SVG
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
  void set_fullscreen_on_touch(bool en) { fullscreen_on_touch_ = en; }
  void set_fullscreen_anim_ms(uint32_t ms) { fullscreen_anim_ms_ = ms; }
  void set_release_on_close(bool en) { release_on_close_ = en; }

  void add_on_play_callback(std::function<void()> &&callback) { on_play_callbacks_.add(std::move(callback)); }
  void add_on_stop_callback(std::function<void()> &&callback) { on_stop_callbacks_.add(std::move(callback)); }
  void add_on_close_callback(std::function<void()> &&callback) { on_close_callbacks_.add(std::move(callback)); }

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

  // Fullscreen toggle (animated). When the player has an embedded parent
  // (e.g. a dashboard card), this animates the canvas to the screen size
  // and back, so a touch can "open" the animation full-screen.
  void enter_fullscreen();
  void exit_fullscreen();
  void toggle_fullscreen();
  bool is_fullscreen() const { return fullscreen_active_; }

  // Close the player UI (browser + viewers) and fire `on_close`.
  // Useful when the user dismisses a media-browser overlay.
  void close();
  // Request a close from an LVGL event context (e.g. the on_swipe_up action).
  // Defers the real close() to loop() so we neither delete widgets nor block
  // while LVGL is still dispatching the gesture/touch event.
  void request_close();
  // Free heavy PSRAM buffers (display, jpeg, audio). Safe to call when
  // stopped — buffers are re-allocated on the next play_file().
  void release_resources();

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

  // Spectrum analyzer
  static constexpr int SPECTRUM_BANDS = 16;
  static constexpr int FFT_SIZE = 512;
  void create_spectrum_ui_();
  void destroy_spectrum_ui_();
  void update_spectrum_();
  void compute_spectrum_from_pcm_(const int16_t *pcm, size_t sample_count);
  static void fft_simple_(float *real, float *imag, int n);

  // Static callbacks
  static void play_btn_cb_(lv_event_t *e);
  static void stop_btn_cb_(lv_event_t *e);
  static void spectrum_stop_cb_(lv_event_t *e);
  static void spectrum_playpause_cb_(lv_event_t *e);
  static void spectrum_next_cb_(lv_event_t *e);
  static void spectrum_prev_cb_(lv_event_t *e);
  void play_next_track_();
  void play_prev_track_();
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
  void navigate_to_directory_(std::string path);
  void scan_media_files_(const std::string &path);
  static FileType get_file_type_(const std::string &name);
  void open_media_file_(const std::string &path, FileType type);
  void show_image_viewer_(const std::string &path);
  bool show_jpeg_image_(const std::string &path);
  void destroy_image_viewer_();
  static void image_close_cb_(lv_event_t *e);
  static void file_item_cb_(lv_event_t *e);
  static void back_btn_cb_(lv_event_t *e);
  static void refresh_btn_cb_(lv_event_t *e);

  // Deferred navigation. LVGL tree surgery (lv_obj_clean / lv_obj_del) must
  // NOT run inside a widget's own event callback: the widget being clicked is
  // a child of browser_list_/browser_container_/spectrum_container_, so
  // deleting it there leaves LVGL dereferencing freed memory once the callback
  // returns (Load access fault). The callback records the request via
  // schedule_nav_(); loop() executes it after the event dispatch completes.
  enum class PendingNav { NONE, NAVIGATE, OPEN_FILE, CLOSE, STOP, NEXT_TRACK, PREV_TRACK };
  void schedule_nav_(PendingNav action, const std::string &path = "",
                     FileType type = FileType::UNKNOWN);

  struct FileEntry {
    std::string full_path;
    std::string name;
    size_t size;
    bool is_directory;
    FileType file_type;
  };
  std::vector<FileEntry> file_entries_;
  std::vector<std::string> media_directories_;
  std::string current_browse_path_;
  bool browser_active_{false};

  // Pending deferred-navigation request (set by schedule_nav_, run in loop())
  volatile PendingNav pending_nav_{PendingNav::NONE};
  std::string pending_nav_path_;
  FileType pending_nav_type_{FileType::UNKNOWN};
  lv_obj_t *browser_container_{nullptr};
  lv_obj_t *browser_list_{nullptr};
  lv_obj_t *browser_title_{nullptr};
  lv_obj_t *image_viewer_{nullptr};
  std::string image_viewer_path_;
  bool image_viewer_close_pending_{false};
  lv_draw_buf_t *lottie_draw_buf_{nullptr};  // Draw buffer for Lottie rendering

  // Spectrum analyzer UI
  lv_obj_t *spectrum_container_{nullptr};
  lv_obj_t *spectrum_bars_[SPECTRUM_BANDS]{};
  lv_obj_t *spectrum_title_label_{nullptr};
  lv_obj_t *spectrum_artist_label_{nullptr};
  lv_obj_t *spectrum_play_btn_{nullptr};
  lv_obj_t *spectrum_volume_slider_{nullptr};
  lv_obj_t *spectrum_time_label_{nullptr};
  lv_obj_t *spectrum_glow_circle_{nullptr};
  lv_obj_t *spectrum_glow_ring_{nullptr};
  lv_obj_t *spectrum_bars_area_{nullptr};
  lv_obj_t *spectrum_mode_btn_{nullptr};
  lv_obj_t *spectrum_peak_indicators_[SPECTRUM_BANDS]{};
  uint8_t spectrum_viz_mode_{0};  // 0=neon bars, 1=abstract sphere, 2=spectrum analyzer
  uint32_t spectrum_color_phase_{0};
  static void spectrum_volume_cb_(lv_event_t *e);
  static void spectrum_mode_cb_(lv_event_t *e);
  void rebuild_spectrum_viz_();
  float spectrum_magnitudes_[SPECTRUM_BANDS]{};
  float spectrum_peaks_[SPECTRUM_BANDS]{};
  float spectrum_smooth_[SPECTRUM_BANDS]{};
  volatile bool audio_only_mode_{false};

  // Playlist (built from current directory's audio files)
  std::vector<std::string> playlist_;
  int playlist_index_{-1};
  volatile bool track_finished_{false};
  uint32_t spectrum_last_update_{0};

  // Configuration
  std::string file_path_;
  bool loop_{true};
  bool auto_play_{true};
  bool controls_enabled_{true};
  uint8_t volume_level_{60};
  // When true (default), close() frees the heavy PSRAM buffers via
  // release_resources() so leaving the player page reclaims ~3 MB.
  bool release_on_close_{true};

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
  extractor_video_format_t video_format_{EXTRACTOR_VIDEO_FORMAT_NONE};
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
  bool output_mono_{true};  // ESP32-P4 board has single speaker - downmix to mono

  // Downmix stereo PCM to mono (in-place, returns new size = half)
  size_t downmix_stereo_to_mono_(uint8_t *pcm_data, size_t size);

  // Audio frame queue: decouples audio decode from video decode (like Waveshare)
  // Playback task puts encoded audio frames on the queue
  // Audio decode task reads from queue, decodes, downmixes, pushes to ring buffer
  struct AudioFrameItem {
    uint8_t *data;
    size_t size;
  };
  static constexpr int AUDIO_FRAME_QUEUE_SIZE = 12;
  QueueHandle_t audio_frame_queue_{nullptr};
  static void audio_decode_task_(void *arg);
  TaskHandle_t audio_decode_task_handle_{nullptr};
  volatile bool audio_decode_task_running_{false};
  // Set true by the task right before it vTaskDelete()s itself, so teardown
  // can confirm the task has really exited before any buffer it touches is freed.
  volatile bool audio_decode_task_exited_{false};

  // Audio ring buffer for decoupling decode from speaker output
  uint8_t *audio_ring_buffer_{nullptr};
  size_t audio_ring_size_{0};
  volatile size_t audio_ring_read_{0};
  volatile size_t audio_ring_write_{0};

  // Audio output task
  static void audio_output_task_(void *arg);
  TaskHandle_t audio_task_handle_{nullptr};
  volatile bool audio_task_running_{false};
  volatile bool audio_task_exited_{false};  // see audio_decode_task_exited_

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

  // Fullscreen animation state
  bool fullscreen_on_touch_{false};
  bool fullscreen_active_{false};
  uint32_t fullscreen_anim_ms_{350};
  // Saved geometry of the embedded canvas before going fullscreen
  lv_obj_t *fs_orig_parent_{nullptr};
  lv_coord_t fs_orig_x_{0};
  lv_coord_t fs_orig_y_{0};
  lv_coord_t fs_orig_w_{0};
  lv_coord_t fs_orig_h_{0};
  // Animation callbacks for canvas geometry
  static void fs_anim_x_cb_(void *obj, int32_t v);
  static void fs_anim_y_cb_(void *obj, int32_t v);
  static void fs_anim_w_cb_(void *obj, int32_t v);
  static void fs_anim_h_cb_(void *obj, int32_t v);
  static void fs_anim_ready_enter_cb_(lv_anim_t *a);
  static void fs_anim_ready_exit_cb_(lv_anim_t *a);
  void animate_geometry_(lv_obj_t *target,
                         lv_coord_t x0, lv_coord_t y0, lv_coord_t w0, lv_coord_t h0,
                         lv_coord_t x1, lv_coord_t y1, lv_coord_t w1, lv_coord_t h1,
                         uint32_t duration_ms,
                         lv_anim_ready_cb_t ready_cb);

  // Automation triggers
  CallbackManager<void()> on_play_callbacks_;
  CallbackManager<void()> on_stop_callbacks_;
  CallbackManager<void()> on_close_callbacks_;
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

template<typename... Ts> class EnterFullscreenAction : public Action<Ts...>, public Parented<Mp4Player> {
 public:
  void play(const Ts &...x) override { this->parent_->enter_fullscreen(); }
};

template<typename... Ts> class ExitFullscreenAction : public Action<Ts...>, public Parented<Mp4Player> {
 public:
  void play(const Ts &...x) override { this->parent_->exit_fullscreen(); }
};

template<typename... Ts> class ToggleFullscreenAction : public Action<Ts...>, public Parented<Mp4Player> {
 public:
  void play(const Ts &...x) override { this->parent_->toggle_fullscreen(); }
};

template<typename... Ts> class CloseAction : public Action<Ts...>, public Parented<Mp4Player> {
 public:
  // Deferred: the action often fires from an LVGL gesture (on_swipe_up), so
  // close() must not run inside the event dispatch. See request_close().
  void play(const Ts &...x) override { this->parent_->request_close(); }
};

template<typename... Ts> class ReleaseResourcesAction : public Action<Ts...>, public Parented<Mp4Player> {
 public:
  void play(const Ts &...x) override { this->parent_->release_resources(); }
};

template<typename... Ts> class ShowBrowserAction : public Action<Ts...>, public Parented<Mp4Player> {
 public:
  void play(const Ts &...x) override { this->parent_->show_file_browser(); }
};

class PlayTrigger : public Trigger<> {
 public:
  explicit PlayTrigger(Mp4Player *player) { player->add_on_play_callback([this]() { this->trigger(); }); }
};

class StopTrigger : public Trigger<> {
 public:
  explicit StopTrigger(Mp4Player *player) { player->add_on_stop_callback([this]() { this->trigger(); }); }
};

class CloseTrigger : public Trigger<> {
 public:
  explicit CloseTrigger(Mp4Player *player) { player->add_on_close_callback([this]() { this->trigger(); }); }
};

}  // namespace mp4_player
}  // namespace esphome

#endif
