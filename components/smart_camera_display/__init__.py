import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import lvgl
from esphome.const import CONF_ID
from esphome import automation

DEPENDENCIES = ["lvgl"]
AUTO_LOAD = []

smart_camera_display_ns = cg.esphome_ns.namespace("smart_camera_display")
SmartCameraDisplay = smart_camera_display_ns.class_("SmartCameraDisplay", cg.Component)

# Actions
StartStreamingAction = smart_camera_display_ns.class_("StartStreamingAction", automation.Action)
StopStreamingAction = smart_camera_display_ns.class_("StopStreamingAction", automation.Action)

CONF_CAMERA_MODEL = "camera_model"
CONF_WIDTH = "width"
CONF_HEIGHT = "height"
CONF_X_OFFSET = "x_offset"
CONF_Y_OFFSET = "y_offset"
CONF_PARENT_ID = "parent_id"
CONF_DISPLAY_CANVAS = "display_canvas"
CONF_SHOW_CONTROLS = "show_controls"
CONF_AUTO_START = "auto_start"
CONF_ENABLE_DMA_OPTIMIZATION = "enable_dma_optimization"
CONF_TARGET_FPS = "target_fps"

CAMERA_MODELS = ["sc202cs", "ov5647", "ov02c10"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SmartCameraDisplay),
        cv.Optional(CONF_CAMERA_MODEL, default="sc202cs"): cv.one_of(*CAMERA_MODELS, lower=True),
        cv.Optional(CONF_WIDTH, default=800): cv.int_range(min=320, max=1920),
        cv.Optional(CONF_HEIGHT, default=600): cv.int_range(min=240, max=1080),
        cv.Optional(CONF_X_OFFSET, default=0): cv.int_,
        cv.Optional(CONF_Y_OFFSET, default=0): cv.int_,
        cv.Optional(CONF_PARENT_ID): cv.use_id(lvgl.LvglComponent),  # Page LVGL
        cv.Optional(CONF_DISPLAY_CANVAS): cv.string,  # ID du canvas LVGL
        cv.Optional(CONF_SHOW_CONTROLS, default=False): cv.boolean,
        cv.Optional(CONF_AUTO_START, default=False): cv.boolean,
        cv.Optional(CONF_ENABLE_DMA_OPTIMIZATION, default=True): cv.boolean,
        cv.Optional(CONF_TARGET_FPS, default=30.0): cv.float_range(min=1.0, max=120.0),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_camera_model(config[CONF_CAMERA_MODEL]))
    cg.add(var.set_width(config[CONF_WIDTH]))
    cg.add(var.set_height(config[CONF_HEIGHT]))
    cg.add(var.set_x_offset(config[CONF_X_OFFSET]))
    cg.add(var.set_y_offset(config[CONF_Y_OFFSET]))
    cg.add(var.set_show_controls(config[CONF_SHOW_CONTROLS]))
    cg.add(var.set_auto_start(config[CONF_AUTO_START]))
    cg.add(var.set_enable_dma_optimization(config[CONF_ENABLE_DMA_OPTIMIZATION]))
    cg.add(var.set_target_fps(config[CONF_TARGET_FPS]))

    # Get LVGL parent page if specified
    if CONF_PARENT_ID in config:
        parent = await cg.get_variable(config[CONF_PARENT_ID])
        cg.add(var.set_parent(parent.get_scr_act()))

    # TODO: Get canvas object from LVGL
    # For now, user needs to pass canvas object via lambda in on_load

    # Add build flags
    cg.add_build_flag("-DUSE_ESP_IDF")


# Actions pour automation
@automation.register_action(
    "smart_camera_display.start_streaming",
    StartStreamingAction,
    automation.maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(SmartCameraDisplay),
        }
    ),
)
async def start_streaming_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action(
    "smart_camera_display.stop_streaming",
    StopStreamingAction,
    automation.maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(SmartCameraDisplay),
        }
    ),
)
async def stop_streaming_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
