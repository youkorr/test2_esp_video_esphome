#include "camera_web_server.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF

extern "C" {
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include "esp_timer.h"
}

namespace esphome {
namespace camera_web_server {

static const char *const TAG = "camera_web_server";

// ------------------------
// MJPEG boundary & headers
// ------------------------
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ------------------------
// JPEG M2M V4L2 globals
// ------------------------
static int s_jpeg_fd = -1;
static void *s_cap_buf = nullptr;
static size_t s_cap_buf_size = 0;

// ------------------------
// FPS globals
// ------------------------
static uint32_t fps_frame_counter = 0;
static uint32_t fps_last_time = 0;
static int current_fps = 0;

// --------------------------------------------------
// Helper: open /dev/video10 (esp_video_jpeg_device)
// --------------------------------------------------
static int open_jpeg_device() {
  int fd = ::open("/dev/video10", O_RDWR);
  if (fd < 0) {
    ESP_LOGE(TAG, "Failed to open /dev/video10: errno=%d", errno);
    return -1;
  }

  struct v4l2_capability cap {};
  if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QUERYCAP(/dev/video10) failed: errno=%d", errno);
    ::close(fd);
    return -1;
  }

  ESP_LOGI(TAG, "JPEG M2M opened: driver=%s card=%s caps=0x%X devcaps=0x%X",
           cap.driver, cap.card, cap.capabilities, cap.device_caps);
  return fd;
}

// --------------------------------------------------
// Component methods
// --------------------------------------------------

void CameraWebServer::setup() {
  ESP_LOGI(TAG, "Setting up Camera Web Server on port %d", this->port_);
  ESP_LOGI(TAG, "Server is DISABLED by default - enable via switch in Home Assistant");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not set!");
    this->mark_failed();
    return;
  }

  int w = this->camera_->get_image_width();
  int h = this->camera_->get_image_height();
  ESP_LOGI(TAG, "Camera initial resolution: %dx%d (RGB565 via ISP)", w, h);
  // ⚠️ On n'initialise pas le JPEG ici (lazy init dans /pic et /stream)
}

void CameraWebServer::loop() {
  if (this->enabled_ && this->server_ == nullptr) {
    ESP_LOGI(TAG, "Starting Camera Web Server...");
    if (this->start_server_() == ESP_OK) {
      ESP_LOGI(TAG, "Camera Web Server started");
    } else {
      ESP_LOGE(TAG, "Failed to start Camera Web Server");
    }
  }

  if (!this->enabled_ && this->server_ != nullptr) {
    ESP_LOGI(TAG, "Stopping Camera Web Server...");
    this->stop_server_();
    ESP_LOGI(TAG, "Camera Web Server stopped");
  }
}

// --------------------------------------------------
// HTTP server: start / stop
// --------------------------------------------------

esp_err_t CameraWebServer::start_server_() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = this->port_;
  config.ctrl_port = this->port_ + 1;
  config.max_uri_handlers = 10;
  config.max_open_sockets = 3;
  config.stack_size = 8192;

  if (httpd_start(&this->server_, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
  }

  // /pic
  if (this->enable_snapshot_) {
    httpd_uri_t uri_pic = {
        .uri = "/pic",
        .method = HTTP_GET,
        .handler = snapshot_handler_,
        .user_ctx = this};
    httpd_register_uri_handler(this->server_, &uri_pic);
    ESP_LOGI(TAG, "Registered /pic");
  }

  // /stream
  if (this->enable_stream_) {
    httpd_uri_t uri_stream = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler_,
        .user_ctx = this};
    httpd_register_uri_handler(this->server_, &uri_stream);
    ESP_LOGI(TAG, "Registered /stream");
  }

  // /status
  {
    httpd_uri_t uri_status = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler_,
        .user_ctx = this};
    httpd_register_uri_handler(this->server_, &uri_status);
    ESP_LOGI(TAG, "Registered /status");
  }

  // /info
  {
    httpd_uri_t uri_info = {
        .uri = "/info",
        .method = HTTP_GET,
        .handler = info_handler_,
        .user_ctx = this};
    httpd_register_uri_handler(this->server_, &uri_info);
    ESP_LOGI(TAG, "Registered /info");
  }

  // /view (page avec image + FPS en bas)
  {
    httpd_uri_t uri_view = {
        .uri = "/view",
        .method = HTTP_GET,
        .handler = view_handler_,
        .user_ctx = this};
    httpd_register_uri_handler(this->server_, &uri_view);
    ESP_LOGI(TAG, "Registered /view");
  }

  return ESP_OK;
}

