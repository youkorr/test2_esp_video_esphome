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

    # Ajouter les sources
    sources = [
        "port/src/esp_h264_alloc.c",
        "port/src/esp_h264_alloc_less_than_5_3.c",
        "port/src/esp_h264_cache.c",
        "sw/src/h264_color_convert.c",
        "sw/src/esp_h264_enc_sw_param.c",
        "sw/src/esp_h264_dec_sw.c",
        "sw/src/esp_h264_enc_single_sw.c",
        "hw/src/esp_h264_enc_single_hw.c",
        "interface/include/src/esp_h264_enc_param.c",
        "interface/include/src/esp_h264_enc_param_hw.c",
        "interface/include/src/esp_h264_enc_dual.c",
        "interface/include/src/esp_h264_dec_param.c",
        "interface/include/src/esp_h264_version.c",
        "interface/include/src/esp_h264_dec.c",
        "interface/include/src/esp_h264_enc_single.c",
    ]

    for src in sources:
        src_path = os.path.join(component_dir, src)
        if os.path.exists(src_path):
            cg.add_library(src_path)
