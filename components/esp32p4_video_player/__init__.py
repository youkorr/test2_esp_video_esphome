import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.const import (
    CONF_ID,
)

DEPENDENCIES = ["lvgl"]
AUTO_LOAD = []
CODEOWNERS = ["@youkorr"]

video_player_ns = cg.esphome_ns.namespace("video_player")
VideoPlayer = video_player_ns.class_("VideoPlayer", cg.Component)

# ----- Keys -----
CONF_SOURCE = "source"
CONF_DEVICE = "device"
CONF_WIDTH = "width"
CONF_HEIGHT = "height"
CONF_AUTOPLAY = "autoplay"
CONF_LOOP = "loop"

# ----- Schema -----
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(VideoPlayer),

    # chemin du fichier vidéo (ex: /sdcard/test.h264)
    cv.Required(CONF_SOURCE): cv.string,

    # device H.264 (ex: /dev/video30)
    cv.Required(CONF_DEVICE): cv.string,

    # résolution (doit rester cohérente avec la vidéo)
    cv.Required(CONF_WIDTH): cv.positive_int,
    cv.Required(CONF_HEIGHT): cv.positive_int,

    # flags optionnels
    cv.Optional(CONF_AUTOPLAY, default=False): cv.boolean,
    cv.Optional(CONF_LOOP, default=False): cv.boolean,

}).extend(cv.COMPONENT_SCHEMA)


# ----- to_code() -----
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Chemin source (H.264 file)
    cg.add(var.set_source_path(config[CONF_SOURCE]))

    # Chemin du device H.264 (/dev/videoXX)
    cg.add(var.set_device_path(config[CONF_DEVICE]))

    # Resolution
    cg.add(var.set_width(config[CONF_WIDTH]))
    cg.add(var.set_height(config[CONF_HEIGHT]))

    # Autoplay et boucle
    cg.add(var.set_autoplay(config[CONF_AUTOPLAY]))
    cg.add(var.set_loop(config[CONF_LOOP]))
