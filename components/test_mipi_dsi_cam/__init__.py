import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@youkorr"]

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDSICamComponent = mipi_dsi_cam_ns.class_("MipiDSICamComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MipiDSICamComponent),
    cv.Optional("sensor", default="sc202cs"): cv.string,
    cv.Optional("i2c_id", default=0): cv.int_,
    cv.Optional("lane", default=1): cv.int_,
    cv.Optional("external_clock_pin", default="GPIO36"): cv.string,
    cv.Optional("frequency", default=24000000): cv.int_,
    cv.Optional("address_sensor", default=0x36): cv.int_,
    cv.Optional("resolution", default="720P"): cv.string,
    cv.Optional("pixel_format", default="JPEG"): cv.string,
    cv.Optional("framerate", default=30): cv.int_,
    cv.Optional("jpeg_quality", default=10): cv.int_,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_sensor_type(0))
    cg.add(var.set_i2c_id(config["i2c_id"]))
    cg.add(var.set_lane(config["lane"]))
    cg.add(var.set_xclk_pin(config["external_clock_pin"]))
    cg.add(var.set_xclk_freq(config["frequency"]))
    cg.add(var.set_sensor_addr(config["address_sensor"]))
    cg.add(var.set_resolution(config["resolution"]))
    cg.add(var.set_pixel_format(config["pixel_format"]))
    cg.add(var.set_framerate(config["framerate"]))
    cg.add(var.set_jpeg_quality(config["jpeg_quality"]))
