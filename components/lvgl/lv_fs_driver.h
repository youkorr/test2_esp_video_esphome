#pragma once

#include "esphome/core/component.h"
#include "lvgl.h"
#include <cstdio>

namespace esphome {
namespace lvgl {

/**
 * LVGL Filesystem Driver for ESP-IDF VFS
 *
 * Provides LVGL access to SD card files via the 'S:' drive letter.
 * Maps S:/ to /sdcard/ mount point.
 *
 * Required for loading Lottie animations and SVG images from SD card.
 */
class LvglFsDriver {
 public:
  static void init();

 private:
  // LVGL filesystem callbacks
  static void *fs_open_cb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode);
  static lv_fs_res_t fs_close_cb(lv_fs_drv_t *drv, void *file_p);
  static lv_fs_res_t fs_read_cb(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br);
  static lv_fs_res_t fs_seek_cb(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence);
  static lv_fs_res_t fs_tell_cb(lv_fs_drv_t *drv, void *file_p, uint32_t *pos);

  static const char *MOUNT_POINT;
};

}  // namespace lvgl
}  // namespace esphome
