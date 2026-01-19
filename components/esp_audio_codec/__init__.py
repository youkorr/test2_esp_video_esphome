"""
ESP Audio Codec - ESP-IDF native audio codec library for ESPHome

This component provides AAC, MP3, and other audio codec support
through Espressif's precompiled esp_audio_codec library.
"""

import esphome.codegen as cg
import os

CODEOWNERS = ["@youkorr"]

# No user configuration needed - this component is used internally
# by other components like simple_video_player

CONFIG_SCHEMA = {}  # Empty schema - no user-facing configuration


async def to_code(config):
    """Configure esp_audio_codec build settings"""

    # Get component directory
    component_dir = os.path.dirname(__file__)

    # Add include paths for codec headers using platformio build_flags
    include_dir = os.path.join(component_dir, "include")
    if os.path.exists(include_dir):
        # Use platformio option to add include path - this gets processed early
        cg.add_platformio_option("build_flags", [f"-I{include_dir}"])

    # Note: ESPHome will automatically compile .c files in src/ directory

    # Link precompiled codec library for ESP32-P4
    lib_dir = os.path.join(component_dir, "lib", "esp32p4")
    if os.path.exists(lib_dir):
        # Add library path
        cg.add_platformio_option("build_flags", [f"-L{lib_dir}"])

        # Link the main audio codec library
        codec_lib = os.path.join(lib_dir, "libesp_audio_codec.a")
        if os.path.exists(codec_lib):
            # Use platformio link flags
            cg.add_platformio_option("build_flags", [f"-Wl,{codec_lib}"])
