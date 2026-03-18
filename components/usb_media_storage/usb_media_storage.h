#pragma once
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#include <vector>
#include <string>
#include <functional>

#ifdef USE_ESP_IDF
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#endif

namespace esphome {
namespace usb_media_storage {

struct FileInfo {
  std::string path;
  size_t size;
  bool is_directory;

  FileInfo(std::string const &path, size_t size, bool is_directory);
};

class UsbMediaStorage : public Component {
#ifdef USE_SENSOR
  SUB_SENSOR(used_space)
  SUB_SENSOR(total_space)
  SUB_SENSOR(free_space)
#endif
#ifdef USE_TEXT_SENSOR
  SUB_TEXT_SENSOR(usb_status)
#endif
 public:
  enum ErrorCode {
    ERR_USB_HOST_INIT,
    ERR_MSC_INIT,
    ERR_MOUNT,
    ERR_NO_DEVICE,
  };

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

  // File operations
  void write_file(const char *path, const uint8_t *buffer, size_t len, const char *mode);
  void write_file(const char *path, const uint8_t *buffer, size_t len);
  void append_file(const char *path, const uint8_t *buffer, size_t len);
  bool delete_file(const char *path);
  bool delete_file(std::string const &path);

  // Read operations
  std::vector<uint8_t> read_file(const char *path);
  std::vector<uint8_t> read_file(std::string const &path);
  void read_file_stream(const char *path, size_t offset, size_t chunk_size,
                        std::function<void(const uint8_t*, size_t)> callback);
  std::vector<uint8_t> read_file_video(const char *path, size_t max_size = 0);

  // Directory operations
  bool create_directory(const char *path);
  bool remove_directory(const char *path);
  bool is_directory(const char *path);
  bool is_directory(std::string const &path);
  std::vector<std::string> list_directory(const char *path, uint8_t depth);
  std::vector<std::string> list_directory(std::string path, uint8_t depth);
  std::vector<FileInfo> list_directory_file_info(const char *path, uint8_t depth);
  std::vector<FileInfo> list_directory_file_info(std::string path, uint8_t depth);

  // File info
  size_t file_size(const char *path);
  size_t file_size(std::string const &path);
  bool file_exists(const char *path);

  // Status
  bool is_mounted() const { return this->mounted_; }
  bool is_device_connected() const { return this->device_connected_; }

  // Mount point access (for other components like simple_video_player)
  const std::string &get_mount_point() const { return MOUNT_POINT; }

  // Hot-plug callbacks (called from ISR context via MSC event callback)
  void on_device_connected(uint8_t address);
  void on_device_disconnected();

#ifdef USE_SENSOR
  void add_file_size_sensor(sensor::Sensor *sensor, std::string const &path);
#endif

 protected:
  ErrorCode init_error_;
  bool mounted_{false};
  bool device_connected_{false};
  bool install_attempted_{false};

  // Hot-plug state
  volatile bool pending_connect_{false};
  volatile bool pending_disconnect_{false};
  uint8_t pending_device_address_{0};
#ifdef USE_ESP_IDF
  msc_host_device_handle_t msc_device_{NULL};
  msc_host_vfs_handle_t vfs_handle_{NULL};
#endif

  void handle_mount_();
  void handle_unmount_();

  static const std::string MOUNT_POINT;

  std::string build_path(const char *path) const;

#ifdef USE_SENSOR
  struct FileSizeSensor {
    sensor::Sensor *sensor{nullptr};
    std::string path;
    FileSizeSensor(sensor::Sensor *s, std::string const &p) : sensor(s), path(p) {}
  };
  std::vector<FileSizeSensor> file_size_sensors_{};
#endif

  void update_sensors();
  std::vector<FileInfo> &list_directory_file_info_rec(const char *path, uint8_t depth, std::vector<FileInfo> &list);
  static std::string error_code_to_string(ErrorCode code);
};

// Action templates
template<typename... Ts> class UsbWriteFileAction : public Action<Ts...> {
 public:
  UsbWriteFileAction(UsbMediaStorage *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, path)
  TEMPLATABLE_VALUE(std::vector<uint8_t>, data)

  void play(Ts... x) {
    auto path = this->path_.value(x...);
    auto buffer = this->data_.value(x...);
    this->parent_->write_file(path.c_str(), buffer.data(), buffer.size());
  }

 protected:
  UsbMediaStorage *parent_;
};

template<typename... Ts> class UsbAppendFileAction : public Action<Ts...> {
 public:
  UsbAppendFileAction(UsbMediaStorage *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, path)
  TEMPLATABLE_VALUE(std::vector<uint8_t>, data)

  void play(Ts... x) {
    auto path = this->path_.value(x...);
    auto buffer = this->data_.value(x...);
    this->parent_->append_file(path.c_str(), buffer.data(), buffer.size());
  }

 protected:
  UsbMediaStorage *parent_;
};

template<typename... Ts> class UsbCreateDirectoryAction : public Action<Ts...> {
 public:
  UsbCreateDirectoryAction(UsbMediaStorage *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, path)

  void play(Ts... x) {
    auto path = this->path_.value(x...);
    this->parent_->create_directory(path.c_str());
  }

 protected:
  UsbMediaStorage *parent_;
};

template<typename... Ts> class UsbRemoveDirectoryAction : public Action<Ts...> {
 public:
  UsbRemoveDirectoryAction(UsbMediaStorage *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, path)

  void play(Ts... x) {
    auto path = this->path_.value(x...);
    this->parent_->remove_directory(path.c_str());
  }

 protected:
  UsbMediaStorage *parent_;
};

template<typename... Ts> class UsbDeleteFileAction : public Action<Ts...> {
 public:
  UsbDeleteFileAction(UsbMediaStorage *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, path)

  void play(Ts... x) {
    auto path = this->path_.value(x...);
    this->parent_->delete_file(path.c_str());
  }

 protected:
  UsbMediaStorage *parent_;
};

}  // namespace usb_media_storage
}  // namespace esphome
