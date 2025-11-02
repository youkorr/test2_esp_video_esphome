#include "tab5_camera.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#ifdef USE_ESP32_VARIANT_ESP32P4

// ============================================================================
// HEADERS ESP-IDF NÉCESSAIRES
// ============================================================================

extern "C" {
#include "driver/i2c_master.h"
}

// ============================================================================
// DÉFINITIONS COMPLÈTES DES STRUCTURES CAMERA SENSOR
// ============================================================================

extern "C" {

// Type pour SCCB handle
typedef struct esp_sccb_io_t* esp_sccb_io_handle_t;

// Structure SCCB IO config
typedef struct {
    i2c_addr_bit_len_t dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
    uint32_t addr_bits_width;
    uint32_t val_bits_width;
} sccb_i2c_config_t;

// Structure pour l'ID du capteur
typedef struct {
    uint8_t midh;
    uint8_t midl;
    uint16_t pid;
    uint8_t ver;
} esp_cam_sensor_id_t;

// Type de port du capteur
typedef enum {
    ESP_CAM_SENSOR_DVP,
    ESP_CAM_SENSOR_MIPI_CSI,
} esp_cam_sensor_port_t;

// Forward declaration pour les opérations
struct esp_cam_sensor_device_t;
typedef struct esp_cam_sensor_ops_t {
    int (*set_format)(struct esp_cam_sensor_device_t *dev, const void *format);
    int (*get_format)(struct esp_cam_sensor_device_t *dev, void *format);
    int (*priv_ioctl)(struct esp_cam_sensor_device_t *dev, uint32_t cmd, void *arg);
    int (*del)(struct esp_cam_sensor_device_t *dev);
} esp_cam_sensor_ops_t;

// Structure principale du device
typedef struct esp_cam_sensor_device_t {
    char *name;
    esp_sccb_io_handle_t sccb_handle;
    int8_t xclk_pin;
    int8_t reset_pin;
    int8_t pwdn_pin;
    esp_cam_sensor_port_t sensor_port;
    const void *cur_format;
    esp_cam_sensor_id_t id;
    uint8_t stream_status;
    const esp_cam_sensor_ops_t *ops;
    void *priv;
} esp_cam_sensor_device_t;

// Configuration du capteur
typedef struct {
    esp_sccb_io_handle_t sccb_handle;
    int8_t reset_pin;
    int8_t pwdn_pin;
    int8_t xclk_pin;
    int32_t xclk_freq_hz;
    esp_cam_sensor_port_t sensor_port;
} esp_cam_sensor_config_t;

} // extern "C"

// ============================================================================
// IMPLÉMENTATIONS SCCB ET SENSOR
// ============================================================================

// Structure SCCB IO interne (utilise ESPHome I2C au lieu d'ESP-IDF)
struct esp_sccb_io_t {
    esphome::i2c::I2CDevice *i2c_device;
    uint32_t addr_bits_width;
    uint32_t val_bits_width;
};

// Forward declarations
esp_err_t sccb_new_i2c_io_esphome(esphome::i2c::I2CDevice *i2c_device,
                                   const sccb_i2c_config_t *config,
                                   esp_sccb_io_handle_t *io_handle);

esp_err_t esp_sccb_transmit_reg_a16v8(esp_sccb_io_handle_t handle, 
                                       uint16_t reg_addr, 
                                       uint8_t reg_val);

esp_err_t esp_sccb_transmit_receive_reg_a16v8(esp_sccb_io_handle_t handle, 
                                              uint16_t reg_addr, 
                                              uint8_t *reg_val);

