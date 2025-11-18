#include "camera_web_server.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF
#include <esp_timer.h>
#include <sys/time.h>
#include "driver/jpeg_encode.h"
#include "esp_heap_caps.h"
#endif

namespace esphome {
namespace camera_web_server {

static const char *const TAG = "camera_web_server";

#ifdef USE_ESP_IDF

// Boundary pour MJPEG multipart stream
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

void CameraWebServer::setup() {
  ESP_LOGI(TAG, "Setting up Camera Web Server on port %d", this->port_);
  ESP_LOGI(TAG, "Server is DISABLED by default - enable via switch in Home Assistant");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not set!");
    this->mark_failed();
    return;
  }

  // Initialiser le JPEG encoder hardware
  if (this->init_jpeg_encoder_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize JPEG encoder");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Camera Web Server initialized (not started yet)");
  ESP_LOGI(TAG, "Turn on the 'Camera Web Server' switch to start");
  ESP_LOGI(TAG, "  Snapshot: http://<ip>:%d/pic", this->port_);
  ESP_LOGI(TAG, "  Stream:   http://<ip>:%d/stream", this->port_);
  ESP_LOGI(TAG, "  Status:   http://<ip>:%d/status", this->port_);
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

esp_err_t CameraWebServer::init_jpeg_encoder_() {
  // Initialiser le moteur JPEG encoder hardware ESP32-P4
  jpeg_encode_engine_cfg_t encode_cfg = {
      .timeout_ms = 5000,
  };

  esp_err_t ret = jpeg_new_encoder_engine(&encode_cfg, &this->jpeg_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create JPEG encoder engine: %d", ret);
    return ret;
  }

  ESP_LOGI(TAG, "JPEG encoder engine created");

  // Allouer buffer pour output JPEG
  // Pour RGB565 (2 bytes/pixel), JPEG typiquement 1/3 à 1/2 de la taille
  // 1280x720x2 = 1843200 bytes RGB565
  // Allouer ~1MB pour JPEG output (suffisant pour quality 80)
  size_t input_size = 1280 * 720 * 2;  // Max resolution RGB565
  size_t jpeg_alloc_size = input_size / 2;  // ~920 KB

  jpeg_encode_memory_alloc_cfg_t mem_cfg = {
      .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
  };

  this->jpeg_buffer_ = (uint8_t *)jpeg_alloc_encoder_mem(
      jpeg_alloc_size,
      &mem_cfg,
      &this->jpeg_buffer_size_
  );

  if (this->jpeg_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate JPEG output buffer");
    jpeg_del_encoder_engine(this->jpeg_handle_);
    this->jpeg_handle_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "JPEG encoder initialized:");
  ESP_LOGI(TAG, "  Output buffer: %u bytes", this->jpeg_buffer_size_);
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

  // Enregistrer les handlers
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

  // Status endpoint (toujours actif)
  httpd_uri_t status_uri = {
      .uri = "/status",
      .method = HTTP_GET,
      .handler = status_handler_,
      .user_ctx = this
  };
  httpd_register_uri_handler(this->server_, &status_uri);
  ESP_LOGI(TAG, "Registered /status endpoint");

  return ESP_OK;
}

void CameraWebServer::stop_server_() {
  if (this->server_) {
    httpd_stop(this->server_);
    this->server_ = nullptr;
  }

  // Nettoyer le JPEG encoder
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

  // Obtenir le buffer image (RGB565)
  uint8_t *image_data = server->camera_->get_image_data();
  size_t image_size = server->camera_->get_image_size();

  if (image_data == nullptr || image_size == 0) {
    ESP_LOGE(TAG, "No image data available");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  // Encoder RGB565 → JPEG avec hardware encoder ESP32-P4
  jpeg_encode_cfg_t encode_config = {};
  encode_config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
  encode_config.image_quality = (uint32_t)server->jpeg_quality_;
  encode_config.width = server->camera_->get_image_width();
  encode_config.height = server->camera_->get_image_height();
  encode_config.sub_sample = JPEG_DOWN_SAMPLING_YUV422;

  uint32_t jpeg_size = 0;
  esp_err_t ret = jpeg_encoder_process(
      server->jpeg_handle_,
      &encode_config,
      image_data,              // RGB565 input
      image_size,              // RGB565 size
      server->jpeg_buffer_,    // JPEG output
      server->jpeg_buffer_size_,
      &jpeg_size               // JPEG size résultant
  );

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "JPEG encoding failed: %d", ret);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  ESP_LOGV(TAG, "JPEG encoded: %u bytes (from %u bytes RGB565)", jpeg_size, image_size);

  // Envoyer le JPEG encodé
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=snapshot.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  esp_err_t res = httpd_resp_send(req, (const char *)server->jpeg_buffer_, jpeg_size);

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

  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "30");

  ESP_LOGI(TAG, "MJPEG stream started");

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
      ESP_LOGW(TAG, "Invalid frame data: ptr=%p size=%u", image_data, image_size);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    ESP_LOGV(TAG, "Frame captured: %ux%u RGB565 = %u bytes",
             server->camera_->get_image_width(),
             server->camera_->get_image_height(),
             image_size);

    // Encoder RGB565 → JPEG
    jpeg_encode_cfg_t encode_config = {};
    encode_config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
    encode_config.image_quality = (uint32_t)server->jpeg_quality_;
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

    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "JPEG encoding failed in stream: %d", ret);
      continue;
    }

    ESP_LOGV(TAG, "JPEG encoded: %u bytes (quality=%d)", jpeg_size, server->jpeg_quality_);

    // Envoyer boundary
    if (httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY)) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected");
      break;
    }

    // Envoyer header de la part avec taille JPEG
    char part_buf[128];
    int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, jpeg_size);
    if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK) {
      break;
    }

    // Envoyer les données JPEG
    if (httpd_resp_send_chunk(req, (const char *)server->jpeg_buffer_, jpeg_size) != ESP_OK) {
      ESP_LOGI(TAG, "Failed to send JPEG chunk");
      break;
    }

    ESP_LOGV(TAG, "Frame sent successfully");

    // Limiter le framerate pour ne pas surcharger le réseau
    vTaskDelay(pdMS_TO_TICKS(33));  // ~30 FPS
  }

  ESP_LOGI(TAG, "MJPEG stream ended");
  return ESP_OK;
}

// Handler pour status JSON
esp_err_t CameraWebServer::status_handler_(httpd_req_t *req) {
  CameraWebServer *server = static_cast<CameraWebServer *>(req->user_ctx);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char json[256];
  snprintf(json, sizeof(json),
           "{\"streaming\":%s,\"width\":%d,\"height\":%d,\"format\":\"RGB565\"}",
           server->camera_->is_streaming() ? "true" : "false",
           server->camera_->get_image_width(),
           server->camera_->get_image_height());

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
