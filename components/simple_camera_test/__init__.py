import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import lvgl
from esphome.const import CONF_ID

DEPENDENCIES = ["lvgl"]
AUTO_LOAD = ["lvgl"]

simple_camera_test_ns = cg.esphome_ns.namespace("simple_camera_test")
SimpleCameraTest = simple_camera_test_ns.class_("SimpleCameraTest", cg.Component)

CONF_WIDTH = "width"
CONF_HEIGHT = "height"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SimpleCameraTest),
        cv.Optional(CONF_WIDTH, default=800): cv.int_range(min=320, max=1920),
        cv.Optional(CONF_HEIGHT, default=600): cv.int_range(min=240, max=1080),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Get LVGL display object
    paren = await cg.get_variable(lvgl.CONF_LVGL_ID)
    cg.add(var.set_parent(paren.get_scr_act()))

    cg.add(var.set_width(config[CONF_WIDTH]))
    cg.add(var.set_height(config[CONF_HEIGHT]))

    # Add required build flags
    cg.add_build_flag("-DUSE_ESP_IDF")
