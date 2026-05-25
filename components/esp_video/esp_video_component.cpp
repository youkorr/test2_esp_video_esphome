#include "esp_video_component.h"
#include "i2c_helper.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

// Headers ESP-Video
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

  ESP_LOGI(TAG, "XCLK initialized on GPIO%d @ %u Hz", gpio_num, freq_hz);
  return ESP_OK;
}

void ESPVideoComponent::setup() {
  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  if (free_heap < 512 * 1024) {
    ESP_LOGW(TAG, "Low memory: %u bytes free", (unsigned)free_heap);
  }

#ifdef CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
  // Initialize XCLK if enabled
  if (this->enable_xclk_init_ && this->xclk_pin_ != (gpio_num_t)-1) {
    esp_err_t xclk_ret = init_xclk_ledc(this->xclk_pin_, this->xclk_freq_);
    if (xclk_ret != ESP_OK) {
      ESP_LOGE(TAG, "XCLK initialization failed - sensor detection will fail");
      this->mark_failed();
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  // Get I2C bus handle via ESP-IDF API
  i2c_master_bus_handle_t i2c_handle = get_i2c_bus_handle_by_port(this->i2c_port_);

  esp_video_init_csi_config_t csi_config = {};

  if (i2c_handle != nullptr) {
    // Use the existing I2C bus handle from ESPHome
    ESP_LOGI(TAG, "Using existing I2C bus (port %d, handle %p)", this->i2c_port_, i2c_handle);
    csi_config.sccb_config.init_sccb = false;
    csi_config.sccb_config.i2c_handle = i2c_handle;
  } else {
    // Fallback: let esp_video create its own I2C bus
    ESP_LOGW(TAG, "Could not get I2C bus handle for port %d, creating new bus (SDA=%d, SCL=%d)",
             this->i2c_port_, this->sda_pin_, this->scl_pin_);
    csi_config.sccb_config.init_sccb = true;
    csi_config.sccb_config.i2c_config.port = this->i2c_port_;
    csi_config.sccb_config.i2c_config.sda_pin = this->sda_pin_;
    csi_config.sccb_config.i2c_config.scl_pin = this->scl_pin_;
  }

  csi_config.sccb_config.freq = 400000;
  csi_config.reset_pin = (gpio_num_t)-1;
  csi_config.pwdn_pin = (gpio_num_t)-1;
  csi_config.xclk_pin = this->xclk_pin_;
  csi_config.xclk_freq = this->xclk_freq_;

  esp_video_init_config_t video_config = {};
  video_config.csi = &csi_config;

  // Run esp_video_init on core 0 (hardware requirement for ESP32-P4)
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
      esp_video_init_task_core0,
      "esp_video_init",
      8192,
      &params,
      5,
      &task_handle,
      0
  );

  if (task_created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create esp_video_init task on core 0");
    vSemaphoreDelete(done_sem);
    this->mark_failed();
    return;
  }

  if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(10000)) != pdTRUE) {
    ESP_LOGE(TAG, "esp_video_init() timed out after 10 seconds");
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

  // Check video devices
  int fd = open("/dev/video0", O_RDWR);
  if (fd >= 0) {
    ESP_LOGI(TAG, "/dev/video0 available (camera sensor detected)");
    close(fd);
  } else {
    ESP_LOGW(TAG, "/dev/video0 not available - sensor detection may have failed");
  }

  fd = open("/dev/video10", O_RDWR);
  if (fd >= 0) {
    ESP_LOGI(TAG, "/dev/video10 available (JPEG encoder)");
    close(fd);
  }

  fd = open("/dev/video20", O_RDWR);
  if (fd >= 0) {
    ESP_LOGI(TAG, "/dev/video20 available (ISP device)");
    close(fd);
  }

#ifdef ESP_VIDEO_ISP_ENABLED
  if (esp_video_isp_pipeline_is_initialized()) {
    ESP_LOGI(TAG, "ISP Pipeline active");
  } else {
    ESP_LOGW(TAG, "ISP Pipeline NOT initialized - image quality may be degraded");
  }
#endif
#endif  // CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE

  this->initialized_ = true;
  ESP_LOGI(TAG, "esp-video: ok");
}

void ESPVideoComponent::loop() {
}

void ESPVideoComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP-Video Component:");
  ESP_LOGCONFIG(TAG, "  Status: %s", this->initialized_ ? "Ready" : "Failed");
  ESP_LOGCONFIG(TAG, "  I2C Port: %d (SDA=%d, SCL=%d)", this->i2c_port_, this->sda_pin_, this->scl_pin_);

#ifdef ESP_VIDEO_JPEG_ENABLED
  ESP_LOGCONFIG(TAG, "  JPEG: Hardware encoder enabled");
#endif
#ifdef ESP_VIDEO_ISP_ENABLED
  ESP_LOGCONFIG(TAG, "  ISP: Enabled");
#endif
#ifdef CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
  ESP_LOGCONFIG(TAG, "  Interface: MIPI-CSI");
#endif
}

}  // namespace esp_video
}  // namespace esphome
