"""
ESPHome Number Platform for ESP32-P4 Camera ISP Controls

Provides runtime-adjustable ISP (Image Signal Processor) controls for:
- SC202CS sensor
- OV02C10 sensor

NOTE: Not recommended for OV5647 (has excellent default image quality)

Controls: brightness, hue, contrast, saturation, RGB gains (red, green, blue)
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_ID,
    CONF_TYPE,
    CONF_RED,
    CONF_GREEN,
    CONF_BLUE,
)
from .. import (
    esp_cam_sensor_ns,
    CONF_ESP_CAM_SENSOR_ID,
    EspCamSensorComponent,
)

# Constants pour les contrôles ISP
CONF_BRIGHTNESS = "brightness"
CONF_HUE = "hue"
CONF_CONTRAST = "contrast"
CONF_SATURATION = "saturation"
CONF_FILTER = "filter"

CONF_TYPE_ISP = "isp"

# Classe C++ pour le number ISP
CameraSensorNumber = esp_cam_sensor_ns.class_(
    "CameraSensorNumber", number.Number, cg.Component
)

# Schema de configuration complet
CONFIG_SCHEMA = cv.typed_schema(
    {
        CONF_TYPE_ISP: cv.Schema(
            {
                cv.GenerateID(CONF_ESP_CAM_SENSOR_ID): cv.use_id(EspCamSensorComponent),
                cv.Optional(CONF_BRIGHTNESS): number.number_schema(CameraSensorNumber),
                cv.Optional(CONF_HUE): number.number_schema(CameraSensorNumber),
                cv.Optional(CONF_CONTRAST): number.number_schema(CameraSensorNumber),
                cv.Optional(CONF_SATURATION): number.number_schema(CameraSensorNumber),
                cv.Optional(CONF_FILTER): number.number_schema(CameraSensorNumber),
                cv.Optional(CONF_RED): number.number_schema(CameraSensorNumber),
                cv.Optional(CONF_GREEN): number.number_schema(CameraSensorNumber),
                cv.Optional(CONF_BLUE): number.number_schema(CameraSensorNumber),
            }
        ),
    },
    default_type=CONF_TYPE_ISP,
)


async def to_code(config):
    """Génère le code C++ pour les contrôles ISP"""
    if config[CONF_TYPE] == CONF_TYPE_ISP:
        # Mappage des contrôles vers les méthodes C++ et leurs paramètres
        controls = {
            CONF_BRIGHTNESS: ("set_brightness", -128, 127, 0),  # min, max, default
            CONF_HUE: ("set_hue", -180, 180, 0),
            CONF_CONTRAST: ("set_contrast", 0, 255, 128),
            CONF_SATURATION: ("set_saturation", 0, 255, 128),
            CONF_FILTER: ("set_filter", 0, 100, 0),
            CONF_RED: ("set_rgb_gain_red", 0.1, 4.0, 1.0),
            CONF_GREEN: ("set_rgb_gain_green", 0.1, 4.0, 1.0),
            CONF_BLUE: ("set_rgb_gain_blue", 0.1, 4.0, 1.0),
        }

        parent = await cg.get_variable(config[CONF_ESP_CAM_SENSOR_ID])

        for control_name, (method_name, min_val, max_val, default_val) in controls.items():
            if control_name in config:
                conf = config[control_name]
                var = await number.new_number(
                    conf,
                    min_value=min_val,
                    max_value=max_val,
                    step=1 if isinstance(min_val, int) else 0.1,
                )

                # Lier le number à la caméra parent
                cg.add(var.set_parent(parent))
                cg.add(var.set_control_method(method_name))
