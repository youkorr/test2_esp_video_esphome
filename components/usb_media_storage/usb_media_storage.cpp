#include "usb_media_storage.h"
#include "esp_task_wdt.h"

#include <algorithm>
#include <vector>
#include <cstdio>
#include <sys/stat.h>
#include <cmath>

#include "esphome/core/log.h"

#ifdef USE_ESP_IDF
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "driver/gpio.h"
#include "esp_private/usb_phy.h"
#include <dirent.h>
#include <errno.h>
#endif

namespace esphome {
namespace usb_media_storage {

static const char *TAG = "usb_media_storage";

#ifdef USE_ESP_IDF
static const size_t FILE_PATH_MAX = ESP_VFS_PATH_MAX + 256;

static SemaphoreHandle_t s_device_connect_sem = NULL;
static uint8_t s_device_address = 0;

static void msc_event_callback(const msc_host_event_t *event, void *arg) {
  switch (event->event) {
    case msc_host_event_t::MSC_DEVICE_CONNECTED:
      ESP_LOGI(TAG, "MSC device connected, address: %d", event->device.address);
      s_device_address = event->device.address;
      if (s_device_connect_sem) {
        xSemaphoreGive(s_device_connect_sem);
      }
      break;
    case msc_host_event_t::MSC_DEVICE_DISCONNECTED:
      ESP_LOGW(TAG, "MSC device disconnected");
      break;
    default:
      break;
  }
}
#endif

const std::string UsbMediaStorage::MOUNT_POINT("/usb");

FileInfo::FileInfo(std::string const &path, size_t size, bool is_directory)
    : path(path), size(size), is_directory(is_directory) {}

std::string UsbMediaStorage::build_path(const char *path) const {
  return MOUNT_POINT + path;
}

void UsbMediaStorage::loop() {
  // USB host library events are handled by the dedicated usb_host_task
  // No additional servicing needed here
}

void UsbMediaStorage::dump_config() {
  ESP_LOGCONFIG(TAG, "USB Media Storage");
  ESP_LOGCONFIG(TAG, "  Mount Point: %s", MOUNT_POINT.c_str());

  if (this->mounted_) {
    ESP_LOGCONFIG(TAG, "  Status: Mounted");
  } else {
    ESP_LOGCONFIG(TAG, "  Status: Not mounted");
  }

#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Used space", this->used_space_sensor_);
  LOG_SENSOR("  ", "Total space", this->total_space_sensor_);
  LOG_SENSOR("  ", "Free space", this->free_space_sensor_);
  for (auto &sensor : this->file_size_sensors_) {
    if (sensor.sensor != nullptr)
      LOG_SENSOR("  ", "File size", sensor.sensor);
  }
#endif
#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "USB Status", this->usb_status_text_sensor_);
#endif

  if (this->is_failed()) {
    ESP_LOGE(TAG, "Setup failed: %s", UsbMediaStorage::error_code_to_string(this->init_error_).c_str());
  }
}

#ifdef USE_ESP_IDF

// USB Host library task - runs in background to handle USB events
static void usb_host_task(void *arg) {
  ESP_LOGI(TAG, "USB Host library task started (priority %d)", uxTaskPriorityGet(NULL));
  while (true) {
    uint32_t event_flags;
    esp_err_t err = usb_host_lib_handle_events(pdMS_TO_TICKS(1000), &event_flags);
    if (err == ESP_ERR_TIMEOUT) {
      continue;
    }
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "USB Host lib event error: %s", esp_err_to_name(err));
      continue;
    }
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_LOGI(TAG, "USB Host: no clients connected");
    }
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
      ESP_LOGI(TAG, "USB Host: all devices freed");
    }
  }
}

