"""
Composant ESPHome pour ESP Camera Sensor (esp_cam_sensor)
Dépendance d'ESP-Video
"""

import esphome.codegen as cg
import esphome.config_validation as cv
import os

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32"]

# Ce composant est une bibliothèque uniquement, pas de configuration utilisateur
CONFIG_SCHEMA = cv.invalid("esp_cam_sensor est un composant interne, ne pas l'utiliser directement dans le YAML")

async def to_code(config):
    """Configure le composant esp_cam_sensor pour ESPHome"""
    component_dir = os.path.dirname(os.path.abspath(__file__))

    # Ajouter les includes publics
    public_includes = [
        "include",
        "sensor/ov5647/include",
        "sensor/sc202cs/include",
        "sensor/ov02c10/include",
    ]

    for inc in public_includes:
        inc_path = os.path.join(component_dir, inc)
        if os.path.exists(inc_path):
            cg.add_build_flag(f"-I{inc_path}")

    # Ajouter les includes privés
    private_includes = [
        "src",
        "src/driver_spi",
        "src/driver_cam",
        "sensor/ov5647/private_include",
        "sensor/sc202cs/private_include",
        "sensor/ov02c10/private_include",
    ]

    for inc in private_includes:
        inc_path = os.path.join(component_dir, inc)
        if os.path.exists(inc_path):
            cg.add_build_flag(f"-I{inc_path}")

    # NOTE: Les sources sont compilées par esp_video_build.py (script PlatformIO)
    # Ne pas utiliser cg.add_library() ici pour éviter la double compilation
