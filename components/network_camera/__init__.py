import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_URL

DEPENDENCIES = ["lvgl", "wifi"]
CODEOWNERS = ["@esphome"]

CONF_CANVAS_ID = "canvas_id"
CONF_UPDATE_INTERVAL = "update_interval"
CONF_WIDTH = "width"
CONF_HEIGHT = "height"

network_camera_ns = cg.esphome_ns.namespace("network_camera")
NetworkCamera = network_camera_ns.class_("NetworkCamera", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(NetworkCamera),
    cv.Required(CONF_URL): cv.string,
    cv.Required(CONF_CANVAS_ID): cv.string,
    cv.Required(CONF_WIDTH): cv.int_range(min=16, max=1920),
    cv.Required(CONF_HEIGHT): cv.int_range(min=16, max=1080),
    cv.Optional(CONF_UPDATE_INTERVAL, default="100ms"): cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_url(config[CONF_URL]))
    cg.add(var.set_width(config[CONF_WIDTH]))
    cg.add(var.set_height(config[CONF_HEIGHT]))

    update_interval_ms = config[CONF_UPDATE_INTERVAL].total_milliseconds
    cg.add(var.set_update_interval(int(update_interval_ms)))