// Implémentation de sccb_new_i2c_io pour ESPHome
esp_err_t sccb_new_i2c_io_esphome(esphome::i2c::I2CDevice *i2c_device,
                                   const sccb_i2c_config_t *config,
                                   esp_sccb_io_handle_t *io_handle) {
    if (!i2c_device || !config || !io_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_sccb_io_t *sccb = (esp_sccb_io_t*)malloc(sizeof(esp_sccb_io_t));
    if (!sccb) {
        return ESP_ERR_NO_MEM;
    }
    
    sccb->i2c_device = i2c_device;
    sccb->addr_bits_width = config->addr_bits_width ? config->addr_bits_width : 8;
    sccb->val_bits_width = config->val_bits_width ? config->val_bits_width : 8;
    
    *io_handle = sccb;
    return ESP_OK;
}

// Implémentations des fonctions SCCB pour communication I2C
esp_err_t esp_sccb_transmit_reg_a16v8(esp_sccb_io_handle_t handle, 
                                       uint16_t reg_addr, 
                                       uint8_t reg_val) {
    if (!handle || !handle->i2c_device) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t data[3] = {
        (uint8_t)((reg_addr >> 8) & 0xFF),
        (uint8_t)(reg_addr & 0xFF),
        reg_val
    };
    
    esphome::i2c::ErrorCode err = handle->i2c_device->write(data, 3);
    return (err == esphome::i2c::ERROR_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_sccb_transmit_receive_reg_a16v8(esp_sccb_io_handle_t handle, 
                                              uint16_t reg_addr, 
                                              uint8_t *reg_val) {
    if (!handle || !handle->i2c_device || !reg_val) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t addr_buf[2] = {
        (uint8_t)((reg_addr >> 8) & 0xFF),
        (uint8_t)(reg_addr & 0xFF)
    };
    
    esphome::i2c::ErrorCode err = handle->i2c_device->write(addr_buf, 2, false);
    if (err != esphome::i2c::ERROR_OK) {
        return ESP_FAIL;
    }
    
    err = handle->i2c_device->read(reg_val, 1);
    return (err == esphome::i2c::ERROR_OK) ? ESP_OK : ESP_FAIL;
}

// Implémentation de esp_cam_sensor_ioctl
esp_err_t esp_cam_sensor_ioctl(esp_cam_sensor_device_t *dev, 
                               uint32_t cmd, 
                               void *arg) {
    if (!dev || !dev->ops) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!dev->ops->priv_ioctl) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return dev->ops->priv_ioctl(dev, cmd, arg);
}

// Symboles faibles pour éviter les erreurs de linking
__attribute__((weak)) void *__esp_cam_sensor_detect_fn_array_start = nullptr;
__attribute__((weak)) void *__esp_cam_sensor_detect_fn_array_end = nullptr;

// ============================================================================
// CODE COMPLET DU DRIVER SC202CS
// ============================================================================

extern "C" {

static const char *SC202CS_TAG = "sc202cs";

// Types SC202CS
typedef struct {
    uint16_t reg;
    uint8_t val;
} sc202cs_reginfo_t;

// Registres SC202CS
#define SC202CS_REG_END   0xffff
#define SC202CS_REG_DELAY 0xfffe
#define SC202CS_REG_SENSOR_ID_H 0x3107
#define SC202CS_REG_SENSOR_ID_L 0x3108
#define SC202CS_REG_SLEEP_MODE  0x0100
#define SC202CS_REG_DIG_COARSE_GAIN 0x3e06
#define SC202CS_REG_DIG_FINE_GAIN   0x3e07
#define SC202CS_REG_ANG_GAIN        0x3e09
#define SC202CS_REG_SHUTTER_TIME_H 0x3e00
#define SC202CS_REG_SHUTTER_TIME_M 0x3e01
#define SC202CS_REG_SHUTTER_TIME_L 0x3e02

#define SC202CS_PID         0xeb52
#define SC202CS_SENSOR_NAME "SC202CS"

#ifndef portTICK_RATE_MS
#define portTICK_RATE_MS portTICK_PERIOD_MS
#endif
#define delay_ms(ms) vTaskDelay((ms > portTICK_PERIOD_MS ? ms / portTICK_PERIOD_MS : 1))

// Liste de registres pour 1280x720 @ 30fps RAW8
static const sc202cs_reginfo_t init_reglist_1280x720_30fps[] = {
    {0x0103, 0x01},
    {SC202CS_REG_SLEEP_MODE, 0x00},
    {0x36e9, 0x80}, {0x36ea, 0x06}, {0x36eb, 0x0a}, {0x36ec, 0x01},
    {0x36ed, 0x18}, {0x36e9, 0x24}, {0x301f, 0x18}, {0x3031, 0x08},
    {0x3037, 0x00}, {0x3200, 0x00}, {0x3201, 0xa0}, {0x3202, 0x00},
    {0x3203, 0xf0}, {0x3204, 0x05}, {0x3205, 0xa7}, {0x3206, 0x03},
    {0x3207, 0xc7}, {0x3208, 0x05}, {0x3209, 0x00}, {0x320a, 0x02},
    {0x320b, 0xd0}, {0x3210, 0x00}, {0x3211, 0x04}, {0x3212, 0x00},
    {0x3213, 0x04}, {0x3301, 0xff}, {0x3304, 0x68}, {0x3306, 0x40},
    {0x3308, 0x08}, {0x3309, 0xa8}, {0x330b, 0xd0}, {0x330c, 0x18},
    {0x330d, 0xff}, {0x330e, 0x20}, {0x331e, 0x59}, {0x331f, 0x99},
    {0x3333, 0x10}, {0x335e, 0x06}, {0x335f, 0x08}, {0x3364, 0x1f},
    {0x337c, 0x02}, {0x337d, 0x0a}, {0x338f, 0xa0}, {0x3390, 0x01},
    {0x3391, 0x03}, {0x3392, 0x1f}, {0x3393, 0xff}, {0x3394, 0xff},
    {0x3395, 0xff}, {0x33a2, 0x04}, {0x33ad, 0x0c}, {0x33b1, 0x20},
    {0x33b3, 0x38}, {0x33f9, 0x40}, {0x33fb, 0x48}, {0x33fc, 0x0f},
    {0x33fd, 0x1f}, {0x349f, 0x03}, {0x34a6, 0x03}, {0x34a7, 0x1f},
    {0x34a8, 0x38}, {0x34a9, 0x30}, {0x34ab, 0xd0}, {0x34ad, 0xd8},
    {0x34f8, 0x1f}, {0x34f9, 0x20}, {0x3630, 0xa0}, {0x3631, 0x92},
    {0x3632, 0x64}, {0x3633, 0x43}, {0x3637, 0x49}, {0x363a, 0x85},
    {0x363c, 0x0f}, {0x3650, 0x31}, {0x3670, 0x0d}, {0x3674, 0xc0},
    {0x3675, 0xa0}, {0x3676, 0xa0}, {0x3677, 0x92}, {0x3678, 0x96},
    {0x3679, 0x9a}, {0x367c, 0x03}, {0x367d, 0x0f}, {0x367e, 0x01},
    {0x367f, 0x0f}, {0x3698, 0x83}, {0x3699, 0x86}, {0x369a, 0x8c},
    {0x369b, 0x94}, {0x36a2, 0x01}, {0x36a3, 0x03}, {0x36a4, 0x07},
    {0x36ae, 0x0f}, {0x36af, 0x1f}, {0x36bd, 0x22}, {0x36be, 0x22},
    {0x36bf, 0x22}, {0x36d0, 0x01}, {0x370f, 0x02}, {0x3721, 0x6c},
    {0x3722, 0x8d}, {0x3725, 0xc5}, {0x3727, 0x14}, {0x3728, 0x04},
    {0x37b7, 0x04}, {0x37b8, 0x04}, {0x37b9, 0x06}, {0x37bd, 0x07},
    {0x37be, 0x0f}, {0x3901, 0x02}, {0x3903, 0x40}, {0x3905, 0x8d},
    {0x3907, 0x00}, {0x3908, 0x41}, {0x391f, 0x41}, {0x3933, 0x80},
    {0x3934, 0x02}, {0x3937, 0x6f}, {0x393a, 0x01}, {0x393d, 0x01},
    {0x393e, 0xc0}, {0x39dd, 0x41}, {0x3e00, 0x00}, {0x3e01, 0x4d},
    {0x3e02, 0xc0}, {0x3e06, 0x00}, {0x3e07, 0x80}, {0x3e09, 0x00},
    {0x4509, 0x28}, {0x450d, 0x61},
    {SC202CS_REG_END, 0x00},
};

// Structures et types pour la gestion des paramètres
typedef struct {
    uint8_t dgain_fine;
    uint8_t dgain_coarse;
    uint8_t analog_gain;
} sc202cs_gain_t;

typedef struct {
    uint32_t exposure_val;
    uint32_t gain_index;
    uint32_t vflip_en : 1;
    uint32_t hmirror_en : 1;
} sc202cs_para_t;

struct sc202cs_cam {
    sc202cs_para_t sc202cs_para;
};

// Macros pour l'exposition
#define SC202CS_FETCH_EXP_H(val) (((val) >> 12) & 0xF)
#define SC202CS_FETCH_EXP_M(val) (((val) >> 4) & 0xFF)
#define SC202CS_FETCH_EXP_L(val) (((val)&0xF) << 4)

// Tables de gain complètes (192 entrées)
static const uint32_t sc202cs_abs_gain_val_map[] = {
    1000,  1031,  1063,  1094,  1125,  1156,  1188,  1219,
    1250,  1281,  1313,  1344,  1375,  1406,  1438,  1469,
    1500,  1531,  1563,  1594,  1625,  1656,  1688,  1719,
    1750,  1781,  1813,  1844,  1875,  1906,  1938,  1969,
    2000,  2062,  2126,  2188,  2250,  2312,  2376,  2438,
    2500,  2562,  2626,  2688,  2750,  2812,  2876,  2938,
    3000,  3062,  3126,  3188,  3250,  3312,  3376,  3438,
    3500,  3562,  3626,  3688,  3750,  3812,  3876,  3938,
    4000,  4124,  4252,  4376,  4500,  4624,  4752,  4876,
    5000,  5124,  5252,  5376,  5500,  5624,  5752,  5876,
    6000,  6124,  6252,  6376,  6500,  6624,  6752,  6876,
    7000,  7124,  7252,  7376,  7500,  7624,  7752,  7876,
    8000,  8248,  8504,  8752,  9000,  9248,  9504,  9752,
    10000, 10248, 10504, 10752, 11000, 11248, 11504, 11752,
    12000, 12248, 12504, 12752, 13000, 13248, 13504, 13752,
    14000, 14248, 14504, 14752, 15000, 15248, 15504, 15752,
    16000, 16496, 17008, 17504, 18000, 18496, 19008, 19504,
    20000, 20496, 21008, 21504, 22000, 22496, 23008, 23504,
    24000, 24496, 25008, 25504, 26000, 26496, 27008, 27504,
    28000, 28496, 29008, 29504, 30000, 30496, 31008, 31504,
    32000, 33008, 34000, 35008, 36000, 37008, 38000, 39008,
    40000, 41008, 42000, 43008, 44000, 45008, 46000, 47008,
    48000, 49008, 50000, 51008, 52000, 53008, 54000, 55008,
    56000, 57008, 58000, 59008, 60000, 61008, 62000, 63008,
};

static const sc202cs_gain_t sc202cs_gain_map[] = {
    {0x80, 0x00, 0x00}, {0x84, 0x00, 0x00}, {0x88, 0x00, 0x00}, {0x8c, 0x00, 0x00},
    {0x90, 0x00, 0x00}, {0x94, 0x00, 0x00}, {0x98, 0x00, 0x00}, {0x9c, 0x00, 0x00},
    {0xa0, 0x00, 0x00}, {0xa4, 0x00, 0x00}, {0xa8, 0x00, 0x00}, {0xac, 0x00, 0x00},
    {0xb0, 0x00, 0x00}, {0xb4, 0x00, 0x00}, {0xb8, 0x00, 0x00}, {0xbc, 0x00, 0x00},
    {0xc0, 0x00, 0x00}, {0xc4, 0x00, 0x00}, {0xc8, 0x00, 0x00}, {0xcc, 0x00, 0x00},
    {0xd0, 0x00, 0x00}, {0xd4, 0x00, 0x00}, {0xd8, 0x00, 0x00}, {0xdc, 0x00, 0x00},
    {0xe0, 0x00, 0x00}, {0xe4, 0x00, 0x00}, {0xe8, 0x00, 0x00}, {0xec, 0x00, 0x00},
    {0xf0, 0x00, 0x00}, {0xf4, 0x00, 0x00}, {0xf8, 0x00, 0x00}, {0xfc, 0x00, 0x00},
    {0x80, 0x00, 0x01}, {0x84, 0x00, 0x01}, {0x88, 0x00, 0x01}, {0x8c, 0x00, 0x01},
    {0x90, 0x00, 0x01}, {0x94, 0x00, 0x01}, {0x98, 0x00, 0x01}, {0x9c, 0x00, 0x01},
    {0xa0, 0x00, 0x01}, {0xa4, 0x00, 0x01}, {0xa8, 0x00, 0x01}, {0xac, 0x00, 0x01},
    {0xb0, 0x00, 0x01}, {0xb4, 0x00, 0x01}, {0xb8, 0x00, 0x01}, {0xbc, 0x00, 0x01},
    {0xc0, 0x00, 0x01}, {0xc4, 0x00, 0x01}, {0xc8, 0x00, 0x01}, {0xcc, 0x00, 0x01},
    {0xd0, 0x00, 0x01}, {0xd4, 0x00, 0x01}, {0xd8, 0x00, 0x01}, {0xdc, 0x00, 0x01},
    {0xe0, 0x00, 0x01}, {0xe4, 0x00, 0x01}, {0xe8, 0x00, 0x01}, {0xec, 0x00, 0x01},
    {0xf0, 0x00, 0x01}, {0xf4, 0x00, 0x01}, {0xf8, 0x00, 0x01}, {0xfc, 0x00, 0x01},
    {0x80, 0x00, 0x03}, {0x84, 0x00, 0x03}, {0x88, 0x00, 0x03}, {0x8c, 0x00, 0x03},
    {0x90, 0x00, 0x03}, {0x94, 0x00, 0x03}, {0x98, 0x00, 0x03}, {0x9c, 0x00, 0x03},
    {0xa0, 0x00, 0x03}, {0xa4, 0x00, 0x03}, {0xa8, 0x00, 0x03}, {0xac, 0x00, 0x03},
    {0xb0, 0x00, 0x03}, {0xb4, 0x00, 0x03}, {0xb8, 0x00, 0x03}, {0xbc, 0x00, 0x03},
    {0xc0, 0x00, 0x03}, {0xc4, 0x00, 0x03}, {0xc8, 0x00, 0x03}, {0xcc, 0x00, 0x03},
    {0xd0, 0x00, 0x03}, {0xd4, 0x00, 0x03}, {0xd8, 0x00, 0x03}, {0xdc, 0x00, 0x03},
    {0xe0, 0x00, 0x03}, {0xe4, 0x00, 0x03}, {0xe8, 0x00, 0x03}, {0xec, 0x00, 0x03},
    {0xf0, 0x00, 0x03}, {0xf4, 0x00, 0x03}, {0xf8, 0x00, 0x03}, {0xfc, 0x00, 0x03},
    {0x80, 0x00, 0x07}, {0x84, 0x00, 0x07}, {0x88, 0x00, 0x07}, {0x8c, 0x00, 0x07},
    {0x90, 0x00, 0x07}, {0x94, 0x00, 0x07}, {0x98, 0x00, 0x07}, {0x9c, 0x00, 0x07},
    {0xa0, 0x00, 0x07}, {0xa4, 0x00, 0x07}, {0xa8, 0x00, 0x07}, {0xac, 0x00, 0x07},
    {0xb0, 0x00, 0x07}, {0xb4, 0x00, 0x07}, {0xb8, 0x00, 0x07}, {0xbc, 0x00, 0x07},
    {0xc0, 0x00, 0x07}, {0xc4, 0x00, 0x07}, {0xc8, 0x00, 0x07}, {0xcc, 0x00, 0x07},
    {0xd0, 0x00, 0x07}, {0xd4, 0x00, 0x07}, {0xd8, 0x00, 0x07}, {0xdc, 0x00, 0x07},
    {0xe0, 0x00, 0x07}, {0xe4, 0x00, 0x07}, {0xe8, 0x00, 0x07}, {0xec, 0x00, 0x07},
    {0xf0, 0x00, 0x07}, {0xf4, 0x00, 0x07}, {0xf8, 0x00, 0x07}, {0xfc, 0x00, 0x07},
    {0x80, 0x00, 0x0f}, {0x84, 0x00, 0x0f}, {0x88, 0x00, 0x0f}, {0x8c, 0x00, 0x0f},
    {0x90, 0x00, 0x0f}, {0x94, 0x00, 0x0f}, {0x98, 0x00, 0x0f}, {0x9c, 0x00, 0x0f},
    {0xa0, 0x00, 0x0f}, {0xa4, 0x00, 0x0f}, {0xa8, 0x00, 0x0f}, {0xac, 0x00, 0x0f},
    {0xb0, 0x00, 0x0f}, {0xb4, 0x00, 0x0f}, {0xb8, 0x00, 0x0f}, {0xbc, 0x00, 0x0f},
    {0xc0, 0x00, 0x0f}, {0xc4, 0x00, 0x0f}, {0xc8, 0x00, 0x0f}, {0xcc, 0x00, 0x0f},
    {0xd0, 0x00, 0x0f}, {0xd4, 0x00, 0x0f}, {0xd8, 0x00, 0x0f}, {0xdc, 0x00, 0x0f},
    {0xe0, 0x00, 0x0f}, {0xe4, 0x00, 0x0f}, {0xe8, 0x00, 0x0f}, {0xec, 0x00, 0x0f},
    {0xf0, 0x00, 0x0f}, {0xf4, 0x00, 0x0f}, {0xf8, 0x00, 0x0f}, {0xfc, 0x00, 0x0f},
    {0x80, 0x01, 0x0f}, {0x84, 0x01, 0x0f}, {0x88, 0x01, 0x0f}, {0x8c, 0x01, 0x0f},
    {0x90, 0x01, 0x0f}, {0x94, 0x01, 0x0f}, {0x98, 0x01, 0x0f}, {0x9c, 0x01, 0x0f},
    {0xa0, 0x01, 0x0f}, {0xa4, 0x01, 0x0f}, {0xa8, 0x01, 0x0f}, {0xac, 0x01, 0x0f},
    {0xb0, 0x01, 0x0f}, {0xb4, 0x01, 0x0f}, {0xb8, 0x01, 0x0f}, {0xbc, 0x01, 0x0f},
    {0xc0, 0x01, 0x0f}, {0xc4, 0x01, 0x0f}, {0xc8, 0x01, 0x0f}, {0xcc, 0x01, 0x0f},
    {0xd0, 0x01, 0x0f}, {0xd4, 0x01, 0x0f}, {0xd8, 0x01, 0x0f}, {0xdc, 0x01, 0x0f},
    {0xe0, 0x01, 0x0f}, {0xe4, 0x01, 0x0f}, {0xe8, 0x01, 0x0f}, {0xec, 0x01, 0x0f},
    {0xf0, 0x01, 0x0f}, {0xf4, 0x01, 0x0f}, {0xf8, 0x01, 0x0f}, {0xfc, 0x01, 0x0f},
};

static const uint32_t s_limited_abs_gain = 63008;
static size_t s_limited_abs_gain_index = sizeof(sc202cs_abs_gain_val_map) / sizeof(sc202cs_abs_gain_val_map[0]);

// ISP Info pour 720p
#define SENSOR_ISP_INFO_VERSION_DEFAULT 0
#define ESP_CAM_SENSOR_BAYER_BGGR 3
#define ESP_CAM_SENSOR_EXPOSURE_VAL 0x04020001
#define ESP_CAM_SENSOR_GAIN 0x04020002
#define ESP_CAM_SENSOR_VFLIP 0x04000010
#define ESP_CAM_SENSOR_HMIRROR 0x04000011

// Fonctions SCCB de base
static esp_err_t sc202cs_write(esp_sccb_io_handle_t handle, uint16_t reg, uint8_t data) {
    return esp_sccb_transmit_reg_a16v8(handle, reg, data);
}

static esp_err_t sc202cs_read(esp_sccb_io_handle_t handle, uint16_t reg, uint8_t *data) {
    return esp_sccb_transmit_receive_reg_a16v8(handle, reg, data);
}

static esp_err_t sc202cs_write_array(esp_sccb_io_handle_t handle, sc202cs_reginfo_t *regarray) {
    int i = 0;
    esp_err_t ret = ESP_OK;
    while ((ret == ESP_OK) && regarray[i].reg != SC202CS_REG_END) {
        if (regarray[i].reg != SC202CS_REG_DELAY) {
            ret = sc202cs_write(handle, regarray[i].reg, regarray[i].val);
        } else {
            delay_ms(regarray[i].val);
        }
        i++;
    }
    return ret;
}

static esp_err_t sc202cs_set_reg_bits(esp_sccb_io_handle_t handle, 
                                       uint16_t reg, 
                                       uint8_t offset, 
                                       uint8_t length,
                                       uint8_t value) {
    esp_err_t ret = ESP_OK;
    uint8_t reg_data = 0;

    ret = sc202cs_read(handle, reg, &reg_data);
    if (ret != ESP_OK) {
        return ret;
    }
    
    uint8_t mask = ((1 << length) - 1) << offset;
    value = (reg_data & ~mask) | ((value << offset) & mask);
    ret = sc202cs_write(handle, reg, value);
    return ret;
}

static esp_err_t sc202cs_set_mirror(esp_cam_sensor_device_t *dev, int enable) {
    return sc202cs_set_reg_bits(dev->sccb_handle, 0x3221, 1, 2, enable ? 0x03 : 0x00);
}

static esp_err_t sc202cs_set_vflip(esp_cam_sensor_device_t *dev, int enable) {
    return sc202cs_set_reg_bits(dev->sccb_handle, 0x3221, 5, 2, enable ? 0x03 : 0x00);
}

static esp_err_t sc202cs_get_sensor_id(esp_cam_sensor_device_t *dev, esp_cam_sensor_id_t *id) {
    uint8_t pid_h, pid_l;
    esp_err_t ret = sc202cs_read(dev->sccb_handle, SC202CS_REG_SENSOR_ID_H, &pid_h);
    if (ret != ESP_OK) return ret;
    
    ret = sc202cs_read(dev->sccb_handle, SC202CS_REG_SENSOR_ID_L, &pid_l);
    if (ret != ESP_OK) return ret;
    
    id->pid = (pid_h << 8) | pid_l;
    return ESP_OK;
}

static esp_err_t sc202cs_set_stream(esp_cam_sensor_device_t *dev, int enable) {
    esp_err_t ret = sc202cs_write(dev->sccb_handle, SC202CS_REG_SLEEP_MODE, enable ? 0x01 : 0x00);
    dev->stream_status = enable;
    ESP_LOGI(SC202CS_TAG, "Stream=%d", enable);
    return ret;
}

// Fonctions de gestion des paramètres
static esp_err_t sc202cs_set_para_value(esp_cam_sensor_device_t *dev, uint32_t id, const void *arg, size_t size) {
    esp_err_t ret = ESP_OK;
    uint32_t u32_val = *(uint32_t *)arg;
    struct sc202cs_cam *cam_sc202cs = (struct sc202cs_cam *)dev->priv;
    
    if (!cam_sc202cs) return ESP_ERR_INVALID_ARG;
    
    switch (id) {
        case ESP_CAM_SENSOR_EXPOSURE_VAL: {
            uint32_t max_exp = 1244;
            if (u32_val > max_exp) u32_val = max_exp;
            if (u32_val < 0xff) u32_val = 0xff;
            
            ESP_LOGD(SC202CS_TAG, "Set exposure: 0x%X (max=0x%X)", u32_val, max_exp);
            
            ret = sc202cs_write(dev->sccb_handle, SC202CS_REG_SHUTTER_TIME_H, SC202CS_FETCH_EXP_H(u32_val));
            ret |= sc202cs_write(dev->sccb_handle, SC202CS_REG_SHUTTER_TIME_M, SC202CS_FETCH_EXP_M(u32_val));
            ret |= sc202cs_write(dev->sccb_handle, SC202CS_REG_SHUTTER_TIME_L, SC202CS_FETCH_EXP_L(u32_val));
            
            if (ret == ESP_OK) {
                cam_sc202cs->sc202cs_para.exposure_val = u32_val;
            }
            break;
        }
        
        case ESP_CAM_SENSOR_GAIN: {
            if (u32_val >= s_limited_abs_gain_index) {
                u32_val = s_limited_abs_gain_index - 1;
            }
            
            ESP_LOGD(SC202CS_TAG, "Set gain[%u]: abs_gain=%u, DFine=0x%02X, DCoarse=0x%02X, Analog=0x%02X",
                     u32_val,
                     sc202cs_abs_gain_val_map[u32_val],
                     sc202cs_gain_map[u32_val].dgain_fine,
                     sc202cs_gain_map[u32_val].dgain_coarse,
                     sc202cs_gain_map[u32_val].analog_gain);
            
            ret = sc202cs_write(dev->sccb_handle, SC202CS_REG_DIG_FINE_GAIN, 
                               sc202cs_gain_map[u32_val].dgain_fine);
            ret |= sc202cs_write(dev->sccb_handle, SC202CS_REG_DIG_COARSE_GAIN, 
                                sc202cs_gain_map[u32_val].dgain_coarse);
            ret |= sc202cs_write(dev->sccb_handle, SC202CS_REG_ANG_GAIN, 
                                sc202cs_gain_map[u32_val].analog_gain);
            
            if (ret == ESP_OK) {
                cam_sc202cs->sc202cs_para.gain_index = u32_val;
            }
            break;
        }
        
        case ESP_CAM_SENSOR_VFLIP: {
            int *value = (int *)arg;
            ret = sc202cs_set_vflip(dev, *value);
            break;
        }
        
        case ESP_CAM_SENSOR_HMIRROR: {
            int *value = (int *)arg;
            ret = sc202cs_set_mirror(dev, *value);
            break;
        }
        
        default:
            ESP_LOGE(SC202CS_TAG, "Paramètre non supporté: 0x%08X", id);
            ret = ESP_ERR_INVALID_ARG;
            break;
    }
    
    return ret;
}

static esp_err_t sc202cs_set_format(esp_cam_sensor_device_t *dev, const void *format) {
    const sc202cs_reginfo_t *reg_list = init_reglist_1280x720_30fps;
    
    ESP_LOGI(SC202CS_TAG, "Configuration: 1280x720 @ 30fps (RAW8 -> RGB565)");
    
    if (reg_list == NULL) {
        ESP_LOGE(SC202CS_TAG, "Liste de registres invalide");
        return ESP_FAIL;
    }
    
    esp_err_t ret = sc202cs_write_array(dev->sccb_handle, (sc202cs_reginfo_t*)reg_list);
    
    if (ret != ESP_OK) {
        ESP_LOGE(SC202CS_TAG, "Set format failed: %d", ret);
        return ret;
    }
    
    if (dev->priv) {
        struct sc202cs_cam *cam_sc202cs = (struct sc202cs_cam *)dev->priv;
        cam_sc202cs->sc202cs_para.exposure_val = 0x4dc;
        cam_sc202cs->sc202cs_para.gain_index = 0;
        ESP_LOGI(SC202CS_TAG, "Paramètres par défaut: exp=0x%X, gain_idx=%u", 
                 cam_sc202cs->sc202cs_para.exposure_val,
                 cam_sc202cs->sc202cs_para.gain_index);
    }
    
    ESP_LOGI(SC202CS_TAG, "✓ Format 1280x720 configuré avec succès");
    return ESP_OK;
}

static esp_err_t sc202cs_priv_ioctl(esp_cam_sensor_device_t *dev, uint32_t cmd, void *arg) {
    esp_err_t ret = ESP_OK;
    
    switch (cmd) {
        case 0x04000004:
            ret = sc202cs_set_stream(dev, *(int*)arg);
            break;
            
        case ESP_CAM_SENSOR_VFLIP:
            ret = sc202cs_set_vflip(dev, *(int*)arg);
            ESP_LOGI(SC202CS_TAG, "VFlip: %d", *(int*)arg);
            break;
            
        case ESP_CAM_SENSOR_HMIRROR:
            ret = sc202cs_set_mirror(dev, *(int*)arg);
            ESP_LOGI(SC202CS_TAG, "HMirror: %d", *(int*)arg);
            break;
            
        case ESP_CAM_SENSOR_EXPOSURE_VAL:
        case ESP_CAM_SENSOR_GAIN:
            ret = sc202cs_set_para_value(dev, cmd, arg, sizeof(uint32_t));
            break;
            
        default:
            ESP_LOGD(SC202CS_TAG, "IOCTL non géré: 0x%08X", cmd);
            ret = ESP_ERR_NOT_SUPPORTED;
            break;
    }
    
    return ret;
}

static esp_err_t sc202cs_delete(esp_cam_sensor_device_t *dev) {
    if (dev) {
        if (dev->priv) {
            free(dev->priv);
            dev->priv = NULL;
        }
        free(dev);
    }
    return ESP_OK;
}

static const esp_cam_sensor_ops_t sc202cs_ops = {
    .set_format = (int (*)(esp_cam_sensor_device_t*, const void*))sc202cs_set_format,
    .priv_ioctl = (int (*)(esp_cam_sensor_device_t*, uint32_t, void*))sc202cs_priv_ioctl,
    .del = (int (*)(esp_cam_sensor_device_t*))sc202cs_delete,
};

esp_cam_sensor_device_t *sc202cs_detect(esp_cam_sensor_config_t *config) {
    if (!config) return NULL;
    
    for (size_t i = 0; i < sizeof(sc202cs_abs_gain_val_map) / sizeof(sc202cs_abs_gain_val_map[0]); i++) {
        if (sc202cs_abs_gain_val_map[i] > s_limited_abs_gain) {
            s_limited_abs_gain_index = i - 1;
            break;
        }
    }
    
    ESP_LOGI(SC202CS_TAG, "Gain limité à: %u (index: %u)", s_limited_abs_gain, s_limited_abs_gain_index);
    
    esp_cam_sensor_device_t *dev = (esp_cam_sensor_device_t*)calloc(1, sizeof(esp_cam_sensor_device_t));
    if (!dev) {
        ESP_LOGE(SC202CS_TAG, "Pas de mémoire pour le device");
        return NULL;
    }
    
    struct sc202cs_cam *cam_sc202cs = (struct sc202cs_cam*)calloc(1, sizeof(struct sc202cs_cam));
    if (!cam_sc202cs) {
        ESP_LOGE(SC202CS_TAG, "Pas de mémoire pour priv");
        free(dev);
        return NULL;
    }
    
    dev->name = (char*)SC202CS_SENSOR_NAME;
    dev->sccb_handle = config->sccb_handle;
    dev->xclk_pin = config->xclk_pin;
    dev->reset_pin = config->reset_pin;
    dev->pwdn_pin = config->pwdn_pin;
    dev->sensor_port = config->sensor_port;
    dev->ops = &sc202cs_ops;
    dev->priv = cam_sc202cs;
    
    if (sc202cs_get_sensor_id(dev, &dev->id) != ESP_OK) {
        ESP_LOGE(SC202CS_TAG, "Échec lecture ID capteur");
        free(cam_sc202cs);
        free(dev);
        return NULL;
    }
    
    if (dev->id.pid != SC202CS_PID) {
        ESP_LOGE(SC202CS_TAG, "PID incorrect: 0x%04x (attendu: 0x%04x)", dev->id.pid, SC202CS_PID);
        free(cam_sc202cs);
        free(dev);
        return NULL;
    }
    
    ESP_LOGI(SC202CS_TAG, "✓ SC202CS détecté");
    ESP_LOGI(SC202CS_TAG, "  - PID: 0x%04X", dev->id.pid);
    ESP_LOGI(SC202CS_TAG, "  - Niveaux de gain: %u (1.0x à %.1fx)", 
             s_limited_abs_gain_index, 
             sc202cs_abs_gain_val_map[s_limited_abs_gain_index - 1] / 1000.0f);
    ESP_LOGI(SC202CS_TAG, "  - Exposition: 0x%03X à 0x%03X", 0xff, 0x4dc);
    
    return dev;
}

} // extern "C"

#endif  // USE_ESP32_VARIANT_ESP32P4

// ============================================================================
// CODE TAB5 CAMERA
// ============================================================================

namespace esphome {
namespace tab5_camera {

static const char *const TAG = "tab5_camera";

void Tab5Camera::setup() {
  ESP_LOGI(TAG, "🎥 Initialisation Tab5 Camera");
  
#ifdef USE_ESP32_VARIANT_ESP32P4
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(20);
  }
  
  if (this->pwdn_pin_ != nullptr) {
    this->pwdn_pin_->setup();
    this->pwdn_pin_->digital_write(false);
  }
  
  if (!this->init_sensor_()) {
    ESP_LOGE(TAG, "❌ Échec init capteur");
    this->mark_failed();
    return;
  }
  
  if (!this->init_ldo_()) {
    ESP_LOGE(TAG, "❌ Échec init LDO");
    this->mark_failed();
    return;
  }
  
  if (!this->init_csi_()) {
    ESP_LOGE(TAG, "❌ Échec init CSI");
    this->mark_failed();
    return;
  }
  
  if (!this->init_isp_()) {
    ESP_LOGE(TAG, "❌ Échec init ISP");
    this->mark_failed();
    return;
  }
  
  if (!this->allocate_buffer_()) {
    ESP_LOGE(TAG, "❌ Échec allocation buffer");
    this->mark_failed();
    return;
  }
  
  this->initialized_ = true;
  ESP_LOGI(TAG, "✅ Caméra prête");
  
#else
  ESP_LOGE(TAG, "❌ ESP32-P4 requis");
  this->mark_failed();
#endif
}

#ifdef USE_ESP32_VARIANT_ESP32P4

bool Tab5Camera::init_sensor_() {
  ESP_LOGI(TAG, "Init capteur SC202CS (1280x720)");
  
  sccb_i2c_config_t sccb_config = {};
  sccb_config.device_address = this->sensor_address_;
  sccb_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  sccb_config.scl_speed_hz = 400000;
  sccb_config.addr_bits_width = 16;
  sccb_config.val_bits_width = 8;
  
  esp_sccb_io_handle_t sccb_handle;
  esp_err_t ret = sccb_new_i2c_io_esphome(this, &sccb_config, &sccb_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SCCB init failed: %d", ret);
    return false;
  }
  
  ESP_LOGI(TAG, "✓ SCCB initialisé");
  
  esp_cam_sensor_config_t sensor_config = {};
  sensor_config.sccb_handle = sccb_handle;
  sensor_config.reset_pin = -1;
  sensor_config.pwdn_pin = -1;
  sensor_config.xclk_pin = (int8_t)this->external_clock_pin_;
  sensor_config.xclk_freq_hz = this->external_clock_frequency_;
  sensor_config.sensor_port = ESP_CAM_SENSOR_MIPI_CSI;
  
  this->sensor_device_ = sc202cs_detect(&sensor_config);
  
  if (this->sensor_device_ == nullptr) {
    ESP_LOGE(TAG, "SC202CS detection failed");
    return false;
  }
  
  if (sc202cs_set_format(this->sensor_device_, NULL) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set format");
    free(this->sensor_device_);
    this->sensor_device_ = nullptr;
    return false;
  }
  
  if (this->flip_mirror_) {
    int enable_flip = 1;
    int enable_mirror = 1;
    
    esp_err_t ret_flip = esp_cam_sensor_ioctl(this->sensor_device_, 0x04000010, &enable_flip);
    esp_err_t ret_mirror = esp_cam_sensor_ioctl(this->sensor_device_, 0x04000011, &enable_mirror);
    
    if (ret_flip == ESP_OK && ret_mirror == ESP_OK) {
      ESP_LOGI(TAG, "✓ Flip/Mirror activé");
    } else {
      ESP_LOGW(TAG, "⚠ Flip/Mirror partiellement activé (flip=%d, mirror=%d)", ret_flip, ret_mirror);
    }
  }
  
  ESP_LOGI(TAG, "✓ SC202CS détecté (PID: 0x%04X)", this->sensor_device_->id.pid);
  return true;
}

bool Tab5Camera::init_ldo_() {
  ESP_LOGI(TAG, "Init LDO MIPI");
  
  esp_ldo_channel_config_t ldo_config = {
    .chan_id = 3,
    .voltage_mv = 2500,
  };
  
  esp_err_t ret = esp_ldo_acquire_channel(&ldo_config, &this->ldo_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LDO failed: %d", ret);
    return false;
  }
  
  ESP_LOGI(TAG, "✓ LDO OK (2.5V)");
  return true;
}

bool Tab5Camera::init_csi_() {
  ESP_LOGI(TAG, "Init MIPI-CSI pour 1280x720");
  
  esp_cam_ctlr_csi_config_t csi_config = {};
  csi_config.ctlr_id = 0;
  csi_config.clk_src = MIPI_CSI_PHY_CLK_SRC_DEFAULT;
  csi_config.h_res = 1280;
  csi_config.v_res = 720;
  csi_config.lane_bit_rate_mbps = 576;
  csi_config.input_data_color_type = CAM_CTLR_COLOR_RAW8;
  csi_config.output_data_color_type = CAM_CTLR_COLOR_RGB565;
  csi_config.data_lane_num = 1;
  csi_config.byte_swap_en = false;
  csi_config.queue_items = 1;  // Réduit à 1 pour éviter crash SDIO
  
  esp_err_t ret = esp_cam_new_csi_ctlr(&csi_config, &this->csi_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "CSI failed: %d", ret);
    return false;
  }
  
  esp_cam_ctlr_evt_cbs_t callbacks = {
    .on_get_new_trans = Tab5Camera::on_csi_new_frame_,
    .on_trans_finished = Tab5Camera::on_csi_frame_done_,
  };
  
  ret = esp_cam_ctlr_register_event_callbacks(this->csi_handle_, &callbacks, this);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Callbacks failed: %d", ret);
    return false;
  }
  
  ret = esp_cam_ctlr_enable(this->csi_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Enable CSI failed: %d", ret);
    return false;
  }
  
  ESP_LOGI(TAG, "✓ CSI configuré (queue_items=1 pour stabilité)");
  return true;
}

bool Tab5Camera::init_isp_() {
  ESP_LOGI(TAG, "Init ISP pour 1280x720");
  
  esp_isp_processor_cfg_t isp_config = {};
  isp_config.clk_src = ISP_CLK_SRC_DEFAULT;
  isp_config.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
  isp_config.input_data_color_type = ISP_COLOR_RAW8;
  isp_config.output_data_color_type = ISP_COLOR_RGB565;
  isp_config.h_res = 1280;
  isp_config.v_res = 720;
  isp_config.has_line_start_packet = false;
  isp_config.has_line_end_packet = false;
  isp_config.clk_hz = 120000000;
  isp_config.bayer_order = (color_raw_element_order_t)0;  // RGGB pour SC202CS
  
  esp_err_t ret = esp_isp_new_processor(&isp_config, &this->isp_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Échec création ISP: 0x%x", ret);
    return false;
  }
  
  ret = esp_isp_enable(this->isp_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Échec activation ISP: 0x%x", ret);
    esp_isp_del_processor(this->isp_handle_);
    this->isp_handle_ = nullptr;
    return false;
  }
  
  ESP_LOGI(TAG, "✓ ISP initialisé pour 1280x720 (120 MHz, RGGB)");
  
  this->configure_isp_color_correction_();
  
  return true;
}

void Tab5Camera::configure_isp_color_correction_() {
  ESP_LOGI(TAG, "Configuration corrections couleur");
  
#ifdef CONFIG_ISP_COLOR_ENABLED
  esp_isp_color_config_t color_config = {};
  color_config.color_contrast = {145, 145, 145};
  color_config.color_saturation = {135, 135, 135};
  color_config.color_hue = 0;
  color_config.color_brightness = 60;
  
  esp_err_t ret = esp_isp_color_configure(this->isp_handle_, &color_config);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "✓ Corrections couleur configurées");
  } else {
    ESP_LOGW(TAG, "Échec configuration couleur: %s", esp_err_to_name(ret));
  }
