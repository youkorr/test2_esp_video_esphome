import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation

DEPENDENCIES = ["lvgl"]
CODEOWNERS = ["@youkorr"]

# ESP-IDF components needed for H.264 decoding
ESP_IDF_COMPONENTS = ["esp_h264"]

simple_video_player_ns = cg.esphome_ns.namespace("simple_video_player")
SimpleVideoPlayer = simple_video_player_ns.class_("SimpleVideoPlayer", cg.Component)

# Actions
PlayAction = simple_video_player_ns.class_("PlayAction", automation.Action)
PauseAction = simple_video_player_ns.class_("PauseAction", automation.Action)
StopAction = simple_video_player_ns.class_("StopAction", automation.Action)
ResumeAction = simple_video_player_ns.class_("ResumeAction", automation.Action)

CONF_FILE_PATH = "file_path"
CONF_WIDTH = "width"
CONF_HEIGHT = "height"
CONF_BUFFER_SIZE = "buffer_size"
CONF_AUTO_PLAY = "auto_play"
CONF_LOOP = "loop"
CONF_SHOW_CONTROLS = "show_controls"
CONF_PARENT_ID = "parent_id"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SimpleVideoPlayer),
    cv.Required(CONF_FILE_PATH): cv.string,
    cv.Optional(CONF_WIDTH, default=800): cv.positive_int,
    cv.Optional(CONF_HEIGHT, default=480): cv.positive_int,
    cv.Optional(CONF_BUFFER_SIZE, default=100000): cv.positive_int,
    cv.Optional(CONF_AUTO_PLAY, default=True): cv.boolean,
    cv.Optional(CONF_LOOP, default=True): cv.boolean,
    cv.Optional(CONF_SHOW_CONTROLS, default=False): cv.boolean,
    cv.Optional(CONF_PARENT_ID): cv.use_id(cg.void),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_file_path(config[CONF_FILE_PATH]))
    cg.add(var.set_width(config[CONF_WIDTH]))
    cg.add(var.set_height(config[CONF_HEIGHT]))
    cg.add(var.set_buffer_size(config[CONF_BUFFER_SIZE]))
    cg.add(var.set_auto_play(config[CONF_AUTO_PLAY]))
    cg.add(var.set_loop(config[CONF_LOOP]))
    cg.add(var.set_show_controls(config[CONF_SHOW_CONTROLS]))

    if CONF_PARENT_ID in config:
        parent = await cg.get_variable(config[CONF_PARENT_ID])
        cg.add(var.set_parent(parent.obj))


# Action schemas
SIMPLE_VIDEO_PLAYER_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(SimpleVideoPlayer),
})


@automation.register_action("simple_video_player.play", PlayAction, SIMPLE_VIDEO_PLAYER_ACTION_SCHEMA)
async def play_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("simple_video_player.pause", PauseAction, SIMPLE_VIDEO_PLAYER_ACTION_SCHEMA)
async def pause_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("simple_video_player.stop", StopAction, SIMPLE_VIDEO_PLAYER_ACTION_SCHEMA)
async def stop_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("simple_video_player.resume", ResumeAction, SIMPLE_VIDEO_PLAYER_ACTION_SCHEMA)
async def resume_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
