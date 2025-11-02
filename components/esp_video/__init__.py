"""
Composant ESPHome pour ESP-Video d'Espressif (v1.3.1)

Ce composant wrappe le framework ESP-Video complet pour ESPHome,
permettant l'utilisation de l'API V4L2-like sur ESP32-P4.

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
    
    Cette fonction:
    1. Détecte automatiquement le chemin du composant (fonctionne depuis GitHub)
    2. Configure tous les chemins d'include
    3. Active les build flags nécessaires
    4. Ajoute automatiquement le script de build PlatformIO
    """
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Vérifier que ESP-IDF est utilisé
    if CORE.using_arduino:
        raise cv.Invalid(
            "esp_video nécessite 'framework: type: esp-idf' (pas Arduino)\n"
            "Ajoutez dans votre YAML:\n"
            "esp32:\n"
            "  framework:\n"
            "    type: esp-idf\n"
            "    version: 5.4.2"
        )
    
    # ========================================================================
    # AUTO-DÉTECTION DU CHEMIN (fonctionne depuis GitHub ou local)
    # ========================================================================
    
    # Trouver où ce fichier __init__.py est situé
    component_dir = os.path.dirname(os.path.abspath(__file__))
    
    # Calculer le chemin relatif depuis le répertoire du projet ESPHome
    try:
        rel_path = os.path.relpath(component_dir, CORE.config_dir)
    except ValueError:
        # Sur Windows, si sur des lecteurs différents
        rel_path = component_dir
    
    # Log pour debug
    cg.add(cg.RawExpression(f'// ESP-Video component at: {rel_path}'))
    
    # ========================================================================
    # CHEMINS D'INCLUDE
    # ========================================================================
    
    # Headers publics ESP-Video
    cg.add_build_flag(f"-I{rel_path}/include")
    cg.add_build_flag(f"-I{rel_path}/include/linux")
    cg.add_build_flag(f"-I{rel_path}/include/sys")
    cg.add_build_flag(f"-I{rel_path}/src")
    cg.add_build_flag(f"-I{rel_path}/src/device")
    
    # Headers privés
    cg.add_build_flag(f"-I{rel_path}/private_include")
    
    # ========================================================================
    # BUILD FLAGS - Configuration ESP-Video
    # ========================================================================
    
    # Device CSI (MIPI-CSI camera interface)
    cg.add_build_flag("-DCONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1")
    
    # Device ISP (Image Signal Processor)
    cg.add_build_flag("-DCONFIG_ESP_VIDEO_ENABLE_ISP=1")
    cg.add_build_flag("-DCONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1")
    cg.add_build_flag("-DCONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1")
    
    # Utiliser le heap allocator (recommandé pour ESPHome)
    cg.add_build_flag("-DCONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1")
    
    # Target ESP32-P4
    cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32P4=1")
    
    # ========================================================================
    # BUILD SCRIPT - Compilation des sources .c
    # ========================================================================
    
    # Le script de build qui compile tous les fichiers .c d'ESP-Video
    build_script_path = os.path.join(component_dir, "esp_video_build.py")
    
    if not os.path.exists(build_script_path):
        raise cv.Invalid(
            f"Le script de build ESP-Video est introuvable: {build_script_path}\n"
            f"Assurez-vous que esp_video_build.py existe dans le composant esp_video."
        )
    
    # Ajouter le script aux extra_scripts de PlatformIO
    # ESPHome va automatiquement l'exécuter pendant le build
    cg.add_platformio_option("extra_scripts", [f"post:{rel_path}/esp_video_build.py"])
    
    # Message de confirmation
    cg.add(cg.RawExpression(f'// ESP-Video build script: {rel_path}/esp_video_build.py'))
    
    # ========================================================================
    # DÉFINIR LA VERSION
    # ========================================================================
    
    cg.add_define("ESP_VIDEO_VERSION", '"1.3.1"')
    cg.add_define("ESP_VIDEO_ESPHOME_WRAPPER", '"1.0.0"')


