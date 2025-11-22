#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF

// LVGL
#include "lvgl.h"

// Tous les headers C doivent être dans extern "C"
extern "C" {
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "linux/videodev2.h"
}

#endif  // USE_ESP_IDF

namespace esphome {
namespace video_player {

class VideoPlayer : public Component {
 public:
  // --------- Configuration depuis YAML -------------
  void set_source_path(const std::string &path) { source_path_ = path; }
  void set_device_path(const std::string &path) { device_path_ = path; }

  void set_width(int w) { width_ = w; }
  void set_height(int h) { height_ = h; }

  void set_autoplay(bool b) { autoplay_ = b; }
  void set_loop(bool b) { loop_ = b; }

  // --------- ESPHome ----------
  void setup() override;
  void loop() override;

  // --------- Contrôle vidéo depuis automations ---------
  void play();
  void pause();
  void stop();

  float get_setup_priority() const override {
    return setup_priority::LATE;  // comme le LVGL panel
  }

 protected:

  // ============================
  // H.264 DECODER ESP_VIDEO M2M
  // ============================

  static const int CAPTURE_BUFFER_COUNT = 3;

  struct CaptureBuffer {
    void *addr = nullptr;
    size_t length = 0;
  };

  // --- init, deinit du pipeline M2M ---
  esp_err_t init_decoder_();
  void deinit_decoder_();

  // --- lecture du fichier H.264 ---
  size_t read_h264_chunk_(uint8_t *buf, size_t max_size);

  // --- decode une frame ---
  bool feed_and_decode_one_frame_();

  // --- update LVGL ---
  void update_lvgl_frame_(const uint8_t *rgb565, size_t len);

  // =================================
  // Membres
  // =================================

  // Chemin du fichier vidéo (ex: "/sdcard/video.h264")
  std::string source_path_;

  // Chemin du device H.264 (ex: "/dev/video30")
  std::string device_path_;

  // Résolution de sortie
  int width_{0};
  int height_{0};

  // Contrôle
  bool autoplay_{false};
  bool loop_{false};
  bool playing_{false};
  bool eof_{false};

#ifdef USE_ESP_IDF

  // Handle LVGL
  lv_obj_t *img_obj_{nullptr};
  lv_img_dsc_t img_dsc_{};

  // Buffer RGB565
  std::vector<uint8_t> frame_buffer_;

  // H.264 M2M file descriptor
  int fd_h264_{-1};

  // Buffers de capture (MMAP)
  CaptureBuffer capture_bufs_[CAPTURE_BUFFER_COUNT];

  // Fichier H.264
  FILE *file_{nullptr};

  bool decoder_ready_{false};

#endif  // USE_ESP_IDF
};

}  // namespace video_player
}  // namespace esphome

