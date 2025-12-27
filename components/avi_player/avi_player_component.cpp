#include "avi_player_component.h"

#ifdef USE_ESP_IDF

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace avi_player {

static const char *const TAG = "avi_player";

void AviPlayerComponent::setup() {
  ESP_LOGI(TAG, "Setting up AVI Player");
  ESP_LOGI(TAG, "File: %s, Size: %dx%d, Buffer: %zu bytes",
           file_path_.c_str(), width_, height_, buffer_size_);
  ESP_LOGI(TAG, "Controls: %s, Slider: %s, Preload: %s",
           show_controls_ ? "ON" : "OFF",
           show_slider_ ? "ON" : "OFF",
           preload_to_memory_ ? "ON" : "OFF");

  // Create LVGL image object
  if (parent_ == nullptr) {
    parent_ = lv_scr_act();
  }

  img_ = lv_img_create(parent_);
  if (img_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create LVGL image object");
    return;
  }

  lv_obj_set_size(img_, width_, height_);
  lv_obj_center(img_);

  // Initialize hardware rotation if configured
  if (rotation_ != 0) {
    esp_imgfx_rotate_cfg_t rotate_cfg = {
      .in_res = {
        .width = (uint16_t)width_,
        .height = (uint16_t)height_,
      },
      .in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE,
      .degree = rotation_,
    };

    esp_imgfx_err_t ret = esp_imgfx_rotate_open(&rotate_cfg, &rotate_handle_);
    if (ret != ESP_IMGFX_ERR_OK) {
      ESP_LOGE(TAG, "Failed to initialize rotation: %d", ret);
    } else {
      ESP_LOGI(TAG, "Hardware rotation initialized: %d degrees", rotation_);
    }
  }

  // Create controls if requested
  if (show_controls_ || show_slider_) {
    create_controls();
  }

  // Allocate video buffer for MJPEG frames with 64-byte alignment for JPEG decoder
  size_t buffer_size = width_ * height_ * sizeof(lv_color_t);
  video_buffer_ = (lv_color_t *)heap_caps_aligned_alloc(64, buffer_size,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (video_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate video buffer");
    return;
  }

  // Preload file to memory if requested
  if (preload_to_memory_) {
    if (!load_file_to_memory()) {
      ESP_LOGE(TAG, "Failed to preload file to memory");
      return;
    }
  }

  // Initialize AVI player
  avi_player_config_t config = {
    .buffer_size = buffer_size_,
    .video_cb = video_frame_callback,
    .audio_cb = audio_frame_callback,
    .audio_set_clock_cb = audio_set_clock_callback,
    .avi_play_end_cb = play_end_callback,
    .priority = 5,
    .coreID = 0,
    .user_data = this,
    .stack_size = 4096,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    .stack_in_psram = false,
#endif
  };

  esp_err_t err = avi_player_init(config, &avi_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize AVI player: %s", esp_err_to_name(err));
    return;
  }

  // Initialize JPEG decoder engine
  jpeg_decode_engine_cfg_t engine_cfg = {
    .timeout_ms = 100,
  };

  err = jpeg_new_decoder_engine(&engine_cfg, &jpeg_decoder_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize JPEG decoder: %s", esp_err_to_name(err));
    return;
  }

  // Configure JPEG decoder for RGB565 output
  jpeg_decode_cfg_.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  jpeg_decode_cfg_.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;

  // Initialize LVGL image descriptor
  lvgl_img_dsc_.header.always_zero = 0;
  lvgl_img_dsc_.header.w = width_;
  lvgl_img_dsc_.header.h = height_;
  lvgl_img_dsc_.data_size = width_ * height_ * sizeof(lv_color_t);
  lvgl_img_dsc_.header.cf = LV_IMG_CF_TRUE_COLOR;
  lvgl_img_dsc_.data = (uint8_t *)video_buffer_;

  ESP_LOGI(TAG, "AVI Player initialized successfully");

  if (auto_play_) {
    delay(100);  // Small delay before auto-play
    play();
  }
}

void AviPlayerComponent::loop() {
  // Handle LVGL object resize (thread-safe, only once on first frame)
  if (need_resize_ && img_ != nullptr) {
    // Use rotated dimensions if rotation is enabled, otherwise use actual dimensions
    uint32_t display_width = lvgl_img_dsc_.header.w;
    uint32_t display_height = lvgl_img_dsc_.header.h;
    lv_obj_set_size(img_, display_width, display_height);
    lv_obj_center(img_);
    need_resize_ = false;
    ESP_LOGI(TAG, "Resized LVGL object to %dx%d", display_width, display_height);
  }

  // Handle frame updates (thread-safe, updates LVGL from main loop)
  if (frame_ready_ && img_ != nullptr) {
    lv_img_set_src(img_, &lvgl_img_dsc_);
    lv_obj_invalidate(img_);
    frame_ready_ = false;
  }

  // Update slider position if enabled
  if (show_slider_ && slider_ != nullptr && state_ == PlayerState::PLAYING) {
    update_slider_position();
  }
}

void AviPlayerComponent::play() {
  if (avi_handle_ == nullptr) {
    ESP_LOGE(TAG, "AVI player not initialized");
    return;
  }

  if (state_ == PlayerState::PLAYING) {
    ESP_LOGW(TAG, "Already playing");
    return;
  }

  ESP_LOGI(TAG, "Starting playback: %s", file_path_.c_str());

  // Trigger on_play callbacks (e.g., to stop microphone)
  if (on_play_trigger_ != nullptr) {
    on_play_trigger_->trigger();
  }

  // Reset frame detection flags for new playback
  actual_width_ = 0;
  actual_height_ = 0;

  // Reset rotation buffer to force reallocation (but keep rotation_initialized_ and rotate_handle_)
  if (rotate_buffer_ != nullptr) {
    heap_caps_free(rotate_buffer_);
    rotate_buffer_ = nullptr;
    rotate_buffer_size_ = 0;
  }

  esp_err_t err;
  if (preload_to_memory_ && memory_buffer_ != nullptr) {
    ESP_LOGI(TAG, "Playing from memory (%zu bytes)", memory_buffer_size_);
    err = avi_player_play_from_memory(avi_handle_, memory_buffer_, memory_buffer_size_);
  } else {
    err = avi_player_play_from_file(avi_handle_, file_path_.c_str());
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to play: %s", esp_err_to_name(err));
    return;
  }

  state_ = PlayerState::PLAYING;

  // Update button states
  if (play_btn_ != nullptr) {
    lv_obj_add_state(play_btn_, LV_STATE_DISABLED);
  }
  if (stop_btn_ != nullptr) {
    lv_obj_clear_state(stop_btn_, LV_STATE_DISABLED);
  }
}

void AviPlayerComponent::stop() {
  if (avi_handle_ == nullptr) {
    return;
  }

  if (state_ == PlayerState::STOPPED) {
    return;
  }

  ESP_LOGI(TAG, "Stopping playback");
  avi_player_play_stop(avi_handle_);
  state_ = PlayerState::STOPPED;

  // Trigger on_stop callbacks (e.g., to restart microphone)
  if (on_stop_trigger_ != nullptr) {
    on_stop_trigger_->trigger();
  }

  // Reset frame dimensions for next playback
  actual_width_ = 0;
  actual_height_ = 0;

  // Reset rotation buffer for next playback
  if (rotate_buffer_ != nullptr) {
    heap_caps_free(rotate_buffer_);
    rotate_buffer_ = nullptr;
    rotate_buffer_size_ = 0;
  }

  // Update button states
  if (play_btn_ != nullptr) {
    lv_obj_clear_state(play_btn_, LV_STATE_DISABLED);
  }
  if (stop_btn_ != nullptr) {
    lv_obj_add_state(stop_btn_, LV_STATE_DISABLED);
  }
}

void AviPlayerComponent::video_frame_callback(frame_data_t *data, void *arg) {
  AviPlayerComponent *player = static_cast<AviPlayerComponent *>(arg);
  if (player != nullptr) {
    player->render_frame(data);
  }
}

void AviPlayerComponent::audio_frame_callback(frame_data_t *data, void *arg) {
  AviPlayerComponent *player = static_cast<AviPlayerComponent *>(arg);
  if (player == nullptr || player->speaker_ == nullptr || data == nullptr || data->data == nullptr) {
    return;
  }

  // Don't write audio if player is not in PLAYING state
  if (player->state_ != PlayerState::PLAYING) {
    return;
  }

  ESP_LOGV(TAG, "Audio frame: %d bytes", data->data_bytes);

  // Write PCM audio data to speaker
  size_t bytes_written = player->speaker_->play(data->data, data->data_bytes);

  if (bytes_written < data->data_bytes) {
    ESP_LOGW(TAG, "Audio buffer full, dropped %zu bytes", data->data_bytes - bytes_written);
  }
}

void AviPlayerComponent::audio_set_clock_callback(uint32_t rate, uint32_t bits_cfg, uint32_t ch, void *arg) {
  AviPlayerComponent *player = static_cast<AviPlayerComponent *>(arg);
  if (player != nullptr) {
    player->audio_sample_rate_ = rate;
    player->audio_bits_per_sample_ = bits_cfg;
    player->audio_channels_ = ch;
    ESP_LOGI(TAG, "Audio config: rate=%u, bits=%u, channels=%u", rate, bits_cfg, ch);

    if (player->speaker_ != nullptr) {
      ESP_LOGI(TAG, "Audio output enabled via speaker");
    } else {
      ESP_LOGI(TAG, "No speaker configured - audio will not be played");
    }
  }
}

void AviPlayerComponent::play_end_callback(void *arg) {
  AviPlayerComponent *player = static_cast<AviPlayerComponent *>(arg);
  if (player != nullptr) {
    ESP_LOGI(TAG, "Playback ended");
    player->state_ = PlayerState::STOPPED;

    // Reset dimension detection for next playback
    player->actual_width_ = 0;
    player->actual_height_ = 0;

    if (player->loop_) {
      ESP_LOGI(TAG, "Looping playback");
      delay(100);
      player->play();
    } else {
      // Trigger on_stop callbacks when video ends naturally
      if (player->on_stop_trigger_ != nullptr) {
        player->on_stop_trigger_->trigger();
      }

      // Update button states to allow replay
      if (player->play_btn_ != nullptr) {
        lv_obj_clear_state(player->play_btn_, LV_STATE_DISABLED);
      }
      if (player->stop_btn_ != nullptr) {
        lv_obj_add_state(player->stop_btn_, LV_STATE_DISABLED);
      }
    }
  }
}

void AviPlayerComponent::render_frame(frame_data_t *data) {
  if (data == nullptr || data->data == nullptr || img_ == nullptr || jpeg_decoder_ == nullptr) {
    return;
  }

  if (data->video_info.frame_format != FORMAT_MJEPG) {
    ESP_LOGW(TAG, "Unsupported video format: %d", data->video_info.frame_format);
    return;
  }

  ESP_LOGV(TAG, "Decoding JPEG frame: %d bytes, size: %dx%d",
           data->data_bytes, data->video_info.width, data->video_info.height);

  // Parse JPEG header to get actual dimensions (first frame only)
  if (actual_width_ == 0 || actual_height_ == 0) {
    esp_err_t err = jpeg_decoder_get_info(data->data, data->data_bytes, &frame_info_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to parse JPEG header: %s", esp_err_to_name(err));
      return;
    }

    // JPEG decoder aligns dimensions to 16 bytes
    actual_width_ = (frame_info_.width + 15) & ~15;
    actual_height_ = (frame_info_.height + 15) & ~15;
    video_buffer_size_ = actual_width_ * actual_height_ * sizeof(lv_color_t);

    ESP_LOGI(TAG, "JPEG actual size: %dx%d (original: %dx%d), buffer: %u bytes",
             actual_width_, actual_height_, frame_info_.width, frame_info_.height, video_buffer_size_);

    // Reinitialize rotation handle with actual video dimensions if rotation is enabled
    // Only do this once - not on every video restart
    if (rotation_ != 0 && rotate_handle_ != nullptr && !rotation_initialized_) {
      // Close old rotation handle that was initialized with config dimensions
      esp_imgfx_rotate_close(rotate_handle_);
      rotate_handle_ = nullptr;

      // Create new rotation handle with actual video dimensions
      esp_imgfx_rotate_cfg_t rotate_cfg = {
        .in_res = {
          .width = (uint16_t)actual_width_,
          .height = (uint16_t)actual_height_,
        },
        .in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE,
        .degree = rotation_,
      };

      esp_imgfx_err_t ret = esp_imgfx_rotate_open(&rotate_cfg, &rotate_handle_);
      if (ret != ESP_IMGFX_ERR_OK) {
        ESP_LOGE(TAG, "Failed to reinitialize rotation with actual dimensions: %d", ret);
        rotate_handle_ = nullptr;
      } else {
        ESP_LOGI(TAG, "Rotation reinitialized with actual dimensions: %dx%d -> %d degrees",
                 actual_width_, actual_height_, rotation_);
        rotation_initialized_ = true;  // Mark as initialized
      }
    }

    // Reallocate buffer if needed
    if (video_buffer_size_ > width_ * height_ * sizeof(lv_color_t)) {
      if (video_buffer_ != nullptr) {
        heap_caps_free(video_buffer_);
      }
      video_buffer_ = (lv_color_t *)heap_caps_aligned_alloc(64, video_buffer_size_,
                                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (video_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to reallocate video buffer");
        return;
      }
      // Update LVGL image descriptor with actual dimensions (will be updated again if rotation is enabled)
      lvgl_img_dsc_.header.w = actual_width_;
      lvgl_img_dsc_.header.h = actual_height_;
      lvgl_img_dsc_.data = (uint8_t *)video_buffer_;

      // Don't set need_resize_ yet if rotation is enabled - wait until after rotation is applied
      if (rotation_ == 0) {
        need_resize_ = true;
      }
    }
  }

  // Decode JPEG to RGB565
  uint32_t out_size = 0;

  esp_err_t err = jpeg_decoder_process(jpeg_decoder_,
                                        &jpeg_decode_cfg_,
                                        data->data,
                                        data->data_bytes,
                                        (uint8_t *)video_buffer_,
                                        video_buffer_size_,
                                        &out_size);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "JPEG decode failed: %s", esp_err_to_name(err));
    return;
  }

  ESP_LOGV(TAG, "JPEG decoded: output %u bytes", out_size);

  // Apply hardware rotation if configured
  if (rotation_ != 0 && rotate_handle_ != nullptr) {
    // Allocate rotation buffer on first use
    if (rotate_buffer_ == nullptr) {
      esp_imgfx_resolution_t rotated_res;
      esp_imgfx_rotate_get_rotated_resolution(rotate_handle_, &rotated_res);
      rotate_buffer_size_ = rotated_res.width * rotated_res.height * sizeof(lv_color_t);
      rotate_buffer_ = (lv_color_t *)heap_caps_aligned_alloc(64, rotate_buffer_size_,
                                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (rotate_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate rotation buffer");
        frame_ready_ = true;
        return;
      }
      ESP_LOGI(TAG, "Allocated rotation buffer: %u bytes, rotated size: %dx%d",
               rotate_buffer_size_, rotated_res.width, rotated_res.height);

      // First frame with rotation - will need to resize LVGL object after rotation is applied
      need_resize_ = true;
    }

    // Rotate the decoded frame
    esp_imgfx_data_t in_image = {
      .data = (uint8_t *)video_buffer_,
      .data_len = video_buffer_size_,
    };
    esp_imgfx_data_t out_image = {
      .data = (uint8_t *)rotate_buffer_,
      .data_len = rotate_buffer_size_,
    };

    esp_imgfx_err_t ret = esp_imgfx_rotate_process(rotate_handle_, &in_image, &out_image);
    if (ret != ESP_IMGFX_ERR_OK) {
      ESP_LOGE(TAG, "Rotation failed: %d", ret);
      frame_ready_ = true;
      return;
    }

    // Update LVGL descriptor to use rotated buffer
    esp_imgfx_resolution_t rotated_res;
    esp_imgfx_rotate_get_rotated_resolution(rotate_handle_, &rotated_res);
    lvgl_img_dsc_.header.w = rotated_res.width;
    lvgl_img_dsc_.header.h = rotated_res.height;
    lvgl_img_dsc_.data = (uint8_t *)rotate_buffer_;
  }

  // Set flag to update LVGL in main loop (thread-safe)
  frame_ready_ = true;
}

// Static callbacks for LVGL button events
static void play_btn_event_cb(lv_event_t *e) {
  AviPlayerComponent *player = static_cast<AviPlayerComponent *>(lv_event_get_user_data(e));
  if (player != nullptr && lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ESP_LOGI(TAG, "Play button clicked");
    player->play();
  }
}

static void stop_btn_event_cb(lv_event_t *e) {
  AviPlayerComponent *player = static_cast<AviPlayerComponent *>(lv_event_get_user_data(e));
  if (player != nullptr && lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ESP_LOGI(TAG, "Stop button clicked");
    player->stop();
  }
}

void AviPlayerComponent::create_controls() {
  if (parent_ == nullptr) {
    return;
  }

  ESP_LOGI(TAG, "Creating playback controls");

  // Create controls panel
  if (show_controls_) {
    controls_panel_ = lv_obj_create(parent_);
    lv_obj_set_size(controls_panel_, width_, 50);
    lv_obj_align(controls_panel_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(controls_panel_, LV_OPA_80, 0);

    // Create play button
    play_btn_ = lv_btn_create(controls_panel_);
    lv_obj_set_size(play_btn_, 60, 40);
    lv_obj_align(play_btn_, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *play_label = lv_label_create(play_btn_);
    lv_label_set_text(play_label, LV_SYMBOL_PLAY);
    lv_obj_center(play_label);

    // Add event callback for play button
    lv_obj_add_event_cb(play_btn_, play_btn_event_cb, LV_EVENT_CLICKED, this);

    // Create stop button
    stop_btn_ = lv_btn_create(controls_panel_);
    lv_obj_set_size(stop_btn_, 60, 40);
    lv_obj_align(stop_btn_, LV_ALIGN_LEFT_MID, 80, 0);
    lv_obj_t *stop_label = lv_label_create(stop_btn_);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP);
    lv_obj_center(stop_label);
    lv_obj_add_state(stop_btn_, LV_STATE_DISABLED);

    // Add event callback for stop button
    lv_obj_add_event_cb(stop_btn_, stop_btn_event_cb, LV_EVENT_CLICKED, this);
  }

  // Create slider
  if (show_slider_) {
    slider_ = lv_slider_create(parent_);
    lv_obj_set_width(slider_, width_ - 20);
    lv_obj_align(slider_, LV_ALIGN_BOTTOM_MID, 0, show_controls_ ? -60 : -10);
    lv_slider_set_range(slider_, 0, 100);
    lv_slider_set_value(slider_, 0, LV_ANIM_OFF);
  }
}

void AviPlayerComponent::update_slider_position() {
  if (slider_ == nullptr) {
    return;
  }

  // TODO: Get actual playback position from avi_player
  // For now, this is a placeholder
  // You'll need to add position tracking to avi_player.c
}

bool AviPlayerComponent::load_file_to_memory() {
  ESP_LOGI(TAG, "Loading file to memory: %s", file_path_.c_str());

  FILE *file = fopen(file_path_.c_str(), "rb");
  if (file == nullptr) {
    ESP_LOGE(TAG, "Failed to open file: %s", file_path_.c_str());
    return false;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  memory_buffer_size_ = ftell(file);
  fseek(file, 0, SEEK_SET);

  ESP_LOGI(TAG, "File size: %zu bytes", memory_buffer_size_);

  // Allocate memory buffer in PSRAM
  memory_buffer_ = (uint8_t *)heap_caps_malloc(memory_buffer_size_,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate memory buffer (%zu bytes)", memory_buffer_size_);
    fclose(file);
    return false;
  }

  // Read file into memory
  size_t bytes_read = fread(memory_buffer_, 1, memory_buffer_size_, file);
  fclose(file);

  if (bytes_read != memory_buffer_size_) {
    ESP_LOGE(TAG, "Failed to read file completely (read %zu of %zu bytes)",
             bytes_read, memory_buffer_size_);
    heap_caps_free(memory_buffer_);
    memory_buffer_ = nullptr;
    memory_buffer_size_ = 0;
    return false;
  }

  ESP_LOGI(TAG, "File preloaded to memory successfully");
  return true;
}

}  // namespace avi_player
}  // namespace esphome

#endif  // USE_ESP_IDF
