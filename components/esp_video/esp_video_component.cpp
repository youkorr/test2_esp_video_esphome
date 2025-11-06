#include "esp_video_component.h"
#include "i2c_helper.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"

// Headers ESP-Video
extern "C" {
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "driver/i2c_master.h"
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
  // Configuration CSI pour esp_video_init
  // IMPORTANT: init_sccb = true - laissons esp_video créer son propre bus I2C
  ESP_LOGI(TAG, "Configuration I2C pour ESP-Video:");
  ESP_LOGI(TAG, "  SDA: GPIO%d, SCL: GPIO%d, Freq: %u Hz",
           this->sda_pin_, this->scl_pin_, this->i2c_frequency_);

  esp_video_init_csi_config_t csi_config = {};
  csi_config.sccb_config.init_sccb = true;  // esp_video créera son propre bus I2C
  // Utiliser i2c_config (union) car init_sccb = true
  csi_config.sccb_config.i2c_config.port = 0;  // Port I2C 0
  csi_config.sccb_config.i2c_config.sda_pin = static_cast<gpio_num_t>(this->sda_pin_);
  csi_config.sccb_config.i2c_config.scl_pin = static_cast<gpio_num_t>(this->scl_pin_);
  csi_config.sccb_config.freq = this->i2c_frequency_;  // Fréquence I2C
  csi_config.reset_pin = (gpio_num_t)-1;  // Pas de pin de reset
  csi_config.pwdn_pin = (gpio_num_t)-1;   // Pas de pin de power-down

  esp_video_init_config_t video_config = {};
  video_config.csi = &csi_config;

  ESP_LOGI(TAG, "Appel esp_video_init()...");
  esp_err_t ret = esp_video_init(&video_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ Échec esp_video_init(): %d (%s)", ret, esp_err_to_name(ret));
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "✅ esp_video_init() réussi - Devices vidéo créés");
#else
  ESP_LOGW(TAG, "MIPI-CSI désactivé - esp_video_init() non appelé");
#endif

  this->initialized_ = true;

  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "✅ ESP-Video prêt");
  ESP_LOGI(TAG, "Les devices /dev/video* sont disponibles");
  ESP_LOGI(TAG, "========================================");
}

void ESPVideoComponent::loop() {
  // Rien à faire dans la boucle principale
}

void ESPVideoComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP-Video Component:");

#ifdef ESP_VIDEO_VERSION
  ESP_LOGCONFIG(TAG, "  Version: %s", ESP_VIDEO_VERSION);
#endif

  ESP_LOGCONFIG(TAG, "  État: %s", this->initialized_ ? "Prêt" : "Non initialisé");

  if (this->i2c_bus_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Bus I2C: Configuré");
  } else {
    ESP_LOGCONFIG(TAG, "  Bus I2C: Non configuré");
  }

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
#endif

  // Afficher l'utilisation mémoire actuelle
  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  ESP_LOGCONFIG(TAG, "  Mémoire libre: %u octets", (unsigned)free_heap);
}

}  // namespace esp_video
}  // namespace esphome
