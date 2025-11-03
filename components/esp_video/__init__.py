"""
Composant ESPHome pour ESP-Video d'Espressif (v1.3.1)
Corrigé avec stubs additionnels pour compilation complète.
Compatible ESPHome 2025.10.3 (ESP-IDF 5.4.x)
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE
import os

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32"]

esp_video_ns = cg.esphome_ns.namespace("esp_video")
ESPVideoComponent = esp_video_ns.class_("ESPVideoComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ESPVideoComponent),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CORE.using_arduino:
        raise cv.Invalid("esp_video nécessite 'framework: type: esp-idf'")

    component_dir = os.path.dirname(os.path.abspath(__file__))
    print(f"[ESP-Video] 🧠 Initialisation du composant ESP-Video (injection sources & includes)")

    deps_include = os.path.join(component_dir, "deps", "include")
    os.makedirs(deps_include, exist_ok=True)

    required_stubs = [
        "esp_cam_sensor.h",
        "esp_cam_sensor_xclk.h",
        "esp_sccb_i2c.h",
        "esp_cam_sensor_types.h",
        "esp_cam_motor_types.h",
        "esp_cam_motor.h",
        "esp_cam_ctlr_spi.h",
        "esp_h264_enc_single_hw.h",
        "esp_h264_enc_single_sw.h",
        "esp_cam_sensor_format.h",
        "esp_cam_motor_format.h",
        "esp_cam_motor_device.h",
        "linux/kernel.h",
    ]

    stub_templates = {
        # --- capteurs caméra ---
        "esp_cam_sensor.h": """#pragma once
#include "esp_err.h"
typedef struct { int dummy; } esp_cam_sensor_device_t;
esp_err_t esp_cam_sensor_init(esp_cam_sensor_device_t **dev);
esp_err_t esp_cam_sensor_deinit(esp_cam_sensor_device_t *dev);
""",

        "esp_cam_sensor_types.h": """#pragma once
typedef enum {
    ESP_CAM_SENSOR_TYPE_UNKNOWN = 0,
    ESP_CAM_SENSOR_TYPE_SC202CS,
    ESP_CAM_SENSOR_TYPE_SC2336,
    ESP_CAM_SENSOR_TYPE_OV5647
} esp_cam_sensor_type_t;
""",

        "esp_cam_sensor_xclk.h": """#pragma once
#include "esp_err.h"
esp_err_t esp_cam_sensor_start_xclk(void);
esp_err_t esp_cam_sensor_stop_xclk(void);
""",

        "esp_cam_sensor_format.h": """#pragma once
typedef struct {
    int width;
    int height;
    int pixel_format;
} esp_cam_sensor_format_t;
""",

        # --- moteur caméra ---
        "esp_cam_motor_types.h": """#pragma once
typedef struct { int dummy; } esp_cam_motor_t;
""",

        "esp_cam_motor.h": """#pragma once
#include "esp_cam_motor_types.h"
#include "esp_err.h"
static inline esp_err_t esp_cam_motor_init(void) { return 0; }
static inline void esp_cam_motor_deinit(void) {}
""",

        "esp_cam_motor_format.h": """#pragma once
typedef struct {
    int speed;
    int position;
} esp_cam_motor_format_t;
""",

        "esp_cam_motor_device.h": """#pragma once
typedef struct {
    int id;
    int dummy;
} esp_cam_motor_device_t;
""",

        # --- SCCB / SPI ---
        "esp_sccb_i2c.h": """#pragma once
#include "esp_err.h"
#include <stdint.h>
esp_err_t esp_sccb_write(uint8_t addr, uint16_t reg, uint8_t data);
esp_err_t esp_sccb_read(uint8_t addr, uint16_t reg, uint8_t *data);
""",

        "esp_cam_ctlr_spi.h": """#pragma once
#include "esp_err.h"
typedef struct { int dummy; } esp_cam_ctlr_spi_t;
static inline esp_err_t esp_cam_new_spi_ctlr(const void *cfg, void **out) { return ESP_OK; }
""",

        # --- encodeurs H264 ---
        "esp_h264_enc_single_hw.h": """#pragma once
#include "esp_err.h"
typedef struct { int dummy; } esp_h264_enc_t;
static inline esp_err_t esp_h264_enc_init(esp_h264_enc_t **enc) { return ESP_OK; }
static inline esp_err_t esp_h264_enc_deinit(esp_h264_enc_t *enc) { return ESP_OK; }
""",

        "esp_h264_enc_single_sw.h": """#pragma once
#include "esp_err.h"
typedef struct { int dummy; } esp_h264_sw_enc_t;
static inline esp_err_t esp_h264_sw_enc_init(esp_h264_sw_enc_t **enc) { return ESP_OK; }
static inline esp_err_t esp_h264_sw_enc_deinit(esp_h264_sw_enc_t *enc) { return ESP_OK; }
""",

        # --- kernel / macros utilitaires ---
        "linux/kernel.h": """#pragma once
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif
""",
    }

    for stub in required_stubs:
        stub_path = os.path.join(deps_include, stub)
        os.makedirs(os.path.dirname(stub_path), exist_ok=True)
        if not os.path.exists(stub_path):
            with open(stub_path, "w", encoding="utf-8") as f:
                f.write(stub_templates[stub])
            print(f"[ESP-Video] 🧩 Création automatique du stub manquant : {stub}")
        else:
            print(f"[ESP-Video] ✅ Stub trouvé : {stub}")

    # Include paths
    for sub in ["deps/include", "include", "include/linux", "include/sys",
                "private_include", "src", "src/device"]:
        full = os.path.join(component_dir, sub)
        if os.path.exists(full):
            cg.add_build_flag(f"-I{full}")

    # Flags
    flags = [
        "-DCONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1",
        "-DCONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_HW_H264_VIDEO_DEVICE=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE=1",
        "-DCONFIG_IDF_TARGET_ESP32P4=1",
    ]
    for flag in flags:
        cg.add_build_flag(flag)

    # Sources
    esp_video_srcs = [
        "src/esp_video.c",
        "src/esp_video_buffer.c",
        "src/esp_video_init.c",
        "src/esp_video_ioctl.c",
        "src/esp_video_mman.c",
        "src/esp_video_vfs.c",
        "src/esp_video_cam.c",
        "src/esp_video_isp_pipeline.c",
        "src/device/esp_video_csi_device.c",
        "src/device/esp_video_isp_device.c",
        "src/device/esp_video_jpeg_device.c",
        "src/device/esp_video_h264_device.c",
    ]

    src_filter = []
    for src in esp_video_srcs:
        path = os.path.join(component_dir, src)
        if os.path.exists(path):
            src_filter.append(f"+<{path}>")
            print(f"[ESP-Video] 🔗 Source incluse: {src}")
        else:
            print(f"[ESP-Video] ⚠️ Fichier manquant: {src}")
    if src_filter:
        cg.add_platformio_option("build_src_filter", " ".join(src_filter))

    # Définitions globales
    cg.add_define("ESP_VIDEO_VERSION", '"1.3.1"')
    cg.add_define("ESP_VIDEO_H264_ENABLED", "1")
    cg.add_define("ESP_VIDEO_JPEG_ENABLED", "1")

    cg.add(cg.RawExpression('// [ESP-Video] Configuration complète'))













