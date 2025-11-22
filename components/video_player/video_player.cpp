#include "video_player.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF

// Tous les headers C doivent être dans extern "C" pour C++
extern "C" {
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "linux/videodev2.h"
}

namespace esphome {
namespace video_player {

static const char *const TAG = "video_player";

// --------------------------------------------------
// Setup / Loop
// --------------------------------------------------

void VideoPlayer::setup() {
  ESP_LOGI(TAG, "Setting up H.264 Video Player (source='%s', dev='%s')",
           this->source_path_.c_str(), this->device_path_.c_str());

  if (this->width_ <= 0 || this->height_ <= 0) {
    ESP_LOGE(TAG, "Invalid resolution %dx%d - please set width/height in YAML",
             this->width_, this->height_);
    this->mark_failed();
    return;
  }

  // Créer un widget LVGL (image) pour afficher la vidéo
  lv_obj_t *scr = lv_scr_act();
  this->img_obj_ = lv_img_create(scr);

  // Préparer le buffer de frame (RGB565) pour LVGL
  size_t fb_size = static_cast<size_t>(this->width_) * this->height_ * 2;  // RGB565 (2 bytes/pixel)
  this->frame_buffer_.assign(fb_size, 0);

  memset(&this->img_dsc_, 0, sizeof(this->img_dsc_));
  this->img_dsc_.header.always_zero = 0;
  this->img_dsc_.header.w = this->width_;
  this->img_dsc_.header.h = this->height_;
  this->img_dsc_.header.cf = LV_IMG_CF_TRUE_COLOR;  // RGB565 supporté
  this->img_dsc_.data = this->frame_buffer_.data();
  this->img_dsc_.data_size = this->frame_buffer_.size();

  lv_img_set_src(this->img_obj_, &this->img_dsc_);
  lv_obj_center(this->img_obj_);

  // Ouvrir le fichier H.264 (carte SD, SPIFFS, etc.)
  if (!this->source_path_.empty()) {
    this->file_ = fopen(this->source_path_.c_str(), "rb");
    if (this->file_ == nullptr) {
      ESP_LOGE(TAG, "Failed to open video source file: %s (errno=%d)",
               this->source_path_.c_str(), errno);
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "Video source opened: %s", this->source_path_.c_str());
  } else {
    ESP_LOGW(TAG, "No video source path set");
    this->mark_failed();
    return;
  }

  // Initialiser le décodeur H.264 M2M (/dev/videoXX)
  if (this->init_decoder_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init H.264 decoder");
    this->mark_failed();
    return;
  }

  if (this->autoplay_) {
    this->play();
  }
}

void VideoPlayer::loop() {
  if (!this->decoder_ready_ || !this->playing_) {
    return;
  }

  // Décoder une frame à chaque boucle
  if (!this->feed_and_decode_one_frame_()) {
    if (this->eof_) {
      ESP_LOGI(TAG, "End of video reached");
      if (this->loop_) {
        ESP_LOGI(TAG, "Loop enabled, restarting file");
        if (this->file_) {
          fseek(this->file_, 0, SEEK_SET);
          this->eof_ = false;
        }
      } else {
        this->stop();
      }
    }
  }
}

// --------------------------------------------------
// Control API
// --------------------------------------------------

void VideoPlayer::play() {
  if (!this->decoder_ready_) {
    ESP_LOGW(TAG, "Decoder not ready, cannot play");
    return;
  }
  if (this->file_ == nullptr) {
    ESP_LOGW(TAG, "No video file, cannot play");
    return;
  }
  if (this->playing_) return;

  ESP_LOGI(TAG, "Video playback started");
  this->playing_ = true;
}

void VideoPlayer::pause() {
  if (!this->playing_) return;
  ESP_LOGI(TAG, "Video playback paused");
  this->playing_ = false;
}

void VideoPlayer::stop() {
  if (!this->playing_) return;
  ESP_LOGI(TAG, "Video playback stopped");
  this->playing_ = false;
}

// --------------------------------------------------
// Decoder init / deinit (H.264 M2M via V4L2)
// --------------------------------------------------

esp_err_t VideoPlayer::init_decoder_() {
  if (this->decoder_ready_) return ESP_OK;

  if (this->device_path_.empty()) {
    ESP_LOGE(TAG, "No H.264 device path set (device_path_)");
    return ESP_FAIL;
  }

  // Ouvrir le device H.264 M2M (/dev/videoXX)
  this->fd_h264_ = ::open(this->device_path_.c_str(), O_RDWR /* | O_NONBLOCK */);
  if (this->fd_h264_ < 0) {
    ESP_LOGE(TAG, "Failed to open H.264 M2M device '%s': errno=%d",
             this->device_path_.c_str(), errno);
    return ESP_FAIL;
  }

  struct v4l2_capability cap {};
  if (ioctl(this->fd_h264_, VIDIOC_QUERYCAP, &cap) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed on '%s': errno=%d",
             this->device_path_.c_str(), errno);
    ::close(this->fd_h264_);
    this->fd_h264_ = -1;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "H.264 device opened: driver=%s card=%s caps=0x%X devcaps=0x%X",
           cap.driver, cap.card, cap.capabilities, cap.device_caps);

