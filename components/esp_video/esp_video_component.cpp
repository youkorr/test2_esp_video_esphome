#include "esp_video_component.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"

// Headers ESP-Video
extern "C" {
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "driver/gpio.h"
}

namespace esphome {
namespace esp_video {

static const char *const TAG = "esp_video";

void ESPVideoComponent::setup() {
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  ESP-Video Component Initialization");
  ESP_LOGI(TAG, "========================================");

#ifdef ESP_VIDEO_VERSION
  ESP_LOGI(TAG, "Version: %s", ESP_VIDEO_VERSION);
#else
  ESP_LOGI(TAG, "Version: Unknown");
#endif

  // Afficher les fonctionnalités activées
  ESP_LOGI(TAG, "Fonctionnalités activées:");

#ifdef ESP_VIDEO_H264_ENABLED
  ESP_LOGI(TAG, "  ✓ Encodeur H.264 matériel");
#else
  ESP_LOGI(TAG, "  ✗ Encodeur H.264 désactivé");
#endif

#ifdef ESP_VIDEO_JPEG_ENABLED
  ESP_LOGI(TAG, "  ✓ Encodeur JPEG matériel");
#else
  ESP_LOGI(TAG, "  ✗ Encodeur JPEG désactivé");
#endif

#ifdef ESP_VIDEO_ISP_ENABLED
  ESP_LOGI(TAG, "  ✓ Image Signal Processor (ISP)");
#else
  ESP_LOGI(TAG, "  ✗ ISP désactivé");
#endif

#ifdef CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
  ESP_LOGI(TAG, "  ✓ Support MIPI-CSI");
#else
  ESP_LOGW(TAG, "  ✗ Support MIPI-CSI désactivé");
#endif

  // Vérification de la mémoire disponible
  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t min_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

  ESP_LOGI(TAG, "Mémoire:");
  ESP_LOGI(TAG, "  Libre actuellement: %u octets", (unsigned)free_heap);
  ESP_LOGI(TAG, "  Minimum atteint: %u octets", (unsigned)min_heap);

  // Recommandation mémoire
  if (free_heap < 512 * 1024) {
    ESP_LOGW(TAG, "⚠️  Mémoire faible! Recommandé: > 512 KB");
    ESP_LOGW(TAG, "    Considérez réduire la résolution ou la qualité");
  }

  // Initialiser ESP-Video
  ESP_LOGI(TAG, "----------------------------------------");
  ESP_LOGI(TAG, "Initialisation ESP-Video...");

#ifdef CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
  if (this->has_i2c_config_) {
    ESP_LOGI(TAG, "Configuration CSI:");
    ESP_LOGI(TAG, "  I2C SDA: GPIO%d", this->i2c_sda_);
    ESP_LOGI(TAG, "  I2C SCL: GPIO%d", this->i2c_scl_);
    ESP_LOGI(TAG, "  I2C Port: %d", this->i2c_port_);
    ESP_LOGI(TAG, "  I2C Freq: %d Hz", this->i2c_frequency_);
    if (this->reset_pin_ >= 0) {
      ESP_LOGI(TAG, "  Reset Pin: GPIO%d", this->reset_pin_);
    }
    if (this->pwdn_pin_ >= 0) {
      ESP_LOGI(TAG, "  PWDN Pin: GPIO%d", this->pwdn_pin_);
    }

    // Configuration CSI pour esp_video_init
    esp_video_init_csi_config_t csi_config = {};
    csi_config.sccb_config.init_sccb = true;
    csi_config.sccb_config.i2c_config.port = this->i2c_port_;
    csi_config.sccb_config.i2c_config.scl_pin = (gpio_num_t)this->i2c_scl_;
    csi_config.sccb_config.i2c_config.sda_pin = (gpio_num_t)this->i2c_sda_;
    csi_config.sccb_config.freq = this->i2c_frequency_;
    csi_config.reset_pin = (gpio_num_t)this->reset_pin_;
    csi_config.pwdn_pin = (gpio_num_t)this->pwdn_pin_;

    esp_video_init_config_t video_config = {};
    video_config.csi = &csi_config;

    esp_err_t ret = esp_video_init(&video_config);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "❌ Échec esp_video_init(): %d", ret);
      this->mark_failed();
      return;
    }

    ESP_LOGI(TAG, "✅ esp_video_init() réussi");
  } else {
    ESP_LOGW(TAG, "Pas de configuration I2C - esp_video_init() non appelé");
    ESP_LOGW(TAG, "Les devices vidéo ne seront pas disponibles");
  }
#else
  ESP_LOGW(TAG, "MIPI-CSI désactivé - esp_video_init() non appelé");
#endif

  this->initialized_ = true;

  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "✅ ESP-Video prêt");
  ESP_LOGI(TAG, "========================================");
}

void ESPVideoComponent::loop() {
  // Rien à faire dans la boucle principale
  // Les composants utilisant ESP-Video gèrent leur propre boucle
}

void ESPVideoComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP-Video Component:");

#ifdef ESP_VIDEO_VERSION
  ESP_LOGCONFIG(TAG, "  Version: %s", ESP_VIDEO_VERSION);
#endif

  ESP_LOGCONFIG(TAG, "  État: %s", this->initialized_ ? "Prêt" : "Non initialisé");

  ESP_LOGCONFIG(TAG, "  Encodeurs:");
#ifdef ESP_VIDEO_H264_ENABLED
  ESP_LOGCONFIG(TAG, "    - H.264 (matériel)");
#endif
#ifdef ESP_VIDEO_JPEG_ENABLED
  ESP_LOGCONFIG(TAG, "    - JPEG (matériel)");
#endif

#ifdef ESP_VIDEO_ISP_ENABLED
  ESP_LOGCONFIG(TAG, "  ISP: Activé");
#endif

  ESP_LOGCONFIG(TAG, "  Interfaces:");
#ifdef CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
  ESP_LOGCONFIG(TAG, "    - MIPI-CSI");
  if (this->has_i2c_config_) {
    ESP_LOGCONFIG(TAG, "      I2C: GPIO%d (SDA), GPIO%d (SCL)", this->i2c_sda_, this->i2c_scl_);
    ESP_LOGCONFIG(TAG, "      Port: %d, Freq: %d Hz", this->i2c_port_, this->i2c_frequency_);
  }
#endif

  // Afficher l'utilisation mémoire actuelle
  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  ESP_LOGCONFIG(TAG, "  Mémoire libre: %u octets", (unsigned)free_heap);
}

}  // namespace esp_video
}  // namespace esphome