#endif

  if (this->sensor_device_) {
    int awb_value = 1;
    esp_err_t ret = esp_cam_sensor_ioctl(this->sensor_device_, 0x03010001, &awb_value);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "✓ AWB activé");
    } else {
      ESP_LOGW(TAG, "AWB non supporté (erreur: 0x%x)", ret);
    }
  }
}

bool Tab5Camera::allocate_buffer_() {
  // Calculer la taille du buffer (1280x720 RGB565 = 2 bytes par pixel)
  this->frame_buffer_size_ = 1280 * 720 * 2;
  
  // Arrondir à un multiple de 64 pour l'alignement SDIO
  size_t aligned_size = (this->frame_buffer_size_ + 63) & ~63;
  
  ESP_LOGI(TAG, "Allocation buffers: %u bytes (aligné: %u)", 
           this->frame_buffer_size_, aligned_size);
  
  // Allouer le premier buffer avec alignement strict
  this->frame_buffers_[0] = (uint8_t*)heap_caps_aligned_alloc(
    64, aligned_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA
  );
  
  if (!this->frame_buffers_[0]) {
    ESP_LOGE(TAG, "❌ Échec allocation buffer 0");
    return false;
  }
  
  // Initialiser à zéro pour éviter les données corrompues
  memset(this->frame_buffers_[0], 0, aligned_size);
  
  // Allouer le second buffer
  this->frame_buffers_[1] = (uint8_t*)heap_caps_aligned_alloc(
    64, aligned_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA
  );
  
  if (!this->frame_buffers_[1]) {
    ESP_LOGE(TAG, "❌ Échec allocation buffer 1");
    heap_caps_free(this->frame_buffers_[0]);
    this->frame_buffers_[0] = nullptr;
    return false;
  }
  
  // Initialiser à zéro
  memset(this->frame_buffers_[1], 0, aligned_size);
  
  // Mettre à jour la taille avec la taille alignée
  this->frame_buffer_size_ = aligned_size;
  
  // Définir le buffer actuel
  this->current_frame_buffer_ = this->frame_buffers_[0];
  
  ESP_LOGI(TAG, "✓ Buffers alloués:");
  ESP_LOGI(TAG, "  - Buffer 0: %p", this->frame_buffers_[0]);
  ESP_LOGI(TAG, "  - Buffer 1: %p", this->frame_buffers_[1]);
  ESP_LOGI(TAG, "  - Taille: 2x%u bytes (aligné 64)", aligned_size);
  
  return true;
}

