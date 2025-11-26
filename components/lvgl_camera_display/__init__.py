import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
import os

DEPENDENCIES = ["lvgl", "mipi_dsi_cam"]
AUTO_LOAD = ["mipi_dsi_cam"]

CONF_CAMERA_ID = "camera_id"
CONF_CANVAS_ID = "canvas_id"
CONF_UPDATE_INTERVAL = "update_interval"
CONF_FACE_DETECTION = "face_detection"
CONF_PEDESTRIAN_DETECTION = "pedestrian_detection"

lvgl_camera_display_ns = cg.esphome_ns.namespace("lvgl_camera_display")
LVGLCameraDisplay = lvgl_camera_display_ns.class_("LVGLCameraDisplay", cg.Component)

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
# Utiliser le nom réel de la classe C++ (MipiDSICamComponent)
MipiDsiCam = mipi_dsi_cam_ns.class_("MipiDSICamComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LVGLCameraDisplay),
    cv.Required(CONF_CAMERA_ID): cv.use_id(MipiDsiCam),
    cv.Required(CONF_CANVAS_ID): cv.string,
    cv.Optional(CONF_UPDATE_INTERVAL, default="33ms"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_FACE_DETECTION, default=False): cv.boolean,
    cv.Optional(CONF_PEDESTRIAN_DETECTION, default=False): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))

    update_interval_ms = config[CONF_UPDATE_INTERVAL].total_milliseconds
    cg.add(var.set_update_interval(int(update_interval_ms)))

    if config[CONF_FACE_DETECTION]:
        cg.add(var.set_face_detection_enabled(True))

    if config[CONF_PEDESTRIAN_DETECTION]:
        cg.add(var.set_pedestrian_detection_enabled(True))

    # Add ESP-DL detection component defines (from Kconfig)
    cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MSRMNP_S8_V1=1")
    cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=1")
    cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_TYPE=0")
    cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION=0")
    cg.add_build_flag("-DCONFIG_PEDESTRIAN_DETECT_PICO_S8_V1=1")
    cg.add_build_flag("-DCONFIG_PEDESTRIAN_DETECT_MODEL_IN_FLASH_RODATA=1")
    cg.add_build_flag("-DCONFIG_PEDESTRIAN_DETECT_MODEL_TYPE=0")
    cg.add_build_flag("-DCONFIG_PEDESTRIAN_DETECT_MODEL_LOCATION=0")
    cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32P4=1")

    # Add ESP-DL detection components include paths
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    # Add human_face_detect include path
    human_face_detect_dir = os.path.join(parent_components_dir, "human_face_detect")
    if os.path.exists(human_face_detect_dir):
        cg.add_build_flag(f"-I{human_face_detect_dir}")

    # Add pedestrian_detect include path
    pedestrian_detect_dir = os.path.join(parent_components_dir, "pedestrian_detect")
    if os.path.exists(pedestrian_detect_dir):
        cg.add_build_flag(f"-I{pedestrian_detect_dir}")

    # Add ESP-DL include paths
    esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
    if os.path.exists(esp_dl_dir):
        esp_dl_includes = [
            "dl",
            "dl/tool/include",
            "dl/tensor/include",
            "dl/base",
            "dl/base/isa",
            "dl/base/isa/esp32p4",
            "dl/math/include",
            "dl/model/include",
            "dl/module/include",
            "fbs_loader/include",
            "vision/detect",
            "vision/image",
            "vision/image/isa",
            "vision/image/isa/esp32p4",
            "vision/recognition",
            "vision/classification",
        ]
        for inc in esp_dl_includes:
            inc_path = os.path.join(esp_dl_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")

    # Add build script for compiling ESP-DL sources
    build_script_path = os.path.join(component_dir, "lvgl_camera_display_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
