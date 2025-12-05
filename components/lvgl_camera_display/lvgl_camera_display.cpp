#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

// ESP-IDF detection components - now enabled with proper Kconfig options
#include "human_face_detect.hpp"
#include "pedestrian_detect.hpp"
#include "dl_image.hpp"

// Detection is now enabled
// #define ESPHOME_BUILD_WITHOUT_ESPIDF_DETECTION 1

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

  // Initialize face detector if enabled
  if (this->face_detection_enabled_) {
    ESP_LOGI(TAG, "🔍 Initializing face detection...");
    this->face_detector_ = new HumanFaceDetect();
    if (this->face_detector_ != nullptr) {
      // Low thresholds for maximum sensitivity
      // score_thr: lower = more sensitive (0.3 detects faces with 30%+ confidence)
      // nms_thr: higher = less aggressive overlap filtering
      this->face_detector_->set_score_thr(0.3);  // Low threshold = more sensitive
      this->face_detector_->set_nms_thr(0.5);    // Moderate overlap filtering
      ESP_LOGI(TAG, "✅ Face detector initialized (score_thr=0.3, nms_thr=0.5 - sensitive)");
    } else {
      ESP_LOGE(TAG, "❌ Failed to initialize face detector");
      this->face_detection_enabled_ = false;
    }
  }

  // Initialize pedestrian detector if enabled
  if (this->pedestrian_detection_enabled_) {
    ESP_LOGI(TAG, "🚶 Initializing pedestrian detection...");
    this->pedestrian_detector_ = new PedestrianDetect();
    if (this->pedestrian_detector_ != nullptr) {
      ESP_LOGI(TAG, "✅ Pedestrian detector initialized");
    } else {
      ESP_LOGE(TAG, "❌ Failed to initialize pedestrian detector");
      this->pedestrian_detection_enabled_ = false;
    }
  }

  ESP_LOGI(TAG, "✅ LVGL Camera Display initialisé (not started yet)");
  ESP_LOGI(TAG, "   Camera: Opérationnelle");
  ESP_LOGI(TAG, "   Face detection: %s", this->face_detection_enabled_ ? "ENABLED" : "DISABLED");
  ESP_LOGI(TAG, "   Pedestrian detection: %s", this->pedestrian_detection_enabled_ ? "ENABLED" : "DISABLED");
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

  // Détecter et dessiner les objets avant d'afficher
  if ((this->face_detection_enabled_ && this->face_detector_ != nullptr) ||
      (this->pedestrian_detection_enabled_ && this->pedestrian_detector_ != nullptr)) {
    this->detect_and_draw_objects_(img_data, width, height);
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

void LVGLCameraDisplay::detect_and_draw_objects_(uint8_t* img_data, uint16_t width, uint16_t height) {
#ifndef ESPHOME_BUILD_WITHOUT_ESPIDF_DETECTION
  if (img_data == nullptr) {
    return;
  }

  // Créer la structure d'image pour esp-dl
  dl::image::img_t img = {
    .data = img_data,
    .width = width,
    .height = height,
    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565
  };

  // Statistiques
  static uint32_t detect_count = 0;
  static uint32_t total_face_time = 0;
  static uint32_t total_ped_time = 0;
  static uint32_t total_faces = 0;
  static uint32_t total_pedestrians = 0;

  detect_count++;

  // Détecter les visages (avec frame skipping pour améliorer les performances)
  // Skip 1 frame, detect on the 2nd (runs 1/2 of the time = smooth detection)
  if (this->face_detection_enabled_ && this->face_detector_ != nullptr) {
    this->face_detection_frame_skip_++;

    // Only run face detection every 2nd frame (balanced performance)
    if (this->face_detection_frame_skip_ >= 2) {
      this->face_detection_frame_skip_ = 0;

      uint32_t t1 = millis();
      std::list<dl::detect::result_t> &face_results = this->face_detector_->run(img);
      uint32_t t2 = millis();

      total_face_time += (t2 - t1);
      total_faces += face_results.size();

      // Debug: Log detection results every 50 detections
      static uint32_t debug_count = 0;
      debug_count++;
      if (debug_count % 50 == 0) {
        ESP_LOGI(TAG, "🔍 DEBUG: Detection #%u - Found %u faces in %ums",
                 debug_count, face_results.size(), (t2 - t1));
      }

    // Dessiner les rectangles VERTS pour les visages
    std::vector<uint8_t> green = {0x00, 0xF8};  // Vert en RGB565 big-endian
    for (auto &result : face_results) {
      dl::image::draw_hollow_rectangle(
        img,
        result.box[0], result.box[1],
        result.box[2], result.box[3],
        green, 3
      );

      // Log la première détection
      static bool first_face = true;
      if (first_face) {
        ESP_LOGI(TAG, "👤 Visage détecté: box=[%d,%d,%d,%d] score=%.2f",
                 result.box[0], result.box[1], result.box[2], result.box[3], result.score);
        first_face = false;
      }
    }
    } // End of frame skip check
  }

  // Détecter les piétons
  if (this->pedestrian_detection_enabled_ && this->pedestrian_detector_ != nullptr) {
    uint32_t t1 = millis();
    std::list<dl::detect::result_t> &ped_results = this->pedestrian_detector_->run(img);
    uint32_t t2 = millis();

    total_ped_time += (t2 - t1);
    total_pedestrians += ped_results.size();

    // Dessiner les rectangles BLEUS pour les piétons
    std::vector<uint8_t> blue = {0x1F, 0x00};  // Bleu en RGB565 big-endian
    for (auto &result : ped_results) {
      dl::image::draw_hollow_rectangle(
        img,
        result.box[0], result.box[1],
        result.box[2], result.box[3],
        blue, 3
      );

      // Log la première détection
      static bool first_ped = true;
      if (first_ped) {
        ESP_LOGI(TAG, "🚶 Piéton détecté: box=[%d,%d,%d,%d] score=%.2f",
                 result.box[0], result.box[1], result.box[2], result.box[3], result.score);
        first_ped = false;
      }
    }
  }

  // Log des statistiques toutes les 100 frames
  if (detect_count % 100 == 0) {
    if (this->face_detection_enabled_) {
      float avg_time = total_face_time / 100.0f;
      float avg_faces = total_faces / 100.0f;
      ESP_LOGI(TAG, "🔍 Face detection: %.1fms avg | %.1f faces avg", avg_time, avg_faces);
    }
    if (this->pedestrian_detection_enabled_) {
      float avg_time = total_ped_time / 100.0f;
      float avg_peds = total_pedestrians / 100.0f;
      ESP_LOGI(TAG, "🚶 Pedestrian detection: %.1fms avg | %.1f pedestrians avg", avg_time, avg_peds);
    }
    total_face_time = 0;
    total_ped_time = 0;
    total_faces = 0;
    total_pedestrians = 0;
  }
#else
  // Détection désactivée - ne rien faire
  (void)img_data;
  (void)width;
  (void)height;
#endif
}

}  // namespace lvgl_camera_display
}  // namespace esphome






