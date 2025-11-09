import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID
from esphome import automation

# Dépendances requises
DEPENDENCIES = ["esp_video"]
# Note: imlib est compilé directement via CMakeLists.txt (pas de dépendance Python)
CODEOWNERS = ["@youkorr"]

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDSICamComponent = mipi_dsi_cam_ns.class_("MipiDSICamComponent", cg.Component)
CaptureSnapshotAction = mipi_dsi_cam_ns.class_("CaptureSnapshotAction", automation.Action)
StartStreamingAction = mipi_dsi_cam_ns.class_("StartStreamingAction", automation.Action)
StopStreamingAction = mipi_dsi_cam_ns.class_("StopStreamingAction", automation.Action)

# Configuration du composant
CONF_SENSOR_TYPE = "sensor_type"
CONF_SENSOR = "sensor"  # Alias pour compatibilité arrière
CONF_I2C_ID = "i2c_id"
CONF_LANE = "lane"
CONF_XCLK_PIN = "xclk_pin"
CONF_EXTERNAL_CLOCK_PIN = "external_clock_pin"  # Alias pour compatibilité arrière
CONF_XCLK_FREQ = "xclk_freq"
CONF_FREQUENCY = "frequency"  # Alias pour compatibilité arrière
CONF_SENSOR_ADDR = "sensor_addr"
CONF_RESOLUTION = "resolution"
CONF_PIXEL_FORMAT = "pixel_format"
CONF_FRAMERATE = "framerate"
CONF_JPEG_QUALITY = "jpeg_quality"
CONF_MIRROR_X = "mirror_x"  # Hardware PPA transform (M5Stack-style)
CONF_MIRROR_Y = "mirror_y"  # Hardware PPA transform
CONF_ROTATION = "rotation"  # Hardware PPA transform (0/90/180/270)
CONF_FILENAME = "filename"
CONF_RGB_GAINS = "rgb_gains"
CONF_RED_GAIN = "red"
CONF_GREEN_GAIN = "green"
CONF_BLUE_GAIN = "blue"

def validate_and_normalize_config(config):
    """Normalise la configuration pour accepter l'ancienne et la nouvelle syntaxe"""
    # Compatibilité: sensor -> sensor_type
    if CONF_SENSOR in config and CONF_SENSOR_TYPE not in config:
        config[CONF_SENSOR_TYPE] = config[CONF_SENSOR]

    # Compatibilité: external_clock_pin -> xclk_pin
    if CONF_EXTERNAL_CLOCK_PIN in config and CONF_XCLK_PIN not in config:
        config[CONF_XCLK_PIN] = config[CONF_EXTERNAL_CLOCK_PIN]

    # Compatibilité: frequency -> xclk_freq
    if CONF_FREQUENCY in config and CONF_XCLK_FREQ not in config:
        config[CONF_XCLK_FREQ] = config[CONF_FREQUENCY]

    # Valeur par défaut pour sensor_type si absent
    if CONF_SENSOR_TYPE not in config:
        config[CONF_SENSOR_TYPE] = "sc202cs"

    return config

CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(MipiDSICamComponent),
        # Nouvelle syntaxe (préférée)
        cv.Optional(CONF_SENSOR_TYPE): cv.string,
        # Ancienne syntaxe (compatibilité)
        cv.Optional(CONF_SENSOR): cv.string,
        cv.Optional(CONF_I2C_ID, default=0): cv.Any(cv.int_, cv.string),
        cv.Optional(CONF_LANE, default=1): cv.int_range(min=1, max=4),
        # Nouvelle syntaxe (préférée)
        cv.Optional(CONF_XCLK_PIN, default="GPIO36"): cv.string,
        # Ancienne syntaxe (compatibilité)
        cv.Optional(CONF_EXTERNAL_CLOCK_PIN): cv.string,
        # Nouvelle syntaxe (préférée)
        cv.Optional(CONF_XCLK_FREQ, default=24000000): cv.int_,
        # Ancienne syntaxe (compatibilité)
        cv.Optional(CONF_FREQUENCY): cv.int_,
        cv.Optional(CONF_SENSOR_ADDR, default=0x36): cv.hex_int,
        cv.Optional(CONF_RESOLUTION, default="720P"): cv.string,
        cv.Optional(CONF_PIXEL_FORMAT, default="JPEG"): cv.string,
        cv.Optional(CONF_FRAMERATE, default=30): cv.int_range(min=1, max=60),
        cv.Optional(CONF_JPEG_QUALITY, default=10): cv.int_range(min=1, max=63),
        # Options obsolètes (acceptées mais ignorées)
        cv.Optional(CONF_MIRROR_X): cv.boolean,
        cv.Optional(CONF_MIRROR_Y): cv.boolean,
        cv.Optional(CONF_ROTATION): cv.int_,
        # Contrôles ISP avancés (CCM RGB gains pour correction couleur)
        cv.Optional(CONF_RGB_GAINS): cv.Schema({
            cv.Optional(CONF_RED_GAIN, default=1.0): cv.float_range(min=0.1, max=4.0),
            cv.Optional(CONF_GREEN_GAIN, default=1.0): cv.float_range(min=0.1, max=4.0),
            cv.Optional(CONF_BLUE_GAIN, default=1.0): cv.float_range(min=0.1, max=4.0),
        }),
    }).extend(cv.COMPONENT_SCHEMA),
    validate_and_normalize_config
)

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

    # Configuration mirror/rotate (PPA hardware M5Stack-style)
    if CONF_MIRROR_X in config:
        cg.add(var.set_mirror_x(config[CONF_MIRROR_X]))
    if CONF_MIRROR_Y in config:
        cg.add(var.set_mirror_y(config[CONF_MIRROR_Y]))
    if CONF_ROTATION in config:
        cg.add(var.set_rotation(config[CONF_ROTATION]))

    # Configuration des gains RGB CCM si présents
    if CONF_RGB_GAINS in config:
        rgb_config = config[CONF_RGB_GAINS]
        cg.add(var.set_rgb_gains_config(
            rgb_config[CONF_RED_GAIN],
            rgb_config[CONF_GREEN_GAIN],
            rgb_config[CONF_BLUE_GAIN]
        ))

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

# Action pour démarrer le streaming vidéo
@automation.register_action(
    "mipi_dsi_cam.start_streaming",
    StartStreamingAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(MipiDSICamComponent),
    })
)
async def start_streaming_action_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg)
    cg.add(var.set_parent(paren))
    return var

# Action pour arrêter le streaming vidéo
@automation.register_action(
    "mipi_dsi_cam.stop_streaming",
    StopStreamingAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(MipiDSICamComponent),
    })
)
async def stop_streaming_action_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg)
    cg.add(var.set_parent(paren))
    return var
