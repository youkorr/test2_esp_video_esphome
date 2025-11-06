import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_FREQUENCY,
    CONF_ADDRESS,
)
from esphome import pins

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["i2c", "esp32"]
MULTI_CONF = True

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDsiCam = mipi_dsi_cam_ns.class_("MipiDsiCam", cg.Component, i2c.I2CDevice)

CONF_EXTERNAL_CLOCK_PIN = "external_clock_pin"
CONF_RESET_PIN = "reset_pin"
CONF_SENSOR = "sensor"
CONF_LANE = "lane"
CONF_ADDRESS_SENSOR = "address_sensor"
CONF_RESOLUTION = "resolution"
CONF_PIXEL_FORMAT = "pixel_format"
CONF_FRAMERATE = "framerate"
CONF_JPEG_QUALITY = "jpeg_quality"

PixelFormat = mipi_dsi_cam_ns.enum("PixelFormat")
PIXEL_FORMAT_RGB565 = PixelFormat.PIXEL_FORMAT_RGB565
PIXEL_FORMAT_YUV422 = PixelFormat.PIXEL_FORMAT_YUV422
PIXEL_FORMAT_RAW8 = PixelFormat.PIXEL_FORMAT_RAW8

PIXEL_FORMATS = {
    "RGB565": PIXEL_FORMAT_RGB565,
    "YUV422": PIXEL_FORMAT_YUV422,
    "RAW8": PIXEL_FORMAT_RAW8,
}

# Résolutions disponibles
RESOLUTIONS = {
    "720P": (1280, 720),
    "800x640": (800, 640),
    "800x480": (800, 480),      # Ajout pour OV5647_480P
    "1280x800": (1280, 800),    # Ajout pour OV02C10
}

AVAILABLE_SENSORS = {}

def load_sensors():
    import logging
    logger = logging.getLogger(__name__)
    
    try:
        from .sensor_mipi_csi_sc202cs import get_sensor_info, get_driver_code
        AVAILABLE_SENSORS['sc202cs'] = {
            'info': get_sensor_info(),
            'driver': get_driver_code
        }
        logger.info("SC202CS sensor loaded")
    except ImportError as e:
        logger.warning(f"SC202CS sensor not available: {e}")
    except Exception as e:
        logger.error(f"Error loading SC202CS: {e}")
    
    try:
        from .sensor_mipi_csi_sc2336 import get_sensor_info, get_driver_code
        AVAILABLE_SENSORS['sc2336'] = {
            'info': get_sensor_info(),
            'driver': get_driver_code
        }
        logger.info("SC2336 sensor loaded")
    except ImportError as e:
        logger.warning(f"SC2336 sensor not available: {e}")
    except Exception as e:
        logger.error(f"Error loading SC2336: {e}")

    try:
        from .sensor_mipi_csi_ov5647 import get_sensor_info, get_driver_code
        AVAILABLE_SENSORS['ov5647'] = {
            'info': get_sensor_info(),
            'driver': get_driver_code
        }
        logger.info("OV5647 sensor loaded (800x640)")
    except ImportError as e:
        logger.warning(f"OV5647 sensor not available: {e}")
    except Exception as e:
        logger.error(f"Error loading OV5647: {e}")
    
    try:
        from .sensor_mipi_csi_ov5647_480p import get_sensor_info, get_driver_code
        AVAILABLE_SENSORS['ov5647_480p'] = {
            'info': get_sensor_info(),
            'driver': get_driver_code
        }
        logger.info("OV5647_480P sensor loaded (800x480)")
    except ImportError as e:
        logger.warning(f"OV5647_480P sensor not available: {e}")
    except Exception as e:
        logger.error(f"Error loading OV5647_480P: {e}")
    try:
        from .sensor_mipi_csi_ov5647_vga import get_sensor_info, get_driver_code
        AVAILABLE_SENSORS['ov5647_vga'] = {
            'info': get_sensor_info(),
            'driver': get_driver_code
        }
        logger.info("OV5647_VGA sensor loaded (640x480 @ 90fps)")
    except ImportError as e:
        logger.warning(f"OV5647_VGA sensor not available: {e}")
    
    try:
        from .sensor_mipi_csi_ov02c10 import get_sensor_info, get_driver_code
        AVAILABLE_SENSORS['ov02c10'] = {
            'info': get_sensor_info(),
            'driver': get_driver_code
        }
        logger.info("OV02C10 sensor loaded (1280x800)")
    except ImportError as e:
        logger.warning(f"OV02C10 sensor not available: {e}")
    except Exception as e:
        logger.error(f"Error loading OV02C10: {e}")
    
    if not AVAILABLE_SENSORS:
        raise cv.Invalid(
            "Aucun sensor MIPI disponible. "
            "Assurez-vous que les fichiers sensor_mipi_csi_*.py sont dans components/mipi_dsi_cam/"
        )
    
    logger.info(f"Sensors disponibles: {', '.join(AVAILABLE_SENSORS.keys())}")

