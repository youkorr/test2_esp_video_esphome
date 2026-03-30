import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation
import os

DEPENDENCIES = ["esp_cam_sensor"]
AUTO_LOAD = ["esp_cam_sensor"]

CONF_CAMERA_ID = "camera_id"
CONF_CANVAS_ID = "canvas_id"
CONF_SCORE_THRESHOLD = "score_threshold"
CONF_NMS_THRESHOLD = "nms_threshold"
CONF_DETECTION_INTERVAL = "detection_interval"
CONF_DRAW_ENABLED = "draw_enabled"
CONF_ON_OBJECT_DETECTED = "on_object_detected"
CONF_MODEL_LOCATION = "model_location"
CONF_MODEL_PATH = "model_path"

# Model location types
MODEL_LOCATION_FLASH = "flash_rodata"
MODEL_LOCATION_SDCARD = "sdcard"

yolo11_detection_ns = cg.esphome_ns.namespace("yolo11_detection")
YOLO11DetectionComponent = yolo11_detection_ns.class_("YOLO11DetectionComponent", cg.Component)

# Triggers
ObjectDetectedTrigger = yolo11_detection_ns.class_("ObjectDetectedTrigger", automation.Trigger.template(cg.int_))

esp_cam_sensor_ns = cg.esphome_ns.namespace("esp_cam_sensor")
MipiDsiCam = esp_cam_sensor_ns.class_("MipiDSICamComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(YOLO11DetectionComponent),
    cv.Required(CONF_CAMERA_ID): cv.use_id(MipiDsiCam),
    cv.Optional(CONF_CANVAS_ID): cv.string,
    cv.Optional(CONF_SCORE_THRESHOLD, default=0.3): cv.float_range(min=0.0, max=1.0),
    cv.Optional(CONF_NMS_THRESHOLD, default=0.5): cv.float_range(min=0.0, max=1.0),
    cv.Optional(CONF_DETECTION_INTERVAL, default=8): cv.int_range(min=1, max=600),
    cv.Optional(CONF_DRAW_ENABLED, default=True): cv.boolean,
    cv.Optional(CONF_MODEL_LOCATION, default=MODEL_LOCATION_FLASH): cv.one_of(
        MODEL_LOCATION_FLASH, MODEL_LOCATION_SDCARD, lower=True
    ),
    cv.Optional(CONF_MODEL_PATH): cv.string,
    cv.Optional(CONF_ON_OBJECT_DETECTED): automation.validate_automation({
        cv.GenerateID(): cv.declare_id(ObjectDetectedTrigger),
    }),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))

    if CONF_CANVAS_ID in config:
        cg.add(var.set_canvas_id(config[CONF_CANVAS_ID]))

    cg.add(var.set_score_threshold(config[CONF_SCORE_THRESHOLD]))
    cg.add(var.set_nms_threshold(config[CONF_NMS_THRESHOLD]))
    cg.add(var.set_detection_interval(config[CONF_DETECTION_INTERVAL]))
    cg.add(var.set_draw_enabled(config[CONF_DRAW_ENABLED]))

    # Setup automations
    for conf in config.get(CONF_ON_OBJECT_DETECTED, []):
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(trigger, [(cg.int_, "object_count")], conf)

    # Set build flag for YOLO11 model
    cg.add_build_flag("-DESP_DL_MODEL_YOLO11=1")
    cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32P4=1")

    # YOLO11 detection configuration
    cg.add_build_flag("-DCONFIG_YOLO11_DETECT_S8_V1=1")
    cg.add_build_flag("-DCONFIG_COCO_DETECT_YOLO11N_320_s8_v3=1")
    cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_TYPE=0")

    # Model location configuration
    model_location = config.get(CONF_MODEL_LOCATION, MODEL_LOCATION_FLASH)

    if model_location == MODEL_LOCATION_SDCARD:
        # SD card mode
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_IN_SDCARD=1")
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_IN_FLASH_RODATA=0")
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_LOCATION=2")

        # Pass SD card path to C++ component
        if CONF_MODEL_PATH in config:
            cg.add(var.set_sdcard_model_path(cg.RawExpression(f'"{config[CONF_MODEL_PATH]}"')))
        else:
            # Default SD card path
            cg.add(var.set_sdcard_model_path(cg.RawExpression('"/sdcard"')))
    else:
        # Flash rodata mode (default)
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_IN_FLASH_RODATA=1")
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_IN_SDCARD=0")
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_LOCATION=0")

    # Add include paths
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    # Add yolo11_detect include path
    yolo11_detect_dir = os.path.join(parent_components_dir, "yolo11_detect")
    if os.path.exists(yolo11_detect_dir):
        cg.add_build_flag(f"-I{yolo11_detect_dir}")

    # Add ESP-DL include paths
    esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
    if os.path.exists(esp_dl_dir):
        esp_dl_includes = [
            "dl",
            "dl/tool/include",
            "dl/tool/isa/esp32p4",
            "dl/tool/src",
            "dl/tensor/include",
            "dl/tensor/src",
            "dl/base",
            "dl/base/isa",
            "dl/base/isa/esp32p4",
            "dl/math/include",
            "dl/math/src",
            "dl/model/include",
            "dl/model/src",
            "dl/module/include",
            "dl/module/src",
            "fbs_loader/include",
            "fbs_loader/lib/esp32p4",
            "fbs_loader/src",
            "vision/detect",
            "vision/image",
            "vision/image/isa",
            "vision/image/isa/esp32p4",
        ]
        for inc in esp_dl_includes:
            inc_path = os.path.join(esp_dl_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")

    # Add build script for compiling ESP-DL sources
    build_script_path = os.path.join(component_dir, "yolo11_detection_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