void CameraWebServer::stop_server_() {
  if (this->server_) {
    httpd_stop(this->server_);
    this->server_ = nullptr;
  }

  this->cleanup_jpeg_encoder_();
}

// --------------------------------------------------
// JPEG M2M init / cleanup
// --------------------------------------------------

void CameraWebServer::cleanup_jpeg_encoder_() {
  if (s_jpeg_fd >= 0) {
    ESP_LOGI(TAG, "Stopping JPEG M2M device");

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(s_jpeg_fd, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(s_jpeg_fd, VIDIOC_STREAMOFF, &type);

    if (s_cap_buf != nullptr) {
      munmap(s_cap_buf, s_cap_buf_size);
      s_cap_buf = nullptr;
      s_cap_buf_size = 0;
    }

    ::close(s_jpeg_fd);
    s_jpeg_fd = -1;
  }
}

esp_err_t CameraWebServer::init_jpeg_encoder_() {
  if (s_jpeg_fd >= 0) {
    return ESP_OK;  // déjà prêt
  }

  // S'assurer que la caméra streame et a une vraie résolution (pas 0x0)
  if (!this->camera_->is_streaming()) {
    ESP_LOGI(TAG, "Starting camera streaming for JPEG init");
    if (!this->camera_->start_streaming()) {
      ESP_LOGE(TAG, "Failed to start camera streaming");
      return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (!this->camera_->capture_frame()) {
    ESP_LOGE(TAG, "Failed to capture frame for JPEG init");
    return ESP_FAIL;
  }

  int width = this->camera_->get_image_width();
  int height = this->camera_->get_image_height();

  if (width <= 0 || height <= 0) {
    ESP_LOGE(TAG, "Invalid camera resolution: %dx%d", width, height);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Initializing JPEG M2M for %dx%d (OV5647 max ~800x640)", width, height);

  s_jpeg_fd = open_jpeg_device();
  if (s_jpeg_fd < 0) {
    ESP_LOGE(TAG, "open_jpeg_device() failed");
    return ESP_FAIL;
  }

  // OUTPUT = RGB565
  {
    struct v4l2_format fmt_out {};
    fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt_out.fmt.pix.width = width;
    fmt_out.fmt.pix.height = height;
    fmt_out.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    fmt_out.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(s_jpeg_fd, VIDIOC_S_FMT, &fmt_out) < 0) {
      ESP_LOGE(TAG, "VIDIOC_S_FMT(OUTPUT RGB565) failed: errno=%d", errno);
      this->cleanup_jpeg_encoder_();
      return ESP_FAIL;
    }

    ESP_LOGI(TAG, "JPEG OUTPUT format set: %ux%u RGB565",
             fmt_out.fmt.pix.width, fmt_out.fmt.pix.height);
  }

  // CAPTURE = JPEG
  {
    struct v4l2_format fmt_cap {};
    fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt_cap.fmt.pix.width = width;
    fmt_cap.fmt.pix.height = height;
    fmt_cap.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
    fmt_cap.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(s_jpeg_fd, VIDIOC_S_FMT, &fmt_cap) < 0) {
      ESP_LOGE(TAG, "VIDIOC_S_FMT(CAPTURE JPEG) failed: errno=%d", errno);
      this->cleanup_jpeg_encoder_();
      return ESP_FAIL;
    }

    ESP_LOGI(TAG, "JPEG CAPTURE format set: %ux%u JPEG",
             fmt_cap.fmt.pix.width, fmt_cap.fmt.pix.height);
  }

  // REQBUFS CAPTURE (MMAP)
  {
    struct v4l2_requestbuffers req_cap {};
    req_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req_cap.count = 1;
    req_cap.memory = V4L2_MEMORY_MMAP;

    if (ioctl(s_jpeg_fd, VIDIOC_REQBUFS, &req_cap) < 0) {
      ESP_LOGE(TAG, "VIDIOC_REQBUFS(CAPTURE) failed: errno=%d", errno);
      this->cleanup_jpeg_encoder_();
      return ESP_FAIL;
    }

    if (req_cap.count < 1) {
      ESP_LOGE(TAG, "No CAPTURE buffer allocated");
      this->cleanup_jpeg_encoder_();
      return ESP_FAIL;
    }

    struct v4l2_buffer buf_cap {};
    buf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf_cap.memory = V4L2_MEMORY_MMAP;
    buf_cap.index = 0;

    if (ioctl(s_jpeg_fd, VIDIOC_QUERYBUF, &buf_cap) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QUERYBUF(CAPTURE) failed: errno=%d", errno);
      this->cleanup_jpeg_encoder_();
      return ESP_FAIL;
    }

    s_cap_buf_size = buf_cap.length;
    s_cap_buf = mmap(nullptr, buf_cap.length,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED, s_jpeg_fd, buf_cap.m.offset);

    if (s_cap_buf == MAP_FAILED) {
      ESP_LOGE(TAG, "mmap(CAPTURE) failed: errno=%d", errno);
      s_cap_buf = nullptr;
      s_cap_buf_size = 0;
      this->cleanup_jpeg_encoder_();
      return ESP_FAIL;
    }

    if (ioctl(s_jpeg_fd, VIDIOC_QBUF, &buf_cap) < 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF(CAPTURE initial) failed: errno=%d", errno);
      this->cleanup_jpeg_encoder_();
      return ESP_FAIL;
    }
  }

  // REQBUFS OUTPUT (USERPTR)
  {
    struct v4l2_requestbuffers req_out {};
    req_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req_out.count = 1;
    req_out.memory = V4L2_MEMORY_USERPTR;

    if (ioctl(s_jpeg_fd, VIDIOC_REQBUFS, &req_out) < 0) {
      ESP_LOGE(TAG, "VIDIOC_REQBUFS(OUTPUT USERPTR) failed: errno=%d", errno);
      this->cleanup_jpeg_encoder_();
      return ESP_FAIL;
    }
  }

  // STREAMON OUTPUT/CAPTURE
  {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(s_jpeg_fd, VIDIOC_STREAMON, &type) < 0) {
      ESP_LOGE(TAG, "VIDIOC_STREAMON(OUTPUT) failed: errno=%d", errno);
      this->cleanup_jpeg_encoder_();
      return ESP_FAIL;
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_jpeg_fd, VIDIOC_STREAMON, &type) < 0) {
      ESP_LOGE(TAG, "VIDIOC_STREAMON(CAPTURE) failed: errno=%d", errno);
      this->cleanup_jpeg_encoder_();
      return ESP_FAIL;
    }
  }

  ESP_LOGI(TAG, "JPEG M2M encoder ready (RGB565 -> JPEG)");
  return ESP_OK;
}

