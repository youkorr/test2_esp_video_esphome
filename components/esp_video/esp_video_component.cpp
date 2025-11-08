#include "esp_video_component.h"
#include "i2c_helper.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"
#include <sys/stat.h>
#include <unistd.h>

// Headers ESP-Video
extern "C" {
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "driver/ledc.h"  // For XCLK generation via LEDC (like M5Stack does)

// Forward declaration for ISP pipeline check
#ifdef ESP_VIDEO_ISP_ENABLED
bool esp_video_isp_pipeline_is_initialized(void);
#endif
}

namespace esphome {
namespace esp_video {

static const char *const TAG = "esp_video";

/**
 * @brief Initialize camera XCLK using LEDC (like M5Stack Tab5 does)
 *
 * CRITICAL: For MIPI-CSI sensors on ESP32-P4, esp_video_init() does NOT initialize XCLK!
 * XCLK initialization only happens for DVP sensors in esp_video_init.c.
 *
 * For MIPI-CSI, we must initialize XCLK BEFORE calling esp_video_init(), otherwise
 * the sensor will not respond on I2C during detection (PID=0x0).
 *
 * This matches M5Stack's approach in bsp_cam_osc_init() which uses LEDC to generate
 * 24 MHz clock on GPIO 36.
 *
 * @param gpio_num GPIO pin for XCLK output
 * @param freq_hz XCLK frequency in Hz (typically 24000000 for MIPI-CSI sensors)
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t init_xclk_ledc(gpio_num_t gpio_num, uint32_t freq_hz) {
  ESP_LOGI(TAG, "🔧 Initializing XCLK via LEDC on GPIO%d @ %u Hz", gpio_num, freq_hz);

  // Configure LEDC timer for XCLK generation (matching M5Stack's implementation)
  ledc_timer_config_t timer_conf = {};
  timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_conf.timer_num = LEDC_TIMER_0;
  timer_conf.duty_resolution = LEDC_TIMER_1_BIT;  // 1-bit resolution for 50% duty cycle
  timer_conf.freq_hz = freq_hz;
  timer_conf.clk_cfg = LEDC_AUTO_CLK;

  esp_err_t ret = ledc_timer_config(&timer_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ LEDC timer config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  // Configure LEDC channel to output XCLK on the specified GPIO
  ledc_channel_config_t ch_conf = {};
  ch_conf.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_conf.channel = LEDC_CHANNEL_0;
  ch_conf.timer_sel = LEDC_TIMER_0;
  ch_conf.intr_type = LEDC_INTR_DISABLE;
  ch_conf.gpio_num = gpio_num;
  ch_conf.duty = 1;  // 50% duty cycle (1 out of 2^1 = 2)
  ch_conf.hpoint = 0;

  ret = ledc_channel_config(&ch_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ LEDC channel config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "✅ XCLK initialized successfully via LEDC");
  ESP_LOGI(TAG, "   → GPIO%d now outputs %u Hz clock signal", gpio_num, freq_hz);
  ESP_LOGI(TAG, "   → Sensor can now respond on I2C during detection");

  return ESP_OK;
}

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

  // CRITICAL: Initialize XCLK BEFORE calling esp_video_init()!
  // For MIPI-CSI sensors, esp_video_init() does NOT initialize XCLK (only for DVP).
  // Without XCLK active, the sensor will NOT respond on I2C → detection fails (PID=0x0)
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  Initializing XCLK (BEFORE esp_video_init)");
  ESP_LOGI(TAG, "========================================");

  esp_err_t xclk_ret = init_xclk_ledc(this->xclk_pin_, this->xclk_freq_);
  if (xclk_ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ XCLK initialization failed!");
    ESP_LOGE(TAG, "   Sensor will NOT respond on I2C without XCLK");
    this->mark_failed();
    return;
  }

  // CRITICAL: Wait for sensor to stabilize after XCLK starts
  // Camera sensors need time to power up and initialize internal logic after XCLK becomes active
  ESP_LOGI(TAG, "⏳ Waiting 100ms for sensor to stabilize...");
  delay(100);
  ESP_LOGI(TAG, "✅ Sensor should be ready for I2C communication");

  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  Calling esp_video_init()");
  ESP_LOGI(TAG, "========================================");

  esp_video_init_csi_config_t csi_config = {};

  // Ne PAS initialiser SCCB - utiliser le bus I2C ESPHome existant
  csi_config.sccb_config.init_sccb = false;

  // Utiliser i2c_handle (union) car init_sccb = false
  csi_config.sccb_config.i2c_handle = i2c_handle;
  csi_config.sccb_config.freq = 400000;  // Fréquence I2C

  csi_config.reset_pin = (gpio_num_t)-1;  // Pas de pin de reset
  csi_config.pwdn_pin = (gpio_num_t)-1;   // Pas de pin de power-down

  // NOTE: xclk_pin and xclk_freq are NOT used by esp_video_init() for MIPI-CSI!
  // XCLK initialization only happens for DVP sensors in esp_video_init.c.
  // For MIPI-CSI, XCLK must be initialized BEFORE calling esp_video_init(),
  // which we did above using init_xclk_ledc().
  // Setting these fields here for documentation/completeness only:
  csi_config.xclk_pin = this->xclk_pin_;      // IGNORED for MIPI-CSI
  csi_config.xclk_freq = this->xclk_freq_;    // IGNORED for MIPI-CSI

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

  // Vérifier quels devices vidéo ont été créés
  ESP_LOGW(TAG, "🔍 Vérification des devices vidéo créés:");
  struct stat st;
  if (stat("/dev/video0", &st) == 0) {
    ESP_LOGW(TAG, "   ✅ /dev/video0 existe (CSI video device - capteur détecté!)");
  } else {
    ESP_LOGW(TAG, "   ❌ /dev/video0 N'EXISTE PAS (capteur NON détecté!)");
    ESP_LOGW(TAG, "      Cela signifie que la détection du capteur a échoué dans esp_video_init()");
  }
  if (stat("/dev/video10", &st) == 0) {
    ESP_LOGW(TAG, "   ✅ /dev/video10 existe (JPEG encoder)");
  }
  if (stat("/dev/video11", &st) == 0) {
    ESP_LOGW(TAG, "   ✅ /dev/video11 existe (H.264 encoder)");
  }
  if (stat("/dev/video20", &st) == 0) {
    ESP_LOGW(TAG, "   ✅ /dev/video20 existe (ISP device)");
  }

  // Tenter de lire l'ID du capteur directement via I2C pour vérifier que XCLK fonctionne
  ESP_LOGW(TAG, "🔍 Test direct I2C du capteur SC202CS (addr 0x36):");
  uint8_t sensor_id_high = 0, sensor_id_low = 0;

  // SC202CS: Chip ID register high byte at 0x3107, low byte at 0x3108
  // Expected ID: 0xEB52 (SC202CS_PID from sc202cs.c)
  esp_err_t err_h = i2c_read_register(i2c_handle, 0x36, 0x3107, &sensor_id_high);
  esp_err_t err_l = i2c_read_register(i2c_handle, 0x36, 0x3108, &sensor_id_low);

  if (err_h == ESP_OK && err_l == ESP_OK) {
    uint16_t chip_id = (sensor_id_high << 8) | sensor_id_low;
    ESP_LOGW(TAG, "   ✅ I2C lecture réussie: Chip ID = 0x%04X (attendu: 0xEB52 pour SC202CS)", chip_id);
    if (chip_id == 0xEB52) {
      ESP_LOGW(TAG, "      ✅ SC202CS identifié correctement - XCLK fonctionne!");
    } else if (chip_id == 0x0000 || chip_id == 0xFFFF) {
      ESP_LOGW(TAG, "      ❌ ID invalide - XCLK probablement inactif ou capteur déconnecté");
    } else {
      ESP_LOGW(TAG, "      ⚠️  ID inattendu (0x%04X) - possible autre capteur", chip_id);
      // Liste des IDs connus:
      // 0xEB52 = SC202CS
      // 0x5647 = OV5647
      // 0x0C10 = OV02C10
    }
  } else {
    ESP_LOGW(TAG, "   ❌ I2C lecture échouée (err_h=%d, err_l=%d)", err_h, err_l);
    ESP_LOGW(TAG, "      Causes possibles:");
    ESP_LOGW(TAG, "      1. XCLK non initialisé/inactif");
    ESP_LOGW(TAG, "      2. Mauvaise adresse I2C");
    ESP_LOGW(TAG, "      3. Capteur pas alimenté/connecté");
  }

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