  // --- OUTPUT: H.264 stream ---
  {
    struct v4l2_format fmt_out {};
    fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt_out.fmt.pix.width = this->width_;
    fmt_out.fmt.pix.height = this->height_;
    fmt_out.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    fmt_out.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(this->fd_h264_, VIDIOC_S_FMT, &fmt_out) < 0) {
      ESP_LOGE(TAG, "VIDIOC_S_FMT(OUTPUT H264) failed: errno=%d", errno);
      ::close(this->fd_h264_);
      this->fd_h264_ = -1;
      return ESP_FAIL;
    }

    ESP_LOGI(TAG, "H.264 OUTPUT format set: %ux%u H264",
             fmt_out.fmt.pix.width, fmt_out.fmt.pix.height);
  }

  // --- CAPTURE: RGB565 (décodé) ---
  {
    struct v4l2_format fmt_cap {};
    fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt_cap.fmt.pix.width = this->width_;
    fmt_cap.fmt.pix.height = this->height_;
    fmt_cap.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    fmt_cap.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(this->fd_h264_, VIDIOC_S_FMT, &fmt_cap) < 0) {
      ESP_LOGE(TAG, "VIDIOC_S_FMT(CAPTURE RGB565) failed: errno=%d", errno);
      ::close(this->fd_h264_);
      this->fd_h264_ = -1;
      return ESP_FAIL;
    }

