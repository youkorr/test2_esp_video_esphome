import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation
from esphome.components import speaker

DEPENDENCIES = ["lvgl"]
CODEOWNERS = ["@youkorr"]

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
CONF_SPEAKER = "speaker"
CONF_FPS = "fps"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SimpleVideoPlayer),
    cv.Required(CONF_FILE_PATH): cv.string,
    cv.Optional(CONF_WIDTH, default=800): cv.positive_int,
    cv.Optional(CONF_HEIGHT, default=480): cv.positive_int,
    cv.Optional(CONF_BUFFER_SIZE, default=200000): cv.positive_int,
    cv.Optional(CONF_AUTO_PLAY, default=True): cv.boolean,
    cv.Optional(CONF_LOOP, default=True): cv.boolean,
    cv.Optional(CONF_SHOW_CONTROLS, default=False): cv.boolean,
    cv.Optional(CONF_PARENT_ID): cv.use_id(cg.void),
    cv.Optional(CONF_SPEAKER): cv.use_id(speaker.Speaker),
    cv.Optional(CONF_FPS): cv.positive_float,
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

    if CONF_FPS in config:
        cg.add(var.set_fps(config[CONF_FPS]))

    if CONF_PARENT_ID in config:
        parent = await cg.get_variable(config[CONF_PARENT_ID])
        cg.add(var.set_parent(parent.obj))

    if CONF_SPEAKER in config:
        spk = await cg.get_variable(config[CONF_SPEAKER])
        cg.add(var.set_speaker(spk))

    # Add esp_audio_codec include paths for AAC decoder support
    import os
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    audio_codec_dir = os.path.join(parent_components_dir, "esp_audio_codec")
    if os.path.exists(audio_codec_dir):
        # Add main include directory
        cg.add_build_flag(f"-I{audio_codec_dir}/include")
        # Add decoder include directory
        cg.add_build_flag(f"-I{audio_codec_dir}/include/decoder")
        # Add codec implementations
        cg.add_build_flag(f"-I{audio_codec_dir}/include/codec")
        cg.add_build_flag(f"-I{audio_codec_dir}/include/decoder/impl")

    # Add build script for linking H264 and audio codec libraries
    build_script_path = os.path.join(component_dir, "simple_video_player_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])


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
