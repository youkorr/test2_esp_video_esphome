#pragma once

#include "esphome/core/log.h"
#include <cstdio>
#include <sys/stat.h>

namespace esphome {
namespace lvgl {

/**
 * Helper functions for Lottie debugging
 */

static const char *const LOTTIE_TAG = "lvgl.lottie";

/**
 * Check if a file exists and is readable
 */
inline bool lottie_file_exists(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) {
    ESP_LOGE(LOTTIE_TAG, "File does not exist: %s", path);
    return false;
  }

  if (!S_ISREG(st.st_mode)) {
    ESP_LOGE(LOTTIE_TAG, "Path is not a regular file: %s", path);
    return false;
  }

  ESP_LOGI(LOTTIE_TAG, "File found: %s (size: %ld bytes)", path, st.st_size);

  // Try to open for reading
  FILE *f = fopen(path, "r");
  if (f == nullptr) {
    ESP_LOGE(LOTTIE_TAG, "Cannot open file for reading: %s", path);
    return false;
  }

  fclose(f);
  ESP_LOGI(LOTTIE_TAG, "File is readable: %s", path);
  return true;
}

}  // namespace lvgl
}  // namespace esphome