    ESP_LOGI(TAG, "H.264 CAPTURE format set: %ux%u RGB565",
             fmt_cap.fmt.pix.width, fmt_cap.fmt.pix.height);
  }

  // --- REQBUFS CAPTURE (MMAP) ---
  {
    struct v4l2_requestbuffers req_cap {};
    req_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req_cap.count = CAPTURE_BUFFER_COUNT;
    req_cap.memory = V4L2_MEMORY_MMAP;

    if (ioctl(this->fd_h264_, VIDIOC_REQBUFS, &req_cap) < 0) {
      ESP_LOGE(TAG, "VIDIOC_REQBUFS(CAPTURE) failed: errno=%d", errno);
      ::close(this->fd_h264_);
      this->fd_h264_ = -1;
      return ESP_FAIL;
    }

    if (req_cap.count < 1) {
      ESP_LOGE(TAG, "No CAPTURE buffer allocated");
      ::close(this->fd_h264_);
      this->fd_h264_ = -1;
      return ESP_FAIL;
    }

    for (unsigned i = 0; i < req_cap.count && i < CAPTURE_BUFFER_COUNT; i++) {
      struct v4l2_buffer buf {};
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;

      if (ioctl(this->fd_h264_, VIDIOC_QUERYBUF, &buf) < 0) {
        ESP_LOGE(TAG, "VIDIOC_QUERYBUF(CAPTURE idx=%u) failed: errno=%d", i, errno);
        ::close(this->fd_h264_);
        this->fd_h264_ = -1;
        return ESP_FAIL;
      }

      void *addr = mmap(nullptr, buf.length,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED, this->fd_h264_, buf.m.offset);
      if (addr == MAP_FAILED) {
        ESP_LOGE(TAG, "mmap(CAPTURE idx=%u) failed: errno=%d", i, errno);
        ::close(this->fd_h264_);
        this->fd_h264_ = -1;
        return ESP_FAIL;
      }

      this->capture_bufs_[i].addr = addr;
      this->capture_bufs_[i].length = buf.length;

      if (ioctl(this->fd_h264_, VIDIOC_QBUF, &buf) < 0) {
        ESP_LOGE(TAG, "VIDIOC_QBUF(CAPTURE idx=%u) failed: errno=%d", i, errno);
        ::close(this->fd_h264_);
        this->fd_h264_ = -1;
        return ESP_FAIL;
      }
    }

    ESP_LOGI(TAG, "Allocated %u capture buffers", req_cap.count);
  }

  // --- REQBUFS OUTPUT (USERPTR) ---
  {
    struct v4l2_requestbuffers req_out {};
    req_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req_out.count = 2;  // 2 buffers logiques, en USERPTR
    req_out.memory = V4L2_MEMORY_USERPTR;

    if (ioctl(this->fd_h264_, VIDIOC_REQBUFS, &req_out) < 0) {
      ESP_LOGE(TAG, "VIDIOC_REQBUFS(OUTPUT USERPTR) failed: errno=%d", errno);
      ::close(this->fd_h264_);
      this->fd_h264_ = -1;
      return ESP_FAIL;
    }
  }

  // --- STREAMON OUTPUT / CAPTURE ---
  {
    enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(this->fd_h264_, VIDIOC_STREAMON, &type) < 0) {
      ESP_LOGE(TAG, "VIDIOC_STREAMON(CAPTURE) failed: errno=%d", errno);
      ::close(this->fd_h264_);
      this->fd_h264_ = -1;
      return ESP_FAIL;
    }

    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(this->fd_h264_, VIDIOC_STREAMON, &type) < 0) {
      ESP_LOGE(TAG, "VIDIOC_STREAMON(OUTPUT) failed: errno=%d", errno);
      ::close(this->fd_h264_);
      this->fd_h264_ = -1;
      return ESP_FAIL;
    }
  }

  this->decoder_ready_ = true;
  this->eof_ = false;
  ESP_LOGI(TAG, "H.264 M2M decoder initialized OK");
  return ESP_OK;
}

void VideoPlayer::deinit_decoder_() {
  if (this->fd_h264_ >= 0) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(this->fd_h264_, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(this->fd_h264_, VIDIOC_STREAMOFF, &type);

    for (int i = 0; i < CAPTURE_BUFFER_COUNT; i++) {
      if (this->capture_bufs_[i].addr != nullptr &&
          this->capture_bufs_[i].length > 0) {
        munmap(this->capture_bufs_[i].addr, this->capture_bufs_[i].length);
        this->capture_bufs_[i].addr = nullptr;
        this->capture_bufs_[i].length = 0;
      }
    }

    ::close(this->fd_h264_);
    this->fd_h264_ = -1;
  }

  if (this->file_ != nullptr) {
    fclose(this->file_);
    this->file_ = nullptr;
  }

  this->decoder_ready_ = false;
}

// --------------------------------------------------
// Lecture H.264 (fichier) + decode 1 frame
// --------------------------------------------------

size_t VideoPlayer::read_h264_chunk_(uint8_t *buf, size_t max_size) {
  if (this->file_ == nullptr || this->eof_)
    return 0;

  size_t n = fread(buf, 1, max_size, this->file_);
  if (n == 0) {
    this->eof_ = true;
  }
  return n;
}

