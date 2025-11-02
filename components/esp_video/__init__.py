"""
Composant ESPHome pour ESP-Video d'Espressif (v1.3.1)

Ce composant intègre le framework ESP-Video (V4L2-like) pour ESPHome
et permet la gestion de caméras MIPI-CSI sur ESP32-P4.

Repository: https://github.com/youkorr/test_esp_video_esphome
Author: @youkorr
License: Espressif MIT (pour ESP-Video)
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
    """
    Configure la compilation d'ESP-Video dans ESPHome.

    Cette fonction :
    - détecte le chemin du composant (local ou GitHub)
    - ajoute les includes nécessaires
    - configure les build flags ESP-IDF
    - enregistre le script de build PlatformIO
    """
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # ========================================================================
    # Vérification du framework
    # ========================================================================
    if CORE.using_arduino:
        raise cv.Invalid(
            "esp_video nécessite 'framework: type: esp-idf' (pas Arduino)\n"
            "Ajoutez dans votre YAML :\n"
            "esp32:\n"
            "  framework:\n"
            "    type: esp-idf\n"
            "    version: 5.4.2"
        )

    # ========================================================================
    # Détection automatique du chemin du composant
    # ========================================================================
    component_dir = os.path.dirname(os.path.abspath(__file__))
    try:
        rel_path = os.path.relpath(component_dir, CORE.config_dir)
    except ValueError:
        rel_path = component_dir

    cg.add(cg.RawExpression(f'// [ESP-Video] Component path: {rel_path}'))

    # ========================================================================
    # Includes - headers publics et privés
    # ========================================================================
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
        cg.add_build_flag(f"-I{abs_path}")

    # Ajout prioritaire de deps/include si présent
    deps_include = os.path.join(component_dir, "deps", "include")
    if os.path.exists(deps_include):
        cg.add_build_flag(f"-I{deps_include}")
        cg.add(cg.RawExpression(f'// [ESP-Video] deps/include detected'))

    # ========================================================================
    # Flags de build ESP-Video
    # ========================================================================
    flags = [
        "-DCONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1",
        "-DCONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1",
        "-DCONFIG_IDF_TARGET_ESP32P4=1",
    ]
    for flag in flags:
        cg.add_build_flag(flag)

    # ========================================================================
    # Ajout du script de build PlatformIO
    # ========================================================================
    build_script_path = os.path.join(component_dir, "esp_video_build.py")
    if not os.path.exists(build_script_path):
        raise cv.Invalid(
            f"[ESP-Video] Le script de build est introuvable : {build_script_path}\n"
            f"Assurez-vous que 'esp_video_build.py' est présent dans le composant."
        )

    # Utilisation du chemin absolu au lieu du relatif pour éviter les erreurs
    cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
    cg.add(cg.RawExpression(f'// [ESP-Video] build script added: {build_script_path}'))

    # ========================================================================
    # Définir la version
    # ========================================================================
    cg.add_define("ESP_VIDEO_VERSION", '"1.3.1"')
    cg.add_define("ESP_VIDEO_ESPHOME_WRAPPER", '"1.0.0"')

    # Message de confirmation
    cg.add(cg.RawExpression('// [ESP-Video] Configuration complete'))



