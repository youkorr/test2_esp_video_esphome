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
CONF_CLASS_LABELS = "class_labels"
CONF_DETECT_CLASSES = "detect_classes"
CONF_DETECTION_INTERVAL = "detection_interval"
CONF_DRAW_ENABLED = "draw_enabled"

# 80 classes COCO par défaut
DEFAULT_COCO_LABELS = [
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train",
    "truck", "boat", "traffic light", "fire hydrant", "stop sign",
    "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep",
    "cow", "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella",
    "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard",
    "sports ball", "kite", "baseball bat", "baseball glove", "skateboard",
    "surfboard", "tennis racket", "bottle", "wine glass", "cup", "fork",
    "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair",
    "couch", "potted plant", "bed", "dining table", "toilet", "tv",
    "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave",
    "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase",
    "scissors", "teddy bear", "hair drier", "toothbrush",
]

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
            cv.Optional(CONF_CLASS_LABELS, default=DEFAULT_COCO_LABELS): cv.ensure_list(cv.string),
            cv.Optional(CONF_DETECT_CLASSES): cv.ensure_list(cv.positive_int),
            cv.Optional(CONF_DETECTION_INTERVAL, default=1): cv.positive_int,
            cv.Optional(CONF_DRAW_ENABLED, default=True): cv.boolean,
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
    synchronous=True,
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

    # Set class labels
    for label in config[CONF_CLASS_LABELS]:
        cg.add(var.add_class_label(label))

    # Set class filter (only detect these COCO class indices)
    if CONF_DETECT_CLASSES in config:
        for class_id in config[CONF_DETECT_CLASSES]:
            cg.add(var.add_detect_class(class_id))

    # Detection interval and draw settings
    cg.add(var.set_detection_interval(config[CONF_DETECTION_INTERVAL]))
    cg.add(var.set_draw_enabled(config[CONF_DRAW_ENABLED]))

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

    # ESP-DL: download via PlatformIO lib_deps
    # Include paths are set by the build script (yolov11_build.py)
    cg.add_library("esp-dl", None, "https://github.com/espressif/esp-dl.git#v3.2.3")

    # Register build script
    build_script = os.path.join(component_dir, "yolov11_build.py")
    if os.path.exists(build_script):
        cg.add_platformio_option(
            "extra_scripts", [f"post:{build_script}"]
        )
