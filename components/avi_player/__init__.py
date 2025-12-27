import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation
from esphome.components import speaker

DEPENDENCIES = ["lvgl"]
CODEOWNERS = ["@youkorr"]

avi_player_ns = cg.esphome_ns.namespace("avi_player")
AviPlayerComponent = avi_player_ns.class_("AviPlayerComponent", cg.Component)

# Actions
PlayAction = avi_player_ns.class_("PlayAction", automation.Action)
StopAction = avi_player_ns.class_("StopAction", automation.Action)

CONF_FILE_PATH = "file_path"
CONF_WIDTH = "width"
CONF_HEIGHT = "height"
CONF_BUFFER_SIZE = "buffer_size"
CONF_AUTO_PLAY = "auto_play"
CONF_LOOP = "loop"
CONF_PARENT_ID = "parent_id"
CONF_SHOW_CONTROLS = "show_controls"
CONF_SHOW_SLIDER = "show_slider"
CONF_PRELOAD_TO_MEMORY = "preload_to_memory"
CONF_FPS = "fps"
CONF_ROTATION = "rotation"
CONF_SPEAKER = "speaker"
CONF_ON_PLAY = "on_play"
CONF_ON_STOP = "on_stop"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(AviPlayerComponent),
    cv.Required(CONF_FILE_PATH): cv.string,
    cv.Optional(CONF_WIDTH, default=480): cv.positive_int,
    cv.Optional(CONF_HEIGHT, default=270): cv.positive_int,
    cv.Optional(CONF_BUFFER_SIZE, default=256 * 1024): cv.positive_int,  # 256 KB for high quality videos
    cv.Optional(CONF_AUTO_PLAY, default=True): cv.boolean,
    cv.Optional(CONF_LOOP, default=False): cv.boolean,
    cv.Optional(CONF_SHOW_CONTROLS, default=False): cv.boolean,
    cv.Optional(CONF_SHOW_SLIDER, default=False): cv.boolean,
    cv.Optional(CONF_PRELOAD_TO_MEMORY, default=False): cv.boolean,
    cv.Optional(CONF_FPS): cv.positive_float,
    cv.Optional(CONF_ROTATION, default=0): cv.one_of(0, 90, 180, 270, int=True),
    cv.Optional(CONF_PARENT_ID): cv.use_id(cg.void),
    cv.Optional(CONF_SPEAKER): cv.use_id(speaker.Speaker),
    cv.Optional(CONF_ON_PLAY): automation.validate_automation(single=True),
    cv.Optional(CONF_ON_STOP): automation.validate_automation(single=True),
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
    cg.add(var.set_show_slider(config[CONF_SHOW_SLIDER]))
    cg.add(var.set_preload_to_memory(config[CONF_PRELOAD_TO_MEMORY]))

    if CONF_FPS in config:
        cg.add(var.set_fps(config[CONF_FPS]))

    cg.add(var.set_rotation(config[CONF_ROTATION]))

    if CONF_PARENT_ID in config:
        parent = await cg.get_variable(config[CONF_PARENT_ID])
        cg.add(var.set_parent(parent.obj))

    if CONF_SPEAKER in config:
        spk = await cg.get_variable(config[CONF_SPEAKER])
        cg.add(var.set_speaker(spk))

    # Setup triggers
    if CONF_ON_PLAY in config:
        await automation.build_automation(
            var.get_on_play_trigger(), [], config[CONF_ON_PLAY]
        )

    if CONF_ON_STOP in config:
        await automation.build_automation(
            var.get_on_stop_trigger(), [], config[CONF_ON_STOP]
        )

    # Add include path for avi_player headers
    import os
    component_dir = os.path.dirname(__file__)
    include_dir = os.path.join(component_dir, "include")
    cg.add_build_flag(f"-I{include_dir}")

    # Add include path for esp_image_effects (for rotation)
    imgfx_dir = os.path.join(os.path.dirname(component_dir), "esp_image_effects", "include")
    cg.add_build_flag(f"-I{imgfx_dir}")

    # Link esp_image_effects library (prebuilt)
    imgfx_lib_dir = os.path.join(os.path.dirname(component_dir), "esp_image_effects", "lib", "esp32p4")
    cg.add_build_flag(f"-L{imgfx_lib_dir}")
    cg.add_build_flag("-lesp_image_effects")

    # Define version macros (from idf_component.yml version: 2.0.0)
    cg.add_build_flag("-DAVI_PLAYER_VER_MAJOR=2")
    cg.add_build_flag("-DAVI_PLAYER_VER_MINOR=0")
    cg.add_build_flag("-DAVI_PLAYER_VER_PATCH=0")


# Action schemas
AVI_PLAYER_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(AviPlayerComponent),
})


@automation.register_action("avi_player.play", PlayAction, AVI_PLAYER_ACTION_SCHEMA)
async def play_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("avi_player.stop", StopAction, AVI_PLAYER_ACTION_SCHEMA)
async def stop_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
