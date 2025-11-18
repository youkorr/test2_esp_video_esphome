#include "esphome_fbs_loader.h"

namespace esphome {
namespace esphome_fbs_loader {

void ESPHomeFbsLoader::setup() {
  ESP_LOGI(TAG, "Setting up ESPHome FBS Loader...");

  // Convert ModelLocation enum to fbs::model_location_type_t
  fbs::model_location_type_t fbs_location;
  switch (this->model_location_) {
    case ModelLocation::FLASH_RODATA:
      fbs_location = fbs::MODEL_LOCATION_IN_FLASH_RODATA;
      break;
    case ModelLocation::FLASH_PARTITION:
      fbs_location = fbs::MODEL_LOCATION_IN_FLASH_PARTITION;
      break;
    case ModelLocation::SDCARD:
      fbs_location = fbs::MODEL_LOCATION_IN_SDCARD;
      break;
    default:
      ESP_LOGE(TAG, "Invalid model location!");
      this->mark_failed();
      return;
  }

  // Create FbsLoader instance
  const char *path_ptr = this->model_path_.empty() ? nullptr : this->model_path_.c_str();
  this->fbs_loader_ = new fbs::FbsLoader(path_ptr, fbs_location);

  if (this->fbs_loader_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create FbsLoader instance!");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "FbsLoader created successfully");
  ESP_LOGI(TAG, "Model location: %s", this->fbs_loader_->get_model_location_string());

  // Load the model
  if (!this->load_model_()) {
    ESP_LOGE(TAG, "Failed to load model!");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "FBS Loader setup complete!");
}

void ESPHomeFbsLoader::dump_config() {
  ESP_LOGCONFIG(TAG, "ESPHome FBS Loader:");
  ESP_LOGCONFIG(TAG, "  Model Path: %s", this->model_path_.c_str());

  const char *location_str = "Unknown";
  switch (this->model_location_) {
    case ModelLocation::FLASH_RODATA:
      location_str = "FLASH RODATA";
      break;
    case ModelLocation::FLASH_PARTITION:
      location_str = "FLASH Partition";
      break;
    case ModelLocation::SDCARD:
      location_str = "SD Card";
      break;
  }
  ESP_LOGCONFIG(TAG, "  Model Location: %s", location_str);
  ESP_LOGCONFIG(TAG, "  Parameter Copy: %s", this->param_copy_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Encryption: %s", this->use_encryption_ ? "YES" : "NO");

  if (this->is_model_loaded()) {
    ESP_LOGCONFIG(TAG, "  Model Loaded: YES");

    // Print model info
    if (this->fbs_model_) {
      std::string model_name = this->fbs_model_->get_model_name();
      int64_t version = this->fbs_model_->get_model_version();
      std::string doc_string = this->fbs_model_->get_model_doc_string();

      ESP_LOGCONFIG(TAG, "  Model Name: %s", model_name.c_str());
      ESP_LOGCONFIG(TAG, "  Model Version: %lld", version);
      if (!doc_string.empty()) {
        ESP_LOGCONFIG(TAG, "  Model Description: %s", doc_string.c_str());
      }

      // Print model size
      size_t internal_size = 0, psram_size = 0, psram_rodata_size = 0, flash_size = 0;
      this->fbs_model_->get_model_size(&internal_size, &psram_size, &psram_rodata_size, &flash_size);
      ESP_LOGCONFIG(TAG, "  Model Size:");
      ESP_LOGCONFIG(TAG, "    Internal RAM: %u bytes", internal_size);
      ESP_LOGCONFIG(TAG, "    PSRAM: %u bytes", psram_size);
      ESP_LOGCONFIG(TAG, "    PSRAM Rodata: %u bytes", psram_rodata_size);
      ESP_LOGCONFIG(TAG, "    FLASH: %u bytes", flash_size);

      // Print graph inputs/outputs
      std::vector<std::string> inputs = this->fbs_model_->get_graph_inputs();
      std::vector<std::string> outputs = this->fbs_model_->get_graph_outputs();

      ESP_LOGCONFIG(TAG, "  Graph Inputs: %d", inputs.size());
      for (const auto &input : inputs) {
        std::vector<int> shape = this->fbs_model_->get_value_info_shape(input);
        ESP_LOGCONFIG(TAG, "    - %s: [%d, %d, %d, %d]",
                      input.c_str(),
                      shape.size() > 0 ? shape[0] : 0,
                      shape.size() > 1 ? shape[1] : 0,
                      shape.size() > 2 ? shape[2] : 0,
                      shape.size() > 3 ? shape[3] : 0);
      }

      ESP_LOGCONFIG(TAG, "  Graph Outputs: %d", outputs.size());
      for (const auto &output : outputs) {
        std::vector<int> shape = this->fbs_model_->get_value_info_shape(output);
        ESP_LOGCONFIG(TAG, "    - %s: [%d, %d, %d, %d]",
                      output.c_str(),
                      shape.size() > 0 ? shape[0] : 0,
                      shape.size() > 1 ? shape[1] : 0,
                      shape.size() > 2 ? shape[2] : 0,
                      shape.size() > 3 ? shape[3] : 0);
      }
    }
  } else {
    ESP_LOGCONFIG(TAG, "  Model Loaded: NO");
  }
}

void ESPHomeFbsLoader::set_encryption_key(const std::vector<uint8_t> &key) {
  if (key.size() != 16) {
    ESP_LOGE(TAG, "Encryption key must be exactly 16 bytes (128-bit)!");
    return;
  }

  memcpy(this->encryption_key_, key.data(), 16);
  this->use_encryption_ = true;
  ESP_LOGI(TAG, "Encryption key set (128-bit AES)");
}

std::string ESPHomeFbsLoader::get_model_info() {
  if (!this->is_model_loaded()) {
    return "No model loaded";
  }

  std::string info = "Model: ";
  info += this->fbs_model_->get_model_name();
  info += ", Version: ";
  info += std::to_string(this->fbs_model_->get_model_version());

  std::string doc_string = this->fbs_model_->get_model_doc_string();
  if (!doc_string.empty()) {
    info += ", Doc: ";
    info += doc_string;
  }

  return info;
}

int ESPHomeFbsLoader::get_model_count() {
  if (this->fbs_loader_ == nullptr) {
    return 0;
  }
  return this->fbs_loader_->get_model_num();
}

void ESPHomeFbsLoader::list_all_models() {
  if (this->fbs_loader_ == nullptr) {
    ESP_LOGW(TAG, "FbsLoader not initialized!");
    return;
  }

  ESP_LOGI(TAG, "=== Available Models ===");
  this->fbs_loader_->list_models();
  ESP_LOGI(TAG, "========================");
}

void ESPHomeFbsLoader::get_model_size(size_t *internal_size, size_t *psram_size,
                                       size_t *psram_rodata_size, size_t *flash_size) {
  if (this->fbs_model_ != nullptr) {
    this->fbs_model_->get_model_size(internal_size, psram_size, psram_rodata_size, flash_size);
  } else {
    *internal_size = 0;
    *psram_size = 0;
    *psram_rodata_size = 0;
    *flash_size = 0;
  }
}

bool ESPHomeFbsLoader::load_model_() {
  if (this->fbs_loader_ == nullptr) {
    ESP_LOGE(TAG, "FbsLoader is nullptr!");
    return false;
  }

  const uint8_t *key_ptr = this->use_encryption_ ? this->encryption_key_ : nullptr;

  // Load model by name, index, or default (first model)
  if (!this->model_name_.empty()) {
    ESP_LOGI(TAG, "Loading model by name: '%s'", this->model_name_.c_str());
    this->fbs_model_ = this->fbs_loader_->load(this->model_name_.c_str(), key_ptr, this->param_copy_);
  } else if (this->model_index_ >= 0) {
    ESP_LOGI(TAG, "Loading model by index: %d", this->model_index_);
    this->fbs_model_ = this->fbs_loader_->load(this->model_index_, key_ptr, this->param_copy_);
  } else {
    ESP_LOGI(TAG, "Loading first model (default)");
    this->fbs_model_ = this->fbs_loader_->load(key_ptr, this->param_copy_);
  }

  if (this->fbs_model_ == nullptr) {
    ESP_LOGE(TAG, "Failed to load model!");
    return false;
  }

  ESP_LOGI(TAG, "Model loaded successfully: %s (v%lld)",
           this->fbs_model_->get_model_name().c_str(),
           this->fbs_model_->get_model_version());

  // Print additional model info
  this->fbs_model_->print();

  return true;
}

}  // namespace esphome_fbs_loader
}  // namespace esphome
