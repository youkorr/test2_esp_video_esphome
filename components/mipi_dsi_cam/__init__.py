"""
Composant ESPHome pour caméra MIPI CSI utilisant V4L2 API

Ce composant suit le pattern de la démo M5Stack:
- Utilise /dev/video0 créé par esp_video_init()
- API V4L2 pure (VIDIOC_*)
- mmap() pour les buffers
- Mutex pour la thread-safety
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_FREQUENCY
from esphome.components import i2c
from esphome import automation

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp_video", "i2c"]
MULTI_CONF = False

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDSICamComponent = mipi_dsi_cam_ns.class_("MipiDSICamComponent", cg.Component)

# Configuration
CONF_I2C_ID = "i2c_id"
CONF_SENSOR = "sensor"
CONF_EXTERNAL_CLOCK_PIN = "external_clock_pin"
CONF_RESOLUTION = "resolution"
CONF_PIXEL_FORMAT = "pixel_format"
CONF_FRAMERATE = "framerate"
CONF_JPEG_QUALITY = "jpeg_quality"
CONF_MIRROR_X = "mirror_x"
CONF_MIRROR_Y = "mirror_y"
CONF_ROTATION = "rotation"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MipiDSICamComponent),
    cv.Required(CONF_I2C_ID): cv.use_id(i2c.I2CBus),
    cv.Required(CONF_SENSOR): cv.string,
    cv.Required(CONF_EXTERNAL_CLOCK_PIN): cv.All(cv.int_, cv.only_on_esp32),
    cv.Optional(CONF_FREQUENCY, default=24000000): cv.int_range(min=1000000, max=40000000),
    cv.Optional(CONF_RESOLUTION, default="720P"): cv.string,
    cv.Optional(CONF_PIXEL_FORMAT, default="RGB565"): cv.string,
    cv.Optional(CONF_FRAMERATE, default=30): cv.int_range(min=1, max=60),
    cv.Optional(CONF_JPEG_QUALITY, default=10): cv.int_range(min=1, max=100),
    cv.Optional(CONF_MIRROR_X, default=True): cv.boolean,  # Comme M5Stack demo
    cv.Optional(CONF_MIRROR_Y, default=False): cv.boolean,
    cv.Optional(CONF_ROTATION, default=0): cv.one_of(0, 90, 180, 270, int=True),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Configuration I2C et sensor
    i2c_bus = await cg.get_variable(config[CONF_I2C_ID])
    cg.add(var.set_i2c_bus(i2c_bus))
    cg.add(var.set_sensor(config[CONF_SENSOR]))
    cg.add(var.set_external_clock_pin(config[CONF_EXTERNAL_CLOCK_PIN]))
    cg.add(var.set_frequency(config[CONF_FREQUENCY]))

    # Configuration résolution/format
    cg.add(var.set_resolution(config[CONF_RESOLUTION]))
    cg.add(var.set_pixel_format(config[CONF_PIXEL_FORMAT]))
    cg.add(var.set_framerate(config[CONF_FRAMERATE]))
    cg.add(var.set_jpeg_quality(config[CONF_JPEG_QUALITY]))

    # Configuration PPA
    cg.add(var.set_mirror_x(config[CONF_MIRROR_X]))
    cg.add(var.set_mirror_y(config[CONF_MIRROR_Y]))
    cg.add(var.set_rotation(config[CONF_ROTATION]))
