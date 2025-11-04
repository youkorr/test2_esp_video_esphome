"""
ESPHome Component : MIPI-DSI Camera (ESP-Video)
Supporte liaison carte SD (sd_id) et action snapshot
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components import sd_card

DEPENDENCIES = ["esp32", "sd_card"]
AUTO_LOAD = ["esp32"]

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDSICamComponent = mipi_dsi_cam_ns.class_("MipiDSICamComponent", cg.Component)

# Configuration YAML
CONF_NAME = "name"
CONF_SENSOR = "sensor"
CONF_I2C_ID = "i2c_id"
CONF_LANE = "lane"
CONF_XCLK_PIN = "external_clock_pin"
CONF_FREQUENCY = "frequency"
CONF_ADDR_SENSOR = "address_sensor"
CONF_RESOLUTION = "resolution"
CONF_PIXEL_FORMAT = "pixel_format"
CONF_FRAMERATE = "framerate"
CONF_JPEG_QUALITY = "jpeg_quality"
CONF_SD_ID = "sd_id"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MipiDSICamComponent),
    cv.GenerateID(CONF_SD_ID): cv.use_id(sd_card.SDCardComponent),
    cv.Required(CONF_NAME): cv.string,
    cv.Required(CONF_SENSOR): cv.string,
    cv.Required(CONF_I2C_ID): cv.int_,
    cv.Optional(CONF_LANE, default=1): cv.int_,
    cv.Required(CONF_XCLK_PIN): cv.string,
    cv.Required(CONF_FREQUENCY): cv.int_,
    cv.Required(CONF_ADDR_SENSOR): cv.int_,
    cv.Optional(CONF_RESOLUTION, default="720P"): cv.string,
    cv.Optional(CONF_PIXEL_FORMAT, default="RGB565"): cv.string,
    cv.Optional(CONF_FRAMERATE, default=30): cv.int_,
    cv.Optional(CONF_JPEG_QUALITY, default=10): cv.int_,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    sd_var = await cg.get_variable(config[CONF_SD_ID])
    cg.add(var.set_sd_card(sd_var))

    cg.add_define("USE_ESP32_VARIANT_ESP32P4")

    cg.add(cg.RawExpression('// [MIPI-DSI-CAM] SD intégré + snapshot prêt ✅'))


# Service pour snapshot
@cg.register_action("mipi_dsi_cam.snapshot", MipiDSICamComponent)
def snapshot_action(config, action_id, args):
    """Déclenche une capture snapshot via ESPHome/HA."""
    path = config.get("path", "snapshots/img_latest.jpg")
    return cg.new_Pvariable(action_id, var.capture_snapshot_to_file(path))

