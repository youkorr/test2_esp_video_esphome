#include "camera_web_server.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF
#include <esp_timer.h>
#include <sys/time.h>
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

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "Camera not set!");
    this->mark_failed();
    return;
  }

  // Démarrer le serveur HTTP
  if (this->start_server_() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start web server");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Camera Web Server started successfully");
  ESP_LOGI(TAG, "  Snapshot: http://<ip>:%d/pic", this->port_);
  ESP_LOGI(TAG, "  Stream:   http://<ip>:%d/stream", this->port_);
  ESP_LOGI(TAG, "  Status:   http://<ip>:%d/status", this->port_);
}

void CameraWebServer::loop() {
  // Le serveur HTTP tourne en background, rien à faire ici
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

  // Obtenir le buffer image (RGB565 actuellement)
  uint8_t *image_data = server->camera_->get_image_data();
  size_t image_size = server->camera_->get_image_size();

  if (image_data == nullptr || image_size == 0) {
    ESP_LOGE(TAG, "No image data available");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  // TODO: Si format est RGB565, encoder en JPEG avec hardware encoder
  // Pour l'instant, on envoie le RGB565 brut (le navigateur ne pourra pas l'afficher)
  // Il faudra implémenter l'encodage JPEG hardware ici

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=snapshot.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // ATTENTION: Ceci envoie RGB565 brut, pas JPEG!
  // Il faut ajouter l'encodage JPEG hardware
  esp_err_t res = httpd_resp_send(req, (const char *)image_data, image_size);

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
      continue;
    }

    // Envoyer boundary
    if (httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY)) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected");
      break;
    }

    // TODO: Encoder en JPEG si nécessaire
    // Pour l'instant RGB565 brut (incompatible avec navigateur)

    // Envoyer header de la part
    char part_buf[128];
    int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, image_size);
    if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK) {
      break;
    }

    // Envoyer les données image
    if (httpd_resp_send_chunk(req, (const char *)image_data, image_size) != ESP_OK) {
      break;
    }

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