void UsbMediaStorage::setup() {
  ESP_LOGI(TAG, "Initializing USB Media Storage...");

  // Step 1: Initialize USB PHY for ESP32-P4 USB HS
  usb_phy_config_t phy_config = {
    .controller = USB_PHY_CTRL_OTG,
    .target = USB_PHY_TARGET_INT,
    .otg_mode = USB_OTG_MODE_HOST,
    .otg_speed = USB_PHY_SPEED_UNDEFINED,
  };
  usb_phy_handle_t phy_hdl = NULL;
  esp_err_t ret = usb_new_phy(&phy_config, &phy_hdl);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize USB PHY: %s", esp_err_to_name(ret));
    this->init_error_ = ErrorCode::ERR_USB_HOST_INIT;
    mark_failed();
    return;
  }
  ESP_LOGI(TAG, "USB PHY initialized (Host mode)");

  // Step 2: Install USB Host Library (skip_phy_setup=true since we did it manually)
  const usb_host_config_t host_config = {
    .skip_phy_setup = true,
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };

  ret = usb_host_install(&host_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install USB Host Library: %s", esp_err_to_name(ret));
    this->init_error_ = ErrorCode::ERR_USB_HOST_INIT;
    mark_failed();
    return;
  }
  this->install_attempted_ = true;

  ESP_LOGI(TAG, "USB Host Library installed");

  // Step 3: Create USB host task for event handling
  // Priority must be higher than MSC task to process enumeration first
  BaseType_t task_created = xTaskCreatePinnedToCore(
    usb_host_task,
    "usb_host_task",
    4096,
    NULL,
    10,  // High priority (matches ESP-IDF BSP)
    NULL,
    tskNO_AFFINITY  // Any core
  );

  if (task_created != pdTRUE) {
    ESP_LOGE(TAG, "Failed to create USB host task");
    this->init_error_ = ErrorCode::ERR_USB_HOST_INIT;
    mark_failed();
    return;
  }

  // Step 4: Install MSC Host driver
  const msc_host_driver_config_t msc_config = {
    .create_backround_task = true,
    .task_priority = 5,
    .stack_size = 4096,
    .callback = msc_event_callback,
  };

  ret = msc_host_install(&msc_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install MSC Host driver: %s", esp_err_to_name(ret));
    this->init_error_ = ErrorCode::ERR_MSC_INIT;
    mark_failed();
    return;
  }

  ESP_LOGI(TAG, "MSC Host driver installed");

  // Step 5: Wait for USB device connection via callback
  s_device_connect_sem = xSemaphoreCreateBinary();
  if (!s_device_connect_sem) {
    ESP_LOGE(TAG, "Failed to create semaphore");
    this->init_error_ = ErrorCode::ERR_NO_DEVICE;
    mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Waiting for USB device to be connected (up to 15s)...");
  ESP_LOGI(TAG, "Please ensure USB flash drive is plugged into USB HS Type-C port");
  msc_host_device_handle_t msc_device = NULL;

  if (xSemaphoreTake(s_device_connect_sem, pdMS_TO_TICKS(15000)) == pdTRUE) {
    ESP_LOGI(TAG, "USB device detected at address %d, installing device...", s_device_address);
    ret = msc_host_install_device(s_device_address, &msc_device);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to install MSC device: %s", esp_err_to_name(ret));
      vSemaphoreDelete(s_device_connect_sem);
      s_device_connect_sem = NULL;
      this->init_error_ = ErrorCode::ERR_NO_DEVICE;
      mark_failed();
      return;
    }
    this->device_connected_ = true;
    ESP_LOGI(TAG, "USB device found and connected!");
  } else {
    ESP_LOGE(TAG, "No USB storage device found within 15 seconds");
    ESP_LOGE(TAG, "Check: 1) USB drive plugged into USB HS (not JTAG) port");
    ESP_LOGE(TAG, "Check: 2) Use USB-C to USB-A adapter if drive is USB-A");
    ESP_LOGE(TAG, "Check: 3) USB drive must be FAT32 formatted");
    vSemaphoreDelete(s_device_connect_sem);
    s_device_connect_sem = NULL;
    this->init_error_ = ErrorCode::ERR_NO_DEVICE;
    mark_failed();
    return;
  }

  vSemaphoreDelete(s_device_connect_sem);
  s_device_connect_sem = NULL;

  // Step 6: Mount the USB device filesystem via VFS
  const esp_vfs_fat_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 64,
    .allocation_unit_size = 0,  // Use default
  };

  ret = msc_host_vfs_register(msc_device, MOUNT_POINT.c_str(), &mount_config, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to mount USB filesystem: %s", esp_err_to_name(ret));
    this->init_error_ = ErrorCode::ERR_MOUNT;
    mark_failed();
    return;
  }

  this->mounted_ = true;
  ESP_LOGI(TAG, "USB Media Storage mounted at %s", MOUNT_POINT.c_str());

  // Log filesystem info
  uint64_t total_bytes = 0, free_bytes = 0;
  ret = esp_vfs_fat_info(MOUNT_POINT.c_str(), &total_bytes, &free_bytes);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "  Total: %llu MB", total_bytes / (1024 * 1024));
    ESP_LOGI(TAG, "  Free:  %llu MB", free_bytes / (1024 * 1024));
    ESP_LOGI(TAG, "  Used:  %llu MB", (total_bytes - free_bytes) / (1024 * 1024));
  }

