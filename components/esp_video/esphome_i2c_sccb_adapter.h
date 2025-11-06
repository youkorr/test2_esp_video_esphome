/*
 * Adaptateur I2C-SCCB pour ESP-Video avec ESPHome
 * Convertit l'I2CBus d'ESPHome en interface SCCB pour esp_video_init()
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esp_sccb_io_interface.h"
#include "esp_sccb_types.h"
#include "esp_log.h"

namespace esphome {
namespace esp_video {

static const char *TAG_ADAPTER = "esphome_sccb_adapter";

/**
 * @brief Adaptateur qui enveloppe I2CDevice d'ESPHome pour présenter l'interface SCCB
 *
 * Cette structure hérite de esp_sccb_io_t et implémente toutes les opérations SCCB
 * en appelant les méthodes I2C d'ESPHome.
 */
struct ESPHomeI2CSCCBAdapter {
  esp_sccb_io_t base;           ///< Structure de base SCCB (doit être premier membre)
  i2c::I2CDevice *i2c_device;   ///< Pointeur vers I2CDevice d'ESPHome

  // Fonctions statiques pour l'interface SCCB

  /**
   * @brief Transmet registre 8-bit addr + 8-bit val via I2C
   */
  static esp_err_t transmit_a8v8_(esp_sccb_io_t *io_handle, const uint8_t *write_buffer,
                                  size_t write_size, int xfer_timeout_ms) {
    auto *adapter = reinterpret_cast<ESPHomeI2CSCCBAdapter*>(io_handle);
    if (!adapter || !adapter->i2c_device || !write_buffer) {
      return ESP_ERR_INVALID_ARG;
    }

    // ESPHome I2C write: envoie tous les bytes
    esphome::ErrorCode err = adapter->i2c_device->write(write_buffer, write_size);
    return (err == esphome::ERROR_OK) ? ESP_OK : ESP_FAIL;
  }

  /**
   * @brief Transmet registre 16-bit addr + 8-bit val via I2C
   */
  static esp_err_t transmit_a16v8_(esp_sccb_io_t *io_handle, const uint8_t *write_buffer,
                                   size_t write_size, int xfer_timeout_ms) {
    auto *adapter = reinterpret_cast<ESPHomeI2CSCCBAdapter*>(io_handle);
    if (!adapter || !adapter->i2c_device || !write_buffer) {
      return ESP_ERR_INVALID_ARG;
    }

    esphome::ErrorCode err = adapter->i2c_device->write(write_buffer, write_size);
    return (err == esphome::ERROR_OK) ? ESP_OK : ESP_FAIL;
  }

  /**
   * @brief Transmet registre 8-bit addr + 16-bit val via I2C
   */
  static esp_err_t transmit_a8v16_(esp_sccb_io_t *io_handle, const uint8_t *write_buffer,
                                   size_t write_size, int xfer_timeout_ms) {
    auto *adapter = reinterpret_cast<ESPHomeI2CSCCBAdapter*>(io_handle);
    if (!adapter || !adapter->i2c_device || !write_buffer) {
      return ESP_ERR_INVALID_ARG;
    }

    esphome::ErrorCode err = adapter->i2c_device->write(write_buffer, write_size);
    return (err == esphome::ERROR_OK) ? ESP_OK : ESP_FAIL;
  }

  /**
   * @brief Transmet registre 16-bit addr + 16-bit val via I2C
   */
  static esp_err_t transmit_a16v16_(esp_sccb_io_t *io_handle, const uint8_t *write_buffer,
                                    size_t write_size, int xfer_timeout_ms) {
    auto *adapter = reinterpret_cast<ESPHomeI2CSCCBAdapter*>(io_handle);
    if (!adapter || !adapter->i2c_device || !write_buffer) {
      return ESP_ERR_INVALID_ARG;
    }

    esphome::ErrorCode err = adapter->i2c_device->write(write_buffer, write_size);
    return (err == esphome::ERROR_OK) ? ESP_OK : ESP_FAIL;
  }

  /**
   * @brief Transmet-reçoit registre 8-bit addr + 8-bit val via I2C
   */
  static esp_err_t transmit_receive_a8v8_(esp_sccb_io_t *io_handle, const uint8_t *write_buffer,
                                          size_t write_size, uint8_t *read_buffer,
                                          size_t read_size, int xfer_timeout_ms) {
    auto *adapter = reinterpret_cast<ESPHomeI2CSCCBAdapter*>(io_handle);
    if (!adapter || !adapter->i2c_device || !write_buffer || !read_buffer) {
      return ESP_ERR_INVALID_ARG;
    }

    // ESPHome I2C write_read: écrit l'adresse puis lit la valeur
    esphome::ErrorCode err = adapter->i2c_device->write(write_buffer, write_size);
    if (err != esphome::ERROR_OK) {
      return ESP_FAIL;
    }

    err = adapter->i2c_device->read(read_buffer, read_size);
    return (err == esphome::ERROR_OK) ? ESP_OK : ESP_FAIL;
  }

