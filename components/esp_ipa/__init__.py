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

    # Ajouter la source version.c
    src_path = os.path.join(component_dir, "src/version.c")
    if os.path.exists(src_path):
        cg.add_library(src_path)

    # Ajouter la bibliothèque précompilée
    # Détecter la variante ESP32 (esp32p4 par défaut)
    variant = CORE.data.get("esp32", {}).get("variant", "esp32p4")
    if not variant:
        variant = "esp32p4"

    lib_path = os.path.join(component_dir, f"lib/{variant}")

    if os.path.exists(lib_path):
        # Ajouter le chemin de la bibliothèque
        cg.add_build_flag(f"-L{lib_path}")
        # Linker la bibliothèque
        cg.add_build_flag("-lesp_ipa")
    else:
        import logging
        logging.warning(f"[esp_ipa] Bibliothèque précompilée non trouvée pour {variant}")
