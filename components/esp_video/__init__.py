"""
Composant ESPHome pour ESP-Video d'Espressif (v1.3.1)
Avec support H264 + JPEG activé
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

    # Vérification du framework
    if CORE.using_arduino:
        raise cv.Invalid(
            "esp_video nécessite 'framework: type: esp-idf'"
        )

    # Détection chemin composant
    component_dir = os.path.dirname(os.path.abspath(__file__))
    
    cg.add(cg.RawExpression(f'// [ESP-Video] Component: {component_dir}'))

    # ========================================================================
    # Vérification CMakeLists.txt (CRITIQUE)
    # ========================================================================
    cmake_file = os.path.join(component_dir, "CMakeLists.txt")
    if not os.path.exists(cmake_file):
        raise cv.Invalid(
            f"[ESP-Video] CMakeLists.txt manquant dans {component_dir}\n"
            "ESP-IDF a besoin de CMakeLists.txt pour compiler ESP-Video.\n"
            "Ajoutez le fichier CMakeLists.txt dans components/esp_video/"
        )
    
    cg.add(cg.RawExpression('// [ESP-Video] CMakeLists.txt trouvé'))

    # ========================================================================
    # Vérification stubs
    # ========================================================================
    deps_include = os.path.join(component_dir, "deps", "include")
    if not os.path.exists(deps_include):
        raise cv.Invalid(
            f"[ESP-Video] deps/include/ manquant dans {component_dir}\n"
            "Créez le dossier deps/include/ et ajoutez les 4 stubs requis."
        )
    
    required_stubs = [
        "esp_cam_sensor.h",
        "esp_cam_sensor_xclk.h",
        "esp_sccb_i2c.h",
        "esp_cam_sensor_types.h",
    ]
    
    for stub in required_stubs:
        stub_path = os.path.join(deps_include, stub)
        if not os.path.exists(stub_path):
            raise cv.Invalid(
                f"[ESP-Video] Stub manquant: {stub}\n"
                f"Ajoutez {stub} dans {deps_include}"
            )
    
    cg.add(cg.RawExpression('// [ESP-Video] 4 stubs vérifiés'))

    # ========================================================================
    # Includes
    # ========================================================================
    include_dirs = [
        "deps/include",     # Stubs en priorité
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

    # ========================================================================
    # FLAGS ESP-Video COMPLETS (avec H264 + JPEG)
    # ========================================================================
    flags = [
        # Core ESP-Video
        "-DCONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1",
        "-DCONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1",
        
        # H264 + JPEG (comme Tab5)
        "-DCONFIG_ESP_VIDEO_ENABLE_HW_H264_VIDEO_DEVICE=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE=1",
        
        # Target
        "-DCONFIG_IDF_TARGET_ESP32P4=1",
    ]
    
    for flag in flags:
        cg.add_build_flag(flag)

    # ========================================================================
    # Build script (optionnel - juste pour logs)
    # ========================================================================
    build_script_path = os.path.join(component_dir, "esp_video_build.py")
    if os.path.exists(build_script_path):
        # Ajouter comme extra_script PlatformIO (pas importé par Python)
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
        cg.add(cg.RawExpression('// [ESP-Video] build script ajouté'))

    # ========================================================================
    # Versions
    # ========================================================================
    cg.add_define("ESP_VIDEO_VERSION", '"1.3.1"')
    cg.add_define("ESP_VIDEO_H264_ENABLED", "1")
    cg.add_define("ESP_VIDEO_JPEG_ENABLED", "1")

    cg.add(cg.RawExpression('// [ESP-Video] Configuration complete (H264+JPEG+CMake)'))



