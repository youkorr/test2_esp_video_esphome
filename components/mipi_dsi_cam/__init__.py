import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID
from esphome import automation

# Dépendances requises
DEPENDENCIES = ["esp_video"]
AUTO_LOAD = []
CODEOWNERS = ["@youkorr"]

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDSICamComponent = mipi_dsi_cam_ns.class_("MipiDSICamComponent", cg.Component)
CaptureSnapshotAction = mipi_dsi_cam_ns.class_("CaptureSnapshotAction", automation.Action)

# Configuration du composant
CONF_SENSOR_TYPE = "sensor_type"
CONF_I2C_ID = "i2c_id"
CONF_LANE = "lane"
CONF_XCLK_PIN = "xclk_pin"
CONF_XCLK_FREQ = "xclk_freq"
CONF_SENSOR_ADDR = "sensor_addr"
CONF_RESOLUTION = "resolution"
CONF_PIXEL_FORMAT = "pixel_format"
CONF_FRAMERATE = "framerate"
CONF_JPEG_QUALITY = "jpeg_quality"
CONF_FILENAME = "filename"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MipiDSICamComponent),
    cv.Optional(CONF_SENSOR_TYPE, default="sc202cs"): cv.string,
    cv.Optional(CONF_I2C_ID, default=0): cv.Any(cv.int_, cv.string),
    cv.Optional(CONF_LANE, default=1): cv.int_range(min=1, max=4),
    cv.Optional(CONF_XCLK_PIN, default="GPIO36"): cv.string,
    cv.Optional(CONF_XCLK_FREQ, default=24000000): cv.int_,
    cv.Optional(CONF_SENSOR_ADDR, default=0x36): cv.hex_int,
    cv.Optional(CONF_RESOLUTION, default="720P"): cv.string,
    cv.Optional(CONF_PIXEL_FORMAT, default="JPEG"): cv.string,
    cv.Optional(CONF_FRAMERATE, default=30): cv.int_range(min=1, max=60),
    cv.Optional(CONF_JPEG_QUALITY, default=10): cv.int_range(min=1, max=63),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    cg.add(var.set_sensor_type(config[CONF_SENSOR_TYPE]))
    
    i2c_id_val = config[CONF_I2C_ID]
    if isinstance(i2c_id_val, int):
        cg.add(var.set_i2c_id(i2c_id_val))
    else:
        cg.add(var.set_i2c_id(str(i2c_id_val)))
    
    cg.add(var.set_lane(config[CONF_LANE]))
    cg.add(var.set_xclk_pin(config[CONF_XCLK_PIN]))
    cg.add(var.set_xclk_freq(config[CONF_XCLK_FREQ]))
    cg.add(var.set_sensor_addr(config[CONF_SENSOR_ADDR]))
    cg.add(var.set_resolution(config[CONF_RESOLUTION]))
    cg.add(var.set_pixel_format(config[CONF_PIXEL_FORMAT]))
    cg.add(var.set_framerate(config[CONF_FRAMERATE]))
    cg.add(var.set_jpeg_quality(config[CONF_JPEG_QUALITY]))

# Action pour capturer un snapshot
@automation.register_action(
    "mipi_dsi_cam.capture_snapshot",
    CaptureSnapshotAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(MipiDSICamComponent),
        cv.Required(CONF_FILENAME): cv.templatable(cv.string),
    })
)
async def capture_snapshot_action_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg)
    cg.add(var.set_parent(paren))
    
    template_ = await cg.templatable(config[CONF_FILENAME], args, cg.std_string)
    cg.add(var.set_filename(template_))
    
    return var
