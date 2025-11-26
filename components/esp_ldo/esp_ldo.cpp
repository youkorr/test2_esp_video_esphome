#include "esp_ldo.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF

#include <esp_private/esp_ldo.h>

namespace esphome {
namespace esp_ldo {

static const char *const TAG = "esp_ldo";

void EspLdo::setup() {
  esp_ldo_unit_handle_t ldo_unit = nullptr;
  esp_ldo_channel_config_t ldo_cfg = {
      .chan_id = this->channel_id_, .voltage_mv = this->voltage_mv_, .flags = {.adjustable_ldo = false}};

  if (esp_ldo_acquire_channel(&ldo_cfg, &ldo_unit) != ESP_OK) {
    this->mark_failed("Failed to acquire LDO channel");
    return;
  }

  ESP_LOGCONFIG(TAG, "LDO channel %d initialized at %d mV", this->channel_id_, this->voltage_mv_);
}

void EspLdo::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP LDO:");
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_id_);
  ESP_LOGCONFIG(TAG, "  Voltage: %d mV", this->voltage_mv_);
}

}  // namespace esp_ldo
}  // namespace esphome

#endif  // USE_ESP_IDF