bool IRAM_ATTR Tab5Camera::on_csi_new_frame_(
  esp_cam_ctlr_handle_t handle,
  esp_cam_ctlr_trans_t *trans,
  void *user_data
) {
  Tab5Camera *cam = (Tab5Camera*)user_data;
  
  if (!cam || !trans) {
    return false;
  }
  
  // Vérifier que le buffer est valide
  uint8_t current_buf = cam->buffer_index_;
  if (current_buf >= 2 || cam->frame_buffers_[current_buf] == nullptr) {
    return false;
  }
  
  trans->buffer = cam->frame_buffers_[current_buf];
  trans->buflen = cam->frame_buffer_size_;
  
  return false;
}

bool IRAM_ATTR Tab5Camera::on_csi_frame_done_(
  esp_cam_ctlr_handle_t handle,
  esp_cam_ctlr_trans_t *trans,
  void *user_data
) {
  Tab5Camera *cam = (Tab5Camera*)user_data;
  
  if (!cam || !trans) {
    return false;
  }
  
  // Vérifier que la frame est valide et complète
  if (trans->received_size > 0 && 
      trans->received_size <= cam->frame_buffer_size_) {
    
    // Marquer la frame comme prête
    cam->frame_ready_ = true;
    
    // Passer au buffer suivant (double buffering)
    cam->buffer_index_ = (cam->buffer_index_ + 1) % 2;
  }
  
  return false;
}

