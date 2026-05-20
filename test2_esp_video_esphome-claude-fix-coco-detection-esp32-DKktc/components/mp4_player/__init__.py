import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TRIGGER_ID
from esphome import automation
from esphome.automation import maybe_simple_id
from esphome.components import speaker

DEPENDENCIES = ["lvgl"]
CODEOWNERS = ["@youkorr"]

# Register Montserrat 16 at module-load time (before LVGL's to_code writes lv_conf.h).
# LVGL iterates helpers.lv_fonts_used in its to_code() to generate LV_FONT_MONTSERRAT_*
# defines. Since mp4_player depends on lvgl, LVGL's to_code() runs first, so we MUST
# add the font before that — i.e. at import time.
try:
    from esphome.components.lvgl import helpers as _lv_helpers
    _lv_helpers.lv_fonts_used.add("montserrat_16")
except (ImportError, AttributeError):
    pass

CONF_USB_MEDIA_STORAGE_ID = "usb_media_storage_id"

mp4_player_ns = cg.esphome_ns.namespace("mp4_player")
Mp4Player = mp4_player_ns.class_("Mp4Player", cg.Component)

# Actions
PlayAction = mp4_player_ns.class_("PlayAction", automation.Action)
PauseAction = mp4_player_ns.class_("PauseAction", automation.Action)
StopAction = mp4_player_ns.class_("StopAction", automation.Action)
ReleaseResourcesAction = mp4_player_ns.class_("ReleaseResourcesAction", automation.Action)

# Triggers
PlayTrigger = mp4_player_ns.class_("PlayTrigger", automation.Trigger.template())
StopTrigger = mp4_player_ns.class_("StopTrigger", automation.Trigger.template())
CloseTrigger = mp4_player_ns.class_("CloseTrigger", automation.Trigger.template())

