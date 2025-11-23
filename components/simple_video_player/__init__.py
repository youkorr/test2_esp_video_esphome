import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["lvgl"]
CODEOWNERS = ["@youkorr"]

simple_video_player_ns = cg.esphome_ns.namespace("simple_video_player")
SimpleVideoPlayer = simple_video_player_ns.class_("SimpleVideoPlayer", cg.Component)

CONF_FILE_PATH = "file_path"
CONF_WIDTH = "width"
CONF_HEIGHT = "height"
CONF_BUFFER_SIZE = "buffer_size"
CONF_AUTO_PLAY = "auto_play"
CONF_LOOP = "loop"
CONF_SHOW_CONTROLS = "show_controls"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SimpleVideoPlayer),
    cv.Required(CONF_FILE_PATH): cv.string,
    cv.Optional(CONF_WIDTH, default=800): cv.positive_int,
    cv.Optional(CONF_HEIGHT, default=480): cv.positive_int,
    cv.Optional(CONF_BUFFER_SIZE, default=100000): cv.positive_int,
    cv.Optional(CONF_AUTO_PLAY, default=True): cv.boolean,
    cv.Optional(CONF_LOOP, default=True): cv.boolean,
    cv.Optional(CONF_SHOW_CONTROLS, default=True): cv.boolean,
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