#ifdef USE_TEXT_SENSOR
  if (this->usb_status_text_sensor_ != nullptr) {
    this->usb_status_text_sensor_->publish_state("Mounted");
  }
#endif

  update_sensors();
}

// === File Write Operations ===

void UsbMediaStorage::write_file(const char *path, const uint8_t *buffer, size_t len, const char *mode) {
  if (!this->mounted_) {
    ESP_LOGE(TAG, "USB not mounted, cannot write file");
    return;
  }

  std::string abs_path = build_path(path);
  FILE *file = fopen(abs_path.c_str(), mode);
  if (file == NULL) {
    ESP_LOGE(TAG, "Failed to open file for writing: %s (errno=%d)", path, errno);
    return;
  }

  size_t written = fwrite(buffer, 1, len, file);
  if (written != len) {
    ESP_LOGE(TAG, "Write incomplete: wrote %zu/%zu bytes", written, len);
  }
  fclose(file);
  this->update_sensors();
}

void UsbMediaStorage::write_file(const char *path, const uint8_t *buffer, size_t len) {
  this->write_file(path, buffer, len, "wb");
}

void UsbMediaStorage::append_file(const char *path, const uint8_t *buffer, size_t len) {
  this->write_file(path, buffer, len, "ab");
}

// === File Read Operations ===

std::vector<uint8_t> UsbMediaStorage::read_file(const char *path) {
  if (!this->mounted_) {
    ESP_LOGE(TAG, "USB not mounted, cannot read file");
    return {};
  }

  size_t fsize = this->file_size(path);

  // Safety limit: 10MB for direct read (USB is faster than SD)
  constexpr size_t MAX_SAFE_SIZE = 10 * 1024 * 1024;
  if (fsize > MAX_SAFE_SIZE) {
    ESP_LOGE(TAG, "File too large for direct reading: %zu bytes (max: %zu). Use read_file_stream instead.",
             fsize, MAX_SAFE_SIZE);
    return {};
  }

  std::string abs_path = build_path(path);
  FILE *file = fopen(abs_path.c_str(), "rb");
  if (file == nullptr) {
    ESP_LOGE(TAG, "Failed to open file for reading: %s", path);
    return {};
  }

  std::vector<uint8_t> res(fsize);
  size_t read_len = fread(res.data(), 1, fsize, file);
  fclose(file);

  if (read_len != fsize) {
    ESP_LOGE(TAG, "Read incomplete: expected %zu bytes, got %zu", fsize, read_len);
    return {};
  }

  return res;
}

void UsbMediaStorage::read_file_stream(const char *path, size_t offset, size_t chunk_size,
                                       std::function<void(const uint8_t*, size_t)> callback) {
  if (!this->mounted_) {
    ESP_LOGE(TAG, "USB not mounted, cannot read file");
    return;
  }

  std::string abs_path = build_path(path);
  FILE *file = fopen(abs_path.c_str(), "rb");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file: %s", abs_path.c_str());
    return;
  }

  std::unique_ptr<FILE, decltype(&fclose)> file_guard(file, fclose);

  if (fseek(file, offset, SEEK_SET) != 0) {
    ESP_LOGE(TAG, "Failed to seek to position %zu in file: %s (errno: %d)", offset, abs_path.c_str(), errno);
    return;
  }

  // Use 128KB chunks for USB (faster than SD, can handle larger chunks)
  if (chunk_size == 0) chunk_size = 128 * 1024;

  std::vector<uint8_t> buffer(chunk_size);
  size_t read = 0;
  size_t bytes_since_reset = 0;

  while ((read = fread(buffer.data(), 1, chunk_size, file)) > 0) {
    callback(buffer.data(), read);
    bytes_since_reset += read;

    // Reset watchdog every 128KB
    if (bytes_since_reset >= 128 * 1024) {
      esp_task_wdt_reset();
      bytes_since_reset = 0;
    }
  }

  if (ferror(file)) {
    ESP_LOGE(TAG, "Error reading file: %s", abs_path.c_str());
  }
}

