import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
)
from esphome import automation

DEPENDENCIES = ["lvgl"]

CODEOWNERS = ["@youkorr"]

# ESP-IDF component dependency
ESP_IDF_COMPONENTS = ["esp_h264"]

video_player_ns = cg.esphome_ns.namespace("video_player")
VideoPlayer = video_player_ns.class_("VideoPlayer", cg.Component)

# Actions
PlayAction = video_player_ns.class_("PlayAction", automation.Action)
PauseAction = video_player_ns.class_("PauseAction", automation.Action)
StopAction = video_player_ns.class_("StopAction", automation.Action)

CONF_SOURCE = "source"
CONF_WIDTH = "width"
CONF_HEIGHT = "height"
CONF_AUTOPLAY = "autoplay"
CONF_LOOP = "loop"
CONF_DEVICE = "device"
CONF_PARENT_ID = "parent_id"

# Get LVGL obj type
lvgl_ns = cg.esphome_ns.namespace("lvgl")
LvglComponent = lvgl_ns.class_("LvglComponent", cg.PollingComponent)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(VideoPlayer),

    # Path to video file (local file or HTTP stream)
    cv.Required(CONF_SOURCE): cv.string,

    # Output resolution of the video
    cv.Optional(CONF_WIDTH, default=800): cv.positive_int,
    cv.Optional(CONF_HEIGHT, default=480): cv.positive_int,

    # Auto-start playback
    cv.Optional(CONF_AUTOPLAY, default=True): cv.boolean,

    # Loop playback
    cv.Optional(CONF_LOOP, default=False): cv.boolean,

    # H.264 hardware decoder device path (esp_video)
    cv.Optional(CONF_DEVICE, default="/dev/video30"): cv.string,

    # Parent LVGL object (page/container) - if not set, uses current screen
    cv.Optional(CONF_PARENT_ID): cv.use_id(cg.void),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)

    cg.add(var.set_source_path(config[CONF_SOURCE]))
    cg.add(var.set_resolution(config[CONF_WIDTH], config[CONF_HEIGHT]))
    cg.add(var.set_autoplay(config[CONF_AUTOPLAY]))
    cg.add(var.set_loop(config[CONF_LOOP]))
    cg.add(var.set_device_path(config[CONF_DEVICE]))

    if CONF_PARENT_ID in config:
        parent = await cg.get_variable(config[CONF_PARENT_ID])
        # Access underlying lv_obj_t* - LvPageType stores it in 'obj' member
        cg.add(var.set_parent(parent.obj))


# Action schemas
VIDEO_PLAYER_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(VideoPlayer),
})


@automation.register_action("video_player.play", PlayAction, VIDEO_PLAYER_ACTION_SCHEMA)
async def play_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("video_player.pause", PauseAction, VIDEO_PLAYER_ACTION_SCHEMA)
async def pause_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("video_player.stop", StopAction, VIDEO_PLAYER_ACTION_SCHEMA)
async def stop_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

