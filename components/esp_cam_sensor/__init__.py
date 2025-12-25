import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID
from esphome import automation

# Dépendances requises
DEPENDENCIES = ["esp_video"]
# Note: imlib est compilé directement via CMakeLists.txt (pas de dépendance Python)
CODEOWNERS = ["@youkorr"]

esp_cam_sensor_ns = cg.esphome_ns.namespace("esp_cam_sensor")
EspCamSensorComponent = esp_cam_sensor_ns.class_("MipiDSICamComponent", cg.Component)
CaptureSnapshotAction = esp_cam_sensor_ns.class_("CaptureSnapshotAction", automation.Action)
StartStreamingAction = esp_cam_sensor_ns.class_("StartStreamingAction", automation.Action)
StopStreamingAction = esp_cam_sensor_ns.class_("StopStreamingAction", automation.Action)

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
# DEPRECATED: These parameters have been moved to lvgl_camera_display component
# They are kept here only for backward compatibility and will show a warning
CONF_MIRROR_X = "mirror_x"           # DEPRECATED: Use lvgl_camera_display.mirror_x instead
CONF_MIRROR_Y = "mirror_y"           # DEPRECATED: Use lvgl_camera_display.mirror_y instead
CONF_ROTATION = "rotation"           # DEPRECATED: Use lvgl_camera_display.rotation instead
CONF_CROP_OFFSET_X = "crop_offset_x" # DEPRECATED: Will be removed
CONF_OUTPUT_WIDTH = "output_width"   # DEPRECATED: Will be removed
CONF_OUTPUT_HEIGHT = "output_height" # DEPRECATED: Will be removed
CONF_PPA_ENABLED = "ppa_enabled"     # DEPRECATED: PPA disabled for OV02C10
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
        cv.GenerateID(): cv.declare_id(EspCamSensorComponent),
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
        # DEPRECATED: Transform options moved to lvgl_camera_display component
        # These are accepted for backward compatibility but will show deprecation warnings
        cv.Optional(CONF_MIRROR_X): cv.invalid(
            "mirror_x has been moved to lvgl_camera_display component. "
            "Please configure it there instead."
        ),
        cv.Optional(CONF_MIRROR_Y): cv.invalid(
            "mirror_y has been moved to lvgl_camera_display component. "
            "Please configure it there instead."
        ),
        cv.Optional(CONF_ROTATION): cv.invalid(
            "rotation has been moved to lvgl_camera_display component. "
            "Please configure it there instead."
        ),
        cv.Optional(CONF_CROP_OFFSET_X): cv.invalid(
            "crop_offset_x is deprecated and will be removed. "
            "PPA transformations should be done in lvgl_camera_display."
        ),
        cv.Optional(CONF_OUTPUT_WIDTH): cv.invalid(
            "output_width is deprecated and will be removed. "
            "Resize should be done in lvgl_camera_display or LVGL widgets."
        ),
        cv.Optional(CONF_OUTPUT_HEIGHT): cv.invalid(
            "output_height is deprecated and will be removed. "
            "Resize should be done in lvgl_camera_display or LVGL widgets."
        ),
        cv.Optional(CONF_PPA_ENABLED): cv.invalid(
            "ppa_enabled is deprecated. PPA is now automatically disabled for OV02C10. "
            "Use transformation options in lvgl_camera_display instead."
        ),
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

    # REMOVED: PPA transformation parameters (moved to lvgl_camera_display)
    # The following parameters are now configured in lvgl_camera_display:
    #   - rotation: Use lvgl_camera_display.rotation instead
    #   - mirror_x: Use lvgl_camera_display.mirror_x instead
    #   - mirror_y: Use lvgl_camera_display.mirror_y instead
    #   - crop_offset_x: Removed (use LVGL canvas positioning instead)
    #   - output_width/height: Removed (use LVGL widgets or canvas size instead)
    #   - ppa_enabled: Removed (PPA auto-disabled for OV02C10)

    # Configuration des gains RGB CCM si présents
    if CONF_RGB_GAINS in config:
        rgb_config = config[CONF_RGB_GAINS]
        cg.add(var.set_rgb_gains_config(
            rgb_config[CONF_RED_GAIN],
            rgb_config[CONF_GREEN_GAIN],
            rgb_config[CONF_BLUE_GAIN]
        ))

    # Build flags pour ESP32-P4 avec ESP-IDF 5.x
    cg.add_build_flag("-DBOARD_HAS_PSRAM")
    cg.add_build_flag("-DUSE_ESP32_VARIANT_ESP32P4")
    cg.add_build_flag("-DCONFIG_CAMERA_CORE0=1")

    # Dual-task H.264 flags are defined in esp_h264/__init__.py to avoid redefinition warnings

    # Build flags spécifiques au sensor OV5647
    if config[CONF_SENSOR_TYPE].lower() == "ov5647":
        cg.add_build_flag("-DCONFIG_CAMERA_OV5647=1")
        cg.add_build_flag("-DCONFIG_CAMERA_OV5647_ANA_GAIN_PRIORITY=1")
        cg.add_build_flag("-DCONFIG_CAMERA_OV5647_ABSOLUTE_GAIN_LIMIT=63008")
        cg.add_build_flag("-DCONFIG_CAMERA_OV5647_MAX_SUPPORT=1")
        cg.add_build_flag("-DCONFIG_CAMERA_OV5647_MIPI_IF_FORMAT_INDEX_DAFAULT=0")

# Action pour capturer un snapshot
@automation.register_action(
    "esp_cam_sensor.capture_snapshot",
    CaptureSnapshotAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(EspCamSensorComponent),
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
    "esp_cam_sensor.start_streaming",
    StartStreamingAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(EspCamSensorComponent),
    })
)
async def start_streaming_action_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg)
    cg.add(var.set_parent(paren))
    return var

# Action pour arrêter le streaming vidéo
@automation.register_action(
    "esp_cam_sensor.stop_streaming",
    StopStreamingAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(EspCamSensorComponent),
    })
)
async def stop_streaming_action_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg)
    cg.add(var.set_parent(paren))
    return var
