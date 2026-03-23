import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation
import os

CONF_ESP32_CAMERA_ID = "esp32_camera_id"
CONF_CAMERA_ID = "camera_id"
CONF_CANVAS_ID = "canvas_id"
CONF_SCORE_THRESHOLD = "score_threshold"
CONF_NMS_THRESHOLD = "nms_threshold"
CONF_DETECTION_INTERVAL = "detection_interval"
CONF_DRAW_ENABLED = "draw_enabled"
CONF_ON_OBJECT_DETECTED = "on_object_detected"

yolo11_detection_ns = cg.esphome_ns.namespace("yolo11_detection")
YOLO11DetectionComponent = yolo11_detection_ns.class_("YOLO11DetectionComponent", cg.Component)

# Triggers
ObjectDetectedTrigger = yolo11_detection_ns.class_("ObjectDetectedTrigger", automation.Trigger.template(cg.int_))

# esp32_camera (ESPHome standard - for ESP32-S3, etc.)
esp32_camera_ns = cg.esphome_ns.namespace("esp32_camera")
ESP32Camera = esp32_camera_ns.class_("ESP32Camera", cg.Component)

# esp_cam_sensor (MIPI DSI - ESP32-P4)
esp_cam_sensor_ns = cg.esphome_ns.namespace("esp_cam_sensor")
MipiDsiCam = esp_cam_sensor_ns.class_("MipiDSICamComponent", cg.Component)


def validate_camera_config(config):
    has_esp32_cam = CONF_ESP32_CAMERA_ID in config
    has_mipi_cam = CONF_CAMERA_ID in config
    if not has_esp32_cam and not has_mipi_cam:
        raise cv.Invalid(
            "Either 'esp32_camera_id' or 'camera_id' must be specified"
        )
    if has_esp32_cam and has_mipi_cam:
        raise cv.Invalid(
            "Only one of 'esp32_camera_id' or 'camera_id' can be specified"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(YOLO11DetectionComponent),
        cv.Optional(CONF_ESP32_CAMERA_ID): cv.use_id(ESP32Camera),
        cv.Optional(CONF_CAMERA_ID): cv.use_id(MipiDsiCam),
        cv.Optional(CONF_CANVAS_ID): cv.string,
        cv.Optional(CONF_SCORE_THRESHOLD, default=0.3): cv.float_range(min=0.0, max=1.0),
        cv.Optional(CONF_NMS_THRESHOLD, default=0.5): cv.float_range(min=0.0, max=1.0),
        cv.Optional(CONF_DETECTION_INTERVAL, default=8): cv.int_range(min=1, max=600),
        cv.Optional(CONF_DRAW_ENABLED, default=True): cv.boolean,
        cv.Optional(CONF_ON_OBJECT_DETECTED): automation.validate_automation({
            cv.GenerateID(): cv.declare_id(ObjectDetectedTrigger),
        }),
    }).extend(cv.COMPONENT_SCHEMA),
    validate_camera_config,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set camera (mutually exclusive)
    if CONF_ESP32_CAMERA_ID in config:
        camera = await cg.get_variable(config[CONF_ESP32_CAMERA_ID])
        cg.add(var.set_esp32_camera(camera))
        cg.add_build_flag("-DUSE_YOLO11_ESP32_CAMERA")
    elif CONF_CAMERA_ID in config:
        camera = await cg.get_variable(config[CONF_CAMERA_ID])
        cg.add(var.set_camera(camera))
        cg.add_build_flag("-DUSE_YOLO11_MIPI_CAMERA")

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

    # Build flags
    cg.add_build_flag("-DESP_DL_MODEL_YOLO11=1")

    # Add include paths
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    # Detect target platform for ISA-specific includes
    # ESP32-P4 uses MIPI camera, ESP32-S3 uses esp32_camera
    is_p4 = CONF_CAMERA_ID in config  # MIPI = P4
    is_s3 = CONF_ESP32_CAMERA_ID in config  # esp32_camera = S3 or other

    if is_p4:
        cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32P4=1")
        isa_target = "esp32p4"
    else:
        # ESP32-S3 uses TIE728 SIMD engine
        cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32S3=1")
        isa_target = "tie728"

    # ESP-DL: download via PlatformIO lib_deps
    # Include paths are set by the build script (yolo11_detection_build.py)
    cg.add_library("esp-dl", None, "https://github.com/espressif/esp-dl.git#v3.2.3")

    # Build script for model embedding
    build_script_path = os.path.join(component_dir, "yolo11_detection_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
