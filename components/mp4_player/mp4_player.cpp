#include "mp4_player.h"

#ifdef USE_ESP_IDF

#include "esp_heap_caps.h"
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace esphome {
namespace mp4_player {

static const char *TAG = "mp4_player";

static constexpr size_t JPEG_BUFFER_SIZE = 256 * 1024;
static constexpr size_t EXTRACTOR_POOL_SIZE = 256 * 1024;
static constexpr size_t EXTRACTOR_POOL_BLOCKS = 3;

// ============================================================================
// File I/O wrappers for esp_extractor
// ============================================================================
void *Mp4Player::file_open_cb_(char *url, void *ctx) {
  int fd = open(url, O_RDONLY);
  if (fd < 0) {
    ESP_LOGE(TAG, "Failed to open: %s", url);
    return nullptr;
  }
  return (void *)(intptr_t)fd;
}

int Mp4Player::file_read_cb_(void *data, uint32_t size, void *ctx) {
  int fd = (int)(intptr_t)ctx;
  ssize_t bytes = read(fd, data, size);
  return bytes < 0 ? 0 : (int)bytes;
}

int Mp4Player::file_seek_cb_(uint32_t position, void *ctx) {
  int fd = (int)(intptr_t)ctx;
  return lseek(fd, position, SEEK_SET) < 0 ? -1 : 0;
}

int Mp4Player::file_close_cb_(void *ctx) {
  int fd = (int)(intptr_t)ctx;
  return close(fd);
}

uint32_t Mp4Player::file_size_cb_(void *ctx) {
  int fd = (int)(intptr_t)ctx;
  off_t cur = lseek(fd, 0, SEEK_CUR);
  off_t end = lseek(fd, 0, SEEK_END);
  lseek(fd, cur, SEEK_SET);
  return end <= 0 ? 0 : (uint32_t)end;
}

// ============================================================================
// Setup
// ============================================================================
void Mp4Player::setup() {
  ESP_LOGI(TAG, "Setting up MP4 Player...");

  this->playback_event_group_ = xEventGroupCreate();
  if (!this->playback_event_group_) {
    ESP_LOGE(TAG, "Failed to create event group");
    this->mark_failed();
    return;
  }

  // Allocate JPEG input buffer
  this->jpeg_buffer_ = (uint8_t *)heap_caps_malloc(JPEG_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  if (!this->jpeg_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
    this->mark_failed();
    return;
  }

  // Initialize hardware JPEG decoder
  jpeg_decode_engine_cfg_t cfg = {
    .intr_priority = 0,
    .timeout_ms = 1000,
  };
  if (jpeg_new_decoder_engine(&cfg, &this->jpeg_decoder_) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init JPEG decoder");
    this->mark_failed();
    return;
  }

  // Register extractors
  esp_mp4_extractor_register();
  esp_avi_extractor_register();

  // Probe video file to get resolution/fps/duration
  ESP_LOGI(TAG, "Probing: %s", this->file_path_.c_str());

  esp_extractor_handle_t probe = nullptr;
  esp_extractor_config_t probe_cfg = {};
  probe_cfg.open = file_open_cb_;
  probe_cfg.read = file_read_cb_;
  probe_cfg.seek = file_seek_cb_;
  probe_cfg.file_size = file_size_cb_;
  probe_cfg.close = file_close_cb_;
  probe_cfg.extract_mask = ESP_EXTRACT_MASK_VIDEO | ESP_EXTRACT_MASK_AUDIO;
  probe_cfg.url = (char *)this->file_path_.c_str();
  probe_cfg.input_ctx = nullptr;
  probe_cfg.output_pool_size = EXTRACTOR_POOL_SIZE;
  probe_cfg.cache_block_num = EXTRACTOR_POOL_BLOCKS;
  probe_cfg.cache_block_size = EXTRACTOR_POOL_SIZE / EXTRACTOR_POOL_BLOCKS;

  if (esp_extractor_open(&probe_cfg, &probe) == ESP_OK) {
    if (esp_extractor_parse_stream_info(probe) == ESP_OK) {
      // Video info
      uint16_t vnum = 0;
      esp_extractor_get_stream_num(probe, EXTRACTOR_STREAM_TYPE_VIDEO, &vnum);
      if (vnum > 0) {
        extractor_stream_info_t sinfo = {};
        if (esp_extractor_get_stream_info(probe, EXTRACTOR_STREAM_TYPE_VIDEO, 0, &sinfo) == ESP_OK) {
          this->video_width_ = sinfo.stream_info.video_info.width;
          this->video_height_ = sinfo.stream_info.video_info.height;
          this->video_fps_ = sinfo.stream_info.video_info.fps > 0 ? sinfo.stream_info.video_info.fps : 25;
          this->total_duration_ms_ = sinfo.duration;
          ESP_LOGI(TAG, "Video: %ux%u @ %u fps, duration: %u ms",
                   this->video_width_, this->video_height_, this->video_fps_, this->total_duration_ms_);
        }
      }
      // Audio info
      uint16_t anum = 0;
      esp_extractor_get_stream_num(probe, EXTRACTOR_STREAM_TYPE_AUDIO, &anum);
      this->has_audio_ = (anum > 0);
      if (this->has_audio_) ESP_LOGI(TAG, "Audio track detected");
    }
    esp_extractor_close(probe);
  }

  esp_extractor_unregister_all();

  if (this->video_width_ == 0 || this->video_height_ == 0) {
    ESP_LOGW(TAG, "Could not probe video, using 800x480");
    this->video_width_ = 800;
    this->video_height_ = 480;
  }

  // Allocate display buffers (RGB565 double buffer)
  this->display_buffer_size_ = this->video_width_ * this->video_height_ * 2;
  for (int i = 0; i < 2; i++) {
    this->display_buffer_[i] = (uint8_t *)heap_caps_aligned_alloc(
        64, this->display_buffer_size_, MALLOC_CAP_SPIRAM);
    if (!this->display_buffer_[i]) {
      ESP_LOGE(TAG, "Failed to allocate display buffer %d", i);
      this->mark_failed();
      return;
    }
    memset(this->display_buffer_[i], 0, this->display_buffer_size_);
  }

  // Create LVGL UI
  this->create_ui_();

  ESP_LOGI(TAG, "MP4 Player ready");

  if (this->auto_play_) {
    this->play();
  }
}

// ============================================================================
// Loop - update LVGL canvas when a new frame is ready
// ============================================================================
void Mp4Player::loop() {
  if (this->frame_ready_) {
    if (this->canvas_) {
      lv_canvas_set_buffer(this->canvas_,
                           this->display_buffer_[this->current_display_buf_],
                           this->video_width_, this->video_height_,
                           LV_COLOR_FORMAT_RGB565);
      lv_obj_invalidate(this->canvas_);
    }
    this->frame_ready_ = false;
    this->frame_count_++;

    if (this->loading_label_ && this->frame_count_ == 1) {
      lv_obj_add_flag(this->loading_label_, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (this->state_ == PlayerState::PLAYING && this->controls_visible_) {
    this->update_progress_();
  }
}

// ============================================================================
// Dump Config
// ============================================================================
void Mp4Player::dump_config() {
  ESP_LOGCONFIG(TAG, "MP4 Player:");
  ESP_LOGCONFIG(TAG, "  File: %s", this->file_path_.c_str());
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u", this->video_width_, this->video_height_);
  ESP_LOGCONFIG(TAG, "  FPS: %u", this->video_fps_);
  ESP_LOGCONFIG(TAG, "  Volume: %u%%", this->volume_level_);
  ESP_LOGCONFIG(TAG, "  Loop: %s", this->loop_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Audio: %s", this->has_audio_ ? "yes" : "no");
}

// ============================================================================
// Play
// ============================================================================
void Mp4Player::play() {
  if (this->state_ == PlayerState::PLAYING) return;

  ESP_LOGI(TAG, "Play");

  if (this->state_ == PlayerState::PAUSED) {
    this->state_ = PlayerState::PLAYING;
    xEventGroupSetBits(this->playback_event_group_, EVENT_START);
    if (this->play_btn_) {
      lv_obj_t *lbl = lv_obj_get_child(this->play_btn_, 0);
      if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PAUSE);
    }
    return;
  }

  this->state_ = PlayerState::PLAYING;
  this->frame_count_ = 0;
  this->current_time_ms_ = 0;
  this->stop_requested_ = false;

  if (this->play_btn_) {
    lv_obj_t *lbl = lv_obj_get_child(this->play_btn_, 0);
    if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PAUSE);
  }

  if (!this->playback_task_handle_) {
    xEventGroupClearBits(this->playback_event_group_, EVENT_START | EVENT_STOP | EVENT_TASK_EXIT);
    BaseType_t ret = xTaskCreatePinnedToCore(
        playback_task_, "mp4_play", 8192, this, 5,
        &this->playback_task_handle_, 1);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create playback task");
      this->state_ = PlayerState::STOPPED;
      return;
    }
  }

  xEventGroupSetBits(this->playback_event_group_, EVENT_START);
}

// ============================================================================
// Pause
// ============================================================================
void Mp4Player::pause() {
  if (this->state_ != PlayerState::PLAYING) return;
  ESP_LOGI(TAG, "Pause");
  this->state_ = PlayerState::PAUSED;
  if (this->play_btn_) {
    lv_obj_t *lbl = lv_obj_get_child(this->play_btn_, 0);
    if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PLAY);
  }
}

// ============================================================================
// Stop
// ============================================================================
void Mp4Player::stop() {
  if (this->state_ == PlayerState::STOPPED) return;
  ESP_LOGI(TAG, "Stop");
  this->stop_requested_ = true;
  this->state_ = PlayerState::STOPPED;

  if (this->playback_task_handle_) {
    xEventGroupSetBits(this->playback_event_group_, EVENT_STOP);
    EventBits_t bits = xEventGroupWaitBits(this->playback_event_group_,
                                            EVENT_TASK_EXIT, pdTRUE, pdFALSE,
                                            pdMS_TO_TICKS(3000));
    if (!(bits & EVENT_TASK_EXIT)) {
      ESP_LOGW(TAG, "Playback task did not exit in time");
    }
    this->playback_task_handle_ = nullptr;
  }

  this->current_time_ms_ = 0;
  this->frame_count_ = 0;

  if (this->play_btn_) {
    lv_obj_t *lbl = lv_obj_get_child(this->play_btn_, 0);
    if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PLAY);
  }
}

// ============================================================================
// Playback Task - uses esp_extractor directly
// ============================================================================
void Mp4Player::playback_task_(void *arg) {
  Mp4Player *player = static_cast<Mp4Player *>(arg);
  ESP_LOGI(TAG, "Playback task started");

  while (true) {
    EventBits_t bits = xEventGroupWaitBits(player->playback_event_group_,
                                            EVENT_START | EVENT_STOP,
                                            pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & EVENT_STOP) break;
    if (!(bits & EVENT_START)) continue;

    // Register extractors
    esp_mp4_extractor_register();
    esp_avi_extractor_register();

    bool do_loop = true;
    while (do_loop && !player->stop_requested_) {
      ESP_LOGI(TAG, "Starting playback: %s", player->file_path_.c_str());

      // Open extractor
      esp_extractor_handle_t ext = nullptr;
      esp_extractor_config_t ext_cfg = {};
      ext_cfg.open = file_open_cb_;
      ext_cfg.read = file_read_cb_;
      ext_cfg.seek = file_seek_cb_;
      ext_cfg.file_size = file_size_cb_;
      ext_cfg.close = file_close_cb_;
      ext_cfg.extract_mask = ESP_EXTRACT_MASK_VIDEO;
      ext_cfg.url = (char *)player->file_path_.c_str();
      ext_cfg.input_ctx = nullptr;
      ext_cfg.output_pool_size = EXTRACTOR_POOL_SIZE;
      ext_cfg.cache_block_num = EXTRACTOR_POOL_BLOCKS;
      ext_cfg.cache_block_size = EXTRACTOR_POOL_SIZE / EXTRACTOR_POOL_BLOCKS;

      esp_err_t ret = esp_extractor_open(&ext_cfg, &ext);
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Extractor open failed: %d", ret);
        break;
      }

      ret = esp_extractor_parse_stream_info(ext);
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Parse stream info failed: %d", ret);
        esp_extractor_close(ext);
        break;
      }

      // Get video info
      uint16_t vnum = 0;
      esp_extractor_get_stream_num(ext, EXTRACTOR_STREAM_TYPE_VIDEO, &vnum);
      if (vnum > 0) {
        extractor_stream_info_t sinfo = {};
        if (esp_extractor_get_stream_info(ext, EXTRACTOR_STREAM_TYPE_VIDEO, 0, &sinfo) == ESP_OK) {
          player->video_width_ = sinfo.stream_info.video_info.width;
          player->video_height_ = sinfo.stream_info.video_info.height;
          uint32_t fps = sinfo.stream_info.video_info.fps;
          player->video_fps_ = fps > 0 ? fps : 25;
          player->total_duration_ms_ = sinfo.duration;
        }
      }

      uint32_t frame_interval_ms = 1000 / player->video_fps_;
      int64_t last_frame_time = esp_timer_get_time() / 1000;

      // Read frames loop
      while (!player->stop_requested_ && player->state_ != PlayerState::STOPPED) {
        bits = xEventGroupWaitBits(player->playback_event_group_,
                                    EVENT_STOP, pdTRUE, pdFALSE, 0);
        if (bits & EVENT_STOP) {
          player->stop_requested_ = true;
          break;
        }

        // Handle pause
        if (player->state_ == PlayerState::PAUSED) {
          vTaskDelay(pdMS_TO_TICKS(50));
          last_frame_time = esp_timer_get_time() / 1000;
          continue;
        }

        // Frame rate control
        int64_t now = esp_timer_get_time() / 1000;
        int64_t target = last_frame_time + frame_interval_ms;
        if (now < target) {
          uint32_t delay = target - now;
          if (delay > 0 && delay < 1000) {
            vTaskDelay(pdMS_TO_TICKS(delay));
          }
        }
        last_frame_time = esp_timer_get_time() / 1000;

        // Read next frame from extractor
        extractor_frame_info_t frame = {};
        ret = esp_extractor_read_frame(ext, &frame);

        if (ret != ESP_OK || frame.eos) {
          ESP_LOGI(TAG, "End of stream (frames: %u)", player->frame_count_);
          // Release buffer if allocated
          if (frame.frame_buffer) {
            mem_pool_free(esp_extractor_get_output_pool(ext), frame.frame_buffer);
          }
          break;
        }

        // Only process video frames
        if (frame.stream_type == EXTRACTOR_STREAM_TYPE_VIDEO &&
            frame.frame_buffer && frame.frame_size > 0) {

          // Decode JPEG frame
          if (frame.frame_size <= JPEG_BUFFER_SIZE && player->jpeg_decoder_) {
            memcpy(player->jpeg_buffer_, frame.frame_buffer, frame.frame_size);

            uint8_t buf_idx = (player->current_display_buf_ + 1) % 2;

            jpeg_decode_cfg_t decode_cfg = {
              .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
              .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
              .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
            };

            uint32_t decoded_size = 0;
            esp_err_t dec_ret = jpeg_decoder_process(player->jpeg_decoder_,
                                                      &decode_cfg,
                                                      player->jpeg_buffer_,
                                                      frame.frame_size,
                                                      player->display_buffer_[buf_idx],
                                                      player->display_buffer_size_,
                                                      &decoded_size);
            if (dec_ret == ESP_OK) {
              player->current_display_buf_ = buf_idx;
              player->frame_ready_ = true;
              player->current_time_ms_ = (player->frame_count_ * 1000) / player->video_fps_;
            }
          }
        }

        // Release frame buffer back to pool
        if (frame.frame_buffer) {
          mem_pool_free(esp_extractor_get_output_pool(ext), frame.frame_buffer);
        }
      }

      esp_extractor_close(ext);

      if (player->stop_requested_ || !player->loop_) {
        do_loop = false;
      } else {
        ESP_LOGI(TAG, "Looping");
        vTaskDelay(pdMS_TO_TICKS(200));
      }
    }

    esp_extractor_unregister_all();
    if (player->stop_requested_) break;
  }

  ESP_LOGI(TAG, "Playback task exiting");
  xEventGroupSetBits(player->playback_event_group_, EVENT_TASK_EXIT);
  player->playback_task_handle_ = nullptr;
  vTaskDelete(nullptr);
}

