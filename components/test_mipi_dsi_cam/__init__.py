import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components import sd_card

DEPENDENCIES = ["esp32", "sd_card"]
CODEOWNERS = ["@youkorr"]

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDSICamComponent = mipi_dsi_cam_ns.class_("MipiDSICamComponent", cg.Component)

CONF_SENSOR = "sensor"
CONF_I2C_ID = "i2c_id"
CONF_LANE = "lane"
CONF_EXTCLK_PIN = "external_clock_pin"
CONF_FREQ = "frequency"
CONF_ADDR = "address_sensor"
CONF_RESOLUTION = "resolution"
CONF_PIXEL_FMT = "pixel_format"
CONF_FRAMERATE = "framerate"
CONF_JPEG_QUALITY = "jpeg_quality"
CONF_SD_CARD_ID = "sd_card_id"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MipiDSICamComponent),
    cv.Optional(CONF_SENSOR, default="sc202cs"): cv.string,
    cv.Optional(CONF_I2C_ID, default=0): cv.int_,
    cv.Optional(CONF_LANE, default=1): cv.int_,
    cv.Optional(CONF_EXTCLK_PIN, default="GPIO36"): cv.string,
    cv.Optional(CONF_FREQ, default=24000000): cv.int_,
    cv.Optional(CONF_ADDR, default=0x36): cv.int_,
    cv.Optional(CONF_RESOLUTION, default="720P"): cv.string,
    cv.Optional(CONF_PIXEL_FMT, default="JPEG"): cv.string,
    cv.Optional(CONF_FRAMERATE, default=30): cv.int_,
    cv.Optional(CONF_JPEG_QUALITY, default=10): cv.int_,
    cv.Optional(CONF_SD_CARD_ID): cv.use_id(sd_card.SDCardComponent),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_sensor_type(0))
    cg.add(var.set_i2c_id(config[CONF_I2C_ID]))
    cg.add(var.set_lane(config[CONF_LANE]))
    cg.add(var.set_xclk_pin(config[CONF_EXTCLK_PIN]))
    cg.add(var.set_xclk_freq(config[CONF_FREQ]))
    cg.add(var.set_sensor_addr(config[CONF_ADDR]))
    cg.add(var.set_resolution(config[CONF_RESOLUTION]))
    cg.add(var.set_pixel_format(config[CONF_PIXEL_FMT]))
    cg.add(var.set_framerate(config[CONF_FRAMERATE]))
    cg.add(var.set_jpeg_quality(config[CONF_JPEG_QUALITY]))

    # Associer la carte SD si présente
    if CONF_SD_CARD_ID in config:
        sd = await cg.get_variable(config[CONF_SD_CARD_ID])
        cg.add(var.set_sd_card(sd))
        cg.add_define("USE_SD_CARD")

    # Déclarer un service pour déclencher un snapshot depuis Home Assistant / API
    cg.add_define("USE_MIPI_DSI_CAM_SNAPSHOT_SERVICE")

