#include "esp_video_component.h"
#include "i2c_helper.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

extern "C" {
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#ifdef ESP_VIDEO_ISP_ENABLED
bool esp_video_isp_pipeline_is_initialized(void);
#endif
}

namespace esphome {
namespace esp_video {

static const char *const TAG = "esp_video";

struct esp_video_init_params_t {
  esp_video_init_config_t *video_config;
  esp_err_t result;
  SemaphoreHandle_t done_semaphore;
};

static void esp_video_init_task_core0(void *param) {
  esp_video_init_params_t *params = (esp_video_init_params_t *)param;
  ESP_LOGI(TAG, "esp_video_init() running on core %d", xPortGetCoreID());
  params->result = esp_video_init(params->video_config);
  xSemaphoreGive(params->done_semaphore);
  vTaskDelete(NULL);
}

static esp_err_t init_xclk_ledc(gpio_num_t gpio_num, uint32_t freq_hz) {
  ESP_LOGI(TAG, "Initializing XCLK via LEDC on GPIO%d @ %u Hz", gpio_num, freq_hz);

  ledc_timer_config_t timer_conf = {};
  timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_conf.timer_num = LEDC_TIMER_0;
  timer_conf.duty_resolution = LEDC_TIMER_1_BIT;
  timer_conf.freq_hz = freq_hz;
  timer_conf.clk_cfg = LEDC_AUTO_CLK;

  esp_err_t ret = ledc_timer_config(&timer_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ledc_channel_config_t ch_conf = {};
  ch_conf.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_conf.channel = LEDC_CHANNEL_0;
  ch_conf.timer_sel = LEDC_TIMER_0;
  ch_conf.intr_type = LEDC_INTR_DISABLE;
  ch_conf.gpio_num = gpio_num;
  ch_conf.duty = 1;
  ch_conf.hpoint = 0;

  ret = ledc_channel_config(&ch_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "XCLK OK: GPIO%d @ %u Hz", gpio_num, freq_hz);
  return ESP_OK;
}

void ESPVideoComponent::setup() {
#ifdef CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE

  // ---- Memory check ----
  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  if (free_heap < 512 * 1024) {
    ESP_LOGW(TAG, "Low memory: %u bytes free (recommend > 512 KB)", (unsigned)free_heap);
  }

  // ---- Build SCCB / I2C config ----
  esp_video_init_csi_config_t csi_config = {};

  if (this->init_sccb_) {
    // ========== DEDICATED MODE ==========
    // Camera manages its own I2C bus — no dependency on ESPHome I2C internals.
    ESP_LOGI(TAG, "I2C mode: dedicated (init_sccb=true, port=%u, SDA=%d, SCL=%d, freq=%u)",
             this->sccb_port_, this->sda_pin_, this->scl_pin_, this->sccb_freq_);

    csi_config.sccb_config.init_sccb = true;
    csi_config.sccb_config.i2c_config.port = this->sccb_port_;
    csi_config.sccb_config.i2c_config.sda_pin = this->sda_pin_;
    csi_config.sccb_config.i2c_config.scl_pin = this->scl_pin_;
    csi_config.sccb_config.freq = this->sccb_freq_;
  } else {
    // ========== SHARED-BUS MODE ==========
    // Reuse the I2C bus already created by ESPHome.
    if (this->i2c_bus_ == nullptr) {
      ESP_LOGE(TAG, "i2c_bus is nullptr — check i2c_id in YAML");
      this->mark_failed();
      return;
    }

    i2c_master_bus_handle_t i2c_handle =
        get_i2c_bus_handle(this->i2c_bus_, this->i2c_port_);
    if (i2c_handle == nullptr) {
      ESP_LOGE(TAG,
               "Cannot obtain I2C bus handle. "
               "Upgrade to dedicated mode: replace 'i2c_id' with "
               "'sda_pin' + 'scl_pin' in your esp_video config.");
      this->mark_failed();
      return;
    }

    ESP_LOGI(TAG, "I2C mode: shared (handle=%p)", i2c_handle);

    csi_config.sccb_config.init_sccb = false;
    csi_config.sccb_config.i2c_handle = i2c_handle;
    csi_config.sccb_config.freq = 400000;
  }

  // ---- XCLK ----
  if (this->enable_xclk_init_ && this->xclk_pin_ != (gpio_num_t)-1) {
    ESP_LOGI(TAG, "Initializing XCLK (GPIO%d @ %u Hz)",
             this->xclk_pin_, this->xclk_freq_);

    esp_err_t xclk_ret = init_xclk_ledc(this->xclk_pin_, this->xclk_freq_);
    if (xclk_ret != ESP_OK) {
      ESP_LOGE(TAG, "XCLK init failed: %s — sensor detection will fail",
               esp_err_to_name(xclk_ret));
      this->mark_failed();
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  } else if (!this->enable_xclk_init_) {
    ESP_LOGI(TAG, "XCLK init disabled — BSP/hardware provides clock");
  }

  // ---- Common CSI fields ----
  csi_config.reset_pin = (gpio_num_t)-1;
  csi_config.pwdn_pin  = (gpio_num_t)-1;
  csi_config.xclk_pin  = this->xclk_pin_;
  csi_config.xclk_freq = this->xclk_freq_;

  esp_video_init_config_t video_config = {};
  video_config.csi = &csi_config;

  // ---- Run esp_video_init() on core 0 ----
  SemaphoreHandle_t done_sem = xSemaphoreCreateBinary();
  if (done_sem == NULL) {
    ESP_LOGE(TAG, "Failed to create semaphore");
    this->mark_failed();
    return;
  }

  esp_video_init_params_t params = {};
  params.video_config = &video_config;
  params.done_semaphore = done_sem;

  TaskHandle_t task_handle = NULL;
  BaseType_t task_created = xTaskCreatePinnedToCore(
      esp_video_init_task_core0, "esp_video_init",
      8192, &params, 5, &task_handle, 0);

  if (task_created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create init task on core 0");
    vSemaphoreDelete(done_sem);
    this->mark_failed();
    return;
  }

  if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(10000)) != pdTRUE) {
    ESP_LOGE(TAG, "esp_video_init() timed out (10 s)");
    vSemaphoreDelete(done_sem);
    this->mark_failed();
    return;
  }
  vSemaphoreDelete(done_sem);

  esp_err_t ret = params.result;
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_init() failed: %d (%s)", ret, esp_err_to_name(ret));
    this->mark_failed();
    return;
  }