CONF_FILE_PATH = "file_path"
CONF_PARENT_ID = "parent_id"
CONF_SPEAKER = "speaker"
CONF_VOLUME = "volume"
CONF_LOOP = "loop"
CONF_AUTO_PLAY = "auto_play"
CONF_SHOW_CONTROLS = "show_controls"
CONF_ON_PLAY = "on_play"
CONF_ON_STOP = "on_stop"
CONF_ON_CLOSE = "on_close"
CONF_MEDIA_DIRECTORIES = "media_directories"
CONF_FULLSCREEN_ON_TOUCH = "fullscreen_on_touch"
CONF_FULLSCREEN_ANIM_MS = "fullscreen_anim_ms"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Mp4Player),
    cv.Optional(CONF_FILE_PATH): cv.string,
    cv.Optional(CONF_USB_MEDIA_STORAGE_ID): cv.use_id(cg.Component),
    cv.Optional(CONF_PARENT_ID): cv.use_id(cg.void),
    cv.Optional(CONF_SPEAKER): cv.use_id(speaker.Speaker),
    cv.Optional(CONF_VOLUME, default=80): cv.int_range(min=0, max=100),
    cv.Optional(CONF_LOOP, default=True): cv.boolean,
    cv.Optional(CONF_AUTO_PLAY, default=True): cv.boolean,
    cv.Optional(CONF_SHOW_CONTROLS, default=True): cv.boolean,
    cv.Optional(CONF_FULLSCREEN_ON_TOUCH, default=False): cv.boolean,
    cv.Optional(CONF_FULLSCREEN_ANIM_MS, default=300): cv.int_range(min=0, max=2000),
    cv.Optional(CONF_ON_PLAY): automation.validate_automation(
        {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PlayTrigger)}, single=True
    ),
    cv.Optional(CONF_ON_STOP): automation.validate_automation(
        {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(StopTrigger)}, single=True
    ),
    cv.Optional(CONF_ON_CLOSE): automation.validate_automation(
        {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CloseTrigger)}, single=True
    ),
    cv.Optional(CONF_MEDIA_DIRECTORIES, default=["/usb", "/sdcard"]): cv.ensure_list(cv.string),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_FILE_PATH in config:
        cg.add(var.set_file_path(config[CONF_FILE_PATH]))

    # Media directories for file browser
    for directory in config[CONF_MEDIA_DIRECTORIES]:
        cg.add(var.add_media_directory(directory))

    cg.add(var.set_volume(config[CONF_VOLUME]))
    cg.add(var.set_loop(config[CONF_LOOP]))
    cg.add(var.set_auto_play(config[CONF_AUTO_PLAY]))
    cg.add(var.set_show_controls(config[CONF_SHOW_CONTROLS]))
    cg.add(var.set_fullscreen_on_touch(config[CONF_FULLSCREEN_ON_TOUCH]))
    cg.add(var.set_fullscreen_anim_ms(config[CONF_FULLSCREEN_ANIM_MS]))

    if CONF_USB_MEDIA_STORAGE_ID in config:
        usb_storage = await cg.get_variable(config[CONF_USB_MEDIA_STORAGE_ID])
        cg.add(var.set_usb_storage(usb_storage))

    if CONF_PARENT_ID in config:
        parent = await cg.get_variable(config[CONF_PARENT_ID])
        cg.add(var.set_parent(parent))

    if CONF_SPEAKER in config:
        spk = await cg.get_variable(config[CONF_SPEAKER])
        cg.add(var.set_speaker(spk))

    if CONF_ON_PLAY in config:
        trigger = cg.new_Pvariable(config[CONF_ON_PLAY][automation.CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], config[CONF_ON_PLAY])

    if CONF_ON_STOP in config:
        trigger = cg.new_Pvariable(config[CONF_ON_STOP][automation.CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], config[CONF_ON_STOP])

    if CONF_ON_CLOSE in config:
        trigger = cg.new_Pvariable(config[CONF_ON_CLOSE][automation.CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], config[CONF_ON_CLOSE])

    import os
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    # Add build script
    build_script_path = os.path.join(component_dir, "mp4_player_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])

    # Enable LVGL built-in image decoders for file browser image viewing
    # LODEPNG: built-in PNG decoder (part of LVGL source, no external lib needed)
    # BMP: built-in BMP decoder
    cg.add_build_flag("-DLV_USE_LODEPNG=1")
    cg.add_build_flag("-DLV_USE_BMP=1")


    # esp_extractor include paths and libraries
    esp_extractor_dir = os.path.join(component_dir, "components", "esp_extractor")
    if os.path.exists(esp_extractor_dir):
        inc_path = os.path.join(esp_extractor_dir, "include")
        if os.path.exists(inc_path):
            cg.add_build_flag(f"-I{inc_path}")
        lib_dir = os.path.join(esp_extractor_dir, "lib", "esp32p4")
        if os.path.exists(lib_dir):
            cg.add_build_flag(f"-L{lib_dir}")
            cg.add_build_flag("-lesp_extractor")

    # esp_audio_codec for AAC/MP3 decoding
    esp_audio_codec_dir = os.path.join(parent_components_dir, "esp_audio_codec")
    if os.path.exists(esp_audio_codec_dir):
        audio_inc_paths = [
            os.path.join(esp_audio_codec_dir, "include"),
            os.path.join(esp_audio_codec_dir, "include", "decoder"),
            os.path.join(esp_audio_codec_dir, "include", "decoder", "impl"),
            os.path.join(esp_audio_codec_dir, "include", "simple_dec"),
            os.path.join(esp_audio_codec_dir, "include", "simple_dec", "impl"),
        ]
        for inc_path in audio_inc_paths:
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")
        lib_dir = os.path.join(esp_audio_codec_dir, "lib", "esp32p4")
        if os.path.exists(lib_dir):
            cg.add_build_flag(f"-L{lib_dir}")
            cg.add_build_flag("-lesp_audio_codec")
            cg.add_build_flag("-lesp_audio_simple_dec")

    # Include the main directory for app_extractor.h and app_stream_adapter.h
    main_dir = os.path.join(component_dir, "main")
    if os.path.exists(main_dir):
        cg.add_build_flag(f"-I{main_dir}")


# Action schemas - wrapped with maybe_simple_id to accept shorthand form:
# e.g. mp4_player.stop: my_player_id
MP4_PLAYER_ACTION_SCHEMA = maybe_simple_id(
    {
        cv.GenerateID(): cv.use_id(Mp4Player),
    }
)


@automation.register_action("mp4_player.play", PlayAction, MP4_PLAYER_ACTION_SCHEMA, synchronous=True)
async def play_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("mp4_player.pause", PauseAction, MP4_PLAYER_ACTION_SCHEMA, synchronous=True)
async def pause_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("mp4_player.stop", StopAction, MP4_PLAYER_ACTION_SCHEMA, synchronous=True)
async def stop_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("mp4_player.release_resources", ReleaseResourcesAction, MP4_PLAYER_ACTION_SCHEMA, synchronous=True)
async def release_resources_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