// --------------------------------------------------
// Helper: encode RGB565 -> JPEG via /dev/video10
// --------------------------------------------------

static bool encode_frame_rgb565_to_jpeg(uint8_t *rgb, size_t rgb_size,
                                        uint8_t **jpeg_ptr, size_t *jpeg_size) {
  if (s_jpeg_fd < 0 || s_cap_buf == nullptr || s_cap_buf_size == 0) {
    ESP_LOGE(TAG, "JPEG device not initialized");
    return false;
  }

  // QBUF OUTPUT (userptr)
  struct v4l2_buffer buf_out {};
  buf_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  buf_out.memory = V4L2_MEMORY_USERPTR;
  buf_out.m.userptr = reinterpret_cast<unsigned long>(rgb);
  buf_out.length = rgb_size;

  if (ioctl(s_jpeg_fd, VIDIOC_QBUF, &buf_out) < 0) {
    ESP_LOGE(TAG, "VIDIOC_QBUF(OUTPUT) failed: errno=%d", errno);
    return false;
  }

  // DQBUF CAPTURE
  struct v4l2_buffer buf_cap {};
  buf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf_cap.memory = V4L2_MEMORY_MMAP;

  if (ioctl(s_jpeg_fd, VIDIOC_DQBUF, &buf_cap) < 0) {
    ESP_LOGE(TAG, "VIDIOC_DQBUF(CAPTURE) failed: errno=%d", errno);

    // Essayer de vider OUTPUT pour ne pas bloquer
    struct v4l2_buffer tmp {};
    tmp.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    tmp.memory = V4L2_MEMORY_USERPTR;
    ioctl(s_jpeg_fd, VIDIOC_DQBUF, &tmp);

    return false;
  }

  // DQBUF OUTPUT (libérer)
  struct v4l2_buffer buf_out_done {};
  buf_out_done.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  buf_out_done.memory = V4L2_MEMORY_USERPTR;
  if (ioctl(s_jpeg_fd, VIDIOC_DQBUF, &buf_out_done) < 0) {
    ESP_LOGW(TAG, "VIDIOC_DQBUF(OUTPUT) failed (non-fatal): errno=%d", errno);
  }

  if (buf_cap.index != 0 || buf_cap.bytesused == 0) {
    ESP_LOGE(TAG, "Invalid CAPTURE buffer: index=%u used=%u",
             buf_cap.index, buf_cap.bytesused);
    return false;
  }

  *jpeg_ptr = static_cast<uint8_t *>(s_cap_buf);
  *jpeg_size = buf_cap.bytesused;

  // Requeue CAPTURE
  buf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf_cap.memory = V4L2_MEMORY_MMAP;
  buf_cap.index = 0;
  if (ioctl(s_jpeg_fd, VIDIOC_QBUF, &buf_cap) < 0) {
    ESP_LOGW(TAG, "VIDIOC_QBUF(CAPTURE requeue) failed: errno=%d", errno);
  }

  return true;
}

