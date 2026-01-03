import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["lvgl"]  # Need LVGL to retrieve LCD panel handle
AUTO_LOAD = []

dma_camera_display_ns = cg.esphome_ns.namespace("dma_camera_display")
DmaCameraDisplay = dma_camera_display_ns.class_("DmaCameraDisplay", cg.Component)

CONF_WIDTH = "width"
CONF_HEIGHT = "height"
CONF_X_OFFSET = "x_offset"
CONF_Y_OFFSET = "y_offset"
CONF_ENABLE_VSYNC = "enable_vsync"
CONF_FPS_TARGET = "fps_target"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DmaCameraDisplay),
        cv.Optional(CONF_WIDTH, default=800): cv.int_range(min=320, max=1920),
        cv.Optional(CONF_HEIGHT, default=600): cv.int_range(min=240, max=1080),
        cv.Optional(CONF_X_OFFSET, default=0): cv.int_,
        cv.Optional(CONF_Y_OFFSET, default=0): cv.int_,
        cv.Optional(CONF_ENABLE_VSYNC, default=True): cv.boolean,
        cv.Optional(CONF_FPS_TARGET, default=30.0): cv.float_range(min=1.0, max=120.0),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_width(config[CONF_WIDTH]))
    cg.add(var.set_height(config[CONF_HEIGHT]))
    cg.add(var.set_x_offset(config[CONF_X_OFFSET]))
    cg.add(var.set_y_offset(config[CONF_Y_OFFSET]))
    cg.add(var.set_enable_vsync(config[CONF_ENABLE_VSYNC]))
    cg.add(var.set_fps_target(config[CONF_FPS_TARGET]))

    # Add required build flags
    cg.add_build_flag("-DUSE_ESP_IDF")
