"""
Composant ESPHome pour ESP H.264 Encoder (esp_h264)
Dépendance d'ESP-Video
"""

import esphome.codegen as cg
import esphome.config_validation as cv
import os

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32"]

# Ce composant est une bibliothèque uniquement, pas de configuration utilisateur
CONFIG_SCHEMA = cv.invalid("esp_h264 est un composant interne, ne pas l'utiliser directement dans le YAML")

async def to_code(config):
    """Configure le composant esp_h264 pour ESPHome"""
    component_dir = os.path.dirname(os.path.abspath(__file__))

    # Ajouter les includes
    includes = [
        "interface/include",
        "port/include",
        "port/inc",
        "sw/include",
        "hw/include",
    ]

    for inc in includes:
        inc_path = os.path.join(component_dir, inc)
        if os.path.exists(inc_path):
            cg.add_build_flag(f"-I{inc_path}")

    # Enable dual-core H.264 decoding on ESP32-P4 for 30-50% performance boost
    # These flags must be defined here (early) for esp_h264_dec_sw.c compilation
    # Priority 17 matches library default for optimal performance
    cg.add_build_flag("-DCONFIG_ESP_H264_DUAL_TASK=1")
    cg.add_build_flag("-DCONFIG_ESP_H264_DUAL_TASK_CORE=1")
    cg.add_build_flag("-DCONFIG_ESP_H264_DUAL_TASK_PRIORITY=17")

    # NOTE: Les sources sont compilées par esp_video_build.py (script PlatformIO)
    # Ne pas utiliser cg.add_library() ici pour éviter la double compilation