// --------------------------------------------------
// /pic : snapshot JPEG
// --------------------------------------------------

esp_err_t CameraWebServer::snapshot_handler_(httpd_req_t *req) {
  auto *server = static_cast<CameraWebServer *>(req->user_ctx);

  if (!server->camera_->is_streaming()) {
    ESP_LOGI(TAG, "Camera not streaming, starting for snapshot");
    if (!server->camera_->start_streaming()) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (!server->camera_->capture_frame()) {
    ESP_LOGE(TAG, "capture_frame() failed in /pic");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  uint8_t *rgb = server->camera_->get_image_data();
  size_t rgb_size = server->camera_->get_image_size();

  if (!rgb || rgb_size == 0) {
    ESP_LOGE(TAG, "Invalid RGB data in /pic");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  if (server->init_jpeg_encoder_() != ESP_OK) {
    ESP_LOGE(TAG, "init_jpeg_encoder_() failed in /pic");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  uint8_t *jpeg_ptr = nullptr;
  size_t jpeg_size = 0;

  if (!encode_frame_rgb565_to_jpeg(rgb, rgb_size, &jpeg_ptr, &jpeg_size)) {
    ESP_LOGE(TAG, "encode_frame_rgb565_to_jpeg() failed in /pic");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=snapshot.jpg");

  return httpd_resp_send(req, reinterpret_cast<const char *>(jpeg_ptr), jpeg_size);
}

// --------------------------------------------------
// /stream : MJPEG + calcul FPS
// --------------------------------------------------

esp_err_t CameraWebServer::stream_handler_(httpd_req_t *req) {
  auto *server = static_cast<CameraWebServer *>(req->user_ctx);

  if (!server->camera_->is_streaming()) {
    ESP_LOGI(TAG, "Starting camera streaming for MJPEG");
    if (!server->camera_->start_streaming()) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (server->init_jpeg_encoder_() != ESP_OK) {
    ESP_LOGE(TAG, "init_jpeg_encoder_() failed in /stream");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char fps_hdr[8];
  snprintf(fps_hdr, sizeof(fps_hdr), "%d", current_fps > 0 ? current_fps : 30);
  httpd_resp_set_hdr(req, "X-Framerate", fps_hdr);

  ESP_LOGI(TAG, "MJPEG stream started");

  while (true) {
    if (!server->camera_->capture_frame()) {
      ESP_LOGW(TAG, "capture_frame() failed in /stream");
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    uint8_t *rgb = server->camera_->get_image_data();
    size_t rgb_size = server->camera_->get_image_size();

    if (!rgb || rgb_size == 0) {
      ESP_LOGW(TAG, "Invalid RGB data in /stream");
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    uint8_t *jpeg_ptr = nullptr;
    size_t jpeg_size = 0;

    if (!encode_frame_rgb565_to_jpeg(rgb, rgb_size, &jpeg_ptr, &jpeg_size)) {
      ESP_LOGW(TAG, "JPEG encode failed in /stream");
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // Boundary
    if (httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY)) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected (boundary)");
      break;
    }

    // Header part
    char part_buf[128];
    int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART,
                        static_cast<unsigned>(jpeg_size));
    if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected (part header)");
      break;
    }

    // JPEG data
    if (httpd_resp_send_chunk(req,
                              reinterpret_cast<const char *>(jpeg_ptr),
                              jpeg_size) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected (jpeg data)");
      break;
    }

    // ---- FPS CALCUL ----
    fps_frame_counter++;
    uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    if (fps_last_time == 0) {
      fps_last_time = now;
    } else if (now > fps_last_time) {
      uint32_t dt = now - fps_last_time;
      if (dt >= 1) {
        current_fps = fps_frame_counter / (dt ? dt : 1);
        fps_frame_counter = 0;
        fps_last_time = now;
      }
    }

    //vTaskDelay(pdMS_TO_TICKS(30));  // ~30 FPS
  }

  httpd_resp_send_chunk(req, nullptr, 0);
  ESP_LOGI(TAG, "MJPEG stream ended");
  return ESP_OK;
}

// --------------------------------------------------
// /status : JSON simple (streaming + rés + fps)
// --------------------------------------------------

esp_err_t CameraWebServer::status_handler_(httpd_req_t *req) {
  auto *server = static_cast<CameraWebServer *>(req->user_ctx);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char json[256];
  int fps = (current_fps > 0) ? current_fps : 0;

  snprintf(json, sizeof(json),
           "{"
             "\"streaming\":%s,"
             "\"width\":%d,"
             "\"height\":%d,"
             "\"format\":\"RGB565\","
             "\"fps\":%d"
           "}",
           server->camera_->is_streaming() ? "true" : "false",
           server->camera_->get_image_width(),
           server->camera_->get_image_height(),
           fps);

  return httpd_resp_send(req, json, strlen(json));
}

// --------------------------------------------------
// /info : infos RAW/ISP/JPEG + caméra
// --------------------------------------------------

esp_err_t CameraWebServer::info_handler_(httpd_req_t *req) {
  auto *server = static_cast<CameraWebServer *>(req->user_ctx);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char json[2048];
  memset(json, 0, sizeof(json));

  const char *sensor_name = "OV5647";  // pour ton cas
  int cur_w = server->camera_->get_image_width();
  int cur_h = server->camera_->get_image_height();
  int fps = (current_fps > 0) ? current_fps : 0;

  // JPEG /dev/video10
  struct v4l2_capability cap_jpeg {};
  const char *jpeg_driver = "n/a";
  const char *jpeg_card   = "n/a";
  uint32_t jpeg_caps = 0;
  uint32_t jpeg_dev_caps = 0;

  int fd_jpeg = open("/dev/video10", O_RDWR);
  if (fd_jpeg >= 0) {
    if (ioctl(fd_jpeg, VIDIOC_QUERYCAP, &cap_jpeg) == 0) {
      jpeg_driver = (const char *)cap_jpeg.driver;
      jpeg_card   = (const char *)cap_jpeg.card;
      jpeg_caps = cap_jpeg.capabilities;
      jpeg_dev_caps = cap_jpeg.device_caps;
    }
    close(fd_jpeg);
  }

  // ISP /dev/video1
  struct v4l2_capability cap_isp {};
  const char *isp_driver = "n/a";
  const char *isp_card   = "n/a";

  int fd_isp = open("/dev/video1", O_RDWR);
  if (fd_isp >= 0) {
    if (ioctl(fd_isp, VIDIOC_QUERYCAP, &cap_isp) == 0) {
      isp_driver = (const char *)cap_isp.driver;
      isp_card   = (const char *)cap_isp.card;
    }
    close(fd_isp);
  }

  // RAW /dev/video0
  struct v4l2_capability cap_raw {};
  const char *raw_driver = "n/a";
  const char *raw_card   = "n/a";

  int fd_raw = open("/dev/video0", O_RDWR);
  if (fd_raw >= 0) {
    if (ioctl(fd_raw, VIDIOC_QUERYCAP, &cap_raw) == 0) {
      raw_driver = (const char *)cap_raw.driver;
      raw_card   = (const char *)cap_raw.card;
    }
    close(fd_raw);
  }

  snprintf(json, sizeof(json),
    "{"
      "\"camera\":{"
        "\"model\":\"%s\","
        "\"current_width\":%d,"
        "\"current_height\":%d,"
        "\"fps\":%d,"
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
    sensor_name,
    cur_w, cur_h,
    fps,
    server->camera_->is_streaming() ? "true" : "false",
    jpeg_driver, jpeg_card, jpeg_caps, jpeg_dev_caps,
    isp_driver, isp_card,
    raw_driver, raw_card
  );

  return httpd_resp_send(req, json, strlen(json));
}

// --------------------------------------------------
// /view : page web image + FPS en bas (comme ta capture)
// --------------------------------------------------

esp_err_t CameraWebServer::view_handler_(httpd_req_t *req) {
  // Page ultra simple : image stream + FPS dessous (mis à jour via /status)
  const char html[] =
      "<html><head><meta charset='utf-8'>"
      "<title>ESP32-P4 Camera</title>"
      "<style>"
      "body{margin:0;background:#000;color:#eee;font-family:Arial;text-align:center;}"
      "#wrap{position:relative;display:inline-block;margin-top:10px;}"
      "img{width:100%;max-width:800px;border-radius:8px;}"
      "#bar{position:absolute;left:0;right:0;bottom:0;"
      "background:rgba(0,0,0,0.6);color:#0f0;padding:4px 8px;"
      "font-size:14px;text-align:left;}"
      "</style>"
      "</head><body>"
      "<h3>OV5647 Camera (RGB565 via ISP)</h3>"
      "<div id='wrap'>"
      "<img src='/stream' id='cam'>"
      "<div id='bar'>FPS: --  |  Res: -- x --</div>"
      "</div>"
      "<script>"
      "async function upd(){"
      " try{"
      "  let r=await fetch('/status');"
      "  let j=await r.json();"
      "  document.getElementById('bar').innerText="
      "    'FPS: '+j.fps+'  |  Res: '+j.width+' x '+j.height;"
      " }catch(e){}"
      "}"
      "setInterval(upd, 500);"
      "upd();"
      "</script>"
      "</body></html>";

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, strlen(html));
}

// --------------------------------------------------
// non-IDF stub
// --------------------------------------------------

}  // namespace camera_web_server
}  // namespace esphome

#else  // !USE_ESP_IDF

namespace esphome {
namespace camera_web_server {

void CameraWebServer::setup() {
  ESP_LOGE("camera_web_server", "Camera Web Server requires ESP-IDF");
  this->mark_failed();
}

void CameraWebServer::loop() {}

}  // namespace camera_web_server
}  // namespace esphome

#endif  // USE_ESP_IDF





