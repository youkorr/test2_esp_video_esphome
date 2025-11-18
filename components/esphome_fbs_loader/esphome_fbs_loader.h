#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "fbs_loader.hpp"
#include "fbs_model.hpp"
#include <string>
#include <vector>

namespace esphome {
namespace esphome_fbs_loader {

static const char *const TAG = "esphome_fbs_loader";

enum class ModelLocation {
  FLASH_RODATA = 0,
  FLASH_PARTITION = 1,
  SDCARD = 2
};

/**
 * @brief ESPHome component for loading FlatBuffers models using ESP-DL fbs_loader
 *
 * This component provides an interface to load and manage ESP-DL FlatBuffers models
 * from different storage locations (FLASH RODATA, FLASH Partition, or SD Card).
 *
 * Features:
 * - Load models from multiple storage locations
 * - Support for encrypted models (AES-128)
 * - Access to model metadata and parameters
 * - Multiple model support in a single file
 */
class ESPHomeFbsLoader : public Component {
 public:
  ESPHomeFbsLoader() = default;

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  /**
   * @brief Set the model path or partition label
   *
   * @param path_or_label Path to model file (SD card) or partition label (FLASH)
   */
  void set_model_path(const std::string &path_or_label) { this->model_path_ = path_or_label; }

  /**
   * @brief Set the model storage location
   *
   * @param location Model storage location (FLASH_RODATA, FLASH_PARTITION, SDCARD)
   */
  void set_model_location(ModelLocation location) { this->model_location_ = location; }

  /**
   * @brief Set encryption key for encrypted models
   *
   * @param key 128-bit AES encryption key (16 bytes)
   */
  void set_encryption_key(const std::vector<uint8_t> &key);

  /**
   * @brief Enable/disable parameter copy to PSRAM
   *
   * @param param_copy Set to false to save PSRAM (sacrifices performance)
   */
  void set_param_copy(bool param_copy) { this->param_copy_ = param_copy; }

  /**
   * @brief Set the model name to load (for multi-model files)
   *
   * @param model_name Name of the model to load
   */
  void set_model_name(const std::string &model_name) { this->model_name_ = model_name; }

  /**
   * @brief Set the model index to load (for multi-model files)
   *
   * @param model_index Index of the model (0-based)
   */
  void set_model_index(int model_index) { this->model_index_ = model_index; }

  /**
   * @brief Get the loaded FbsModel pointer
   *
   * @return fbs::FbsModel* Pointer to the loaded model, or nullptr if not loaded
   */
  fbs::FbsModel *get_model() { return this->fbs_model_; }

  /**
   * @brief Get the FbsLoader instance
   *
   * @return fbs::FbsLoader* Pointer to the FbsLoader instance
   */
  fbs::FbsLoader *get_loader() { return this->fbs_loader_; }

  /**
   * @brief Check if a model is successfully loaded
   *
   * @return true if model is loaded, false otherwise
   */
  bool is_model_loaded() const { return this->fbs_model_ != nullptr; }

  /**
   * @brief Get model information as a string
   *
   * @return std::string Model information (name, version, doc string)
   */
  std::string get_model_info();

  /**
   * @brief Get the number of models in the loaded file
   *
   * @return int Number of models
   */
  int get_model_count();

  /**
   * @brief List all available models (for multi-model files)
   */
  void list_all_models();

  /**
   * @brief Get model size information
   *
   * @param internal_size Output: Internal RAM usage
   * @param psram_size Output: PSRAM usage
   * @param psram_rodata_size Output: PSRAM rodata usage
   * @param flash_size Output: FLASH usage
   */
  void get_model_size(size_t *internal_size, size_t *psram_size, size_t *psram_rodata_size, size_t *flash_size);

 protected:
  std::string model_path_{""};
  ModelLocation model_location_{ModelLocation::FLASH_PARTITION};
  std::string model_name_{""};
  int model_index_{-1};  // -1 means load first model
  bool param_copy_{true};
  uint8_t encryption_key_[16] = {0};
  bool use_encryption_{false};

  fbs::FbsLoader *fbs_loader_{nullptr};
  fbs::FbsModel *fbs_model_{nullptr};

  bool load_model_();
};

}  // namespace esphome_fbs_loader
}  // namespace esphome
