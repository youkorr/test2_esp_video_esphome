import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_FREQUENCY,
)
from esphome import pins

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["i2c", "esp32"]

MULTI_CONF = True

tab5_camera_ns = cg.esphome_ns.namespace("tab5_camera")
Tab5Camera = tab5_camera_ns.class_("Tab5Camera", cg.Component, i2c.I2CDevice)

# Configuration pour SC202CS
CONF_RESOLUTION = "resolution"
CONF_PIXEL_FORMAT = "pixel_format"
CONF_JPEG_QUALITY = "jpeg_quality"
CONF_FRAMERATE = "framerate"
CONF_EXTERNAL_CLOCK_PIN = "external_clock_pin"
CONF_RESET_PIN = "reset_pin"
CONF_ADDRESS_SENSOR_SC202CS = "address_sensor_sc202cs"
CONF_FLIP_MIRROR = "flip_mirror"

# 🟢 NOUVEAUX PARAMÈTRES pour le contrôle du gain et de l'exposition
CONF_AUTO_GAIN = "auto_gain"
CONF_MANUAL_GAIN = "manual_gain"
CONF_AUTO_EXPOSURE = "auto_exposure"
CONF_MANUAL_EXPOSURE = "manual_exposure"

# Déclarer les enums C++
CameraResolution = tab5_camera_ns.enum("CameraResolution")
RESOLUTION_VGA = CameraResolution.RESOLUTION_VGA
RESOLUTION_720P = CameraResolution.RESOLUTION_720P
RESOLUTION_1080P = CameraResolution.RESOLUTION_1080P

PixelFormat = tab5_camera_ns.enum("PixelFormat")
PIXEL_FORMAT_RGB565 = PixelFormat.PIXEL_FORMAT_RGB565
PIXEL_FORMAT_YUV422 = PixelFormat.PIXEL_FORMAT_YUV422
PIXEL_FORMAT_RAW8 = PixelFormat.PIXEL_FORMAT_RAW8
PIXEL_FORMAT_JPEG = PixelFormat.PIXEL_FORMAT_JPEG

# Mapping pour validation YAML
CAMERA_RESOLUTIONS = {
    "VGA": RESOLUTION_VGA,
    "720P": RESOLUTION_720P,
    "1080P": RESOLUTION_1080P,
}

PIXEL_FORMATS = {
    "RGB565": PIXEL_FORMAT_RGB565,
    "YUV422": PIXEL_FORMAT_YUV422,
    "RAW8": PIXEL_FORMAT_RAW8,
    "JPEG": PIXEL_FORMAT_JPEG,
}

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Tab5Camera),
            cv.Optional(CONF_NAME, default="Tab5 Camera"): cv.string,
            cv.Optional(CONF_EXTERNAL_CLOCK_PIN, default=36): cv.Any(
                cv.int_range(min=0, max=50),
                pins.internal_gpio_output_pin_schema
            ),
            cv.Optional(CONF_FREQUENCY, default=24000000): cv.int_range(min=6000000, max=40000000),
            cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_ADDRESS_SENSOR_SC202CS, default=0x36): cv.i2c_address,
            cv.Optional(CONF_RESOLUTION, default="720P"): cv.enum(CAMERA_RESOLUTIONS, upper=True),
            cv.Optional(CONF_PIXEL_FORMAT, default="RGB565"): cv.enum(PIXEL_FORMATS, upper=True),
            cv.Optional(CONF_JPEG_QUALITY, default=10): cv.int_range(min=1, max=63),
            cv.Optional(CONF_FRAMERATE, default=30): cv.int_range(min=1, max=60),
            cv.Optional(CONF_FLIP_MIRROR, default=False): cv.boolean,
            
            # 🟢 NOUVEAUX PARAMÈTRES pour le contrôle du gain
            cv.Optional(CONF_AUTO_GAIN, default=True): cv.boolean,
            cv.Optional(CONF_MANUAL_GAIN): cv.int_range(min=0, max=191),  # 0-191 pour 192 niveaux
            
            # 🟢 NOUVEAUX PARAMÈTRES pour le contrôle de l'exposition
            cv.Optional(CONF_AUTO_EXPOSURE, default=True): cv.boolean,
            cv.Optional(CONF_MANUAL_EXPOSURE): cv.int_range(min=0x00FF, max=0x04DC),  # 255 à 1244
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x36))
)


