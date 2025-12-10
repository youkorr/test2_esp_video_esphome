"""
PPA Accelerator Component for ESP32-P4
Hardware-accelerated image processing using ESP32-P4's Pixel Processing Accelerator
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = []
AUTO_LOAD = []

# Only available on ESP32-P4
CODEOWNERS = ["@youkorr"]

ppa_accelerator_ns = cg.esphome_ns.namespace("ppa_accelerator")
PPAAccelerator = ppa_accelerator_ns.class_("PPAAccelerator", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PPAAccelerator),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Add required ESP-IDF component dependency
    cg.add_platformio_option("lib_deps", ["esp_driver_ppa"])

    # Add build flag to enable PPA
    cg.add_define("USE_PPA_ACCELERATOR")
