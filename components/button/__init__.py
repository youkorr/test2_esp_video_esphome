"""
Button component stub for ESPHome core compatibility.
This is a minimal implementation to satisfy ESPHome core includes.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@esphome/core"]
button_ns = cg.esphome_ns.namespace("button")
Button = button_ns.class_("Button", cg.EntityBase)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Button),
}).extend(cv.ENTITY_BASE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