std::vector<uint8_t> UsbMediaStorage::read_file_video(const char *path, size_t max_size) {
  size_t fsize = this->file_size(path);
  if (fsize == 0) {
    ESP_LOGE(TAG, "File not found or empty: %s", path);
    return {};
  }

  size_t bytes_to_read = (max_size > 0 && max_size < fsize) ? max_size : fsize;

  // Check available memory
  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  if (bytes_to_read > free_heap / 2) {
    ESP_LOGE(TAG, "Not enough memory to read video file: need %zu bytes, only %zu available",
             bytes_to_read, free_heap);
    return {};
  }

  ESP_LOGI(TAG, "Reading video file from USB: %s (%zu bytes)", path, bytes_to_read);

  std::vector<uint8_t> result;
  result.reserve(bytes_to_read);

  size_t bytes_read = 0;
  // Use 128KB chunks for USB (faster I/O)
  this->read_file_stream(path, 0, 128 * 1024, [&](const uint8_t* data, size_t len) {
    size_t to_append = len;
    if (max_size > 0 && bytes_read + len > max_size) {
      to_append = max_size - bytes_read;
    }
    if (to_append > 0) {
      result.insert(result.end(), data, data + to_append);
      bytes_read += to_append;
    }
  });

  ESP_LOGI(TAG, "Video file read complete: %zu bytes", result.size());
  return result;
}

// === File Info Operations ===

bool UsbMediaStorage::file_exists(const char *path) {
  if (!this->mounted_) return false;
  std::string abs_path = build_path(path);
  struct stat st;
  return (stat(abs_path.c_str(), &st) == 0);
}

size_t UsbMediaStorage::file_size(const char *path) {
  if (!this->mounted_) return 0;
  std::string abs_path = build_path(path);
  struct stat info;
  if (stat(abs_path.c_str(), &info) < 0) {
    ESP_LOGE(TAG, "Failed to stat file: %s", strerror(errno));
    return 0;
  }
  return info.st_size;
}

// === Directory Operations ===

bool UsbMediaStorage::create_directory(const char *path) {
  if (!this->mounted_) {
    ESP_LOGE(TAG, "USB not mounted");
    return false;
  }
  std::string abs_path = build_path(path);
  if (mkdir(abs_path.c_str(), 0777) < 0) {
    ESP_LOGE(TAG, "Failed to create directory: %s", strerror(errno));
    return false;
  }
  this->update_sensors();
  return true;
}

bool UsbMediaStorage::remove_directory(const char *path) {
  if (!this->mounted_) {
    ESP_LOGE(TAG, "USB not mounted");
    return false;
  }
  if (!this->is_directory(path)) {
    ESP_LOGE(TAG, "Not a directory");
    return false;
  }
  std::string abs_path = build_path(path);
  if (remove(abs_path.c_str()) != 0) {
    ESP_LOGE(TAG, "Failed to remove directory: %s", strerror(errno));
    return false;
  }
  this->update_sensors();
  return true;
}

