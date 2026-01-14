"""
Button component stub for ESPHome core compatibility.
This is a minimal implementation to satisfy ESPHome core includes.
No configuration is required - this component is automatically loaded by LVGL.
"""
import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@esphome/core"]
button_ns = cg.esphome_ns.namespace("button")
Button = button_ns.class_("Button", cg.EntityBase)

# Empty schema - this is just a stub for header compatibility
CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    # No code generation needed - this is just a header stub
    pass
