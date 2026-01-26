#include "lv_fs_driver.h"
#include "esphome/core/log.h"
#include <sys/stat.h>
#include <cstring>

namespace esphome {
namespace lvgl {

static const char *const TAG = "lvgl.fs";

const char *LvglFsDriver::MOUNT_POINT = "/sdcard";

void LvglFsDriver::init() {
  static lv_fs_drv_t fs_drv;
  lv_fs_drv_init(&fs_drv);

  // Register 'S' drive letter (S:/ = SD card)
  fs_drv.letter = 'S';
  fs_drv.cache_size = 0;  // No caching

  // Register callbacks
  fs_drv.open_cb = fs_open_cb;
  fs_drv.close_cb = fs_close_cb;
  fs_drv.read_cb = fs_read_cb;
  fs_drv.seek_cb = fs_seek_cb;
  fs_drv.tell_cb = fs_tell_cb;

  lv_fs_drv_register(&fs_drv);

  ESP_LOGI(TAG, "LVGL filesystem driver registered: S:/ -> %s/", MOUNT_POINT);
}

void *LvglFsDriver::fs_open_cb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode) {
  // Map S:/path to /sdcard/path
  std::string full_path = std::string(MOUNT_POINT) + "/" + path;

  const char *mode_str = "rb";  // Default: read binary
  if (mode == LV_FS_MODE_WR) {
    mode_str = "wb";
  } else if (mode == LV_FS_MODE_RD | LV_FS_MODE_WR) {
    mode_str = "r+b";
  }

  FILE *file = fopen(full_path.c_str(), mode_str);
  if (file == nullptr) {
    ESP_LOGW(TAG, "Failed to open file: %s (mode=%s)", full_path.c_str(), mode_str);
    return nullptr;
  }

  ESP_LOGD(TAG, "Opened file: %s", full_path.c_str());
  return (void *) file;
}

lv_fs_res_t LvglFsDriver::fs_close_cb(lv_fs_drv_t *drv, void *file_p) {
  FILE *file = (FILE *) file_p;
  if (file != nullptr) {
    fclose(file);
  }
  return LV_FS_RES_OK;
}

lv_fs_res_t LvglFsDriver::fs_read_cb(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br) {
  FILE *file = (FILE *) file_p;
  if (file == nullptr) {
    *br = 0;
    return LV_FS_RES_UNKNOWN;
  }

  *br = fread(buf, 1, btr, file);
  return (*br == btr) ? LV_FS_RES_OK : LV_FS_RES_OK;  // Return OK even if EOF reached
}

lv_fs_res_t LvglFsDriver::fs_seek_cb(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence) {
  FILE *file = (FILE *) file_p;
  if (file == nullptr) {
    return LV_FS_RES_UNKNOWN;
  }

  int whence_std = SEEK_SET;
  if (whence == LV_FS_SEEK_CUR) {
    whence_std = SEEK_CUR;
  } else if (whence == LV_FS_SEEK_END) {
    whence_std = SEEK_END;
  }

  int result = fseek(file, pos, whence_std);
  return (result == 0) ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

lv_fs_res_t LvglFsDriver::fs_tell_cb(lv_fs_drv_t *drv, void *file_p, uint32_t *pos) {
  FILE *file = (FILE *) file_p;
  if (file == nullptr) {
    *pos = 0;
    return LV_FS_RES_UNKNOWN;
  }

  *pos = ftell(file);
  return LV_FS_RES_OK;
}

}  // namespace lvgl
}  // namespace esphome
