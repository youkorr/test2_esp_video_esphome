"""
ESPHome Number Platform for ESP32-P4 Camera RGB Gain Controls

Provides runtime-adjustable RGB gain controls via CCM (Color Correction Matrix) for:
- SC202CS sensor
- OV02C10 sensor

NOTE: Not recommended for OV5647 (has excellent default image quality)

Controls: RGB gains (red, green, blue) - range 0.1 to 4.0

Note: Standard V4L2 controls (brightness, contrast, saturation, hue) are not
supported by the ESP32-P4 ISP for these sensors. Only RGB gains via CCM work.
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

CONF_TYPE_ISP = "isp"

# Classe C++ pour le number ISP
CameraSensorNumber = esp_cam_sensor_ns.class_(
    "CameraSensorNumber", number.Number, cg.Component
)

# Schema de configuration complet - seulement RGB gains (CCM)
CONFIG_SCHEMA = cv.typed_schema(
    {
        CONF_TYPE_ISP: cv.Schema(
            {
                cv.GenerateID(CONF_ESP_CAM_SENSOR_ID): cv.use_id(EspCamSensorComponent),
                cv.Optional(CONF_RED): number.number_schema(CameraSensorNumber),
                cv.Optional(CONF_GREEN): number.number_schema(CameraSensorNumber),
                cv.Optional(CONF_BLUE): number.number_schema(CameraSensorNumber),
            }
        ),
    },
    default_type=CONF_TYPE_ISP,
)


async def to_code(config):
    """Génère le code C++ pour les contrôles RGB gains via CCM"""
    if config[CONF_TYPE] == CONF_TYPE_ISP:
        # Mappage des RGB gains vers les méthodes C++ et leurs paramètres
        # Note: brightness, contrast, saturation, hue ne sont PAS supportés par l'ISP ESP32-P4
        # Seuls les RGB gains via CCM fonctionnent
        controls = {
            CONF_RED: ("set_rgb_gain_red", 0.1, 4.0, 1.0),    # min, max, default
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
                    step=0.01,  # Step de 0.01 pour gains RGB précis
                )

                # Lier le number à la caméra parent
                cg.add(var.set_parent(parent))
                cg.add(var.set_control_method(method_name))
