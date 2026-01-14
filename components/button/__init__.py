"""
Button component stub for ESPHome core compatibility.
This is a minimal implementation to satisfy ESPHome core includes.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@esphome/core"]
button_ns = cg.esphome_ns.namespace("button")
Button = button_ns.class_("Button", cg.EntityBase, cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Button),
}).extend(cv.ENTITY_BASE_SCHEMA).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # Define the button count macro - required by ESPHome core
    # Must be set before any entity registration
    cg.add_define("ESPHOME_ENTITY_BUTTON_COUNT", 0)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cg.register_entity(var, config)


# Also add the define at module level as a fallback
# This ensures it's available even if no button entities are configured
def _setup_button_count():
    cg.add_define("ESPHOME_ENTITY_BUTTON_COUNT", 0)

_setup_button_count()
