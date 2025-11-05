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
        "sensor/sc202cs/include/private_include",
    ]

    for inc in private_includes:
        inc_path = os.path.join(component_dir, inc)
        if os.path.exists(inc_path):
            cg.add_build_flag(f"-I{inc_path}")

    # Ajouter les sources
    sources = [
        "src/esp_cam_sensor.c",
        "src/esp_cam_motor.c",
        "src/esp_cam_sensor_xclk.c",
        "src/driver_spi/spi_slave.c",
        "src/driver_cam/esp_cam_ctlr_spi_cam.c",
        "sensor/ov5647/ov5647.c",
        "sensor/sc202cs/sc202cs.c",
    ]

    for src in sources:
        src_path = os.path.join(component_dir, src)
        if os.path.exists(src_path):
            cg.add_library(src_path)
