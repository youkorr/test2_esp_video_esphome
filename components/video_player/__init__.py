import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
)

DEPENDENCIES = ["lvgl"]

CODEOWNERS = ["@youkorr"]

video_player_ns = cg.esphome_ns.namespace("video_player")
VideoPlayer = video_player_ns.class_("VideoPlayer", cg.Component)

CONF_SOURCE = "source"
CONF_WIDTH = "width"
CONF_HEIGHT = "height"
CONF_AUTOPLAY = "autoplay"
CONF_LOOP = "loop"
CONF_DEVICE = "device"

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
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)

    cg.add(var.set_source_path(config[CONF_SOURCE]))
    cg.add(var.set_resolution(config[CONF_WIDTH], config[CONF_HEIGHT]))
    cg.add(var.set_autoplay(config[CONF_AUTOPLAY]))
    cg.add(var.set_loop(config[CONF_LOOP]))
    cg.add(var.set_device_path(config[CONF_DEVICE]))

