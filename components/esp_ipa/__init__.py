"""
Composant ESPHome pour ESP IPA (Image Processing Algorithms)
Dépendance d'ESP-Video
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE
import os

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32"]

# Ce composant est une bibliothèque uniquement, pas de configuration utilisateur
CONFIG_SCHEMA = cv.invalid("esp_ipa est un composant interne, ne pas l'utiliser directement dans le YAML")

async def to_code(config):
    """Configure le composant esp_ipa pour ESPHome"""
    component_dir = os.path.dirname(os.path.abspath(__file__))

    # Ajouter l'include public
    inc_path = os.path.join(component_dir, "include")
    if os.path.exists(inc_path):
        cg.add_build_flag(f"-I{inc_path}")

    # NOTE: Les sources (version.c) et la bibliothèque précompilée (libesp_ipa.a)
    # sont gérées par esp_video_build.py (script PlatformIO)
    # Ne pas utiliser cg.add_library() ici pour éviter la double compilation
