import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation

DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@youkorr"]

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDSICamComponent = mipi_dsi_cam_ns.class_("MipiDSICamComponent", cg.Component)
# Alias pour compatibilité avec lvgl_camera_display (pointe vers la même classe C++)
MipiDsiCam = mipi_dsi_cam_ns.class_("MipiDSICamComponent", cg.Component)

# Actions
CaptureSnapshotAction = mipi_dsi_cam_ns.class_("CaptureSnapshotAction", automation.Action)

CONF_NAME = "name"
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
CONF_MIPI_DSI_CAM_ID = "mipi_dsi_cam_id"
CONF_FILENAME = "filename"

# Résolutions supportées
RESOLUTIONS = {
    "QVGA": (320, 240),
    "VGA": (640, 480),
    "480P": (640, 480),
    "720P": (1280, 720),
    "1080P": (1920, 1080),
}

# Formats de pixels supportés
PIXEL_FORMATS = ["RGB565", "YUYV", "UYVY", "NV12", "JPEG", "MJPEG", "H264"]

def validate_resolution(value):
    """Valide la résolution (preset ou format WxH)"""
    if isinstance(value, str):
        value = value.upper()
        if value in RESOLUTIONS:
            return value
        # Essayer de parser WxH
        parts = value.split('x')
        if len(parts) == 2:
            try:
                w = int(parts[0])
                h = int(parts[1])
                if w > 0 and h > 0 and w <= 1920 and h <= 1080:
                    return value
            except ValueError:
                pass
    raise cv.Invalid(f"Résolution invalide: {value}. Utilisez un preset (QVGA, VGA, 480P, 720P, 1080P) ou le format WIDTHxHEIGHT (ex: 1280x720)")

def validate_i2c_id(value):
    """Valide l'ID I2C (entier 0-1 ou nom de bus comme string)"""
    if isinstance(value, int):
        if 0 <= value <= 1:
            return value
        raise cv.Invalid(f"i2c_id doit être 0 ou 1, reçu: {value}")
    elif isinstance(value, str):
        # Accepter un nom de bus I2C comme "bsp_bus"
        return value
    raise cv.Invalid(f"i2c_id doit être un entier (0-1) ou une chaîne, reçu: {type(value)}")

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MipiDSICamComponent),
    cv.Optional(CONF_NAME): cv.string,
    cv.Optional(CONF_SENSOR, default="sc202cs"): cv.string,
    cv.Optional(CONF_I2C_ID, default=0): validate_i2c_id,
    cv.Optional(CONF_LANE, default=1): cv.int_range(min=1, max=2),
    cv.Optional(CONF_EXTCLK_PIN, default="GPIO36"): cv.string,
    cv.Optional(CONF_FREQ, default=24000000): cv.int_range(min=1000000, max=50000000),
    cv.Optional(CONF_ADDR, default=0x36): cv.hex_int_range(min=0x00, max=0x7F),
    cv.Optional(CONF_RESOLUTION, default="720P"): validate_resolution,
    cv.Optional(CONF_PIXEL_FMT, default="JPEG"): cv.enum(
        {fmt: fmt for fmt in PIXEL_FORMATS}, upper=True
    ),
    cv.Optional(CONF_FRAMERATE, default=30): cv.int_range(min=1, max=60),
    cv.Optional(CONF_JPEG_QUALITY, default=10): cv.int_range(min=1, max=100),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_sensor_type(config[CONF_SENSOR]))
    cg.add(var.set_i2c_id(config[CONF_I2C_ID]))
    cg.add(var.set_lane(config[CONF_LANE]))
    cg.add(var.set_xclk_pin(config[CONF_EXTCLK_PIN]))
    cg.add(var.set_xclk_freq(config[CONF_FREQ]))
    cg.add(var.set_sensor_addr(config[CONF_ADDR]))
    cg.add(var.set_resolution(config[CONF_RESOLUTION]))
    cg.add(var.set_pixel_format(config[CONF_PIXEL_FMT]))
    cg.add(var.set_framerate(config[CONF_FRAMERATE]))
    cg.add(var.set_jpeg_quality(config[CONF_JPEG_QUALITY]))

    cg.add_define("USE_MIPI_DSI_CAM_SNAPSHOT_SERVICE")


# Action pour capturer un snapshot
@automation.register_action(
    "mipi_dsi_cam.capture_snapshot",
    CaptureSnapshotAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(MipiDSICamComponent),
        cv.Required(CONF_FILENAME): cv.templatable(cv.string),
    }),
)
async def capture_snapshot_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    
    template_ = await cg.templatable(config[CONF_FILENAME], args, cg.std_string)
    cg.add(var.set_filename(template_))
    
    return var
