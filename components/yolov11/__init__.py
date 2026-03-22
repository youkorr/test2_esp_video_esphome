import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation
import os

CONF_ESP32_CAMERA_ID = "esp32_camera_id"
CONF_CAMERA_ID = "camera_id"
CONF_MODEL_ID = "model_id"
CONF_SCORE_THRESHOLD = "score_threshold"
CONF_NMS_THRESHOLD = "nms_threshold"

yolov11_ns = cg.esphome_ns.namespace("yolov11")
YOLOV11Component = yolov11_ns.class_("YOLOV11Component", cg.Component)
YOLOV11InferenceAction = yolov11_ns.class_(
    "YOLOV11InferenceAction", automation.Action
)

# External types
file_ns = cg.esphome_ns.namespace("file_component")
FileData = file_ns.class_("FileData", cg.Component)

# esp32_camera (ESPHome standard)
esp32_camera_ns = cg.esphome_ns.namespace("esp32_camera")
ESP32Camera = esp32_camera_ns.class_("ESP32Camera", cg.Component)

# esp_cam_sensor (MIPI DSI - internal camera)
esp_cam_sensor_ns = cg.esphome_ns.namespace("esp_cam_sensor")
MipiDSICamComponent = esp_cam_sensor_ns.class_(
    "MipiDSICamComponent", cg.Component
)


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
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(YOLOV11Component),
            cv.Optional(CONF_ESP32_CAMERA_ID): cv.use_id(ESP32Camera),
            cv.Optional(CONF_CAMERA_ID): cv.use_id(MipiDSICamComponent),
            cv.Required(CONF_MODEL_ID): cv.use_id(FileData),
            cv.Optional(CONF_SCORE_THRESHOLD, default=0.3): cv.float_range(
                min=0.0, max=1.0
            ),
            cv.Optional(CONF_NMS_THRESHOLD, default=0.5): cv.float_range(
                min=0.0, max=1.0
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_camera_config,
)


# Register inference action
YOLOV11_INFERENCE_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(YOLOV11Component),
    }
)


@automation.register_action(
    "yolov11.inference",
    YOLOV11InferenceAction,
    YOLOV11_INFERENCE_ACTION_SCHEMA,
)
async def yolov11_inference_action_to_code(
    config, action_id, template_arg, args
):
    var = cg.new_Pvariable(action_id, template_arg)
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(var.set_parent(parent))
    return var


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set camera
    if CONF_ESP32_CAMERA_ID in config:
        camera = await cg.get_variable(config[CONF_ESP32_CAMERA_ID])
        cg.add(var.set_esp32_camera(camera))
        cg.add_build_flag("-DUSE_YOLOV11_ESP32_CAMERA")
    elif CONF_CAMERA_ID in config:
        camera = await cg.get_variable(config[CONF_CAMERA_ID])
        cg.add(var.set_mipi_camera(camera))
        cg.add_build_flag("-DUSE_YOLOV11_MIPI_CAMERA")

    # Set model
    model = await cg.get_variable(config[CONF_MODEL_ID])
    cg.add(var.set_model(model))

    # Set thresholds
    cg.add(var.set_score_threshold(config[CONF_SCORE_THRESHOLD]))
    cg.add(var.set_nms_threshold(config[CONF_NMS_THRESHOLD]))

    # Build flags for ESP-DL
    cg.add_build_flag("-DESP_DL_MODEL_YOLO11=1")
    cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32P4=1")

    # Include paths
    component_dir = os.path.dirname(os.path.abspath(__file__))
    parent_components_dir = os.path.dirname(component_dir)

    # yolo11_detect includes
    yolo11_detect_dir = os.path.join(parent_components_dir, "yolo11_detect")
    if os.path.exists(yolo11_detect_dir):
        cg.add_build_flag(f"-I{yolo11_detect_dir}")

    # ESP-DL include paths
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

    # Register build script
    build_script = os.path.join(component_dir, "yolov11_build.py")
    if os.path.exists(build_script):
        cg.add_platformio_option(
            "extra_scripts", [f"post:{build_script}"]
        )