CameraResolutionInfo Tab5Camera::get_resolution_info_() const {
  return {1280, 720};
}

bool Tab5Camera::apply_sensor_params_() {
  if (!this->sensor_device_) {
    return false;
  }
  
  bool success = true;
  
  if (!this->auto_gain_ && this->manual_gain_index_ > 0) {
    ESP_LOGI(TAG, "Application du gain manuel: index %u", this->manual_gain_index_);
    if (!this->set_gain(this->manual_gain_index_)) {
      ESP_LOGW(TAG, "Échec application gain manuel");
      success = false;
    }
  } else {
    ESP_LOGI(TAG, "Gain automatique activé");
  }
  
  if (!this->auto_exposure_ && this->manual_exposure_val_ > 0) {
    ESP_LOGI(TAG, "Application de l'exposition manuelle: 0x%03X", this->manual_exposure_val_);
    if (!this->set_exposure(this->manual_exposure_val_)) {
      ESP_LOGW(TAG, "Échec application exposition manuelle");
      success = false;
    }
  } else {
    ESP_LOGI(TAG, "Exposition automatique activée");
  }
  
  return success;
}

bool Tab5Camera::set_gain(uint32_t gain_index) {
  if (!this->sensor_device_ || !this->initialized_) {
    ESP_LOGW(TAG, "Capteur non initialisé");
    return false;
  }
  
  if (gain_index > 191) {
    ESP_LOGW(TAG, "Gain index %u hors limites, limité à 191", gain_index);
    gain_index = 191;
  }
  
  esp_err_t ret = esp_cam_sensor_ioctl(
    this->sensor_device_, 
    0x04020002,
    &gain_index
  );
  
  if (ret == ESP_OK) {
    this->current_gain_index_ = gain_index;
    ESP_LOGD(TAG, "Gain réglé à l'index %u", gain_index);
    return true;
  } else {
    ESP_LOGE(TAG, "Échec réglage gain: 0x%x", ret);
    return false;
  }
}

