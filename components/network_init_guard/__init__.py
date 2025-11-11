"""
Network Initialization Guard Component

Prevents "assert failed: netif_add (netif already added)" crash on ESP32-P4
by wrapping lwIP's netif_add() function to check for duplicate additions.

This fixes a race condition or bug in ESP-IDF 5.4.x where netif_add() can be
called twice on the same network interface.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = []

network_init_guard_ns = cg.esphome_ns.namespace("network_init_guard")
NetworkInitGuard = network_init_guard_ns.class_("NetworkInitGuard", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(NetworkInitGuard),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Verify ESP-IDF framework
    if not CORE.using_esp_idf:
        raise cv.Invalid(
            "Network Init Guard requires ESP-IDF framework. "
            "Add 'framework: type: esp-idf' to your configuration."
        )

    # Add linker flag to wrap netif_add function
    # This allows us to intercept calls to netif_add and check for duplicates
    cg.add_build_flag("-Wl,--wrap=netif_add")

    # Add lwIP include directories for netif.h
    cg.add_build_flag("-I$IDF_PATH/components/lwip/lwip/src/include")
    cg.add_build_flag("-I$IDF_PATH/components/lwip/port/include")

    # Suppress warnings for this component
    cg.add_build_flag("-Wno-unused-function")
