"""
imlib - Image Processing Library for ESP32 Camera
Provides zero-copy drawing functions for real-time video overlay
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Auto-generate namespace
imlib_ns = cg.esphome_ns.namespace("imlib")

# Empty config schema - imlib is a C library with no YAML config
CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    """
    Register imlib component and add compilation flags
    """
    # Add build flag to enable imlib drawing functions
    cg.add_build_flag("-DENABLE_IMLIB_DRAWING")

    # Add to global build flags for ESP-IDF
    cg.add_platformio_option("build_flags", ["-DENABLE_IMLIB_DRAWING"])