bool Tab5Camera::set_exposure(uint32_t exposure_val) {
  if (!this->sensor_device_ || !this->initialized_) {
    ESP_LOGW(TAG, "Capteur non initialisé");
    return false;
  }
  
  if (exposure_val < 0xFF) {
    ESP_LOGW(TAG, "Exposition %u trop faible, limitée à 0xFF", exposure_val);
    exposure_val = 0xFF;
  }
  if (exposure_val > 0x4DC) {
    ESP_LOGW(TAG, "Exposition %u trop élevée, limitée à 0x4DC", exposure_val);
    exposure_val = 0x4DC;
  }
  
  esp_err_t ret = esp_cam_sensor_ioctl(
    this->sensor_device_, 
    0x04020001,
    &exposure_val
  );
  
  if (ret == ESP_OK) {
    this->current_exposure_val_ = exposure_val;
    ESP_LOGD(TAG, "Exposition réglée à 0x%03X (%u lines)", exposure_val, exposure_val);
    return true;
  } else {
    ESP_LOGE(TAG, "Échec réglage exposition: 0x%x", ret);
    return false;
  }
}

bool Tab5Camera::start_streaming() {
  if (!this->initialized_) {
    ESP_LOGE(TAG, "Caméra non initialisée");
    return false;
  }
  
  if (this->streaming_) {
    ESP_LOGW(TAG, "Déjà en streaming");
    return true;
  }
  
  ESP_LOGI(TAG, "Démarrage streaming...");
  
  // Réinitialiser les états
  this->frame_ready_ = false;
  this->buffer_index_ = 0;
  
  // Démarrer le capteur SC202CS
  if (this->sensor_device_) {
    int enable = 1;
    esp_err_t ret = esp_cam_sensor_ioctl(
      this->sensor_device_, 
      0x04000004,  // ESP_CAM_SENSOR_IOC_S_STREAM
      &enable
    );
    
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "❌ Échec démarrage capteur: 0x%x", ret);
      return false;
    }
    
    ESP_LOGI(TAG, "✓ Capteur démarré");
    
    // Délai pour stabilisation du capteur
    delay(150);
    
    // Appliquer les paramètres de gain et exposition
    this->apply_sensor_params_();
  }
  
  // Démarrer le contrôleur CSI
  esp_err_t ret = esp_cam_ctlr_start(this->csi_handle_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ Échec démarrage CSI: 0x%x", ret);
    
    // Arrêter le capteur en cas d'échec
    if (this->sensor_device_) {
      int enable = 0;
      esp_cam_sensor_ioctl(this->sensor_device_, 0x04000004, &enable);
    }
    
    return false;
  }
  
  this->streaming_ = true;
  
  ESP_LOGI(TAG, "✅ Streaming actif (1280x720 @ 30fps)");
  return true;
}