load_sensors()

def validate_sensor(value):
    if value not in AVAILABLE_SENSORS:
        available = ', '.join(AVAILABLE_SENSORS.keys())
        raise cv.Invalid(
            f"Sensor '{value}' non disponible. Disponibles: {available}"
        )
    return value

def validate_resolution(value):
    if isinstance(value, str):
        value_upper = value.upper()
        if value_upper == "720P":
            return RESOLUTIONS["720P"]
        elif value_upper in ["800X640", "800x640"]:
            return RESOLUTIONS["800x640"]
        elif value_upper in ["800X480", "800x480"]:
            return RESOLUTIONS["800x480"]
        elif value_upper in ["1280X800", "1280x800"]:
            return RESOLUTIONS["1280x800"]
        else:
            raise cv.Invalid(
                f"Résolution '{value}' non supportée. Disponibles: 720P, 800x640, 800x480, 1280x800"
            )
    raise cv.Invalid("Le format de résolution doit être '720P', '800x640', '800x480' ou '1280x800'")

# Marqueur spécial pour indiquer "pas d'horloge externe"
NO_CLOCK = "__NO_EXTERNAL_CLOCK__"

def validate_external_clock_pin(value):
    """Valide le pin d'horloge externe ou retourne un marqueur si désactivé"""
    # Si pas de valeur
    if value is None:
        return NO_CLOCK
    
    # Si c'est le booléen False
    if value is False:
        return NO_CLOCK
    
    # Si c'est une string "none" ou "false" (case insensitive)
    if isinstance(value, str):
        lower_val = value.lower()
        if lower_val in ["none", "false", "null"]:
            return NO_CLOCK
    
    # Si c'est un entier, valider la plage
    if isinstance(value, int):
        return cv.int_range(min=0, max=50)(value)
    
    # Sinon, c'est probablement un dictionnaire de config de pin
    # On le passe à travers le validateur de pin
    return pins.internal_gpio_output_pin_schema(value)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MipiDsiCam),
        cv.Optional(CONF_NAME, default="MIPI Camera"): cv.string,
        cv.Optional(CONF_EXTERNAL_CLOCK_PIN): validate_external_clock_pin,
        cv.Optional(CONF_FREQUENCY, default=24000000): cv.int_range(min=6000000, max=40000000),
        cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_SENSOR): validate_sensor,
        cv.Optional(CONF_LANE): cv.int_range(min=1, max=4),
        cv.Optional(CONF_ADDRESS_SENSOR): cv.i2c_address,
        cv.Optional(CONF_RESOLUTION): validate_resolution,
        cv.Optional(CONF_PIXEL_FORMAT, default="RGB565"): cv.enum(PIXEL_FORMATS, upper=True),
        cv.Optional(CONF_FRAMERATE): cv.int_range(min=1, max=60),
        cv.Optional(CONF_JPEG_QUALITY, default=10): cv.int_range(min=1, max=63),
    }
).extend(cv.COMPONENT_SCHEMA).extend(i2c.i2c_device_schema(0x36))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    
    cg.add(var.set_name(config[CONF_NAME]))
    
    # Gérer external_clock_pin
    if CONF_EXTERNAL_CLOCK_PIN in config:
        ext_clock_value = config[CONF_EXTERNAL_CLOCK_PIN]
        
        # Si c'est notre marqueur "pas d'horloge"
        if ext_clock_value == NO_CLOCK:
            cg.add(var.set_external_clock_pin(-1))
        # Si c'est un entier
        elif isinstance(ext_clock_value, int):
            cg.add(var.set_external_clock_pin(ext_clock_value))
        # Si c'est un dictionnaire (pin config)
        elif isinstance(ext_clock_value, dict):
            pin_num = ext_clock_value[pins.CONF_NUMBER]
            cg.add(var.set_external_clock_pin(pin_num))
        else:
            # Fallback
            cg.add(var.set_external_clock_pin(-1))
    else:
        # Pas de clé external_clock_pin dans le config
        cg.add(var.set_external_clock_pin(-1))
    
    cg.add(var.set_external_clock_frequency(config[CONF_FREQUENCY]))
    
    # Récupérer les infos du capteur
    sensor_name = config[CONF_SENSOR]
    sensor_info = AVAILABLE_SENSORS[sensor_name]['info']
    
    # Utiliser la résolution spécifiée ou la résolution native du capteur
    if CONF_RESOLUTION in config:
        width, height = config[CONF_RESOLUTION]
        resolution_source = "configured"
    else:
        width = sensor_info['width']
        height = sensor_info['height']
        resolution_source = "native"
    
    # Utiliser les paramètres du capteur ou ceux spécifiés par l'utilisateur
    lane_count = config.get(CONF_LANE, sensor_info['lane_count'])
    sensor_address = config.get(CONF_ADDRESS_SENSOR, sensor_info['i2c_address'])
    framerate = config.get(CONF_FRAMERATE, sensor_info['fps'])
    
    cg.add(var.set_sensor_type(sensor_name))
    cg.add(var.set_sensor_address(sensor_address))
    cg.add(var.set_lane_count(lane_count))
    cg.add(var.set_resolution(width, height))
    cg.add(var.set_bayer_pattern(sensor_info['bayer_pattern']))
    cg.add(var.set_lane_bitrate(sensor_info['lane_bitrate_mbps']))
    
    cg.add(var.set_pixel_format(config[CONF_PIXEL_FORMAT]))
    cg.add(var.set_jpeg_quality(config[CONF_JPEG_QUALITY]))
    cg.add(var.set_framerate(framerate))
    
    if CONF_RESET_PIN in config:
        reset_pin = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(reset_pin))
    
    import os
    
    all_drivers_code = ""
    
    for sensor_id, sensor_data in AVAILABLE_SENSORS.items():
        driver_code_func = sensor_data['driver']
        all_drivers_code += driver_code_func() + "\n\n"
    
    factory_code = f'''
namespace esphome {{
namespace mipi_dsi_cam {{

inline ISensorDriver* create_sensor_driver(const std::string& sensor_type, i2c::I2CDevice* i2c) {{
'''
    
    for sensor_id in AVAILABLE_SENSORS.keys():
        sensor_upper = sensor_id.upper()
        factory_code += f'''
    if (sensor_type == "{sensor_id}") {{
        return new {sensor_upper}Adapter(i2c);
    }}
'''
    
    factory_code += f'''
    
    ESP_LOGE("mipi_dsi_cam", "Unknown sensor type: %s", sensor_type.c_str());
    return nullptr;
}}

}}
}}
'''
    
    complete_code = all_drivers_code + factory_code
    
    component_dir = os.path.dirname(__file__)
    generated_file_path = os.path.join(component_dir, "mipi_dsi_cam_drivers_generated.h")
    
    with open(generated_file_path, 'w') as f:
        f.write("#ifndef MIPI_DSI_CAM_DRIVERS_GENERATED_H\n")
        f.write("#define MIPI_DSI_CAM_DRIVERS_GENERATED_H\n\n")
        f.write(complete_code)
        f.write("\n#endif\n")
    
    cg.add_build_flag("-DBOARD_HAS_PSRAM")
    cg.add_build_flag("-DCONFIG_CAMERA_CORE0=1")
    cg.add_build_flag("-DUSE_ESP32_VARIANT_ESP32P4")
    
    # Message de log pour la configuration
    has_ext_clock = CONF_EXTERNAL_CLOCK_PIN in config and config[CONF_EXTERNAL_CLOCK_PIN] != NO_CLOCK
    ext_clock_msg = "enabled" if has_ext_clock else "disabled"
    
    cg.add(cg.RawExpression(f'''
        ESP_LOGI("compile", "Camera configuration:");
        ESP_LOGI("compile", "  Sensor: {sensor_name}");
        ESP_LOGI("compile", "  Resolution: {width}x{height} ({resolution_source})");
        ESP_LOGI("compile", "  Lanes: {lane_count}");
        ESP_LOGI("compile", "  Address: 0x{sensor_address:02X}");
        ESP_LOGI("compile", "  Format: {config[CONF_PIXEL_FORMAT]}");
        ESP_LOGI("compile", "  FPS: {framerate}");
        ESP_LOGI("compile", "  External Clock: {ext_clock_msg}");
    '''))