  /**
   * @brief Transmet-reçoit registre 16-bit addr + 8-bit val via I2C
   */
  static esp_err_t transmit_receive_a16v8_(esp_sccb_io_t *io_handle, const uint8_t *write_buffer,
                                           size_t write_size, uint8_t *read_buffer,
                                           size_t read_size, int xfer_timeout_ms) {
    auto *adapter = reinterpret_cast<ESPHomeI2CSCCBAdapter*>(io_handle);
    if (!adapter || !adapter->i2c_device || !write_buffer || !read_buffer) {
      return ESP_ERR_INVALID_ARG;
    }

    esphome::ErrorCode err = adapter->i2c_device->write(write_buffer, write_size);
    if (err != esphome::ERROR_OK) {
      return ESP_FAIL;
    }

    err = adapter->i2c_device->read(read_buffer, read_size);
    return (err == esphome::ERROR_OK) ? ESP_OK : ESP_FAIL;
  }

  /**
   * @brief Transmet-reçoit registre 8-bit addr + 16-bit val via I2C
   */
  static esp_err_t transmit_receive_a8v16_(esp_sccb_io_t *io_handle, const uint8_t *write_buffer,
                                           size_t write_size, uint8_t *read_buffer,
                                           size_t read_size, int xfer_timeout_ms) {
    auto *adapter = reinterpret_cast<ESPHomeI2CSCCBAdapter*>(io_handle);
    if (!adapter || !adapter->i2c_device || !write_buffer || !read_buffer) {
      return ESP_ERR_INVALID_ARG;
    }

    esphome::ErrorCode err = adapter->i2c_device->write(write_buffer, write_size);
    if (err != esphome::ERROR_OK) {
      return ESP_FAIL;
    }

    err = adapter->i2c_device->read(read_buffer, read_size);
    return (err == esphome::ERROR_OK) ? ESP_OK : ESP_FAIL;
  }

  /**
   * @brief Transmet-reçoit registre 16-bit addr + 16-bit val via I2C
   */
  static esp_err_t transmit_receive_a16v16_(esp_sccb_io_t *io_handle, const uint8_t *write_buffer,
                                            size_t write_size, uint8_t *read_buffer,
                                            size_t read_size, int xfer_timeout_ms) {
    auto *adapter = reinterpret_cast<ESPHomeI2CSCCBAdapter*>(io_handle);
    if (!adapter || !adapter->i2c_device || !write_buffer || !read_buffer) {
      return ESP_ERR_INVALID_ARG;
    }

    esphome::ErrorCode err = adapter->i2c_device->write(write_buffer, write_size);
    if (err != esphome::ERROR_OK) {
      return ESP_FAIL;
    }

    err = adapter->i2c_device->read(read_buffer, read_size);
    return (err == esphome::ERROR_OK) ? ESP_OK : ESP_FAIL;
  }

  /**
   * @brief Supprime l'adaptateur (ne fait rien, géré par ESPHome)
   */
  static esp_err_t del_(esp_sccb_io_t *io_handle) {
    // L'I2CDevice est géré par ESPHome, on ne fait rien
    ESP_LOGI(TAG_ADAPTER, "SCCB adapter delete called (no-op)");
    return ESP_OK;
  }

  /**
   * @brief Crée un adaptateur SCCB à partir d'un I2CDevice ESPHome
   */
  static ESPHomeI2CSCCBAdapter* create(i2c::I2CDevice *device) {
    if (!device) {
      ESP_LOGE(TAG_ADAPTER, "I2CDevice est nullptr, impossible de créer l'adaptateur");
      return nullptr;
    }

    auto *adapter = new ESPHomeI2CSCCBAdapter();
    adapter->i2c_device = device;

    // Initialiser les pointeurs de fonction
    adapter->base.transmit_reg_a8v8 = transmit_a8v8_;
    adapter->base.transmit_reg_a16v8 = transmit_a16v8_;
    adapter->base.transmit_reg_a8v16 = transmit_a8v16_;
    adapter->base.transmit_reg_a16v16 = transmit_a16v16_;
    adapter->base.transmit_receive_reg_a8v8 = transmit_receive_a8v8_;
    adapter->base.transmit_receive_reg_a16v8 = transmit_receive_a16v8_;
    adapter->base.transmit_receive_reg_a8v16 = transmit_receive_a8v16_;
    adapter->base.transmit_receive_reg_a16v16 = transmit_receive_a16v16_;
    adapter->base.del = del_;

    ESP_LOGI(TAG_ADAPTER, "✅ Adaptateur I2C-SCCB créé avec I2CDevice %p", device);
    return adapter;
  }

  /**
   * @brief Obtient le handle SCCB de l'adaptateur
   */
  esp_sccb_io_handle_t get_handle() {
    return reinterpret_cast<esp_sccb_io_handle_t>(&this->base);
  }
};

}  // namespace esp_video
}  // namespace esphome