bool Tab5Camera::stop_streaming() {
  if (!this->streaming_) {
    return true;
  }
  
  esp_cam_ctlr_stop(this->csi_handle_);
  
  if (this->sensor_device_) {
    int enable = 0;
    esp_cam_sensor_ioctl(this->sensor_device_, 0x04000004, &enable);
  }
  
  this->streaming_ = false;
  ESP_LOGI(TAG, "⏹ Streaming arrêté");
  return true;
}

bool Tab5Camera::capture_frame() {
  if (!this->streaming_) {
    return false;
  }
  
  // Vérification atomique du flag
  bool was_ready = this->frame_ready_;
  if (!was_ready) {
    return false;
  }
  
  // Réinitialiser le flag AVANT de copier le buffer
  this->frame_ready_ = false;
  
  // Utiliser le buffer qui vient d'être complété
  // buffer_index_ pointe sur le buffer EN COURS de remplissage
  // Donc le buffer complété est l'autre
  uint8_t last_complete_buffer = (this->buffer_index_ + 1) % 2;
  
  // Vérifier que le buffer est valide
  if (this->frame_buffers_[last_complete_buffer] != nullptr) {
    this->current_frame_buffer_ = this->frame_buffers_[last_complete_buffer];
    return true;
  }
  
  ESP_LOGW(TAG, "Buffer invalide détecté");
  return false;
}