bool VideoPlayer::feed_and_decode_one_frame_() {
  if (!this->decoder_ready_ || this->fd_h264_ < 0 || this->file_ == nullptr)
    return false;

  // Lire un chunk H.264 brut (Annex-B)
  static const size_t CHUNK_SIZE = 4096;
  uint8_t in_buf[CHUNK_SIZE];
  size_t n = this->read_h264_chunk_(in_buf, CHUNK_SIZE);
  if (n == 0) {
    // EOF ou pas de données
    return false;
  }

  // QBUF OUTPUT (USERPTR)
  struct v4l2_buffer buf_out {};
  buf_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  buf_out.memory = V4L2_MEMORY_USERPTR;
  buf_out.m.userptr = reinterpret_cast<unsigned long>(in_buf);
  buf_out.length = n;

  if (ioctl(this->fd_h264_, VIDIOC_QBUF, &buf_out) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QBUF(OUTPUT) failed: errno=%d", errno);
    return false;
  }

  // DQBUF CAPTURE (décodé)
  struct v4l2_buffer buf_cap {};
  buf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf_cap.memory = V4L2_MEMORY_MMAP;

  if (ioctl(this->fd_h264_, VIDIOC_DQBUF, &buf_cap) < 0) {
    if (errno == EAGAIN) {
      // Rien de prêt encore, pas grave
      return true;
    }
    ESP_LOGE(TAG, "VIDIOC_DQBUF(CAPTURE) failed: errno=%d", errno);
    return false;
  }

  if (buf_cap.index >= CAPTURE_BUFFER_COUNT ||
      this->capture_bufs_[buf_cap.index].addr == nullptr ||
      buf_cap.bytesused == 0) {
    ESP_LOGE(TAG, "Invalid CAPTURE buffer: idx=%u used=%u",
             buf_cap.index, buf_cap.bytesused);
    return false;
  }

  // Copie la frame décodée (RGB565) dans le buffer LVGL
  size_t copy_size = buf_cap.bytesused;
  if (copy_size > this->frame_buffer_.size())
    copy_size = this->frame_buffer_.size();

  uint8_t *src = static_cast<uint8_t *>(this->capture_bufs_[buf_cap.index].addr);
  this->update_lvgl_frame_(src, copy_size);

  // Réqueue CAPTURE
  if (ioctl(this->fd_h264_, VIDIOC_QBUF, &buf_cap) < 0) {
    ESP_LOGW(TAG, "VIDIOC_QBUF(CAPTURE requeue) failed: errno=%d", errno);
  }

  // DQBUF OUTPUT (libérer)
  struct v4l2_buffer buf_out_done {};
  buf_out_done.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  buf_out_done.memory = V4L2_MEMORY_USERPTR;
  if (ioctl(this->fd_h264_, VIDIOC_DQBUF, &buf_out_done) < 0) {
    if (errno != EAGAIN) {
      ESP_LOGW(TAG, "VIDIOC_DQBUF(OUTPUT) failed (non-fatal): errno=%d", errno);
    }
  }

  return true;
}

// --------------------------------------------------
// LVGL : mise à jour de l'image
// --------------------------------------------------

void VideoPlayer::update_lvgl_frame_(const uint8_t *rgb565, size_t len) {
  if (this->img_obj_ == nullptr || rgb565 == nullptr)
    return;
  if (len > this->frame_buffer_.size())
    len = this->frame_buffer_.size();

  memcpy(this->frame_buffer_.data(), rgb565, len);

  // Actualiser descripteur LVGL
  this->img_dsc_.data = this->frame_buffer_.data();
  this->img_dsc_.data_size = this->frame_buffer_.size();

  lv_img_set_src(this->img_obj_, &this->img_dsc_);
  lv_obj_invalidate(this->img_obj_);
}

}  // namespace video_player
}  // namespace esphome

#else  // !USE_ESP_IDF

namespace esphome {
namespace video_player {

void VideoPlayer::setup() {
  ESP_LOGE("video_player", "VideoPlayer requires ESP-IDF");
  this->mark_failed();
}

void VideoPlayer::loop() {}

void VideoPlayer::play() {}
void VideoPlayer::pause() {}
void VideoPlayer::stop() {}

}  // namespace video_player
}  // namespace esphome

#endif  // USE_ESP_IDF
