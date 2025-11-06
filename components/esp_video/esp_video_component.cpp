#include "esp_video_component.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"

// ESP-Video C headers
extern "C" {
#include "esp_video_init.h"
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"
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

  // Initialiser le régulateur LDO si configuré
  if (this->use_ldo_) {
    ESP_LOGI(TAG, "Configuration LDO: %.1fV sur canal %d", this->ldo_voltage_, this->ldo_channel_);

    esp_ldo_channel_handle_t ldo_handle = NULL;
    esp_ldo_channel_config_t ldo_config = {
      .chan_id = this->ldo_channel_,
      .voltage_mv = static_cast<int>(this->ldo_voltage_ * 1000),  // Convertir V en mV
      .flags = {
        .adjustable_range_mv = 500,  // Plage d'ajustement de ±500mV
      },
    };

    esp_err_t ldo_ret = esp_ldo_acquire_channel(&ldo_config, &ldo_handle);
    if (ldo_ret != ESP_OK) {
      ESP_LOGW(TAG, "⚠️  Échec de la configuration LDO: 0x%x (%s)", ldo_ret, esp_err_to_name(ldo_ret));
      ESP_LOGW(TAG, "    Continuons sans LDO...");
    } else {
      ESP_LOGI(TAG, "✓ LDO configuré avec succès");
      // Note: Le handle LDO reste actif pendant toute la durée de vie du composant
    }
  }

  // Initialiser le pipeline ESP-Video avec configuration MIPI-CSI
  ESP_LOGI(TAG, "Initialisation du pipeline ESP-Video...");
  ESP_LOGI(TAG, "Configuration I2C:");
  ESP_LOGI(TAG, "  Port: %d", this->i2c_port_);
  ESP_LOGI(TAG, "  SDA: GPIO%d", this->i2c_sda_pin_);
  ESP_LOGI(TAG, "  SCL: GPIO%d", this->i2c_scl_pin_);
  ESP_LOGI(TAG, "  Fréquence: %u Hz", this->i2c_frequency_);
  ESP_LOGI(TAG, "  Adresse capteur: 0x%02X", this->sensor_address_);

#if CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
  // Configuration SCCB (I2C pour le capteur de caméra)
  // Utilise les valeurs configurées dans le YAML
  const esp_video_init_csi_config_t csi_config = {
    .sccb_config = {
      .init_sccb = true,
      .i2c_config = {
        .port = this->i2c_port_,
        .scl_pin = static_cast<gpio_num_t>(this->i2c_scl_pin_),
        .sda_pin = static_cast<gpio_num_t>(this->i2c_sda_pin_),
      },
      .freq = this->i2c_frequency_,
    },
    .reset_pin = (this->reset_pin_ >= 0) ? static_cast<gpio_num_t>(this->reset_pin_) : GPIO_NUM_NC,
    .pwdn_pin = (this->pwdn_pin_ >= 0) ? static_cast<gpio_num_t>(this->pwdn_pin_) : GPIO_NUM_NC,
  };

  const esp_video_init_config_t video_config = {
    .csi = &csi_config,
#if CONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE
    .jpeg = nullptr,  // Laisser le driver JPEG gérer son propre handle
#endif
  };

  esp_err_t ret = esp_video_init(&video_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ Échec de l'initialisation ESP-Video: 0x%x (%s)", ret, esp_err_to_name(ret));
    ESP_LOGE(TAG, "   Vérifiez les pins I2C et la configuration du capteur");
    this->initialized_ = false;
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "✓ Pipeline ESP-Video initialisé");
  ESP_LOGI(TAG, "  Devices vidéo créés:");
  ESP_LOGI(TAG, "    - MIPI-CSI: /dev/video0");
#if CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE
  ESP_LOGI(TAG, "    - ISP: /dev/video20");
#endif
#if CONFIG_ESP_VIDEO_ENABLE_JPEG_VIDEO_DEVICE
  ESP_LOGI(TAG, "    - JPEG: /dev/video10");
#endif
#if CONFIG_ESP_VIDEO_ENABLE_H264_VIDEO_DEVICE
  ESP_LOGI(TAG, "    - H.264: /dev/video11");
#endif
#else
  ESP_LOGW(TAG, "⚠️  MIPI-CSI désactivé, aucun pipeline vidéo créé");
#endif

  this->initialized_ = true;

  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "✅ ESP-Video initialisé avec succès");
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

  ESP_LOGCONFIG(TAG, "  État: %s", this->initialized_ ? "Initialisé" : "Non initialisé");
  
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
