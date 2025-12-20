import esphome.codegen as cg
import os

# ESP-GMF Component for ESPHome
# Provides GMF framework and video processing elements

CODEOWNERS = ["@youkorr"]

async def to_code(config):
    # Add build script for compiling GMF sources
    component_dir = os.path.dirname(__file__)
    build_script_path = os.path.join(component_dir, "esp_gmf_build.py")

    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
