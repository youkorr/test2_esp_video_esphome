"""
Composant ESPHome pour ESP-Video d'Espressif (v1.3.1)
Avec support H264 + JPEG activé et auto-création des stubs
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

    # -----------------------------------------------------------------------
    # Vérification du framework
    # -----------------------------------------------------------------------
    if CORE.using_arduino:
        raise cv.Invalid("esp_video nécessite 'framework: type: esp-idf'")

    # -----------------------------------------------------------------------
    # Détection du chemin du composant
    # -----------------------------------------------------------------------
    component_dir = os.path.dirname(os.path.abspath(__file__))
    cg.add(cg.RawExpression(f'// [ESP-Video] Component: {component_dir}'))

    # -----------------------------------------------------------------------
    # Vérification ou création du dossier deps/include
    # -----------------------------------------------------------------------
    deps_include = os.path.join(component_dir, "deps", "include")
    os.makedirs(deps_include, exist_ok=True)

    required_stubs = [
        "esp_cam_sensor.h",
        "esp_cam_sensor_xclk.h",
        "esp_sccb_i2c.h",
        "esp_cam_sensor_types.h",
        "esp_cam_motor_types.h",
    ]

    # Génération automatique de stubs manquants
    stub_templates = {
        "esp_cam_sensor.h": """#pragma once
#include "esp_err.h"
typedef struct { int dummy; } esp_cam_sensor_device_t;
esp_err_t esp_cam_sensor_init(esp_cam_sensor_device_t **dev);
esp_err_t esp_cam_sensor_deinit(esp_cam_sensor_device_t *dev);
""",

        "esp_cam_sensor_xclk.h": """#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_cam_sensor_start_xclk(void);
esp_err_t esp_cam_sensor_stop_xclk(void);
#ifdef __cplusplus
}
#endif
""",

        "esp_sccb_i2c.h": """#pragma once
#include "esp_err.h"
#include <stdint.h>
esp_err_t esp_sccb_write(uint8_t addr, uint16_t reg, uint8_t data);
esp_err_t esp_sccb_read(uint8_t addr, uint16_t reg, uint8_t *data);
""",

        "esp_cam_sensor_types.h": """#pragma once
typedef enum {
    ESP_CAM_SENSOR_TYPE_UNKNOWN = 0,
    ESP_CAM_SENSOR_TYPE_SC202CS,
    ESP_CAM_SENSOR_TYPE_SC2336,
    ESP_CAM_SENSOR_TYPE_OV5647
} esp_cam_sensor_type_t;
""",

        "esp_cam_motor_types.h": """#pragma once
typedef struct { int dummy; } esp_cam_motor_t;
""",
    }

    for stub in required_stubs:
        stub_path = os.path.join(deps_include, stub)
        if not os.path.exists(stub_path):
            with open(stub_path, "w", encoding="utf-8") as f:
                f.write(stub_templates[stub])
            print(f"[ESP-Video] 🧩 Création automatique du stub manquant : {stub}")
        else:
            print(f"[ESP-Video] ✅ Stub trouvé : {stub}")

    # -----------------------------------------------------------------------
    # Ajout des includes (ordre prioritaire)
    # -----------------------------------------------------------------------
    include_dirs = [
        "deps/include",     # Stubs d’abord
        "include",
        "include/linux",
        "include/sys",
        "src",
        "src/device",
        "private_include",
    ]

    for subdir in include_dirs:
        abs_path = os.path.join(component_dir, subdir)
        if os.path.exists(abs_path):
            cg.add_build_flag(f"-I{abs_path}")

    # -----------------------------------------------------------------------
    # FLAGS ESP-Video COMPLETS (H264 + JPEG)
    # -----------------------------------------------------------------------
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

    # -----------------------------------------------------------------------
    # Build script post compilation
    # -----------------------------------------------------------------------
    build_script_path = os.path.join(component_dir, "esp_video_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
        cg.add(cg.RawExpression('// [ESP-Video] build script ajouté'))
    else:
        print(f"[ESP-Video] ⚠️ Aucun esp_video_build.py trouvé dans {component_dir}")

    # -----------------------------------------------------------------------
    # Définitions globales
    # -----------------------------------------------------------------------
    cg.add_define("ESP_VIDEO_VERSION", '"1.3.1"')
    cg.add_define("ESP_VIDEO_H264_ENABLED", "1")
    cg.add_define("ESP_VIDEO_JPEG_ENABLED", "1")

    cg.add(cg.RawExpression('// [ESP-Video] Configuration complete (auto-stubs + H264 + JPEG)'))




