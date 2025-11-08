import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID
from . import mipi_dsi_cam_ns, MipiDSICamComponent, CONF_SENSOR_TYPE

DEPENDENCIES = ["mipi_dsi_cam"]

CONF_CAMERA_ID = "camera_id"

# Types de contrôles
CONF_BRIGHTNESS = "brightness"
CONF_CONTRAST = "contrast"
CONF_SATURATION = "saturation"
CONF_HUE = "hue"
CONF_FILTER = "filter"  # Sharpness
CONF_RED = "red"
CONF_GREEN = "green"
CONF_BLUE = "blue"

# Classes pour les contrôles number
CamBrightnessNumber = mipi_dsi_cam_ns.class_("CamBrightnessNumber", number.Number, cg.Component)
CamContrastNumber = mipi_dsi_cam_ns.class_("CamContrastNumber", number.Number, cg.Component)
CamSaturationNumber = mipi_dsi_cam_ns.class_("CamSaturationNumber", number.Number, cg.Component)
CamHueNumber = mipi_dsi_cam_ns.class_("CamHueNumber", number.Number, cg.Component)
CamFilterNumber = mipi_dsi_cam_ns.class_("CamFilterNumber", number.Number, cg.Component)
CamRedGainNumber = mipi_dsi_cam_ns.class_("CamRedGainNumber", number.Number, cg.Component)
CamGreenGainNumber = mipi_dsi_cam_ns.class_("CamGreenGainNumber", number.Number, cg.Component)
CamBlueGainNumber = mipi_dsi_cam_ns.class_("CamBlueGainNumber", number.Number, cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_CAMERA_ID): cv.use_id(MipiDSICamComponent),
    cv.Optional(CONF_BRIGHTNESS): number.number_schema(CamBrightnessNumber),
    cv.Optional(CONF_CONTRAST): number.number_schema(CamContrastNumber),
    cv.Optional(CONF_SATURATION): number.number_schema(CamSaturationNumber),
    cv.Optional(CONF_HUE): number.number_schema(CamHueNumber),
    cv.Optional(CONF_FILTER): number.number_schema(CamFilterNumber),
    cv.Optional(CONF_RED): number.number_schema(CamRedGainNumber),
    cv.Optional(CONF_GREEN): number.number_schema(CamGreenGainNumber),
    cv.Optional(CONF_BLUE): number.number_schema(CamBlueGainNumber),
})

async def to_code(config):
    camera = await cg.get_variable(config[CONF_CAMERA_ID])

    if brightness_config := config.get(CONF_BRIGHTNESS):
        num = await number.new_number(brightness_config, min_value=-128, max_value=127, step=1)
        await cg.register_component(num, brightness_config)
        cg.add(num.set_camera(camera))

    if contrast_config := config.get(CONF_CONTRAST):
        num = await number.new_number(contrast_config, min_value=0, max_value=255, step=1)
        await cg.register_component(num, contrast_config)
        cg.add(num.set_camera(camera))

    if saturation_config := config.get(CONF_SATURATION):
        num = await number.new_number(saturation_config, min_value=0, max_value=255, step=1)
        await cg.register_component(num, saturation_config)
        cg.add(num.set_camera(camera))

    if hue_config := config.get(CONF_HUE):
        num = await number.new_number(hue_config, min_value=-180, max_value=180, step=1)
        await cg.register_component(num, hue_config)
        cg.add(num.set_camera(camera))

    if filter_config := config.get(CONF_FILTER):
        num = await number.new_number(filter_config, min_value=0, max_value=255, step=1)
        await cg.register_component(num, filter_config)
        cg.add(num.set_camera(camera))

    if red_config := config.get(CONF_RED):
        num = await number.new_number(red_config, min_value=0.1, max_value=4.0, step=0.01)
        await cg.register_component(num, red_config)
        cg.add(num.set_camera(camera))

    if green_config := config.get(CONF_GREEN):
        num = await number.new_number(green_config, min_value=0.1, max_value=4.0, step=0.01)
        await cg.register_component(num, green_config)
        cg.add(num.set_camera(camera))

    if blue_config := config.get(CONF_BLUE):
        num = await number.new_number(blue_config, min_value=0.1, max_value=4.0, step=0.01)
        await cg.register_component(num, blue_config)
        cg.add(num.set_camera(camera))
