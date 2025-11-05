"""
Composant ESPHome pour ESP SCCB Interface (esp_sccb_intf)
Dépendance d'ESP-Video
"""

import esphome.codegen as cg
import esphome.config_validation as cv
import os

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32"]

# Ce composant est une bibliothèque uniquement, pas de configuration utilisateur
CONFIG_SCHEMA = cv.invalid("esp_sccb_intf est un composant interne, ne pas l'utiliser directement dans le YAML")

async def to_code(config):
    """Configure le composant esp_sccb_intf pour ESPHome"""
    component_dir = os.path.dirname(os.path.abspath(__file__))

    # Ajouter les includes
    includes = [
        "include",
        "interface",
        "sccb_i2c/include",
    ]

    for inc in includes:
        inc_path = os.path.join(component_dir, inc)
        if os.path.exists(inc_path):
            cg.add_build_flag(f"-I{inc_path}")

    # Ajouter les sources
    sources = [
        "src/sccb.c",
        "sccb_i2c/src/sccb_i2c.c",
    ]

    for src in sources:
        src_path = os.path.join(component_dir, src)
        if os.path.exists(src_path):
            cg.add_library(src_path)
