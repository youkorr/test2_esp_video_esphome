#include "camera_web_server.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF

extern "C" {
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
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
// FPS globals
// ------------------------
static uint32_t fps_frame_counter = 0;
static uint64_t fps_last_time_us = 0;
static int current_fps = 0;

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

  // On peut initialiser l'encodeur ici, ou lazy-init dans /pic et /stream.
  // On choisit lazy-init pour être sûr d'avoir une résolution valide.
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
// JPEG hardware P4 init / cleanup
// --------------------------------------------------

esp_err_t CameraWebServer::init_jpeg_encoder_() {
  if (this->jpeg_handle_ != nullptr && this->jpeg_buffer_ != nullptr) {
    return ESP_OK;  // déjà prêt
  }

  jpeg_encode_engine_cfg_t encode_cfg = {
      .timeout_ms = 5000,
  };

  esp_err_t ret = jpeg_new_encoder_engine(&encode_cfg, &this->jpeg_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create JPEG encoder engine: %d", ret);
    this->jpeg_handle_ = nullptr;
    return ret;
  }

  ESP_LOGI(TAG, "JPEG encoder engine created");

  // On prend une résolution max 800x640 (OV5647) → RGB565 = W*H*2
  // JPEG sera ~ 1/2 à 1/3 de cette taille.
  const int max_w = 800;
  const int max_h = 640;
  size_t input_size = max_w * max_h * 2;   // RGB565
  size_t jpeg_alloc_size = input_size / 2; // ~50% pour qualité 80

  jpeg_encode_memory_alloc_cfg_t mem_cfg = {
      .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
  };

  this->jpeg_buffer_ = (uint8_t *) jpeg_alloc_encoder_mem(
      jpeg_alloc_size,
      &mem_cfg,
      &this->jpeg_buffer_size_
  );

  if (this->jpeg_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate JPEG output buffer");
    jpeg_del_encoder_engine(this->jpeg_handle_);
    this->jpeg_handle_ = nullptr;
    this->jpeg_buffer_size_ = 0;
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "JPEG encoder initialized:");
  ESP_LOGI(TAG, "  Output buffer: %u bytes", (unsigned) this->jpeg_buffer_size_);
  ESP_LOGI(TAG, "  Quality: %d", this->jpeg_quality_);

  return ESP_OK;
}

void CameraWebServer::cleanup_jpeg_encoder_() {
  if (this->jpeg_buffer_ != nullptr) {
    free(this->jpeg_buffer_);
    this->jpeg_buffer_ = nullptr;
    this->jpeg_buffer_size_ = 0;
  }

  if (this->jpeg_handle_ != nullptr) {
    jpeg_del_encoder_engine(this->jpeg_handle_);
    this->jpeg_handle_ = nullptr;
  }

  ESP_LOGD(TAG, "JPEG encoder cleaned up");
}

// --------------------------------------------------
// /pic : snapshot JPEG (hardware encoder P4)
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

  uint8_t *image_data = server->camera_->get_image_data();
  size_t image_size = server->camera_->get_image_size();

  if (!image_data || image_size == 0) {
    ESP_LOGE(TAG, "Invalid RGB data in /pic");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  if (server->init_jpeg_encoder_() != ESP_OK) {
    ESP_LOGE(TAG, "init_jpeg_encoder_() failed in /pic");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  jpeg_encode_cfg_t encode_config = {};
  encode_config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
  encode_config.image_quality = (uint32_t) server->jpeg_quality_;
  encode_config.width = server->camera_->get_image_width();
  encode_config.height = server->camera_->get_image_height();
  encode_config.sub_sample = JPEG_DOWN_SAMPLING_YUV422;

  uint32_t jpeg_size = 0;
  esp_err_t ret = jpeg_encoder_process(
      server->jpeg_handle_,
      &encode_config,
      image_data,                  // RGB565 input
      image_size,                  // RGB565 size
      server->jpeg_buffer_,        // JPEG output buffer
      server->jpeg_buffer_size_,   // buffer size
      &jpeg_size                   // actual JPEG size
  );

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "JPEG encoding failed in /pic: %d", ret);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  ESP_LOGV(TAG, "Snapshot JPEG encoded: %u bytes (from %u bytes RGB565)",
           (unsigned) jpeg_size, (unsigned) image_size);

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=snapshot.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  return httpd_resp_send(req,
                         reinterpret_cast<const char *>(server->jpeg_buffer_),
                         jpeg_size);
}

// --------------------------------------------------
// /stream : MJPEG + calcul FPS (hardware encoder P4)
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

  ESP_LOGI(TAG, "MJPEG stream started");

  fps_frame_counter = 0;
  fps_last_time_us = esp_timer_get_time();
  current_fps = 0;

  while (true) {
    // 1) Capturer une frame RGB565 depuis la caméra
    if (!server->camera_->capture_frame()) {
      ESP_LOGW(TAG, "capture_frame() failed in /stream");
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    uint8_t *image_data = server->camera_->get_image_data();
    size_t image_size = server->camera_->get_image_size();

    if (!image_data || image_size == 0) {
      ESP_LOGW(TAG, "Invalid RGB data in /stream");
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    // 2) Encoder RGB565 -> JPEG via hardware P4
    jpeg_encode_cfg_t encode_config = {};
    encode_config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
    encode_config.image_quality = (uint32_t) server->jpeg_quality_;
    encode_config.width = server->camera_->get_image_width();
    encode_config.height = server->camera_->get_image_height();
    encode_config.sub_sample = JPEG_DOWN_SAMPLING_YUV422;

    uint32_t jpeg_size = 0;
    esp_err_t ret = jpeg_encoder_process(
        server->jpeg_handle_,
        &encode_config,
        image_data,
        image_size,
        server->jpeg_buffer_,
        server->jpeg_buffer_size_,
        &jpeg_size
    );

    if (ret != ESP_OK || jpeg_size == 0) {
      ESP_LOGW(TAG, "JPEG encoding failed in /stream: %d", ret);
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    // 3) Envoyer MJPEG: boundary + header + data
    if (httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY)) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected (boundary)");
      break;
    }

    char part_buf[128];
    int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART,
                        static_cast<unsigned>(jpeg_size));
    if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected (part header)");
      break;
    }

    if (httpd_resp_send_chunk(req,
                              reinterpret_cast<const char *>(server->jpeg_buffer_),
                              jpeg_size) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected (jpeg data)");
      break;
    }

    // 4) Calcul FPS : nombre de JPEG envoyés / seconde
    fps_frame_counter++;
    uint64_t now_us = esp_timer_get_time();
    uint64_t dt_us = now_us - fps_last_time_us;
    if (dt_us >= 1000000ULL) {  // >= 1 seconde
      current_fps = fps_frame_counter;
      fps_frame_counter = 0;
      fps_last_time_us = now_us;
      ESP_LOGD(TAG, "[httpd]: Current FPS: %d", current_fps);
    }

    // Optionnel : limiter un peu la charge CPU (1ms)
    vTaskDelay(pdMS_TO_TICKS(1));
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
// /info : infos RAW/ISP/JPEG + caméra (via /dev/video0/1/10)
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
      jpeg_driver = (const char *) cap_jpeg.driver;
      jpeg_card   = (const char *) cap_jpeg.card;
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
      isp_driver = (const char *) cap_isp.driver;
      isp_card   = (const char *) cap_isp.card;
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
      raw_driver = (const char *) cap_raw.driver;
      raw_card   = (const char *) cap_raw.card;
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
  // On charge /stream dans un iframe pour ne pas bloquer le JS
  const char html[] =
      "<html><head><meta charset='utf-8'>"
      "<title>ESP32-P4 Camera</title>"
      "<style>"
      "body{margin:0;background:#000;color:#eee;font-family:Arial;text-align:center;}"
      "#wrap{position:relative;display:inline-block;margin-top:10px;}"
      "iframe{width:100%;max-width:800px;height:600px;border:0;border-radius:8px;}"
      "#bar{position:absolute;left:0;right:0;bottom:0;"
      "background:rgba(0,0,0,0.6);color:#0f0;padding:4px 8px;"
      "font-size:14px;text-align:left;}"
      "</style>"
      "</head><body>"
      "<h3>OV5647 Camera (RGB565 via ISP, HW JPEG P4)</h3>"
      "<div id='wrap'>"
      "<iframe id='cam' src='/stream'></iframe>"
      "<div id='bar'>FPS: --  |  Res: -- x --</div>"
      "</div>"
      "<script>"
      "function upd(){"
      "  var xhr = new XMLHttpRequest();"
      "  xhr.onreadystatechange = function(){"
      "    if (xhr.readyState == 4 && xhr.status == 200) {"
      "      try {"
      "        var j = JSON.parse(xhr.responseText);"
      "        document.getElementById('bar').innerText ="
      "          'FPS: ' + j.fps + '  |  Res: ' + j.width + ' x ' + j.height;"
      "      } catch(e){}"
      "    }"
      "  };"
      "  xhr.open('GET','/status',true);"
      "  xhr.send();"
      "}"
      "setInterval(upd,500);"
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





