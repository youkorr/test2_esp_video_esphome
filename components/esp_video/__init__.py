"""ESP-Video component for ESPHome with all includes."""

import logging
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@youkorr"]
AUTO_LOAD = []
DEPENDENCIES = []

esp_video_ns = cg.esphome_ns.namespace("esp_video")

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    """Generate code for ESP-Video component."""
    
    # Trouver le dossier du composant
    component_dir = Path(__file__).parent
    
    _LOGGER.info("[ESP-Video] Configuration des includes...")
    
    # Liste de tous les dossiers à inclure
    include_dirs = [
        component_dir / "deps" / "include",           # Stubs (PRIORITÉ 1)
        component_dir / "include",                    # Headers publics ESP-Video
        component_dir / "include" / "linux",          # Headers Linux (V4L2)
        component_dir / "include" / "sys",            # Headers sys
        component_dir / "private_include",            # Headers privés
    ]
    
    # Vérifier les stubs critiques (4 fichiers maintenant)
    critical_stubs = [
        component_dir / "deps" / "include" / "esp_cam_sensor.h",
        component_dir / "deps" / "include" / "esp_cam_sensor_xclk.h",
        component_dir / "deps" / "include" / "esp_sccb_i2c.h",
        component_dir / "deps" / "include" / "esp_cam_sensor_types.h",
        component_dir / "deps" / "include" / "esp_cam_motor_types.h", # NOUVEAU
    ]
    
    for stub in critical_stubs:
        if not stub.exists():
            raise cv.Invalid(
                f"ESP-Video stub manquant: {stub.name}. "
                f"Vérifiez que deps/include/ contient tous les stubs requis."
            )
        _LOGGER.info(f"[ESP-Video]   ✓ {stub.name}")
    
    # Vérifier les headers ESP-Video critiques
    critical_headers = [
        component_dir / "include" / "esp_video_init.h",
        component_dir / "include" / "esp_video_device.h",
    ]
    
    for header in critical_headers:
        if not header.exists():
            raise cv.Invalid(
                f"ESP-Video header manquant: {header.name}. "
                f"Vérifiez l'intégrité du composant ESP-Video."
            )
        _LOGGER.info(f"[ESP-Video]   ✓ {header.name}")
    
    # Construire les flags d'include
    build_flags = []
    
    for inc_dir in include_dirs:
        if inc_dir.exists():
            build_flags.append(f"-I{inc_dir}")
            _LOGGER.info(f"[ESP-Video]   ➕ {inc_dir.relative_to(component_dir)}")
    
    # Ajouter les flags de configuration
    build_flags.extend([
        "-DCONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1",
        "-DCONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1",
        "-DCONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1",
    ])
    
    # Ajouter tous les flags au build
    cg.add_platformio_option("build_flags", build_flags)
    
    _LOGGER.info(f"[ESP-Video] ✅ {len(build_flags)} flags ajoutés au build")
    _LOGGER.info("[ESP-Video] Configuration terminée")


