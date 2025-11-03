#include "lvgl_camera_display.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esphome/components/lvgl/lvgl_esphome.h"  // pour lv_async_call()

namespace esphome {
namespace lvgl_camera_display {

static const char *const TAG = "lvgl_camera_display";

void LVGLCameraDisplay::setup() {
  ESP_LOGCONFIG(TAG, "🎥 Configuration LVGL Camera Display (FreeRTOS mode)...");

  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "❌ Camera non configurée");
    this->mark_failed();
    return;
  }

  this->update_interval_ = 33;  // cible 30 FPS
  this->frame_count_ = 0;
  this->first_update_ = true;
  this->stop_task_flag_ = false;

  ESP_LOGI(TAG, "✅ Démarrage de la tâche caméra LVGL (core 0)...");
  BaseType_t res = xTaskCreatePinnedToCore(
      LVGLCameraDisplay::camera_task_trampoline_,
      "lvgl_cam_task",
      8192,  // stack size
      this,
      4,     // priorité moyenne
      &this->camera_task_handle_,
      0      // ⚙️ core 0 = même cœur que LVGL → pas de watchdog
  );

  if (res != pdPASS) {
    ESP_LOGE(TAG, "❌ Impossible de créer la tâche caméra LVGL");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "🎬 LVGL Camera Display initialisé et tâche démarrée");
}

void LVGLCameraDisplay::loop() {
  // Rien ici — tout se fait dans la tâche FreeRTOS
}

void LVGLCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Camera Display:");
  ESP_LOGCONFIG(TAG, "  Mode: tâche FreeRTOS dédiée");
  ESP_LOGCONFIG(TAG, "  Intervalle cible: %u ms (~%d FPS)",
                this->update_interval_, 1000 / this->update_interval_);
  ESP_LOGCONFIG(TAG, "  Canvas configuré: %s", this->canvas_obj_ ? "OUI" : "NON");
}

void LVGLCameraDisplay::camera_task_trampoline_(void *param) {
  auto *self = static_cast<LVGLCameraDisplay *>(param);
  self->camera_task_();
}

void LVGLCameraDisplay::camera_task_() {
  ESP_LOGI(TAG, "📸 Tâche caméra LVGL démarrée (core %d)", xPortGetCoreID());

  uint32_t last_log_time = millis();
  this->frame_count_ = 0;

  while (!this->stop_task_flag_) {
    if (!this->camera_->is_streaming()) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // Si une nouvelle frame est prête → planifier affichage
    if (this->camera_->capture_frame()) {
      // ⚡ Planifie la mise à jour du canvas dans le thread LVGL (thread-safe)
      lv_async_call([](void *user) {
        auto *self = static_cast<LVGLCameraDisplay *>(user);
        self->update_canvas_();
      }, this);

      this->frame_count_++;
    }

    // Log des FPS toutes les 3 secondes
    uint32_t now = millis();
    if (now - last_log_time >= 3000) {
      float fps = (float)this->frame_count_ / ((now - last_log_time) / 1000.0f);
      ESP_LOGI(TAG, "🎞️ %u frames affichées - FPS moyen: %.2f", this->frame_count_, fps);
      this->frame_count_ = 0;
      last_log_time = now;
    }

    // Respiration CPU (~100 FPS max, évite le watchdog)
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  ESP_LOGW(TAG, "🛑 Tâche caméra LVGL arrêtée");
  vTaskDelete(NULL);
}

void LVGLCameraDisplay::update_canvas_() {
  if (this->camera_ == nullptr || this->canvas_obj_ == nullptr)
    return;

  uint8_t *img_data = this->camera_->get_image_data();
  uint16_t width = this->camera_->get_image_width();
  uint16_t height = this->camera_->get_image_height();

  if (img_data == nullptr)
    return;

  // Premier affichage → debug info
  if (this->first_update_) {
    ESP_LOGI(TAG, "🖼️ Premier update canvas (via lv_async_call, zéro copie):");
    ESP_LOGI(TAG, "   Dimensions: %ux%u", width, height);
    ESP_LOGI(TAG, "   Buffer caméra: %p", img_data);
    this->first_update_ = false;
  }

  // ⚙️ Exécuté dans le contexte LVGL → thread principal, sans verrou
  lv_canvas_set_buffer(this->canvas_obj_, img_data, width, height, LV_IMG_CF_TRUE_COLOR);
  lv_obj_invalidate(this->canvas_obj_);
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

void LVGLCameraDisplay::stop_task() {
  this->stop_task_flag_ = true;
  ESP_LOGI(TAG, "🧹 Arrêt de la tâche caméra LVGL demandé...");
}

}  // namespace lvgl_camera_display
}  // namespace esphome








