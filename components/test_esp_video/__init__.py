"""
Composant ESPHome pour ESP-Video d'Espressif (v1.3.1)
Support complet H264 + JPEG (sans auto-création de stubs)
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
    # Ajout des includes (ordre prioritaire)
    # -----------------------------------------------------------------------
    include_dirs = [
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
            print(f"[ESP-Video] 📁 Include ajouté : {abs_path}")

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
    # Script post-compilation (optionnel)
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

    cg.add(cg.RawExpression('// [ESP-Video] Configuration complète (H264 + JPEG activés)'))
