#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace lvgl_camera_display {

static const char *const TAG = "lvgl_camera_display";

void LVGLCameraDisplay::setup() {
  ESP_LOGCONFIG(TAG, "🎥 Configuration LVGL Camera Display...");
  ESP_LOGI(TAG, "Display is DISABLED by default - enable via switch in Home Assistant");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "❌ Camera non configurée");
    this->mark_failed();
    return;
  }

  // Vérifier que la caméra est opérationnelle
  if (!this->camera_->is_pipeline_ready()) {
    ESP_LOGE(TAG, "❌ Camera non opérationnelle - pipeline non démarré");
    ESP_LOGE(TAG, "   Le composant mipi_dsi_cam a échoué à s'initialiser");
    ESP_LOGE(TAG, "   Vérifiez les logs de mipi_dsi_cam pour plus de détails");
    this->mark_failed();
    return;
  }

  // Initialiser le détecteur de visages si activé
  if (this->face_detection_enabled_) {
    ESP_LOGI(TAG, "🔍 Initialisation de la détection faciale...");
    try {
      this->face_detector_ = new HumanFaceDetect();
      ESP_LOGI(TAG, "✅ Détecteur de visages initialisé");
    } catch (const std::exception& e) {
      ESP_LOGE(TAG, "❌ Échec de l'initialisation du détecteur de visages: %s", e.what());
      this->face_detection_enabled_ = false;
    }
  }

  ESP_LOGI(TAG, "✅ LVGL Camera Display initialisé (not started yet)");
  ESP_LOGI(TAG, "   Camera: Opérationnelle");
  ESP_LOGI(TAG, "   Face detection: %s", this->face_detection_enabled_ ? "ENABLED" : "DISABLED");
  ESP_LOGI(TAG, "   Update interval: %u ms (~%d FPS) via LVGL timer",
           this->update_interval_, 1000 / this->update_interval_);
  ESP_LOGI(TAG, "Turn on the 'LVGL Camera Display' switch to start");
}

void LVGLCameraDisplay::loop() {
  // Start timer when enabled
  if (this->enabled_ && this->lvgl_timer_ == nullptr) {
    ESP_LOGI(TAG, "Starting LVGL Camera Display...");
    this->lvgl_timer_ = lv_timer_create(lvgl_timer_callback_, this->update_interval_, this);
    if (this->lvgl_timer_ == nullptr) {
      ESP_LOGE(TAG, "❌ Failed to create LVGL timer");
    } else {
      ESP_LOGI(TAG, "✅ LVGL Camera Display started");
    }
  }

  // Stop timer when disabled
  if (!this->enabled_ && this->lvgl_timer_ != nullptr) {
    ESP_LOGI(TAG, "Stopping LVGL Camera Display...");
    lv_timer_del(this->lvgl_timer_);
    this->lvgl_timer_ = nullptr;
    ESP_LOGI(TAG, "LVGL Camera Display stopped");
  }
}

// Callback du timer LVGL (appelé périodiquement par LVGL)
void LVGLCameraDisplay::lvgl_timer_callback_(lv_timer_t *timer) {
  LVGLCameraDisplay *display = static_cast<LVGLCameraDisplay *>(timer->user_data);
  if (display != nullptr) {
    display->update_camera_frame_();
  }
}

// Mise à jour de la frame caméra (appelée par le timer LVGL)
void LVGLCameraDisplay::update_camera_frame_() {
  // Si la caméra est en streaming, capturer ET mettre à jour le canvas
  if (!this->camera_->is_streaming()) {
    return;
  }

  // Statistiques de frames manquées
  static uint32_t attempts = 0;
  static uint32_t skipped = 0;

  uint32_t t1 = millis();
  bool frame_captured = this->camera_->capture_frame();
  uint32_t t2 = millis();

  attempts++;
  if (!frame_captured) {
    skipped++;
    return;
  }

  this->update_canvas_();
  uint32_t t3 = millis();
  this->frame_count_++;

  // Accumuler les temps pour statistiques
  static uint32_t last_time = 0;
  static uint32_t total_capture_ms = 0;
  static uint32_t total_canvas_ms = 0;

  total_capture_ms += (t2 - t1);
  total_canvas_ms += (t3 - t2);

  // Logger performance toutes les 100 frames
  if (this->frame_count_ % 100 == 0) {
    uint32_t now_time = millis();

    if (last_time > 0) {
      float elapsed = (now_time - last_time) / 1000.0f;  // secondes
      float fps = 100.0f / elapsed;
      float avg_capture = total_capture_ms / 100.0f;
      float avg_canvas = total_canvas_ms / 100.0f;
      float skip_rate = (skipped * 100.0f) / attempts;
      ESP_LOGI(TAG, "🎞️ %u frames - FPS: %.2f | capture: %.1fms | canvas: %.1fms | skip: %.1f%%",
               this->frame_count_, fps, avg_capture, avg_canvas, skip_rate);
    }
    last_time = now_time;
    total_capture_ms = 0;
    total_canvas_ms = 0;
    attempts = 0;
    skipped = 0;
  }
}

void LVGLCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Camera Display:");
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->update_interval_);
  ESP_LOGCONFIG(TAG, "  FPS cible: ~%d", 1000 / this->update_interval_);
  ESP_LOGCONFIG(TAG, "  Canvas configuré: %s", this->canvas_obj_ ? "OUI" : "NON");
}

void LVGLCameraDisplay::update_canvas_() {
  if (this->camera_ == nullptr) {
    return;
  }

  if (this->canvas_obj_ == nullptr) {
    if (!this->canvas_warning_shown_) {
      ESP_LOGW(TAG, "❌ Canvas null - pas encore configuré?");
      this->canvas_warning_shown_ = true;
    }
    return;
  }

  // Libérer l'ancien buffer affiché (si présent)
  if (this->displayed_buffer_ != nullptr) {
    this->camera_->release_buffer(this->displayed_buffer_);
    this->displayed_buffer_ = nullptr;
  }

  // Acquérir le nouveau buffer depuis le pool
  mipi_dsi_cam::SimpleBufferElement *buffer = this->camera_->acquire_buffer();
  if (buffer == nullptr) {
    // Pas de buffer disponible - garder l'affichage précédent
    return;
  }

  uint8_t* img_data = this->camera_->get_buffer_data(buffer);
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data == nullptr) {
    return;
  }

  if (this->first_update_) {
    ESP_LOGI(TAG, "🖼️  Premier update canvas (buffer pool):");
    ESP_LOGI(TAG, "   Dimensions: %ux%u", width, height);
    ESP_LOGI(TAG, "   Buffer: %p (index=%u)", img_data, this->camera_->get_buffer_index(buffer));
    ESP_LOGI(TAG, "   Premiers pixels (RGB565): %02X%02X %02X%02X %02X%02X",
             img_data[0], img_data[1], img_data[2], img_data[3], img_data[4], img_data[5]);
    this->first_update_ = false;
  }

  // Détecter et dessiner les visages avant d'afficher
  if (this->face_detection_enabled_ && this->face_detector_ != nullptr) {
    this->detect_and_draw_faces_(img_data, width, height);
  }

  lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_IMG_CF_TRUE_COLOR);
  lv_obj_invalidate(this->canvas_obj_);

  // Tracker ce buffer pour le libérer au prochain update
  this->displayed_buffer_ = buffer;
}

void LVGLCameraDisplay::configure_canvas(lv_obj_t *canvas) {
  this->canvas_obj_ = canvas;
  ESP_LOGI(TAG, "🎨 Canvas configuré: %p", canvas);

  if (canvas != nullptr) {
    lv_coord_t w = lv_obj_get_width(canvas);
    lv_coord_t h = lv_obj_get_height(canvas);
    ESP_LOGI(TAG, "   Taille canvas: %dx%d", w, h);
  }
}

void LVGLCameraDisplay::detect_and_draw_faces_(uint8_t* img_data, uint16_t width, uint16_t height) {
  if (img_data == nullptr || this->face_detector_ == nullptr) {
    return;
  }

  // Créer la structure d'image pour esp-dl
  dl::image::img_t img = {
    .data = img_data,
    .width = width,
    .height = height,
    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
  };

  // Détecter les visages
  uint32_t t1 = millis();
  std::list<dl::detect::result_t> &results = this->face_detector_->run(img);
  uint32_t t2 = millis();

  // Log des statistiques toutes les 100 frames
  static uint32_t detect_count = 0;
  static uint32_t total_detect_time = 0;
  static uint32_t total_faces_detected = 0;

  detect_count++;
  total_detect_time += (t2 - t1);
  total_faces_detected += results.size();

  if (detect_count % 100 == 0) {
    float avg_time = total_detect_time / 100.0f;
    float avg_faces = total_faces_detected / 100.0f;
    ESP_LOGI(TAG, "🔍 Face detection: %.1fms avg | %.1f faces avg", avg_time, avg_faces);
    total_detect_time = 0;
    total_faces_detected = 0;
  }

  // Dessiner les rectangles pour chaque visage détecté
  for (auto &result : results) {
    // Couleur verte pour les rectangles (format RGB565)
    // En RGB565: R(5 bits) G(6 bits) B(5 bits)
    // Vert: 0x07E0
    std::vector<uint8_t> color = {0x00, 0xF8};  // Vert en RGB565 big-endian

    // Dessiner le rectangle
    dl::image::draw_hollow_rectangle(
      img,
      result.box[0],  // x1
      result.box[1],  // y1
      result.box[2],  // x2
      result.box[3],  // y2
      color,
      3  // line width
    );

    // Log la première détection
    static bool first_face = true;
    if (first_face && results.size() > 0) {
      ESP_LOGI(TAG, "👤 Visage détecté: box=[%d,%d,%d,%d] score=%.2f",
               result.box[0], result.box[1], result.box[2], result.box[3], result.score);
      first_face = false;
    }
  }
}

}  // namespace lvgl_camera_display
}  // namespace esphome






