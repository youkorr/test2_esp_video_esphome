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

  // Allocate video buffer for MJPEG frames
  video_buffer_ = (lv_color_t *)heap_caps_malloc(width_ * height_ * sizeof(lv_color_t),
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (video_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate video buffer");
    return;
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

  ESP_LOGI(TAG, "AVI Player initialized successfully");

  if (auto_play_) {
    delay(100);  // Small delay before auto-play
    play();
  }
}

void AviPlayerComponent::loop() {
  // Nothing to do in loop, callbacks handle playback
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

  esp_err_t err = avi_player_play_from_file(avi_handle_, file_path_.c_str());
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to play file: %s", esp_err_to_name(err));
    return;
  }

  state_ = PlayerState::PLAYING;
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
}

void AviPlayerComponent::video_frame_callback(frame_data_t *data, void *arg) {
  AviPlayerComponent *player = static_cast<AviPlayerComponent *>(arg);
  if (player != nullptr) {
    player->render_frame(data);
  }
}

void AviPlayerComponent::audio_frame_callback(frame_data_t *data, void *arg) {
  // Audio playback not implemented yet
  ESP_LOGV(TAG, "Audio frame: %d bytes", data->data_bytes);
}

void AviPlayerComponent::audio_set_clock_callback(uint32_t rate, uint32_t bits_cfg, uint32_t ch, void *arg) {
  ESP_LOGI(TAG, "Audio config: rate=%u, bits=%u, channels=%u", rate, bits_cfg, ch);
}

void AviPlayerComponent::play_end_callback(void *arg) {
  AviPlayerComponent *player = static_cast<AviPlayerComponent *>(arg);
  if (player != nullptr) {
    ESP_LOGI(TAG, "Playback ended");
    player->state_ = PlayerState::STOPPED;

    if (player->loop_) {
      ESP_LOGI(TAG, "Looping playback");
      delay(100);
      player->play();
    }
  }
}

void AviPlayerComponent::render_frame(frame_data_t *data) {
  if (data == nullptr || data->data == nullptr || img_ == nullptr) {
    return;
  }

  // For MJPEG format, we need to decode the JPEG
  // For now, we'll use a simple approach - you may need to add JPEG decoding
  ESP_LOGV(TAG, "Video frame: %d bytes, format: %d, size: %dx%d",
           data->data_bytes, data->video_info.frame_format,
           data->video_info.width, data->video_info.height);

  // TODO: Add JPEG decoding here
  // For now, just log the frame
  // You can use ESP32's JPEG hardware decoder or a software decoder
}

}  // namespace avi_player
}  // namespace esphome

#endif  // USE_ESP_IDF
