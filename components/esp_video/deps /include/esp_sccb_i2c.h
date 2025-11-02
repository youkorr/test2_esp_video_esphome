#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_sccb_io_t *esp_sccb_io_handle_t;

typedef enum {
    I2C_ADDR_BIT_LEN_7 = 0,
    I2C_ADDR_BIT_LEN_10,
} i2c_addr_bit_len_t;

typedef struct {
    uint8_t device_address;
    i2c_addr_bit_len_t dev_addr_length;
    uint32_t scl_speed_hz;
    uint8_t addr_bits_width;
    uint8_t val_bits_width;
} sccb_i2c_config_t;

esp_err_t sccb_new_i2c_io(i2c_master_bus_handle_t bus_handle, 
                           const sccb_i2c_config_t *config,
                           esp_sccb_io_handle_t *ret_handle);

esp_err_t esp_sccb_transmit_reg_a16v8(esp_sccb_io_handle_t handle, 
                                       uint16_t reg, 
                                       uint8_t data);

esp_err_t esp_sccb_transmit_receive_reg_a16v8(esp_sccb_io_handle_t handle,
                                               uint16_t reg,
                                               uint8_t *data);

esp_err_t esp_sccb_del_i2c_io(esp_sccb_io_handle_t handle);

#ifdef __cplusplus
}
#endif