bool UsbMediaStorage::is_directory(const char *path) {
  if (!this->mounted_) return false;
  std::string abs_path = build_path(path);
  struct stat st;
  if (stat(abs_path.c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return false;
}

bool UsbMediaStorage::delete_file(const char *path) {
  if (!this->mounted_) {
    ESP_LOGE(TAG, "USB not mounted");
    return false;
  }
  if (this->is_directory(path)) {
    ESP_LOGE(TAG, "Not a file");
    return false;
  }
  std::string abs_path = build_path(path);
  if (remove(abs_path.c_str()) != 0) {
    ESP_LOGE(TAG, "Failed to remove file: %s", strerror(errno));
    return false;
  }
  this->update_sensors();
  return true;
}

// === Directory Listing ===

std::vector<std::string> UsbMediaStorage::list_directory(const char *path, uint8_t depth) {
  std::vector<std::string> list;
  std::vector<FileInfo> infos = list_directory_file_info(path, depth);
  for (const auto &info : infos) {
    list.push_back(info.path);
  }
  return list;
}

std::vector<FileInfo> UsbMediaStorage::list_directory_file_info(const char *path, uint8_t depth) {
  std::vector<FileInfo> list;
  list_directory_file_info_rec(path, depth, list);
  return list;
}

std::vector<FileInfo> &UsbMediaStorage::list_directory_file_info_rec(const char *path, uint8_t depth,
                                                                     std::vector<FileInfo> &list) {
  std::string vfs_path = build_path(path);

  DIR *dir = opendir(vfs_path.c_str());
  if (dir == nullptr) {
    ESP_LOGE(TAG, "Failed to open directory: %s (errno %d)", vfs_path.c_str(), errno);
    return list;
  }

  char entry_path[FILE_PATH_MAX];
  size_t path_len = strlen(path);
  strlcpy(entry_path, path, sizeof(entry_path));
  strlcpy(entry_path + path_len, "/", sizeof(entry_path) - path_len);
  path_len = strlen(entry_path);

  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    strlcpy(entry_path + path_len, entry->d_name, sizeof(entry_path) - path_len);

    std::string entry_vfs_path = build_path(entry_path);
    struct stat st;
    if (stat(entry_vfs_path.c_str(), &st) != 0) {
      continue;
    }

    bool is_dir = S_ISDIR(st.st_mode);
    size_t file_sz = is_dir ? 0 : st.st_size;

    list.emplace_back(entry_path, file_sz, is_dir);
    if (is_dir && depth)
      list_directory_file_info_rec(entry_path, depth - 1, list);
  }
  closedir(dir);
  return list;
}

// === Sensor Updates ===

void UsbMediaStorage::update_sensors() {
#ifdef USE_SENSOR
  if (!this->mounted_) return;

  uint64_t total_bytes = 0, free_bytes = 0;
  esp_err_t ret = esp_vfs_fat_info(MOUNT_POINT.c_str(), &total_bytes, &free_bytes);
  if (ret == ESP_OK) {
    uint64_t used_bytes = total_bytes - free_bytes;

    if (this->used_space_sensor_ != nullptr)
      this->used_space_sensor_->publish_state(used_bytes);
    if (this->total_space_sensor_ != nullptr)
      this->total_space_sensor_->publish_state(total_bytes);
    if (this->free_space_sensor_ != nullptr)
      this->free_space_sensor_->publish_state(free_bytes);
  } else {
    ESP_LOGE(TAG, "Failed to get filesystem info: %s", esp_err_to_name(ret));
  }

  for (auto &sensor : this->file_size_sensors_) {
    if (sensor.sensor != nullptr)
      sensor.sensor->publish_state(this->file_size(sensor.path));
  }
#endif
}

#else  // !USE_ESP_IDF

void UsbMediaStorage::setup() {
  ESP_LOGE(TAG, "USB Media Storage requires ESP-IDF framework");
  mark_failed();
}

#endif  // USE_ESP_IDF

// === Convenience overloads (framework-independent) ===

size_t UsbMediaStorage::file_size(std::string const &path) { return this->file_size(path.c_str()); }
bool UsbMediaStorage::is_directory(std::string const &path) { return this->is_directory(path.c_str()); }
bool UsbMediaStorage::delete_file(std::string const &path) { return this->delete_file(path.c_str()); }
std::vector<uint8_t> UsbMediaStorage::read_file(std::string const &path) { return this->read_file(path.c_str()); }
std::vector<std::string> UsbMediaStorage::list_directory(std::string path, uint8_t depth) {
  return this->list_directory(path.c_str(), depth);
}
std::vector<FileInfo> UsbMediaStorage::list_directory_file_info(std::string path, uint8_t depth) {
  return this->list_directory_file_info(path.c_str(), depth);
}

#ifdef USE_SENSOR
void UsbMediaStorage::add_file_size_sensor(sensor::Sensor *sensor, std::string const &path) {
  this->file_size_sensors_.emplace_back(sensor, path);
}
#endif

std::string UsbMediaStorage::error_code_to_string(UsbMediaStorage::ErrorCode code) {
  switch (code) {
    case ErrorCode::ERR_USB_HOST_INIT:
      return "Failed to initialize USB Host";
    case ErrorCode::ERR_MSC_INIT:
      return "Failed to initialize MSC driver";
    case ErrorCode::ERR_MOUNT:
      return "Failed to mount USB filesystem";
    case ErrorCode::ERR_NO_DEVICE:
      return "No USB storage device found";
    default:
      return "Unknown error";
  }
}

}  // namespace usb_media_storage
}  // namespace esphome
