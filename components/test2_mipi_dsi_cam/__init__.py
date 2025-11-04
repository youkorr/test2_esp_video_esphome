"""
Composant ESPHome : MIPI-DSI Camera
Version compatible ESP-Video + ESP-IDF 5.4.2
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["esp32"]

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
MipiDSICamComponent = mipi_dsi_cam_ns.class_("MipiDSICamComponent", cg.Component)

CONF_NAME = "name"
CONF_SENSOR = "sensor"
CONF_I2C_ID = "i2c_id"
CONF_LANE = "lane"
CONF_XCLK_PIN = "external_clock_pin"
CONF_FREQUENCY = "frequency"
CONF_ADDR_SENSOR = "address_sensor"
CONF_RESOLUTION = "resolution"
CONF_PIXEL_FORMAT = "pixel_format"
CONF_FRAMERATE = "framerate"
CONF_JPEG_QUALITY = "jpeg_quality"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MipiDSICamComponent),
    cv.Required(CONF_NAME): cv.string,
    cv.Required(CONF_SENSOR): cv.string,
    cv.Required(CONF_I2C_ID): cv.int_,
    cv.Optional(CONF_LANE, default=1): cv.int_,
    cv.Required(CONF_XCLK_PIN): cv.string,
    cv.Required(CONF_FREQUENCY): cv.int_,
    cv.Required(CONF_ADDR_SENSOR): cv.int_,
    cv.Optional(CONF_RESOLUTION, default="720P"): cv.string,
    cv.Optional(CONF_PIXEL_FORMAT, default="RGB565"): cv.string,
    cv.Optional(CONF_FRAMERATE, default=30): cv.int_,
    cv.Optional(CONF_JPEG_QUALITY, default=10): cv.int_,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_name(config[CONF_NAME]))
    cg.add(var.set_sensor_name(config[CONF_SENSOR]))
    cg.add(var.set_i2c_id(config[CONF_I2C_ID]))
    cg.add(var.set_lane(config[CONF_LANE]))
    cg.add(var.set_xclk_pin(config[CONF_XCLK_PIN]))
    cg.add(var.set_xclk_freq(config[CONF_FREQUENCY]))
    cg.add(var.set_sensor_addr(config[CONF_ADDR_SENSOR]))
    cg.add(var.set_resolution(config[CONF_RESOLUTION]))
    cg.add(var.set_pixel_format(config[CONF_PIXEL_FORMAT]))
    cg.add(var.set_framerate(config[CONF_FRAMERATE]))
    cg.add(var.set_jpeg_quality(config[CONF_JPEG_QUALITY]))

    # Inclure automatiquement esp_common/include (esp_err.h)
    import os
    idf_path = os.environ.get("IDF_PATH", "/opt/esp-idf")
    esp_common = os.path.join(idf_path, "components", "esp_common", "include")
    if os.path.exists(esp_common):
        cg.add_build_flag(f"-I{esp_common}")
        print(f"[ESP-IDF] 📁 Include ajouté : {esp_common}")
    else:
        print(f"[ESP-IDF] ⚠️ esp_common non trouvé (esp_err.h peut manquer)")

    # Include du dossier local
    component_dir = os.path.dirname(os.path.abspath(__file__))
    cg.add_build_flag(f"-I{component_dir}")
    cg.add_build_flag(f"-I{os.path.join(component_dir, 'include')}")
    cg.add_build_flag(f"-I{os.path.join(component_dir, '..', 'esp_video', 'include')}")
    cg.add_define("USE_ESP32_VARIANT_ESP32P4")

    cg.add(cg.RawExpression('// [MIPI-DSI-CAM] Configuration complète prête à compiler ✅'))