  // ---- Verify video devices ----
  int fd = open("/dev/video0", O_RDWR);
  if (fd >= 0) {
    ESP_LOGW(TAG, "/dev/video0 OK (CSI camera detected)");
    close(fd);
  } else {
    ESP_LOGW(TAG, "/dev/video0 not found — sensor detection may have failed");
  }

  fd = open("/dev/video10", O_RDWR);
  if (fd >= 0) {
    ESP_LOGW(TAG, "/dev/video10 OK (JPEG encoder)");
    close(fd);
  }

  fd = open("/dev/video20", O_RDWR);
  if (fd >= 0) {
    ESP_LOGW(TAG, "/dev/video20 OK (ISP device)");
    close(fd);
  }

#ifdef ESP_VIDEO_ISP_ENABLED
  bool isp_initialized = esp_video_isp_pipeline_is_initialized();
  if (!isp_initialized) {
    ESP_LOGW(TAG, "ISP pipeline not initialized — image quality degraded");
  } else {
    ESP_LOGI(TAG, "ISP pipeline active");
  }
#endif

#else
  ESP_LOGW(TAG, "MIPI-CSI disabled — esp_video_init() not called");
#endif

  this->initialized_ = true;
  ESP_LOGI(TAG, "esp-video: ok");
}

void ESPVideoComponent::loop() {}

void ESPVideoComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP-Video Component:");

#ifdef ESP_VIDEO_VERSION
  ESP_LOGCONFIG(TAG, "  Version: %s", ESP_VIDEO_VERSION);
#endif

  ESP_LOGCONFIG(TAG, "  Status: %s", this->initialized_ ? "Ready" : "Not initialized");

  if (this->init_sccb_) {
    ESP_LOGCONFIG(TAG, "  I2C mode: dedicated (init_sccb=true)");
    ESP_LOGCONFIG(TAG, "    SDA: GPIO%d, SCL: GPIO%d, port: %u",
                  this->sda_pin_, this->scl_pin_, this->sccb_port_);
  } else {
    ESP_LOGCONFIG(TAG, "  I2C mode: shared (ESPHome bus %p)", this->i2c_bus_);
  }

#ifdef ESP_VIDEO_JPEG_ENABLED
  ESP_LOGCONFIG(TAG, "  JPEG: hardware encoder");
#endif
#ifdef ESP_VIDEO_ISP_ENABLED
  ESP_LOGCONFIG(TAG, "  ISP: enabled");
#endif
#ifdef CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
  ESP_LOGCONFIG(TAG, "  Interface: MIPI-CSI");
#endif

  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  ESP_LOGCONFIG(TAG, "  Free heap: %u bytes", (unsigned)free_heap);
}

}  // namespace esp_video
}  // namespace esphome
