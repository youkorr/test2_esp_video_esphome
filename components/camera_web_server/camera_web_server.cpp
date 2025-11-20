#include "camera_web_server.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#endif  // USE_ESP_IDF

namespace esphome {
namespace camera_web_server {

static const char *const TAG = "camera_web_server";

#ifdef USE_ESP_IDF

// ------------------------
// Config MJPEG / JPEG M2M
// ------------------------

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// Device JPEG M2M fourni par esp-video (esp_video_jpeg_device)
static const char *JPEG_DEV_PATH = "/dev/video10";     // adapter si différent
static const uint32_t JPEG_CAPTURE_BUF_COUNT = 3;      // nombre de buffers mmap côté JPEG

struct JpegCaptureBuf {
  void *start = nullptr;
  size_t length = 0;
};

static JpegCaptureBuf s_cap_bufs[JPEG_CAPTURE_BUF_COUNT];
static int s_jpeg_fd = -1;

// FPS mesurés côté stream
static float s_stream_fps = 0.0f;
static int s_stream_fps_int = 0;
static int s_target_fps = 30;  // valeur par défaut si on ne sait pas encore

// Petite structure pour retourner une frame JPEG encodée
struct EncodedFrame {
  uint8_t *ptr = nullptr;
  size_t size = 0;
  uint32_t index = 0;  // index du buffer capture dans s_cap_bufs
};

// ------------------------
// Helpers V4L2 / esp-video
// ------------------------

static esp_err_t init_jpeg_m2m_device(int width, int height) {
  if (s_jpeg_fd >= 0) {
    ESP_LOGW(TAG, "JPEG M2M device already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Opening JPEG M2M device: %s", JPEG_DEV_PATH);

  // On suppose que esp-video a déjà créé le device JPEG (esp_video_create_jpeg_video_device)
  s_jpeg_fd = ::open(JPEG_DEV_PATH, O_RDWR);
  if (s_jpeg_fd < 0) {
    ESP_LOGE(TAG, "Failed to open %s: errno=%d", JPEG_DEV_PATH, errno);
    return ESP_FAIL;
  }

  // Vérifier les capacités
  struct v4l2_capability cap;
  memset(&cap, 0, sizeof(cap));
  if (ioctl(s_jpeg_fd, VIDIOC_QUERYCAP, &cap) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed: errno=%d", errno);
    ::close(s_jpeg_fd);
    s_jpeg_fd = -1;
    return ESP_FAIL;
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_M2M)) {
    ESP_LOGE(TAG, "Device %s is not a mem2mem device", JPEG_DEV_PATH);
    ::close(s_jpeg_fd);
    s_jpeg_fd = -1;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "JPEG M2M device opened, driver=%s, card=%s",
           (char *) cap.driver, (char *) cap.card);

  // ---------
  // Format OUTPUT : RGB565 (entrée encoder)
  // ---------
  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (ioctl(s_jpeg_fd, VIDIOC_S_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_S_FMT (OUTPUT RGB565) failed: errno=%d", errno);
    ::close(s_jpeg_fd);
    s_jpeg_fd = -1;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "JPEG OUTPUT format set: %ux%u RGB565",
           fmt.fmt.pix.width, fmt.fmt.pix.height);

  // ---------
  // Format CAPTURE : JPEG
  // ---------
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (ioctl(s_jpeg_fd, VIDIOC_S_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "VIDIOC_S_FMT (CAPTURE JPEG) failed: errno=%d", errno);
    ::close(s_jpeg_fd);
    s_jpeg_fd = -1;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "JPEG CAPTURE format set: %ux%u JPEG",
           fmt.fmt.pix.width, fmt.fmt.pix.height);

  // ---------
  // Buffers CAPTURE en MMAP
  // ---------
  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.count = JPEG_CAPTURE_BUF_COUNT;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(s_jpeg_fd, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS (CAPTURE) failed: errno=%d", errno);
    ::close(s_jpeg_fd);
    s_jpeg_fd = -1;
    return ESP_FAIL;
  }

  if (req.count < 1) {
    ESP_LOGE(TAG, "Not enough CAPTURE buffers allocated");
    ::close(s_jpeg_fd);
    s_jpeg_fd = -1;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Allocated %u CAPTURE buffers", req.count);

  for (uint32_t i = 0; i < req.count && i < JPEG_CAPTURE_BUF_COUNT; i++) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (ioctl(s_jpeg_fd, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QUERYBUF (CAPTURE index=%u) failed: errno=%d", i, errno);
      ::close(s_jpeg_fd);
      s_jpeg_fd = -1;
      return ESP_FAIL;
    }

    void *addr = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                      MAP_SHARED, s_jpeg_fd, buf.m.offset);
    if (addr == MAP_FAILED) {
      ESP_LOGE(TAG, "mmap failed for CAPTURE index=%u: errno=%d", i, errno);
      ::close(s_jpeg_fd);
      s_jpeg_fd = -1;
      return ESP_FAIL;
    }

    s_cap_bufs[i].start = addr;
    s_cap_bufs[i].length = buf.length;

    if (ioctl(s_jpeg_fd, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF (CAPTURE index=%u) failed: errno=%d", i, errno);
      ::close(s_jpeg_fd);
      s_jpeg_fd = -1;
      return ESP_FAIL;
    }
  }

  // ---------
  // OUTPUT en USERPTR (on fournit directement le buffer RGB565 de la caméra)
  // ---------
  memset(&req, 0, sizeof(req));
  req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  req.count = 1;
  req.memory = V4L2_MEMORY_USERPTR;

  if (ioctl(s_jpeg_fd, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS (OUTPUT USERPTR) failed: errno=%d", errno);
    ::close(s_jpeg_fd);
    s_jpeg_fd = -1;
    return ESP_FAIL;
  }

  // ---------
  // STREAMON sur OUTPUT et CAPTURE
  // ---------
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  if (ioctl(s_jpeg_fd, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMON (OUTPUT) failed: errno=%d", errno);
    ::close(s_jpeg_fd);
    s_jpeg_fd = -1;
    return ESP_FAIL;
  }

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(s_jpeg_fd, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMON (CAPTURE) failed: errno=%d", errno);
    ::close(s_jpeg_fd);
    s_jpeg_fd = -1;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "JPEG M2M device ready (RGB565 -> JPEG via V4L2)");

  return ESP_OK;
}

static void deinit_jpeg_m2m_device() {
  if (s_jpeg_fd < 0) {
    return;
  }

  ESP_LOGI(TAG, "Stopping JPEG M2M device");

  // STREAMOFF
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  ioctl(s_jpeg_fd, VIDIOC_STREAMOFF, &type);
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ioctl(s_jpeg_fd, VIDIOC_STREAMOFF, &type);

  // munmap buffers
  for (uint32_t i = 0; i < JPEG_CAPTURE_BUF_COUNT; i++) {
    if (s_cap_bufs[i].start != nullptr) {
      munmap(s_cap_bufs[i].start, s_cap_bufs[i].length);
      s_cap_bufs[i].start = nullptr;
      s_cap_bufs[i].length = 0;
    }
  }

  ::close(s_jpeg_fd);
  s_jpeg_fd = -1;

  ESP_LOGI(TAG, "JPEG M2M device stopped");
}

// Encode une frame RGB565 en JPEG via le device M2M
// - rgb / rgb_size : données caméra RGB565
// - out_frame : infos sur le buffer JPEG renvoyé (pointer dans mmap + index buffer)
// IMPORTANT : l'appelant doit RE-QBUF le buffer capture via l’index retourné
static bool encode_frame_rgb565_to_jpeg(uint8_t *rgb, size_t rgb_size, EncodedFrame &out_frame) {
  if (s_jpeg_fd < 0) {
    ESP_LOGE(TAG, "JPEG M2M device not initialized");
    return false;
  }

  // 1) QBUF côté OUTPUT (USERPTR)
  struct v4l2_buffer buf_out;
  memset(&buf_out, 0, sizeof(buf_out));
  buf_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  buf_out.memory = V4L2_MEMORY_USERPTR;
  buf_out.m.userptr = reinterpret_cast<unsigned long>(rgb);
  buf_out.length = rgb_size;

  if (ioctl(s_jpeg_fd, VIDIOC_QBUF, &buf_out) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QBUF (OUTPUT) failed: errno=%d", errno);
    return false;
  }

  // 2) DQBUF côté CAPTURE (bloquant jusqu’à ce que le JPEG soit prêt)
  struct v4l2_buffer buf_cap;
  memset(&buf_cap, 0, sizeof(buf_cap));
  buf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf_cap.memory = V4L2_MEMORY_MMAP;

  if (ioctl(s_jpeg_fd, VIDIOC_DQBUF, &buf_cap) < 0) {
    ESP_LOGE(TAG, "VIDIOC_DQBUF (CAPTURE) failed: errno=%d", errno);

    // Essayer de vider OUTPUT pour ne pas le bloquer
    struct v4l2_buffer tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    tmp.memory = V4L2_MEMORY_USERPTR;
    ioctl(s_jpeg_fd, VIDIOC_DQBUF, &tmp);

    return false;
  }

  // 3) DQBUF côté OUTPUT (USERPTR)
  struct v4l2_buffer buf_out_done;
  memset(&buf_out_done, 0, sizeof(buf_out_done));
  buf_out_done.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  buf_out_done.memory = V4L2_MEMORY_USERPTR;
  if (ioctl(s_jpeg_fd, VIDIOC_DQBUF, &buf_out_done) < 0) {
    ESP_LOGW(TAG, "VIDIOC_DQBUF (OUTPUT) failed: errno=%d", errno);
    // On continue quand même, le JPEG est prêt côté CAPTURE
  }

  // 4) Retourner le buffer JPEG (on NE LE REQUEUE PAS ici)
  if (buf_cap.index >= JPEG_CAPTURE_BUF_COUNT) {
    ESP_LOGE(TAG, "Invalid CAPTURE buffer index: %u", buf_cap.index);
    return false;
  }

  out_frame.ptr = static_cast<uint8_t *>(s_cap_bufs[buf_cap.index].start);
  out_frame.size = buf_cap.bytesused;
  out_frame.index = buf_cap.index;

  return true;
}

// Requeue un buffer CAPTURE après usage
static void requeue_capture_buffer(uint32_t index) {
  if (s_jpeg_fd < 0) {
    return;
  }
  if (index >= JPEG_CAPTURE_BUF_COUNT) {
    return;
  }

  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = index;

  if (ioctl(s_jpeg_fd, VIDIOC_QBUF, &buf) < 0) {
    ESP_LOGW(TAG, "VIDIOC_QBUF (CAPTURE requeue index=%u) failed: errno=%d", index, errno);
  }
}

// ------------------------
// Implémentation classe
// ------------------------

void CameraWebServer::setup() {
  ESP_LOGI(TAG, "Setting up Camera Web Server on port %d", this->port_);
  ESP_LOGI(TAG, "Server is DISABLED by default - enable via switch in Home Assistant");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not set!");
    this->mark_failed();
    return;
  }

  const int width = this->camera_->get_image_width();
  const int height = this->camera_->get_image_height();

  ESP_LOGI(TAG, "Camera initial resolution: %dx%d (RGB565 via ISP)", width, height);

  if (this->init_jpeg_encoder_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize JPEG M2M encoder (esp_video /dev/video10)");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Camera Web Server initialized (not started yet)");
  ESP_LOGI(TAG, "Turn on the 'Camera Web Server' switch to start");
  ESP_LOGI(TAG, "  Snapshot: http://<ip>:%d/pic", this->port_);
  ESP_LOGI(TAG, "  Stream:   http://<ip>:%d/stream", this->port_);
  ESP_LOGI(TAG, "  Status:   http://<ip>:%d/status", this->port_);
  ESP_LOGI(TAG, "  Info:     http://<ip>:%d/info", this->port_);
}

void CameraWebServer::loop() {
  // Start server when enabled
  if (this->enabled_ && this->server_ == nullptr) {
    ESP_LOGI(TAG, "Starting Camera Web Server...");
    if (this->start_server_() == ESP_OK) {
      ESP_LOGI(TAG, "Camera Web Server started successfully");
    } else {
      ESP_LOGE(TAG, "Failed to start Camera Web Server");
    }
  }

  // Stop server when disabled
  if (!this->enabled_ && this->server_ != nullptr) {
    ESP_LOGI(TAG, "Stopping Camera Web Server...");
    this->stop_server_();
    ESP_LOGI(TAG, "Camera Web Server stopped");
  }
}

// Wrapper membre vers le helper V4L2
esp_err_t CameraWebServer::init_jpeg_encoder_() {
  int w = this->camera_->get_image_width();
  int h = this->camera_->get_image_height();
  return init_jpeg_m2m_device(w, h);
}

// Wrapper membre vers le helper
void CameraWebServer::cleanup_jpeg_encoder_() {
  deinit_jpeg_m2m_device();
}

esp_err_t CameraWebServer::start_server_() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = this->port_;
  config.ctrl_port = this->port_ + 1;
  config.max_uri_handlers = 8;
  config.max_open_sockets = 3;
  config.stack_size = 8192;

  ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

  if (httpd_start(&this->server_, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
  }

  // /pic
  if (this->enable_snapshot_) {
    httpd_uri_t pic_uri = {
        .uri = "/pic",
        .method = HTTP_GET,
        .handler = snapshot_handler_,
        .user_ctx = this
    };
    httpd_register_uri_handler(this->server_, &pic_uri);
    ESP_LOGI(TAG, "Registered /pic endpoint");
  }

  // /stream
  if (this->enable_stream_) {
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler_,
        .user_ctx = this
    };
    httpd_register_uri_handler(this->server_, &stream_uri);
    ESP_LOGI(TAG, "Registered /stream endpoint");
  }

  // /status
  httpd_uri_t status_uri = {
      .uri = "/status",
      .method = HTTP_GET,
      .handler = status_handler_,
      .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &status_uri);
  ESP_LOGI(TAG, "Registered /status endpoint");

  // /info
  httpd_uri_t info_uri = {
      .uri = "/info",
      .method = HTTP_GET,
      .handler = info_handler_,
      .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &info_uri);
  ESP_LOGI(TAG, "Registered /info endpoint");

  return ESP_OK;
}

void CameraWebServer::stop_server_() {
  if (this->server_) {
    httpd_stop(this->server_);
    this->server_ = nullptr;
  }

  // Nettoyer le JPEG M2M encoder
  this->cleanup_jpeg_encoder_();
}

// Handler pour snapshot JPEG unique
esp_err_t CameraWebServer::snapshot_handler_(httpd_req_t *req) {
  CameraWebServer *server = static_cast<CameraWebServer *>(req->user_ctx);

  if (!server->camera_->is_streaming()) {
    ESP_LOGW(TAG, "Camera not streaming, starting...");
    if (!server->camera_->start_streaming()) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));  // Attendre que streaming soit stable
  }

  // Capturer une frame
  if (!server->camera_->capture_frame()) {
    ESP_LOGW(TAG, "Failed to capture frame");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  // Obtenir le buffer image (RGB565 via ISP)
  uint8_t *image_data = server->camera_->get_image_data();
  size_t image_size = server->camera_->get_image_size();

  if (image_data == nullptr || image_size == 0) {
    ESP_LOGE(TAG, "No image data available");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  // Encoder via device JPEG M2M (V4L2)
  EncodedFrame frame;
  if (!encode_frame_rgb565_to_jpeg(image_data, image_size, frame)) {
    ESP_LOGE(TAG, "JPEG M2M encoding failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  ESP_LOGV(TAG, "JPEG encoded (snapshot): %u bytes (from %u bytes RGB565)",
           (unsigned) frame.size, (unsigned) image_size);

  // Envoyer le JPEG encodé
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=snapshot.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  esp_err_t res = httpd_resp_send(req, reinterpret_cast<const char *>(frame.ptr), frame.size);

  // Requeue le buffer capture pour réutilisation
  requeue_capture_buffer(frame.index);

  return res;
}

// Handler pour stream MJPEG continu
esp_err_t CameraWebServer::stream_handler_(httpd_req_t *req) {
  CameraWebServer *server = static_cast<CameraWebServer *>(req->user_ctx);

  if (!server->camera_->is_streaming()) {
    ESP_LOGI(TAG, "Starting camera streaming for MJPEG stream");
    if (!server->camera_->start_streaming()) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // FPS ciblés (si caméra a une config connue, sinon 30 par défaut)
  s_target_fps = 30;

  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // On envoie un X-Framerate basé sur la dernière estimation ou la cible
  char fps_hdr[8];
  int fps_to_report = (s_stream_fps_int > 0) ? s_stream_fps_int : s_target_fps;
  snprintf(fps_hdr, sizeof(fps_hdr), "%d", fps_to_report);
  httpd_resp_set_hdr(req, "X-Framerate", fps_hdr);

  ESP_LOGI(TAG, "MJPEG stream started (header X-Framerate=%s)", fps_hdr);

  int64_t last_ts_us = 0;

  while (true) {
    // Capturer une frame
    if (!server->camera_->capture_frame()) {
      ESP_LOGW(TAG, "Failed to capture frame in stream");
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    uint8_t *image_data = server->camera_->get_image_data();
    size_t image_size = server->camera_->get_image_size();

    if (image_data == nullptr || image_size == 0) {
      ESP_LOGW(TAG, "Invalid frame data: ptr=%p size=%u",
               image_data, (unsigned) image_size);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    ESP_LOGV(TAG, "Frame captured: %ux%u RGB565 = %u bytes",
             server->camera_->get_image_width(),
             server->camera_->get_image_height(),
             (unsigned) image_size);

    // Encoder via JPEG M2M
    EncodedFrame frame;
    if (!encode_frame_rgb565_to_jpeg(image_data, image_size, frame)) {
      ESP_LOGW(TAG, "JPEG M2M encoding failed in stream");
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // Envoyer boundary
    if (httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY)) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected (boundary send failed)");
      requeue_capture_buffer(frame.index);
      break;
    }

    // Envoyer header de la part avec taille JPEG
    char part_buf[128];
    int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, (unsigned) frame.size);
    if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected (part header send failed)");
      requeue_capture_buffer(frame.index);
      break;
    }

    // Envoyer les données JPEG
    if (httpd_resp_send_chunk(req,
                              reinterpret_cast<const char *>(frame.ptr),
                              frame.size) != ESP_OK) {
      ESP_LOGI(TAG, "Failed to send JPEG chunk (client disconnected?)");
      requeue_capture_buffer(frame.index);
      break;
    }

    // Requeue le buffer capture maintenant qu’on a fini de l’utiliser
    requeue_capture_buffer(frame.index);

    // Calcul FPS
    int64_t now_us = esp_timer_get_time();
    if (last_ts_us != 0) {
      float dt = (now_us - last_ts_us) / 1000000.0f;
      if (dt > 0.0f) {
        float inst_fps = 1.0f / dt;
        // lissage exponentiel
        if (s_stream_fps <= 0.1f) {
          s_stream_fps = inst_fps;
        } else {
          s_stream_fps = 0.9f * s_stream_fps + 0.1f * inst_fps;
        }
        s_stream_fps_int = static_cast<int>(s_stream_fps + 0.5f);
      }
    }
    last_ts_us = now_us;

    ESP_LOGV(TAG, "Frame sent successfully, FPS ~ %.2f", s_stream_fps);

    // Limiter le framerate (optionnel)
    vTaskDelay(pdMS_TO_TICKS(33));  // ~30 FPS
  }

  // Terminer le stream HTTP proprement
  httpd_resp_send_chunk(req, nullptr, 0);

  ESP_LOGI(TAG, "MJPEG stream ended");
  return ESP_OK;
}

// Handler pour status JSON
esp_err_t CameraWebServer::status_handler_(httpd_req_t *req) {
  CameraWebServer *server = static_cast<CameraWebServer *>(req->user_ctx);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  int fps = (s_stream_fps_int > 0) ? s_stream_fps_int : s_target_fps;

  char json[256];
  snprintf(json, sizeof(json),
           "{\"streaming\":%s,\"width\":%d,\"height\":%d,"
           "\"format\":\"RGB565\",\"fps\":%d}",
           server->camera_->is_streaming() ? "true" : "false",
           server->camera_->get_image_width(),
           server->camera_->get_image_height(),
           fps);

  return httpd_resp_send(req, json, strlen(json));
}

// Handler /info : infos capteur + devices esp-video
esp_err_t CameraWebServer::info_handler_(httpd_req_t *req) {
  CameraWebServer *server = static_cast<CameraWebServer *>(req->user_ctx);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char json[2048];
  memset(json, 0, sizeof(json));

  // --- 1) Informations caméra via esp_cam_sensor ---
  esp_cam_sensor_device_t *dev = nullptr;
  esp_err_t cam_err = esp_cam_sensor_get_device(&dev);

  const char *sensor_model = "unknown";
  int sensor_width = 0;
  int sensor_height = 0;
  int sensor_fps = 0;

  if (cam_err == ESP_OK && dev != nullptr) {
    sensor_model = dev->info.sensor_name;
    sensor_width = dev->info.max_width;
    sensor_height = dev->info.max_height;
    sensor_fps = dev->info.max_framerate;
  }

  // --- 2) Capabilities esp-video (device /dev/video10) ---
  struct v4l2_capability cap_jpeg;
  memset(&cap_jpeg, 0, sizeof(cap_jpeg));

  const char *jpeg_driver = "n/a";
  const char *jpeg_card = "n/a";
  uint32_t jpeg_caps = 0;
  uint32_t jpeg_dev_caps = 0;

  int fd_jpeg = open("/dev/video10", O_RDWR);
  if (fd_jpeg >= 0) {
    if (ioctl(fd_jpeg, VIDIOC_QUERYCAP, &cap_jpeg) == 0) {
      jpeg_driver = (const char *)cap_jpeg.driver;
      jpeg_card = (const char *)cap_jpeg.card;
      jpeg_caps = cap_jpeg.capabilities;
      jpeg_dev_caps = cap_jpeg.device_caps;
    }
    close(fd_jpeg);
  }

  // --- 3) ISP device information (/dev/video1 si utilisé) ---
  struct v4l2_capability cap_isp;
  memset(&cap_isp, 0, sizeof(cap_isp));

  const char *isp_driver = "n/a";
  const char *isp_card = "n/a";

  int fd_isp = open("/dev/video1", O_RDWR);
  if (fd_isp >= 0) {
    if (ioctl(fd_isp, VIDIOC_QUERYCAP, &cap_isp) == 0) {
      isp_driver = (const char *)cap_isp.driver;
      isp_card = (const char *)cap_isp.card;
    }
    close(fd_isp);
  }

  // --- 4) Device RAW capteur (/dev/video0) ---
  struct v4l2_capability cap_raw;
  memset(&cap_raw, 0, sizeof(cap_raw));
  const char *raw_driver = "n/a";
  const char *raw_card = "n/a";

  int fd_raw = open("/dev/video0", O_RDWR);
  if (fd_raw >= 0) {
    if (ioctl(fd_raw, VIDIOC_QUERYCAP, &cap_raw) == 0) {
      raw_driver = (const char *)cap_raw.driver;
      raw_card = (const char *)cap_raw.card;
    }
    close(fd_raw);
  }

  // --- 5) Pipeline status ---
  bool streaming = server->camera_->is_streaming();
  int cur_w = server->camera_->get_image_width();
  int cur_h = server->camera_->get_image_height();

  // Construction JSON
  snprintf(json, sizeof(json),
    "{"
      "\"camera\":{"
        "\"model\":\"%s\","
        "\"max_width\":%d,"
        "\"max_height\":%d,"
        "\"max_fps\":%d,"
        "\"current_width\":%d,"
        "\"current_height\":%d,"
        "\"streaming\":%s"
      "},"
      "\"jpeg_device\":{"
        "\"path\":\"/dev/video10\","
        "\"driver\":\"%s\","
        "\"card\":\"%s\","
        "\"caps\":%u,"
        "\"device_caps\":%u"
      "},"
      "\"isp_device\":{"
        "\"path\":\"/dev/video1\","
        "\"driver\":\"%s\","
        "\"card\":\"%s\""
      "},"
      "\"raw_device\":{"
        "\"path\":\"/dev/video0\","
        "\"driver\":\"%s\","
        "\"card\":\"%s\""
      "}"
    "}",
    // Camera
    sensor_model, sensor_width, sensor_height, sensor_fps,
    cur_w, cur_h, streaming ? "true" : "false",
    // JPEG M2M
    jpeg_driver, jpeg_card, jpeg_caps, jpeg_dev_caps,
    // ISP
    isp_driver, isp_card,
    // RAW
    raw_driver, raw_card
  );

  return httpd_resp_send(req, json, strlen(json));
}

#else  // !USE_ESP_IDF

void CameraWebServer::setup() {
  ESP_LOGE(TAG, "Camera Web Server requires ESP-IDF framework");
  this->mark_failed();
}

void CameraWebServer::loop() {}

#endif  // USE_ESP_IDF

}  // namespace camera_web_server
}  // namespace esphome