// ============================================================================
// UI Creation
// ============================================================================
void Mp4Player::create_ui_() {
  lv_obj_t *parent = this->parent_ ? this->parent_ : lv_scr_act();

  this->canvas_ = lv_canvas_create(parent);
  lv_canvas_set_buffer(this->canvas_, this->display_buffer_[0],
                       this->video_width_, this->video_height_,
                       LV_COLOR_FORMAT_RGB565);
  lv_obj_center(this->canvas_);
  lv_obj_clear_flag(this->canvas_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(this->canvas_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(this->canvas_, 0, 0);
  lv_obj_set_style_shadow_width(this->canvas_, 0, 0);
  lv_obj_set_style_pad_all(this->canvas_, 0, 0);

  this->loading_label_ = lv_label_create(parent);
  lv_label_set_text(this->loading_label_, "Loading...");
  lv_obj_center(this->loading_label_);
  lv_obj_set_style_text_color(this->loading_label_, lv_color_hex(0x00A8FF), 0);
  lv_obj_set_style_text_font(this->loading_label_, &lv_font_montserrat_16, 0);

  this->touch_layer_ = lv_obj_create(parent);
  lv_obj_remove_style_all(this->touch_layer_);
  lv_obj_set_size(this->touch_layer_, this->video_width_, this->video_height_);
  lv_obj_center(this->touch_layer_);
  lv_obj_add_flag(this->touch_layer_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(this->touch_layer_, touch_cb_, LV_EVENT_CLICKED, this);

  if (this->controls_enabled_) {
    this->create_controls_();
    this->hide_timer_ = lv_timer_create(hide_timer_cb_, this->hide_delay_ms_, this);
    lv_timer_pause(this->hide_timer_);
  }
}

// ============================================================================
// Controls UI with Volume Slider
// ============================================================================
void Mp4Player::create_controls_() {
  lv_obj_t *parent = this->parent_ ? this->parent_ : lv_scr_act();

  this->controls_container_ = lv_obj_create(parent);
  lv_obj_set_size(this->controls_container_, this->video_width_, 120);
  lv_obj_align(this->controls_container_, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_opa(this->controls_container_, LV_OPA_70, 0);
  lv_obj_set_style_bg_color(this->controls_container_, lv_color_black(), 0);
  lv_obj_set_style_pad_all(this->controls_container_, 0, 0);
  lv_obj_set_style_border_width(this->controls_container_, 0, 0);
  lv_obj_clear_flag(this->controls_container_, LV_OBJ_FLAG_SCROLLABLE);

  // === ROW 1: Play/Stop + Progress + Time ===
  this->play_btn_ = lv_btn_create(this->controls_container_);
  lv_obj_set_size(this->play_btn_, 55, 40);
  lv_obj_set_pos(this->play_btn_, 10, 5);
  lv_obj_set_style_radius(this->play_btn_, LV_RADIUS_CIRCLE, 0);
  lv_obj_t *play_label = lv_label_create(this->play_btn_);
  lv_label_set_text(play_label, LV_SYMBOL_PAUSE);
  lv_obj_center(play_label);
  lv_obj_add_event_cb(this->play_btn_, play_btn_cb_, LV_EVENT_CLICKED, this);

  this->stop_btn_ = lv_btn_create(this->controls_container_);
  lv_obj_set_size(this->stop_btn_, 55, 40);
  lv_obj_set_pos(this->stop_btn_, 75, 5);
  lv_obj_set_style_radius(this->stop_btn_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(this->stop_btn_, lv_color_hex(0xCC3333), 0);
  lv_obj_t *stop_label = lv_label_create(this->stop_btn_);
  lv_label_set_text(stop_label, LV_SYMBOL_STOP);
  lv_obj_center(stop_label);
  lv_obj_add_event_cb(this->stop_btn_, stop_btn_cb_, LV_EVENT_CLICKED, this);

  this->progress_slider_ = lv_slider_create(this->controls_container_);
  int slider_w = this->video_width_ - 280;
  if (slider_w < 100) slider_w = 100;
  lv_obj_set_size(this->progress_slider_, slider_w, 10);
  lv_obj_set_pos(this->progress_slider_, 145, 18);
  lv_slider_set_range(this->progress_slider_, 0, 100);
  lv_obj_set_style_bg_color(this->progress_slider_, lv_color_hex(0x404040), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(this->progress_slider_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(this->progress_slider_, lv_color_hex(0x00A8FF), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(this->progress_slider_, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(this->progress_slider_, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_set_style_pad_all(this->progress_slider_, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(this->progress_slider_, progress_slider_cb_, LV_EVENT_VALUE_CHANGED, this);

  this->time_label_ = lv_label_create(this->controls_container_);
  lv_label_set_text(this->time_label_, "00:00 / 00:00");
  lv_obj_set_pos(this->time_label_, this->video_width_ - 135, 3);
  lv_obj_set_style_text_color(this->time_label_, lv_color_white(), 0);
  lv_obj_set_style_text_font(this->time_label_, &lv_font_montserrat_14, 0);

  // === ROW 2: Volume ===
  this->volume_icon_ = lv_label_create(this->controls_container_);
  lv_label_set_text(this->volume_icon_, LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_pos(this->volume_icon_, 12, 52);
  lv_obj_set_style_text_color(this->volume_icon_, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(this->volume_icon_, &lv_font_montserrat_16, 0);

  this->volume_slider_ = lv_slider_create(this->controls_container_);
  lv_obj_set_size(this->volume_slider_, 180, 10);
  lv_obj_set_pos(this->volume_slider_, 45, 57);
  lv_slider_set_range(this->volume_slider_, 0, 100);
  lv_slider_set_value(this->volume_slider_, this->volume_level_, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(this->volume_slider_, lv_color_hex(0x404040), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(this->volume_slider_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(this->volume_slider_, lv_color_hex(0xFF8C00), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(this->volume_slider_, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(this->volume_slider_, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_set_style_pad_all(this->volume_slider_, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(this->volume_slider_, volume_slider_cb_, LV_EVENT_VALUE_CHANGED, this);

  // === ROW 3: Format + Resolution + FPS ===
  this->format_badge_ = lv_label_create(this->controls_container_);
  const char *ext = strrchr(this->file_path_.c_str(), '.');
  const char *fmt = "MP4";
  if (ext) {
    if (strcasecmp(ext, ".avi") == 0) fmt = "AVI";
    else if (strcasecmp(ext, ".mkv") == 0) fmt = "MKV";
  }
  lv_label_set_text(this->format_badge_, fmt);
  lv_obj_set_pos(this->format_badge_, 10, 80);
  lv_obj_set_style_text_color(this->format_badge_, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_text_font(this->format_badge_, &lv_font_montserrat_14, 0);

  this->resolution_label_ = lv_label_create(this->controls_container_);
  char res[32];
  snprintf(res, sizeof(res), "%ux%u", this->video_width_, this->video_height_);
  lv_label_set_text(this->resolution_label_, res);
  lv_obj_set_pos(this->resolution_label_, 60, 80);
  lv_obj_set_style_text_color(this->resolution_label_, lv_color_white(), 0);
  lv_obj_set_style_text_font(this->resolution_label_, &lv_font_montserrat_14, 0);

  lv_obj_t *fps_label = lv_label_create(this->controls_container_);
  char fps_text[16];
  snprintf(fps_text, sizeof(fps_text), "%u fps", this->video_fps_);
  lv_label_set_text(fps_label, fps_text);
  lv_obj_set_pos(fps_label, 150, 80);
  lv_obj_set_style_text_color(fps_label, lv_color_hex(0x00BFFF), 0);
  lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_14, 0);

  if (this->has_audio_) {
    lv_obj_t *audio_label = lv_label_create(this->controls_container_);
    lv_label_set_text(audio_label, "AUDIO");
    lv_obj_set_pos(audio_label, 220, 80);
    lv_obj_set_style_text_color(audio_label, lv_color_hex(0xFF8C00), 0);
    lv_obj_set_style_text_font(audio_label, &lv_font_montserrat_14, 0);
  }
}

// ============================================================================
// Progress update
// ============================================================================
void Mp4Player::update_progress_() {
  if (!this->progress_slider_ || !this->time_label_) return;

  if (this->total_duration_ms_ > 0) {
    int progress = (this->current_time_ms_ * 100) / this->total_duration_ms_;
    if (progress > 100) progress = 100;
    lv_slider_set_value(this->progress_slider_, progress, LV_ANIM_OFF);
  }

  char cur[12], tot[12];
  this->format_time_(cur, sizeof(cur), this->current_time_ms_);
  this->format_time_(tot, sizeof(tot), this->total_duration_ms_);
  char time_buf[32];
  snprintf(time_buf, sizeof(time_buf), "%s / %s", cur, tot);
  lv_label_set_text(this->time_label_, time_buf);
}

void Mp4Player::format_time_(char *buf, size_t buf_size, uint32_t time_ms) {
  uint32_t secs = time_ms / 1000;
  uint32_t mins = secs / 60;
  secs %= 60;
  snprintf(buf, buf_size, "%02u:%02u", mins, secs);
}

// ============================================================================
// Volume
// ============================================================================
void Mp4Player::apply_volume_to_pcm_(uint8_t *pcm_data, size_t size) {
  if (this->volume_level_ >= 100) return;
  if (this->volume_level_ == 0) { memset(pcm_data, 0, size); return; }

  int16_t *samples = reinterpret_cast<int16_t *>(pcm_data);
  size_t num = size / 2;
  uint8_t vol = this->volume_level_;
  for (size_t i = 0; i < num; i++) {
    int32_t s = (static_cast<int32_t>(samples[i]) * vol) / 100;
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    samples[i] = static_cast<int16_t>(s);
  }
}

// ============================================================================
// Show/Hide controls
// ============================================================================
void Mp4Player::show_controls_() {
  if (!this->controls_container_) return;
  lv_obj_clear_flag(this->controls_container_, LV_OBJ_FLAG_HIDDEN);
  this->controls_visible_ = true;
  if (this->state_ == PlayerState::PLAYING && this->hide_timer_) {
    lv_timer_reset(this->hide_timer_);
    lv_timer_resume(this->hide_timer_);
  }
}

void Mp4Player::hide_controls_() {
  if (!this->controls_container_) return;
  lv_obj_add_flag(this->controls_container_, LV_OBJ_FLAG_HIDDEN);
  this->controls_visible_ = false;
  if (this->hide_timer_) lv_timer_pause(this->hide_timer_);
}

// ============================================================================
// Static callbacks
// ============================================================================
void Mp4Player::play_btn_cb_(lv_event_t *e) {
  Mp4Player *p = static_cast<Mp4Player *>(lv_event_get_user_data(e));
  if (p->is_playing()) p->pause(); else p->play();
}

void Mp4Player::stop_btn_cb_(lv_event_t *e) {
  Mp4Player *p = static_cast<Mp4Player *>(lv_event_get_user_data(e));
  p->stop();
}

void Mp4Player::progress_slider_cb_(lv_event_t *e) {
  Mp4Player *p = static_cast<Mp4Player *>(lv_event_get_user_data(e));
  int val = lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e)));
  if (p->total_duration_ms_ > 0) {
    p->current_time_ms_ = (p->total_duration_ms_ * val) / 100;
  }
}

void Mp4Player::volume_slider_cb_(lv_event_t *e) {
  Mp4Player *p = static_cast<Mp4Player *>(lv_event_get_user_data(e));
  int val = lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e)));
  p->volume_level_ = (uint8_t)val;
  if (p->volume_icon_) {
    if (val == 0) lv_label_set_text(p->volume_icon_, LV_SYMBOL_MUTE);
    else if (val < 50) lv_label_set_text(p->volume_icon_, LV_SYMBOL_VOLUME_MID);
    else lv_label_set_text(p->volume_icon_, LV_SYMBOL_VOLUME_MAX);
  }
  ESP_LOGD(TAG, "Volume: %d%%", val);
}

void Mp4Player::hide_timer_cb_(lv_timer_t *timer) {
  Mp4Player *p = static_cast<Mp4Player *>(lv_timer_get_user_data(timer));
  p->hide_controls_();
  lv_timer_pause(timer);
}

void Mp4Player::touch_cb_(lv_event_t *e) {
  Mp4Player *p = static_cast<Mp4Player *>(lv_event_get_user_data(e));
  if (p->controls_visible_) p->hide_controls_(); else p->show_controls_();
}

}  // namespace mp4_player
}  // namespace esphome

#else

namespace esphome {
namespace mp4_player {
void Mp4Player::setup() { ESP_LOGE("mp4_player", "Requires ESP-IDF"); this->mark_failed(); }
void Mp4Player::loop() {}
void Mp4Player::dump_config() {}
void Mp4Player::play() {}
void Mp4Player::pause() {}
void Mp4Player::stop() {}
}  // namespace mp4_player
}  // namespace esphome

#endif
