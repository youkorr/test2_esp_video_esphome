"""
ESP-DL shared component for ESPHome
Provides deep learning inference engine for ESP32P4
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
import os

CODEOWNERS = ["@esphome"]

esp_dl_ns = cg.esphome_ns.namespace("esp_dl")

CONFIG_SCHEMA = cv.Schema({}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # Add build flags for ESP32P4
    cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32P4=1")

    # Add include paths
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
    if os.path.exists(esp_dl_dir):
        esp_dl_includes = [
            "dl",
            "dl/tool/include",
            "dl/tool/isa/esp32p4",
            "dl/tool/src",
            "dl/tensor/include",
            "dl/tensor/src",
            "dl/base",
            "dl/base/isa",
            "dl/base/isa/esp32p4",
            "dl/math/include",
            "dl/math/src",
            "dl/model/include",
            "dl/model/src",
            "dl/module/include",
            "dl/module/src",
            "fbs_loader/include",
            "fbs_loader/lib/esp32p4",
            "fbs_loader/src",
            "vision/detect",
            "vision/image",
            "vision/image/isa",
            "vision/image/isa/esp32p4",
            "vision/recognition",
            "vision/classification",
        ]
        for inc in esp_dl_includes:
            inc_path = os.path.join(esp_dl_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")

    # Add build script for compiling ESP-DL sources
    build_script_path = os.path.join(component_dir, "esp_dl_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