uint16_t Tab5Camera::get_image_width() const {
  return 1280;
}

uint16_t Tab5Camera::get_image_height() const {
  return 720;
}

#endif  // USE_ESP32_VARIANT_ESP32P4

void Tab5Camera::loop() {
  // Tout est géré par les callbacks ISR
}

void Tab5Camera::dump_config() {
  ESP_LOGCONFIG(TAG, "Tab5 Camera:");
  ESP_LOGCONFIG(TAG, "  Capteur: SC202CS @ 0x%02X", this->sensor_address_);
  ESP_LOGCONFIG(TAG, "  Résolution: %ux%u", 
                this->get_image_width(), this->get_image_height());
  ESP_LOGCONFIG(TAG, "  Format: RGB565");
  ESP_LOGCONFIG(TAG, "  Flip/Mirror: %s", this->flip_mirror_ ? "OUI" : "NON");
  
  ESP_LOGCONFIG(TAG, "  Gain: %s", this->auto_gain_ ? "AUTO" : "MANUEL");
  if (!this->auto_gain_) {
    ESP_LOGCONFIG(TAG, "    Gain manuel: index %u", this->manual_gain_index_);
  }
  
  ESP_LOGCONFIG(TAG, "  Exposition: %s", this->auto_exposure_ ? "AUTO" : "MANUELLE");
  if (!this->auto_exposure_) {
    ESP_LOGCONFIG(TAG, "    Exposition manuelle: 0x%03X (%u lines)", 
                  this->manual_exposure_val_, this->manual_exposure_val_);
  }
  
  ESP_LOGCONFIG(TAG, "  Streaming: %s", this->streaming_ ? "OUI" : "NON");
  ESP_LOGCONFIG(TAG, "  Initialisé: %s", this->initialized_ ? "OUI" : "NON");
}

}  // namespace tab5_camera
}  // namespace esphome
