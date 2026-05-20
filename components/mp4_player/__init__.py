import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TRIGGER_ID
from esphome import automation
from esphome.automation import maybe_simple_id
from esphome.components import speaker

CONF_ON_CLOSE = "on_close"

DEPENDENCIES = ["lvgl"]
CODEOWNERS = ["@youkorr"]

CONF_USB_MEDIA_STORAGE_ID = "usb_media_storage_id"

mp4_player_ns = cg.esphome_ns.namespace("mp4_player")
Mp4Player = mp4_player_ns.class_("Mp4Player", cg.Component)

# Actions
PlayAction = mp4_player_ns.class_("PlayAction", automation.Action)
PauseAction = mp4_player_ns.class_("PauseAction", automation.Action)
StopAction = mp4_player_ns.class_("StopAction", automation.Action)
EnterFullscreenAction = mp4_player_ns.class_("EnterFullscreenAction", automation.Action)
ExitFullscreenAction = mp4_player_ns.class_("ExitFullscreenAction", automation.Action)
ToggleFullscreenAction = mp4_player_ns.class_("ToggleFullscreenAction", automation.Action)
CloseAction = mp4_player_ns.class_("CloseAction", automation.Action)
ReleaseResourcesAction = mp4_player_ns.class_("ReleaseResourcesAction", automation.Action)
ShowBrowserAction = mp4_player_ns.class_("ShowBrowserAction", automation.Action)

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
CONF_MEDIA_DIRECTORIES = "media_directories"
CONF_FULLSCREEN_ON_TOUCH = "fullscreen_on_touch"
CONF_FULLSCREEN_ANIM_MS = "fullscreen_anim_ms"

_PLAYER_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Mp4Player),
    cv.Optional(CONF_FILE_PATH): cv.string,
    cv.Optional(CONF_USB_MEDIA_STORAGE_ID): cv.use_id(cg.Component),
    cv.Optional(CONF_PARENT_ID): cv.use_id(cg.void),
    cv.Optional(CONF_SPEAKER): cv.use_id(speaker.Speaker),
    cv.Optional(CONF_VOLUME, default=80): cv.int_range(min=0, max=100),
    cv.Optional(CONF_LOOP, default=True): cv.boolean,
    cv.Optional(CONF_AUTO_PLAY, default=True): cv.boolean,
    cv.Optional(CONF_SHOW_CONTROLS, default=True): cv.boolean,
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
    cv.Optional(CONF_FULLSCREEN_ON_TOUCH, default=False): cv.boolean,
    cv.Optional(CONF_FULLSCREEN_ANIM_MS, default=350): cv.int_range(min=50, max=2000),
}).extend(cv.COMPONENT_SCHEMA)

# Accept BOTH a single dict (legacy) and a list of dicts (multi-instance)
CONFIG_SCHEMA = cv.Any(
    cv.ensure_list(_PLAYER_SCHEMA),
    _PLAYER_SCHEMA,
)


def _add_global_build_flags():
    import os
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    # Add build script
    build_script_path = os.path.join(component_dir, "mp4_player_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])

    # Enable LVGL built-in image decoders for file browser image viewing
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


async def _setup_player(config):
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
        cg.add(var.set_parent(cg.RawExpression(f"(lv_obj_t*)({parent})")))
        

    
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


async def to_code(config):
    _add_global_build_flags()

    # Normalize: schema accepts a single dict or a list of dicts
    players = config if isinstance(config, list) else [config]
    for player_cfg in players:
        await _setup_player(player_cfg)


# Action schemas - wrapped with maybe_simple_id to accept shorthand form:
# e.g. mp4_player.release_resources: my_player_id
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


@automation.register_action(
    "mp4_player.enter_fullscreen", EnterFullscreenAction, MP4_PLAYER_ACTION_SCHEMA, synchronous=True
)
async def enter_fullscreen_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action(
    "mp4_player.exit_fullscreen", ExitFullscreenAction, MP4_PLAYER_ACTION_SCHEMA, synchronous=True
)
async def exit_fullscreen_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action(
    "mp4_player.toggle_fullscreen", ToggleFullscreenAction, MP4_PLAYER_ACTION_SCHEMA, synchronous=True
)
async def toggle_fullscreen_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("mp4_player.close", CloseAction, MP4_PLAYER_ACTION_SCHEMA, synchronous=True)
async def close_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action(
    "mp4_player.release_resources", ReleaseResourcesAction, MP4_PLAYER_ACTION_SCHEMA, synchronous=True
)
async def release_resources_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action(
    "mp4_player.show_browser", ShowBrowserAction, MP4_PLAYER_ACTION_SCHEMA, synchronous=True
)
async def show_browser_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
