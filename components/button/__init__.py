"""
Minimal button component for ESPHome core compatibility.

This component provides the button.h/.cpp files required by ESPHome core
(application.h, controller.h) when using external_components.

It does NOT implement any button platforms - those come from ESPHome's
built-in button component or custom platforms.
"""
import esphome.codegen as cg
from esphome.const import CONF_ID
import esphome.config_validation as cv

CODEOWNERS = ["@esphome/core"]
button_ns = cg.esphome_ns.namespace("button")
Button = button_ns.class_("Button", cg.EntityBase)

# Empty config schema - this component is just for C++ headers
CONFIG_SCHEMA = cv.invalid("Button component is for internal use only")


async def to_code(config):
    # This component provides only C++ headers/implementation
    # No code generation needed
    pass