def validate_manual_params(config):
    """Valider que si auto_gain/auto_exposure est désactivé, les valeurs manuelles sont fournies"""
    if not config.get(CONF_AUTO_GAIN, True) and CONF_MANUAL_GAIN not in config:
        raise cv.Invalid(
            f"Vous devez spécifier '{CONF_MANUAL_GAIN}' quand '{CONF_AUTO_GAIN}' est False"
        )
    
    if not config.get(CONF_AUTO_EXPOSURE, True) and CONF_MANUAL_EXPOSURE not in config:
        raise cv.Invalid(
            f"Vous devez spécifier '{CONF_MANUAL_EXPOSURE}' quand '{CONF_AUTO_EXPOSURE}' est False"
        )
    
    return config


CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_manual_params)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    
    cg.add(var.set_name(config[CONF_NAME]))
    
    # Gérer le pin externe de l'horloge
    ext_clock_pin_config = config[CONF_EXTERNAL_CLOCK_PIN]
    if isinstance(ext_clock_pin_config, int):
        cg.add(var.set_external_clock_pin(ext_clock_pin_config))
    else:
        pin_num = ext_clock_pin_config[pins.CONF_NUMBER]
        cg.add(var.set_external_clock_pin(pin_num))
    
    cg.add(var.set_external_clock_frequency(config[CONF_FREQUENCY]))
    cg.add(var.set_sensor_address(config[CONF_ADDRESS_SENSOR_SC202CS]))
    
    # Configuration résolution et format
    cg.add(var.set_resolution(config[CONF_RESOLUTION]))
    cg.add(var.set_pixel_format(config[CONF_PIXEL_FORMAT]))
    cg.add(var.set_jpeg_quality(config[CONF_JPEG_QUALITY]))
    cg.add(var.set_framerate(config[CONF_FRAMERATE]))
    
    # Flip/Mirror
    cg.add(var.set_flip_mirror(config[CONF_FLIP_MIRROR]))
    
    # 🟢 NOUVEAUX PARAMÈTRES: Gain et Exposition
    cg.add(var.set_auto_gain(config[CONF_AUTO_GAIN]))
    if CONF_MANUAL_GAIN in config:
        cg.add(var.set_manual_gain(config[CONF_MANUAL_GAIN]))
    
    cg.add(var.set_auto_exposure(config[CONF_AUTO_EXPOSURE]))
    if CONF_MANUAL_EXPOSURE in config:
        cg.add(var.set_manual_exposure(config[CONF_MANUAL_EXPOSURE]))
    
    if CONF_RESET_PIN in config:
        reset_pin = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(reset_pin))
    
    # Configuration pour ESP32-P4 avec ESP-IDF 5.x
    cg.add_build_flag("-DBOARD_HAS_PSRAM")
    cg.add_build_flag("-DCONFIG_CAMERA_CORE0=1")
    cg.add_build_flag("-DCONFIG_CAMERA_SC202CS=1")
    cg.add_build_flag("-DCONFIG_CAMERA_SC202CS_ANA_GAIN_PRIORITY=1")
    cg.add_build_flag("-DCONFIG_CAMERA_SC202CS_ABSOLUTE_GAIN_LIMIT=63008")
    cg.add_build_flag("-DCONFIG_CAMERA_SC202CS_MAX_SUPPORT=1")
    cg.add_build_flag("-DCONFIG_CAMERA_SC202CS_MIPI_IF_FORMAT_INDEX_DAFAULT=0")
    cg.add_build_flag("-DUSE_ESP32_VARIANT_ESP32P4")
