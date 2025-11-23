#include "simple_video_player.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esp_heap_caps.h"

namespace esphome {
namespace simple_video_player {

static const char *const TAG = "simple_video_player";

void SimpleVideoPlayer::setup() {
  ESP_LOGI(TAG, "Setting up Simple Video Player...");
  ESP_LOGI(TAG, "  File: %s", this->file_path_.c_str());
  ESP_LOGI(TAG, "  Resolution: %dx%d", this->width_, this->height_);

  // Allocate buffers
  this->jpeg_buffer_ = (uint8_t *)heap_caps_malloc(this->buffer_size_, 
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->jpeg_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
    this->mark_failed();
    return;
  }

  this->rgb_buffer_size_ = this->width_ * this->height_ * 2;  // RGB565
  this->rgb_buffer_ = (uint8_t *)heap_caps_aligned_alloc(64, this->rgb_buffer_size_,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (this->rgb_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate RGB buffer");
    this->mark_failed();
    return;
  }

  // Initialize JPEG decoder
  if (!this->init_decoder_()) {
    ESP_LOGE(TAG, "Failed to initialize JPEG decoder");
    this->mark_failed();
    return;
  }

  // Create UI
  this->create_ui_();

  // Open video file
  if (!this->open_video_file_()) {
    ESP_LOGE(TAG, "Failed to open video file");
    this->mark_failed();
    return;
  }

  // Create playback timer
  this->playback_timer_ = lv_timer_create(timer_cb_, this->frame_interval_, this);
  lv_timer_pause(this->playback_timer_);

  if (this->auto_play_) {
    this->play();
  }

  ESP_LOGI(TAG, "Simple Video Player initialized");
}

void SimpleVideoPlayer::loop() {
  // Main processing is done in LVGL timer callback
}

void SimpleVideoPlayer::dump_config() {
  ESP_LOGCONFIG(TAG, "Simple Video Player:");
  ESP_LOGCONFIG(TAG, "  File: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG, "  Resolution: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Buffer size: %u", this->buffer_size_);
  ESP_LOGCONFIG(TAG, "  Auto play: %s", this->auto_play_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Loop: %s", this->loop_ ? "yes" : "no");
}

bool SimpleVideoPlayer::init_decoder_() {
  jpeg_decode_engine_cfg_t cfg = {
    .intr_priority = 0,
    .timeout_ms = 40,
  };

  esp_err_t ret = jpeg_new_decoder_engine(&cfg, &this->decoder_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create JPEG decoder: %s", esp_err_to_name(ret));
    return false;
  }

  return true;
}

bool SimpleVideoPlayer::open_video_file_() {
  this->file_ = fopen(this->file_path_.c_str(), "rb");
  if (this->file_ == nullptr) {
    ESP_LOGE(TAG, "Failed to open file: %s", this->file_path_.c_str());
    return false;
  }

  // Get file size
  fseek(this->file_, 0, SEEK_END);
  this->file_size_ = ftell(this->file_);
  fseek(this->file_, 0, SEEK_SET);

  ESP_LOGI(TAG, "Video file opened: %ld bytes", this->file_size_);
  return true;
}

bool SimpleVideoPlayer::read_next_frame_() {
  if (this->file_ == nullptr) {
    return false;
  }

  // Search for JPEG start marker (FFD8)
  int c1 = 0, c2 = 0;
  while ((c1 = fgetc(this->file_)) != EOF) {
    if (c1 == 0xFF) {
      c2 = fgetc(this->file_);
      if (c2 == 0xD8) {
        break;
      }
    }
  }

  if (c1 == EOF) {
    // End of file
    if (this->loop_) {
      fseek(this->file_, 0, SEEK_SET);
      this->frame_count_ = 0;
      return this->read_next_frame_();
    }
    return false;
  }

  // Start of JPEG found
  this->jpeg_buffer_[0] = 0xFF;
  this->jpeg_buffer_[1] = 0xD8;
  this->jpeg_size_ = 2;

  // Read until end marker (FFD9)
  while (this->jpeg_size_ < this->buffer_size_ - 1) {
    c1 = fgetc(this->file_);
    if (c1 == EOF) {
      break;
    }
    this->jpeg_buffer_[this->jpeg_size_++] = c1;

    if (c1 == 0xFF) {
      c2 = fgetc(this->file_);
      if (c2 == EOF) {
        break;
      }
      this->jpeg_buffer_[this->jpeg_size_++] = c2;
      if (c2 == 0xD9) {
        // End of JPEG
        break;
      }
    }
  }

  this->current_pos_ = ftell(this->file_);
  this->frame_count_++;

  return this->jpeg_size_ > 2;
}

bool SimpleVideoPlayer::decode_frame_() {
  if (this->jpeg_size_ == 0 || this->decoder_ == nullptr) {
    return false;
  }

  jpeg_decode_cfg_t decode_cfg = {
    .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
    .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
  };

  uint32_t out_size = 0;
  esp_err_t ret = jpeg_decoder_process(this->decoder_, &decode_cfg,
                                        this->jpeg_buffer_, this->jpeg_size_,
                                        this->rgb_buffer_, this->rgb_buffer_size_,
                                        &out_size);

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "JPEG decode failed: %s", esp_err_to_name(ret));
    return false;
  }

  return true;
}

void SimpleVideoPlayer::update_display_() {
  if (this->canvas_ == nullptr) {
    return;
  }

  lv_canvas_set_buffer(this->canvas_, this->rgb_buffer_,
                       this->width_, this->height_, LV_IMG_CF_TRUE_COLOR);
  lv_obj_invalidate(this->canvas_);

  // Update slider position
  if (this->slider_ != nullptr && this->file_size_ > 0) {
    int progress = (this->current_pos_ * 100) / this->file_size_;
    lv_slider_set_value(this->slider_, progress, LV_ANIM_OFF);
  }

  // Update time label
  if (this->time_label_ != nullptr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Frame: %lu", (unsigned long)this->frame_count_);
    lv_label_set_text(this->time_label_, buf);
  }
}

void SimpleVideoPlayer::create_ui_() {
  lv_obj_t *scr = lv_scr_act();

  // Create canvas for video display
  this->canvas_ = lv_canvas_create(scr);
  lv_canvas_set_buffer(this->canvas_, this->rgb_buffer_,
                       this->width_, this->height_, LV_IMG_CF_TRUE_COLOR);
  lv_obj_center(this->canvas_);

  if (this->show_controls_) {
    this->create_controls_();
  }
}

void SimpleVideoPlayer::create_controls_() {
  lv_obj_t *scr = lv_scr_act();

  // Controls container at bottom
  this->controls_container_ = lv_obj_create(scr);
  lv_obj_set_size(this->controls_container_, this->width_, 60);
  lv_obj_align(this->controls_container_, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_opa(this->controls_container_, LV_OPA_70, 0);
  lv_obj_set_style_bg_color(this->controls_container_, lv_color_black(), 0);
  lv_obj_set_flex_flow(this->controls_container_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(this->controls_container_, LV_FLEX_ALIGN_CENTER, 
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Play button
  this->play_btn_ = lv_btn_create(this->controls_container_);
  lv_obj_set_size(this->play_btn_, 50, 40);
  lv_obj_t *play_label = lv_label_create(this->play_btn_);
  lv_label_set_text(play_label, LV_SYMBOL_PLAY);
  lv_obj_center(play_label);
  lv_obj_add_event_cb(this->play_btn_, play_btn_cb_, LV_EVENT_CLICKED, this);

  // Pause button
  this->pause_btn_ = lv_btn_create(this->controls_container_);
  lv_obj_set_size(this->pause_btn_, 50, 40);
  lv_obj_t *pause_label = lv_label_create(this->pause_btn_);
  lv_label_set_text(pause_label, LV_SYMBOL_PAUSE);
  lv_obj_center(pause_label);
  lv_obj_add_event_cb(this->pause_btn_, pause_btn_cb_, LV_EVENT_CLICKED, this);

  // Stop button
  this->stop_btn_ = lv_btn_create(this->controls_container_);
  lv_obj_set_size(this->stop_btn_, 50, 40);
  lv_obj_t *stop_label = lv_label_create(this->stop_btn_);
  lv_label_set_text(stop_label, LV_SYMBOL_STOP);
  lv_obj_center(stop_label);
  lv_obj_add_event_cb(this->stop_btn_, stop_btn_cb_, LV_EVENT_CLICKED, this);

  // Progress slider
  this->slider_ = lv_slider_create(this->controls_container_);
  lv_obj_set_flex_grow(this->slider_, 1);
  lv_obj_set_height(this->slider_, 10);
  lv_slider_set_range(this->slider_, 0, 100);
  lv_obj_add_event_cb(this->slider_, slider_cb_, LV_EVENT_VALUE_CHANGED, this);

  // Frame counter
  this->time_label_ = lv_label_create(this->controls_container_);
  lv_label_set_text(this->time_label_, "Frame: 0");
  lv_obj_set_style_text_color(this->time_label_, lv_color_white(), 0);
}

void SimpleVideoPlayer::play() {
  if (this->state_ == PlayerState::PLAYING) {
    return;
  }

  if (this->state_ == PlayerState::STOPPED && this->file_ != nullptr) {
    fseek(this->file_, 0, SEEK_SET);
    this->frame_count_ = 0;
  }

  this->state_ = PlayerState::PLAYING;
  if (this->playback_timer_ != nullptr) {
    lv_timer_resume(this->playback_timer_);
  }
  ESP_LOGI(TAG, "Playback started");
}

void SimpleVideoPlayer::pause() {
  if (this->state_ != PlayerState::PLAYING) {
    return;
  }

  this->state_ = PlayerState::PAUSED;
  if (this->playback_timer_ != nullptr) {
    lv_timer_pause(this->playback_timer_);
  }
  ESP_LOGI(TAG, "Playback paused");
}

void SimpleVideoPlayer::resume() {
  if (this->state_ != PlayerState::PAUSED) {
    return;
  }

  this->state_ = PlayerState::PLAYING;
  if (this->playback_timer_ != nullptr) {
    lv_timer_resume(this->playback_timer_);
  }
  ESP_LOGI(TAG, "Playback resumed");
}

void SimpleVideoPlayer::stop() {
  this->state_ = PlayerState::STOPPED;
  if (this->playback_timer_ != nullptr) {
    lv_timer_pause(this->playback_timer_);
  }
  
  if (this->file_ != nullptr) {
    fseek(this->file_, 0, SEEK_SET);
  }
  this->frame_count_ = 0;
  this->current_pos_ = 0;

  // Update slider
  if (this->slider_ != nullptr) {
    lv_slider_set_value(this->slider_, 0, LV_ANIM_OFF);
  }

  ESP_LOGI(TAG, "Playback stopped");
}

// Static callbacks
void SimpleVideoPlayer::play_btn_cb_(lv_event_t *e) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(lv_event_get_user_data(e));
  if (player->is_paused()) {
    player->resume();
  } else {
    player->play();
  }
}

void SimpleVideoPlayer::pause_btn_cb_(lv_event_t *e) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(lv_event_get_user_data(e));
  player->pause();
}

void SimpleVideoPlayer::stop_btn_cb_(lv_event_t *e) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(lv_event_get_user_data(e));
  player->stop();
}

void SimpleVideoPlayer::slider_cb_(lv_event_t *e) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(lv_event_get_user_data(e));
  lv_obj_t *slider = lv_event_get_target(e);
  
  int value = lv_slider_get_value(slider);
  long new_pos = (player->file_size_ * value) / 100;
  
  if (player->file_ != nullptr) {
    fseek(player->file_, new_pos, SEEK_SET);
    player->current_pos_ = new_pos;
  }
}

void SimpleVideoPlayer::timer_cb_(lv_timer_t *timer) {
  SimpleVideoPlayer *player = static_cast<SimpleVideoPlayer *>(timer->user_data);
  
  if (player->state_ != PlayerState::PLAYING) {
    return;
  }

  if (player->read_next_frame_()) {
    if (player->decode_frame_()) {
      player->update_display_();
    }
  } else {
    // End of video
    if (!player->loop_) {
      player->stop();
    }
  }
}

}  // namespace simple_video_player
}  // namespace esphome

#else  // !USE_ESP_IDF

namespace esphome {
namespace simple_video_player {

void SimpleVideoPlayer::setup() {
  ESP_LOGE("simple_video_player", "Requires ESP-IDF");
  this->mark_failed();
}

void SimpleVideoPlayer::loop() {}
void SimpleVideoPlayer::dump_config() {}
void SimpleVideoPlayer::play() {}
void SimpleVideoPlayer::pause() {}
void SimpleVideoPlayer::stop() {}
void SimpleVideoPlayer::resume() {}

}  // namespace simple_video_player
}  // namespace esphome

#endif  // USE_ESP_IDF
