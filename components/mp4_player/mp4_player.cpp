#include "mp4_player.h"

#ifdef USE_ESP_IDF

#include "esp_heap_caps.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace esphome {
namespace mp4_player {

static const char *TAG = "mp4_player";

static constexpr size_t JPEG_BUFFER_SIZE = 256 * 1024;    //static constexpr size_t JPEG_BUFFER_SIZE = 256 * 1024; 
static constexpr size_t EXTRACTOR_POOL_SIZE = 1024 * 1024;  // 1MB for extractor cache (was 512KB)
static constexpr size_t EXTRACTOR_POOL_BLOCKS = 10;           // 8 blocks of 128KB (was 4)
static constexpr size_t AUDIO_PCM_BUFFER_SIZE = 32 * 1024;  // 32KB for decoded PCM
static constexpr size_t AUDIO_RING_BUFFER_SIZE = 512 * 1024; // 512KB audio ring buffer (~2.7s at 48kHz stereo)

// Read-ahead buffer for file I/O to reduce small read overhead
// USB storage can handle larger buffers for better throughput
static constexpr size_t FILE_READ_AHEAD_SIZE_SD = 64 * 1024;   // 64KB for SD card
static constexpr size_t FILE_READ_AHEAD_SIZE_USB = 128 * 1024; // 128KB for USB (faster bus)

// ============================================================================
// File I/O wrappers for esp_extractor
// Uses stdio (fopen/fread) with setvbuf for large read-ahead buffering
// This dramatically reduces SD card transaction overhead for streaming
// ============================================================================
void *Mp4Player::file_open_cb_(char *url, void *ctx) {
  FILE *fp = fopen(url, "rb");
  if (!fp) {
    ESP_LOGE(TAG, "Failed to open: %s", url);
    return nullptr;
  }
  // Use larger read-ahead buffer for USB (128KB) vs SD card (64KB)
  // USB 2.0 has higher throughput and benefits from larger sequential reads
  bool is_usb = (strncmp(url, "/usb", 4) == 0);
  size_t buf_size = is_usb ? FILE_READ_AHEAD_SIZE_USB : FILE_READ_AHEAD_SIZE_SD;

  uint8_t *io_buf = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
  if (io_buf) {
    setvbuf(fp, (char *)io_buf, _IOFBF, buf_size);
    ESP_LOGI(TAG, "File I/O buffer: %uKB read-ahead (%s)", buf_size / 1024, is_usb ? "USB" : "SD");
  } else {
    ESP_LOGW(TAG, "Failed to allocate I/O buffer, using default");
  }
  return (void *)fp;
}

int Mp4Player::file_read_cb_(void *data, uint32_t size, void *ctx) {
  FILE *fp = (FILE *)ctx;
  size_t bytes = fread(data, 1, size, fp);
  return (int)bytes;
}

int Mp4Player::file_seek_cb_(uint32_t position, void *ctx) {
  FILE *fp = (FILE *)ctx;
  return fseek(fp, (long)position, SEEK_SET);
}

int Mp4Player::file_close_cb_(void *ctx) {
  FILE *fp = (FILE *)ctx;
  return fclose(fp);
}

uint32_t Mp4Player::file_size_cb_(void *ctx) {
  FILE *fp = (FILE *)ctx;
  long cur = ftell(fp);
  fseek(fp, 0, SEEK_END);
  long end = ftell(fp);
  fseek(fp, cur, SEEK_SET);
  return end <= 0 ? 0 : (uint32_t)end;
}

// ============================================================================
// Strip JPEG COM markers that crash ESP32-P4 hardware JPEG decoder
// FFmpeg/Libavcodec adds COM markers like "Lavc60.39." which trigger:
// "jpeg_parse_com_marker: COM marker data underflow"
// Returns the new size after stripping
// ============================================================================
size_t Mp4Player::strip_jpeg_com_markers_(uint8_t *data, size_t size) {
  if (size < 4) return size;

  size_t read_pos = 2;   // Skip SOI (0xFFD8)
  size_t write_pos = 2;
  bool stripped = false;

  while (read_pos + 1 < size) {
    if (data[read_pos] != 0xFF) {
      data[write_pos++] = data[read_pos++];
      continue;
    }

    uint8_t marker = data[read_pos + 1];

    // 0xFF 0x00 is byte stuffing, not a marker
    if (marker == 0x00) {
      data[write_pos++] = data[read_pos++];
      data[write_pos++] = data[read_pos++];
      continue;
    }

    // COM marker (0xFFFE) - strip it
    if (marker == 0xFE) {
      if (read_pos + 3 >= size) break;

      uint16_t marker_size = (data[read_pos + 2] << 8) | data[read_pos + 3];
      if (marker_size < 2) break;

      size_t skip = 2 + marker_size;  // 0xFF 0xFE + size+data
      if (read_pos + skip > size) break;

      read_pos += skip;
      stripped = true;
      continue;
    }

    // SOS (0xFFDA) - copy everything after (entropy-coded data)
    if (marker == 0xDA) {
      while (read_pos < size) {
        data[write_pos++] = data[read_pos++];
      }
      break;
    }

    // Other markers with length field - copy as-is
    if (marker != 0xD8 && marker != 0xD9 && marker != 0x01) {
      if (read_pos + 3 < size) {
        uint16_t marker_size = (data[read_pos + 2] << 8) | data[read_pos + 3];
        size_t copy_size = 2 + marker_size;
        if (read_pos + copy_size <= size) {
          for (size_t i = 0; i < copy_size; i++) {
            data[write_pos++] = data[read_pos++];
          }
          continue;
        }
      }
    }

    // Default: copy byte
    data[write_pos++] = data[read_pos++];
  }

  if (stripped && write_pos < size) {
    return write_pos;
  }
  return size;
}

// ============================================================================
// Setup
// ============================================================================
void Mp4Player::setup() {
  ESP_LOGI(TAG, "Setting up MP4 Player...");

  // If USB storage is configured, verify it's ready before accessing files
  if (this->usb_storage_ != nullptr) {
    if (this->usb_storage_->is_failed()) {
      ESP_LOGE(TAG, "USB storage component failed to initialize - cannot access media files");
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "USB storage available, file path: %s", this->file_path_.c_str());
  }

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
  // Skip probe if file is not accessible yet (e.g. USB drive not mounted)
  bool probe_skipped = false;
  {
    FILE *test_fp = fopen(this->file_path_.c_str(), "rb");
    if (test_fp) {
      fclose(test_fp);
    } else {
      ESP_LOGI(TAG, "File not accessible yet, skipping probe (will detect at playback)");
      probe_skipped = true;
    }
  }

  if (!probe_skipped) {
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
    probe_cfg.output_pool_size = 256 * 1024;
    probe_cfg.cache_block_num = 3;
    probe_cfg.cache_block_size = 256 * 1024 / 3;

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
        if (this->has_audio_) {
          extractor_stream_info_t ainfo = {};
          if (esp_extractor_get_stream_info(probe, EXTRACTOR_STREAM_TYPE_AUDIO, 0, &ainfo) == ESP_OK) {
            this->audio_format_ = ainfo.stream_info.audio_info.format;
            this->audio_sample_rate_ = ainfo.stream_info.audio_info.sample_rate;
            this->audio_channels_ = ainfo.stream_info.audio_info.channel;
            this->audio_bits_per_sample_ = ainfo.stream_info.audio_info.bits_per_sample;
            ESP_LOGI(TAG, "Audio: format=%d, %uHz, %uch, %ubit",
                     this->audio_format_, this->audio_sample_rate_,
                     this->audio_channels_, this->audio_bits_per_sample_);
          }
        }
      }
      esp_extractor_close(probe);
    }

