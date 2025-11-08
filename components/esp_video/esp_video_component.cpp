#include "esp_video_component.h"
#include "i2c_helper.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"

// Headers ESP-Video
extern "C" {
#include "esp_video_init.h"
#include "esp_video_device.h"

// Forward declaration for ISP pipeline check
#ifdef ESP_VIDEO_ISP_ENABLED
bool esp_video_isp_pipeline_is_initialized(void);
#endif
}

namespace esphome {
namespace esp_video {

static const char *const TAG = "esp_video";

void ESPVideoComponent::setup() {
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  ESP-Video Component Initialization");
  ESP_LOGI(TAG, "========================================");

#ifdef ESP_VIDEO_VERSION
  ESP_LOGI(TAG, "Version: %s (XCLK Support Enabled)", ESP_VIDEO_VERSION);
#else
  ESP_LOGI(TAG, "Version: 2025-11-08 (XCLK Support Enabled)");
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
  // Vérifier que le bus I2C ESPHome est fourni
  if (this->i2c_bus_ == nullptr) {
    ESP_LOGE(TAG, "❌ Bus I2C non fourni! Vérifiez la configuration i2c_id");
    this->mark_failed();
    return;
  }

  // Extraire le handle I2C ESP-IDF depuis le bus ESPHome
  ESP_LOGI(TAG, "Configuration esp_video:");
  ESP_LOGI(TAG, "  init_sccb: false (utilise le bus I2C ESPHome)");
  ESP_LOGI(TAG, "  Setup priority: DATA (après I2C BUS:1000)");

  i2c_master_bus_handle_t i2c_handle = get_i2c_bus_handle(this->i2c_bus_);
  if (i2c_handle == nullptr) {
    ESP_LOGE(TAG, "❌ Impossible d'extraire le handle I2C ESP-IDF");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "  ✓ Handle I2C ESP-IDF récupéré: %p", i2c_handle);

  esp_video_init_csi_config_t csi_config = {};

  // Ne PAS initialiser SCCB - utiliser le bus I2C ESPHome existant
  csi_config.sccb_config.init_sccb = false;

  // Utiliser i2c_handle (union) car init_sccb = false
  csi_config.sccb_config.i2c_handle = i2c_handle;
  csi_config.sccb_config.freq = 400000;  // Fréquence I2C

  csi_config.reset_pin = (gpio_num_t)-1;  // Pas de pin de reset
  csi_config.pwdn_pin = (gpio_num_t)-1;   // Pas de pin de power-down

  // Configure XCLK for sensor detection (required for MIPI-CSI sensors!)
  // CRITICAL: Sensors need XCLK active to respond on I2C during detection
  csi_config.xclk_pin = this->xclk_pin_;      // XCLK pin (default: GPIO36)
  csi_config.xclk_freq = this->xclk_freq_;    // XCLK frequency (default: 24MHz)

  esp_video_init_config_t video_config = {};
  video_config.csi = &csi_config;

  ESP_LOGI(TAG, "Appel esp_video_init() avec handle I2C ESPHome...");
  esp_err_t ret = esp_video_init(&video_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ Échec esp_video_init(): %d (%s)", ret, esp_err_to_name(ret));
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "✅ esp_video_init() réussi - Devices vidéo prêts (bus I2C partagé)");

  // Vérifier si l'ISP pipeline est initialisé
#ifdef ESP_VIDEO_ISP_ENABLED
  bool isp_initialized = esp_video_isp_pipeline_is_initialized();
  ESP_LOGI(TAG, "🔍 ISP Pipeline status: %s", isp_initialized ? "INITIALIZED ✅" : "NOT INITIALIZED ❌");

  if (!isp_initialized) {
    ESP_LOGW(TAG, "⚠️  ISP Pipeline NOT initialized despite enable_isp: true");
    ESP_LOGW(TAG, "   This means IPA algorithms (AWB, sharpen, etc) are NOT active");
    ESP_LOGW(TAG, "   Image quality will be degraded (blanc→vert, pas net, etc)");
  } else {
    ESP_LOGI(TAG, "✅ ISP Pipeline active - IPA algorithms running");
  }
#else
  ESP_LOGW(TAG, "⚠️  ISP not enabled in configuration");
#endif
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
  ESP_LOGCONFIG(TAG, "  I2C: Bus ESPHome partagé (%p)", this->i2c_bus_);

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