    esp_extractor_unregister_all();
  }

  if (this->video_width_ == 0 || this->video_height_ == 0) {
    ESP_LOGW(TAG, "Could not probe video, using 800x480");
    this->video_width_ = 800;
    this->video_height_ = 480;
  }

  // Allocate display buffers (RGB565 double buffer)
  // JPEG hardware decoder aligns output to 16-byte boundaries, so allocate with aligned dimensions
  uint32_t aligned_w = (this->video_width_ + 15) & ~15;
  uint32_t aligned_h = (this->video_height_ + 15) & ~15;
  this->display_buffer_size_ = aligned_w * aligned_h * 2;
  ESP_LOGI(TAG, "Display buffer: %ux%u (aligned %ux%u), %u bytes",
           this->video_width_, this->video_height_, aligned_w, aligned_h, this->display_buffer_size_);
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

  // Setup audio decoder if audio track found (or probe skipped) and speaker configured
  if ((this->has_audio_ || probe_skipped) && this->speaker_) {
    // Register individual audio decoders (AAC, MP3, FLAC, PCM)
    esp_aac_dec_register();
    esp_mp3_dec_register();
    esp_flac_dec_register();
    esp_pcm_dec_register();

    // Allocate PCM output buffer
    this->audio_pcm_buffer_size_ = AUDIO_PCM_BUFFER_SIZE;
    this->audio_pcm_buffer_ = (uint8_t *)heap_caps_malloc(this->audio_pcm_buffer_size_, MALLOC_CAP_SPIRAM);
    if (!this->audio_pcm_buffer_) {
      ESP_LOGW(TAG, "Failed to allocate audio PCM buffer, audio disabled");
      this->has_audio_ = false;
    } else {
      ESP_LOGI(TAG, "Audio decoder initialized, PCM buffer %u bytes", this->audio_pcm_buffer_size_);

      // Allocate audio ring buffer for decoupled output
      this->audio_ring_size_ = AUDIO_RING_BUFFER_SIZE;
      this->audio_ring_buffer_ = (uint8_t *)heap_caps_malloc(this->audio_ring_size_, MALLOC_CAP_SPIRAM);
      if (!this->audio_ring_buffer_) {
        ESP_LOGW(TAG, "Failed to allocate audio ring buffer, using direct output");
        this->audio_ring_size_ = 0;
      } else {
        this->audio_ring_read_ = 0;
        this->audio_ring_write_ = 0;
        ESP_LOGI(TAG, "Audio ring buffer: %u bytes", this->audio_ring_size_);
      }
    }
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
  // Update UI when video dimensions changed (e.g. after USB probe during playback)
  if (this->dimensions_changed_) {
    this->dimensions_changed_ = false;
    ESP_LOGI(TAG, "Updating UI for new dimensions: %ux%u", this->video_width_, this->video_height_);

    // Update resolution label
    if (this->resolution_label_) {
      char res[32];
      snprintf(res, sizeof(res), "%ux%u", this->video_width_, this->video_height_);
      lv_label_set_text(this->resolution_label_, res);
    }

    // Update touch layer size
    if (this->touch_layer_) {
      lv_obj_set_size(this->touch_layer_, this->video_width_, this->video_height_);
    }

    // Update controls container width
    if (this->controls_container_) {
      lv_obj_set_size(this->controls_container_, this->video_width_, 120);
    }

    // Update progress slider width
    if (this->progress_slider_) {
      int slider_w = this->video_width_ - 280;
      if (slider_w < 100) slider_w = 100;
      lv_obj_set_size(this->progress_slider_, slider_w, 10);
    }

    // Update time label position
    if (this->time_label_) {
      lv_obj_set_pos(this->time_label_, this->video_width_ - 135, 3);
    }
  }

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

    // Hide loading label on first video frame displayed
    if (this->loading_label_ && !lv_obj_has_flag(this->loading_label_, LV_OBJ_FLAG_HIDDEN)) {
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
  ESP_LOGCONFIG(TAG, "  Storage: %s", this->usb_storage_ != nullptr ? "USB" : "SD Card");
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

  // Fire on_play trigger (e.g. to stop microphone/wake word and free I2S bus)
  this->on_play_callbacks_.call();

  if (this->state_ == PlayerState::PAUSED) {
    this->state_ = PlayerState::PLAYING;
    xEventGroupSetBits(this->playback_event_group_, EVENT_START);
    if (this->play_btn_) {
      lv_obj_t *lbl = lv_obj_get_child(this->play_btn_, 0);
      if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PAUSE);
    }
    // Start auto-hide timer for controls on resume
    if (this->controls_visible_ && this->hide_timer_) {
      lv_timer_reset(this->hide_timer_);
      lv_timer_resume(this->hide_timer_);
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

  // Start auto-hide timer for controls
  if (this->controls_visible_ && this->hide_timer_) {
    lv_timer_reset(this->hide_timer_);
    lv_timer_resume(this->hide_timer_);
  }

  if (!this->playback_task_handle_) {
    xEventGroupClearBits(this->playback_event_group_, EVENT_START | EVENT_STOP | EVENT_TASK_EXIT);
    BaseType_t ret = xTaskCreatePinnedToCore(
        playback_task_, "mp4_play", 16384, this, 12,
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
  // Show controls and stop auto-hide when paused
  this->show_controls_();
  if (this->hide_timer_) lv_timer_pause(this->hide_timer_);
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

  // Show controls when stopped (user needs them visible)
  this->show_controls_();
  if (this->hide_timer_) lv_timer_pause(this->hide_timer_);

  // Fire on_stop trigger (e.g. to restart microphone/wake word)
  this->on_stop_callbacks_.call();
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
      // Extract both audio and video if speaker is available
      // Always extract AV when speaker exists (probe may have been skipped for USB)
      ext_cfg.extract_mask = player->speaker_
                              ? ESP_EXTRACT_MASK_AV : ESP_EXTRACT_MASK_VIDEO;
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

      // Get video info and reallocate display buffers if dimensions changed
      uint16_t vnum = 0;
      esp_extractor_get_stream_num(ext, EXTRACTOR_STREAM_TYPE_VIDEO, &vnum);
      if (vnum > 0) {
        extractor_stream_info_t sinfo = {};
        if (esp_extractor_get_stream_info(ext, EXTRACTOR_STREAM_TYPE_VIDEO, 0, &sinfo) == ESP_OK) {
          uint32_t fps = sinfo.stream_info.video_info.fps;
          player->video_fps_ = fps > 0 ? fps : 25;
          player->total_duration_ms_ = sinfo.duration;

          uint32_t actual_w = sinfo.stream_info.video_info.width;
          uint32_t actual_h = sinfo.stream_info.video_info.height;

          // Check if display buffers need reallocation (e.g. probe failed at setup)
          uint32_t aligned_w = (actual_w + 15) & ~15;
          uint32_t aligned_h = (actual_h + 15) & ~15;
          uint32_t needed_size = aligned_w * aligned_h * 2;

          if (needed_size > player->display_buffer_size_) {
            ESP_LOGW(TAG, "Video %ux%u needs %u bytes but buffer is %u, reallocating",
                     actual_w, actual_h, needed_size, player->display_buffer_size_);
            for (int i = 0; i < 2; i++) {
              if (player->display_buffer_[i]) {
                heap_caps_free(player->display_buffer_[i]);
                player->display_buffer_[i] = nullptr;
              }
              player->display_buffer_[i] = (uint8_t *)heap_caps_aligned_alloc(
                  64, needed_size, MALLOC_CAP_SPIRAM);
              if (!player->display_buffer_[i]) {
                ESP_LOGE(TAG, "Failed to reallocate display buffer %d (%u bytes)", i, needed_size);
                break;
              }
              memset(player->display_buffer_[i], 0, needed_size);
            }
            if (player->display_buffer_[0] && player->display_buffer_[1]) {
              player->display_buffer_size_ = needed_size;
              player->video_width_ = actual_w;
              player->video_height_ = actual_h;
              player->dimensions_changed_ = true;
              ESP_LOGI(TAG, "Display buffers reallocated: %ux%u (aligned %ux%u), %u bytes",
                       actual_w, actual_h, aligned_w, aligned_h, needed_size);
            } else {
              ESP_LOGE(TAG, "Buffer reallocation failed, playback may fail");
            }
          } else if (actual_w != player->video_width_ || actual_h != player->video_height_) {
            // Dimensions changed but buffer is large enough
            player->video_width_ = actual_w;
            player->video_height_ = actual_h;
            player->dimensions_changed_ = true;
          }

          ESP_LOGI(TAG, "Playback video: %ux%u @ %u fps (buffer: %u bytes)",
                   player->video_width_, player->video_height_,
                   player->video_fps_, player->display_buffer_size_);
        }
      }

      // Detect audio if probe was skipped (USB not mounted at setup time)
      if (!player->has_audio_ && player->speaker_) {
        uint16_t anum = 0;
        esp_extractor_get_stream_num(ext, EXTRACTOR_STREAM_TYPE_AUDIO, &anum);
        if (anum > 0) {
          player->has_audio_ = true;
          ESP_LOGI(TAG, "Audio track detected during playback");
        }
      }

      // Open audio decoder if audio is available
      esp_audio_simple_dec_handle_t audio_dec = nullptr;
      bool speaker_started = false;
      if (player->has_audio_ && player->speaker_ && player->audio_pcm_buffer_) {
        uint16_t anum = 0;
        esp_extractor_get_stream_num(ext, EXTRACTOR_STREAM_TYPE_AUDIO, &anum);
        if (anum > 0) {
          extractor_stream_info_t ainfo = {};
          if (esp_extractor_get_stream_info(ext, EXTRACTOR_STREAM_TYPE_AUDIO, 0, &ainfo) == ESP_OK) {
            player->audio_format_ = ainfo.stream_info.audio_info.format;
            player->audio_sample_rate_ = ainfo.stream_info.audio_info.sample_rate;
            player->audio_channels_ = ainfo.stream_info.audio_info.channel;
            player->audio_bits_per_sample_ = ainfo.stream_info.audio_info.bits_per_sample;
          }
          esp_audio_simple_dec_type_t dec_type = map_audio_format_(player->audio_format_);
          if (dec_type != ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
            esp_audio_simple_dec_cfg_t dec_cfg = {};
            dec_cfg.dec_type = dec_type;
            if (esp_audio_simple_dec_open(&dec_cfg, &audio_dec) == ESP_AUDIO_ERR_OK) {
              player->audio_decoder_ready_ = true;
              ESP_LOGI(TAG, "Audio decoder opened (format=%d)", player->audio_format_);

              // Configure speaker with correct audio stream parameters
              uint8_t bps = player->audio_bits_per_sample_ > 0 ? player->audio_bits_per_sample_ : 16;
              uint8_t ch = player->audio_channels_ > 0 ? player->audio_channels_ : 1;
              uint32_t sr = player->audio_sample_rate_ > 0 ? player->audio_sample_rate_ : 16000;
              audio::AudioStreamInfo stream_info(bps, ch, sr);
              player->speaker_->set_audio_stream_info(stream_info);
              player->speaker_->start();
              speaker_started = true;
              ESP_LOGI(TAG, "Speaker started: %uHz, %uch, %ubit", sr, ch, bps);

              // Wait for I2S channel to fully initialize before sending audio data
              vTaskDelay(pdMS_TO_TICKS(100));

              // Start audio output task if ring buffer is available
              // Pin to core 1 (core 0 is used by WiFi which causes audio stalls)
              if (player->audio_ring_buffer_ && !player->audio_task_handle_) {
                player->audio_ring_read_ = 0;
                player->audio_ring_write_ = 0;
                player->audio_task_running_ = true;
                xTaskCreatePinnedToCore(
                    audio_output_task_, "audio_out", 4096, player, 15,
                    &player->audio_task_handle_, 1);
              }
            } else {
              ESP_LOGW(TAG, "Failed to open audio decoder for format %d", player->audio_format_);
            }
          } else {
            ESP_LOGW(TAG, "Unsupported audio format: %d", player->audio_format_);
          }
        }
      }

      player->jpeg_hw_error_logged_ = false;
      player->jpeg_hw_error_count_ = 0;

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

        // Read next frame from extractor (no delay before read - process audio immediately)
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

        // Process audio frames FIRST (priority over video timing)
        if (frame.stream_type == EXTRACTOR_STREAM_TYPE_AUDIO &&
            frame.frame_buffer && frame.frame_size > 0 &&
            audio_dec && player->speaker_ && player->audio_pcm_buffer_) {
          esp_audio_simple_dec_raw_t raw = {};
          raw.buffer = frame.frame_buffer;
          raw.len = frame.frame_size;
          raw.eos = false;
          raw.consumed = 0;
          raw.frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE;

          while (raw.consumed < raw.len && !player->stop_requested_) {
            esp_audio_simple_dec_out_t out = {};
            out.buffer = player->audio_pcm_buffer_;
            out.len = player->audio_pcm_buffer_size_;
            out.decoded_size = 0;

            esp_audio_err_t aret = esp_audio_simple_dec_process(audio_dec, &raw, &out);
            if (aret == ESP_AUDIO_ERR_OK && out.decoded_size > 0) {
              if (player->audio_ring_buffer_) {
                // Push decoded audio to ring buffer, wait longer before dropping
                // With 512KB ring buffer (~2.7s), we can afford to wait
                size_t pushed = 0;
                int retries = 0;
                while (pushed < out.decoded_size && !player->stop_requested_ && retries < 50) {
                  size_t p = player->audio_ring_push_(
                      player->audio_pcm_buffer_ + pushed, out.decoded_size - pushed);
                  pushed += p;
                  if (pushed < out.decoded_size) {
                    vTaskDelay(pdMS_TO_TICKS(1));
                    retries++;
                  }
                }
              } else {
                player->apply_volume_to_pcm_(player->audio_pcm_buffer_, out.decoded_size);
                size_t written = 0;
                while (written < out.decoded_size && !player->stop_requested_) {
                  size_t w = player->speaker_->play(
                      player->audio_pcm_buffer_ + written, out.decoded_size - written);
                  if (w > 0) {
                    written += w;
                  } else {
                    vTaskDelay(pdMS_TO_TICKS(5));
                  }
                }
              }
            } else if (aret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
              break;
            } else {
              break;
            }
          }

          // Release audio frame and continue reading immediately
          if (frame.frame_buffer) {
            mem_pool_free(esp_extractor_get_output_pool(ext), frame.frame_buffer);
          }
          continue;  // Skip to next frame immediately - don't wait for video timing
        }

        // Process video frames (after audio has been handled)
        if (frame.stream_type == EXTRACTOR_STREAM_TYPE_VIDEO &&
            frame.frame_buffer && frame.frame_size > 0) {

          // Log first video frame for diagnostics
          if (player->frame_count_ == 0) {
            ESP_LOGI(TAG, "First video frame: %u bytes (JPEG buf: %u, display buf: %u)",
                     frame.frame_size, JPEG_BUFFER_SIZE, player->display_buffer_size_);
          }

          // Frame rate control - only delay for video frames to maintain fps
          int64_t now = esp_timer_get_time() / 1000;
          int64_t target = last_frame_time + frame_interval_ms;
          if (now < target) {
            uint32_t delay = target - now;
            if (delay > 0 && delay < 1000) {
              vTaskDelay(pdMS_TO_TICKS(delay));
            }
          }
          last_frame_time = esp_timer_get_time() / 1000;

          // Decode JPEG frame
          if (frame.frame_size > JPEG_BUFFER_SIZE) {
            if (player->frame_count_ == 0) {
              ESP_LOGE(TAG, "Video frame too large: %u > %u bytes", frame.frame_size, JPEG_BUFFER_SIZE);
            }
          } else if (!player->jpeg_decoder_) {
            ESP_LOGE(TAG, "No JPEG decoder available");
          }
          if (frame.frame_size <= JPEG_BUFFER_SIZE && player->jpeg_decoder_) {
            memcpy(player->jpeg_buffer_, frame.frame_buffer, frame.frame_size);

            // Strip COM markers that crash ESP32-P4 JPEG hardware decoder
            size_t jpeg_size = strip_jpeg_com_markers_(player->jpeg_buffer_, frame.frame_size);

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
                                                      jpeg_size,
                                                      player->display_buffer_[buf_idx],
                                                      player->display_buffer_size_,
                                                      &decoded_size);
            if (dec_ret == ESP_OK) {
              player->current_display_buf_ = buf_idx;
              player->frame_ready_ = true;
              player->current_time_ms_ = (player->frame_count_ * 1000) / player->video_fps_;
            } else {
              // Hardware JPEG decoder failed (unsupported sampling factor, etc.)
              player->jpeg_hw_error_count_++;
              if (!player->jpeg_hw_error_logged_) {
                player->jpeg_hw_error_logged_ = true;
                ESP_LOGE(TAG, "JPEG HW decode failed (err=%d). Video uses unsupported chroma subsampling.", dec_ret);
                ESP_LOGE(TAG, "Re-encode with: ffmpeg -i input.mp4 -c:v mjpeg -pix_fmt yuvj420p -q:v 3 output.mp4");
              }
              // Update time even on failed frames
              player->current_time_ms_ = (player->frame_count_ * 1000) / player->video_fps_;
            }
          }
        }

        // Release frame buffer back to pool
        if (frame.frame_buffer) {
          mem_pool_free(esp_extractor_get_output_pool(ext), frame.frame_buffer);
        }
      }

      // Stop audio output task first
      if (player->audio_task_handle_) {
        player->audio_task_running_ = false;
        vTaskDelay(pdMS_TO_TICKS(50));  // Let audio task drain and exit
        player->audio_task_handle_ = nullptr;
      }

      // Close audio decoder and stop speaker
      if (audio_dec) {
        esp_audio_simple_dec_close(audio_dec);
        audio_dec = nullptr;
        player->audio_decoder_ready_ = false;
      }
      if (speaker_started) {
        player->speaker_->finish();
        speaker_started = false;
      }

      esp_extractor_close(ext);

      if (player->jpeg_hw_error_count_ > 0) {
        ESP_LOGW(TAG, "JPEG HW decode errors: %u frames skipped", player->jpeg_hw_error_count_);
      }

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
// Audio format mapping
// ============================================================================
esp_audio_simple_dec_type_t Mp4Player::map_audio_format_(extractor_audio_format_t fmt) {
  switch (fmt) {
    case EXTRACTOR_AUDIO_FORMAT_AAC:  return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    case EXTRACTOR_AUDIO_FORMAT_MP3:  return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    case EXTRACTOR_AUDIO_FORMAT_FLAC: return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
    case EXTRACTOR_AUDIO_FORMAT_PCM:  return ESP_AUDIO_SIMPLE_DEC_TYPE_PCM;
    case EXTRACTOR_AUDIO_FORMAT_ADPCM: return ESP_AUDIO_SIMPLE_DEC_TYPE_ADPCM;
    case EXTRACTOR_AUDIO_FORMAT_OPUS: return ESP_AUDIO_SIMPLE_DEC_TYPE_RAW_OPUS;
    case EXTRACTOR_AUDIO_FORMAT_AMRNB: return ESP_AUDIO_SIMPLE_DEC_TYPE_AMRNB;
    case EXTRACTOR_AUDIO_FORMAT_AMRWB: return ESP_AUDIO_SIMPLE_DEC_TYPE_AMRWB;
    case EXTRACTOR_AUDIO_FORMAT_G711A: return ESP_AUDIO_SIMPLE_DEC_TYPE_G711A;
    case EXTRACTOR_AUDIO_FORMAT_G711U: return ESP_AUDIO_SIMPLE_DEC_TYPE_G711U;
    case EXTRACTOR_AUDIO_FORMAT_ALAC: return ESP_AUDIO_SIMPLE_DEC_TYPE_ALAC;
    default: return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
  }
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
  if (this->volume_level_ >= 100) return;  // No adjustment at max
  if (this->volume_level_ == 0) { memset(pcm_data, 0, size); return; }

  int16_t *samples = reinterpret_cast<int16_t *>(pcm_data);
  size_t num = size / 2;
  for (size_t i = 0; i < num; i++) {
    samples[i] = (int16_t)((int32_t)samples[i] * this->volume_level_ / 100);
  }
}

// ============================================================================
// Audio ring buffer - lock-free single producer / single consumer
// ============================================================================
size_t Mp4Player::audio_ring_available_() const {
  size_t w = this->audio_ring_write_;
  size_t r = this->audio_ring_read_;
  if (w >= r) return w - r;
  return this->audio_ring_size_ - r + w;
}

size_t Mp4Player::audio_ring_free_() const {
  return this->audio_ring_size_ - 1 - audio_ring_available_();
}

size_t Mp4Player::audio_ring_push_(const uint8_t *data, size_t len) {
  size_t free = audio_ring_free_();
  if (len > free) len = free;
  if (len == 0) return 0;

  size_t w = this->audio_ring_write_;
  size_t to_end = this->audio_ring_size_ - w;
  if (len <= to_end) {
    memcpy(this->audio_ring_buffer_ + w, data, len);
  } else {
    memcpy(this->audio_ring_buffer_ + w, data, to_end);
    memcpy(this->audio_ring_buffer_, data + to_end, len - to_end);
  }
  this->audio_ring_write_ = (w + len) % this->audio_ring_size_;
  return len;
}

size_t Mp4Player::audio_ring_pop_(uint8_t *data, size_t len) {
  size_t avail = audio_ring_available_();
  if (len > avail) len = avail;
  if (len == 0) return 0;

  size_t r = this->audio_ring_read_;
  size_t to_end = this->audio_ring_size_ - r;
  if (len <= to_end) {
    memcpy(data, this->audio_ring_buffer_ + r, len);
  } else {
    memcpy(data, this->audio_ring_buffer_ + r, to_end);
    memcpy(data + to_end, this->audio_ring_buffer_, len - to_end);
  }
  this->audio_ring_read_ = (r + len) % this->audio_ring_size_;
  return len;
}

// ============================================================================
// Audio output task - drains ring buffer to speaker at steady rate
// ============================================================================
void Mp4Player::audio_output_task_(void *arg) {
  Mp4Player *player = static_cast<Mp4Player *>(arg);
  const size_t chunk_size = 16384;
  uint8_t *chunk = (uint8_t *)heap_caps_malloc(chunk_size, MALLOC_CAP_SPIRAM);
  if (!chunk) {
    ESP_LOGE(TAG, "Audio output task: failed to allocate chunk buffer");
    player->audio_task_running_ = false;
    vTaskDelete(nullptr);
    return;
  }

  ESP_LOGI(TAG, "Audio output task started (priority 15, core 1)");

  // Wait for ring buffer to pre-fill before starting output
  const size_t prefill_target = 96 * 1024;  // 96KB (~0.5s at 48kHz stereo)
  int prefill_wait = 0;
  while (player->audio_task_running_ && prefill_wait < 200) {
    size_t avail = player->audio_ring_available_();
    if (avail >= prefill_target) {
      ESP_LOGI(TAG, "Audio pre-filled: %u bytes, starting output", avail);
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    prefill_wait++;
  }

  // Jitter buffer thresholds to prevent latency accumulation
  // If ring buffer fills above high_watermark, skip old audio to resync
  const size_t high_watermark = player->audio_ring_size_ * 3 / 4;  // 384KB = ~2s
  const size_t target_level = 128 * 1024;  // Target ~0.67s of buffered audio
  uint32_t underrun_count = 0;
  uint32_t jitter_corrections = 0;
  bool was_paused = false;

  while (player->audio_task_running_) {
    // Handle pause: feed silence to keep speaker/I2S alive
    if (player->state_ == PlayerState::PAUSED) {
      if (!was_paused) {
        was_paused = true;
      }
      // Feed silence to prevent I2S DMA underrun and speaker auto-stop
      memset(chunk, 0, 1024);
      player->speaker_->play(chunk, 1024);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    // On resume from pause: flush ring buffer to clear stale audio
    if (was_paused) {
      was_paused = false;
      // Flush the ring buffer - the audio data from before pause is stale
      player->audio_ring_read_ = player->audio_ring_write_;
      underrun_count = 0;
      ESP_LOGI(TAG, "Audio resumed: ring buffer flushed for resync");
      // Wait for fresh audio to fill up
      int refill_wait = 0;
      while (player->audio_task_running_ && refill_wait < 100) {
        if (player->audio_ring_available_() >= prefill_target) break;
        vTaskDelay(pdMS_TO_TICKS(10));
        refill_wait++;
      }
      continue;
    }

    size_t avail = player->audio_ring_available_();

    // Jitter buffer management: if ring buffer is too full, audio is
    // accumulating faster than it's being consumed (latency drift).
    // Skip old audio to resync and prevent progressive degradation.
    if (avail > high_watermark) {
      size_t skip = avail - target_level;
      // Advance read pointer to discard old audio
      player->audio_ring_read_ = (player->audio_ring_read_ + skip) % player->audio_ring_size_;
      avail = player->audio_ring_available_();
      jitter_corrections++;
      if (jitter_corrections <= 5) {
        ESP_LOGW(TAG, "Audio jitter correction: skipped %u bytes to maintain A/V sync", skip);
      }
    }

    if (avail == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
      avail = player->audio_ring_available_();
      if (avail == 0) {
        underrun_count++;
        if (underrun_count == 100) {
          ESP_LOGW(TAG, "Audio underrun detected (ring buffer empty)");
        }
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
    }

    if (underrun_count >= 100) {
      ESP_LOGI(TAG, "Audio recovered after %u underrun cycles", underrun_count);
    }
    underrun_count = 0;

    size_t to_read = avail < chunk_size ? avail : chunk_size;
    size_t got = player->audio_ring_pop_(chunk, to_read);
    if (got > 0) {
      player->apply_volume_to_pcm_(chunk, got);

      size_t written = 0;
      int stall_count = 0;
      while (written < got && player->audio_task_running_) {
        size_t w = player->speaker_->play(chunk + written, got - written);
        if (w > 0) {
          written += w;
          stall_count = 0;
        } else {
          stall_count++;
          if (stall_count > 100) break;
          vTaskDelay(pdMS_TO_TICKS(1));
        }
      }
    }
  }

  free(chunk);
  if (jitter_corrections > 0) {
    ESP_LOGI(TAG, "Audio output task exiting (jitter corrections: %u)", jitter_corrections);
  } else {
    ESP_LOGI(TAG, "Audio output task exiting");
  }
  vTaskDelete(nullptr);
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
