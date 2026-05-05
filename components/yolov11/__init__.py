"""
ESPHome component: yolov11
--------------------------
YOLO11 object detection for ESP32-S3 boards using the standard
`esp32_camera` component (DVP/parallel) instead of the MIPI-CSI
`esp_cam_sensor` we use on the ESP32-P4.

Camera input must be RGB565 - JPEG is NOT supported (the ESP32-S3 has
no hardware JPEG decoder and software decode would crash inference
under 5 fps). Set `pixel_format: rgb565` on your `esp32_camera:` block.

Model selection:
  - `model_path: ./my_model.espdl`  -> picks ANY .espdl file the user
    drops next to the YAML and embeds it at build time.
  - omitted: falls back to the bundled yolo11_detect_s8_v1.espdl model.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE
from esphome import automation
import os

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32_camera"]

# ----- yaml keys -----
CONF_ESP32_CAMERA_ID = "esp32_camera_id"
CONF_MODEL_ID = "model_id"
CONF_MODEL_PATH = "model_path"
CONF_SCORE_THRESHOLD = "score_threshold"
CONF_NMS_THRESHOLD = "nms_threshold"
CONF_DETECTION_INTERVAL_MS = "detection_interval_ms"
CONF_ON_OBJECT_DETECTED = "on_object_detected"
CONF_ON_DETECTION = "on_detection"
CONF_INFERENCE_TASK_STACK_SIZE = "inference_task_stack_size"
CONF_INFERENCE_TASK_PRIORITY = "inference_task_priority"
CONF_MAX_DETECTIONS = "max_detections"
CONF_FRAME_WIDTH = "frame_width"
CONF_FRAME_HEIGHT = "frame_height"

# ----- C++ namespaces -----
yolov11_ns = cg.esphome_ns.namespace("yolov11")
YOLOv11Component = yolov11_ns.class_("YOLOv11Component", cg.Component)

ObjectDetectedTrigger = yolov11_ns.class_(
    "ObjectDetectedTrigger", automation.Trigger.template(cg.int_, cg.std_string)
)
RunInferenceAction = yolov11_ns.class_("RunInferenceAction", automation.Action)

# ----- esp32_camera reference -----
esp32_camera_ns = cg.esphome_ns.namespace("esp32_camera")
ESP32Camera = esp32_camera_ns.class_("ESP32Camera", cg.Component)


_TRIGGER_SCHEMA = automation.validate_automation(
    {
        cv.GenerateID(): cv.declare_id(ObjectDetectedTrigger),
    }
)


def _validate_model_path(value):
    """Validate that the .espdl file exists on disk, relative to YAML."""
    value = cv.string(value)
    if not value:
        raise cv.Invalid("model_path must not be empty")
    abs_path = value
    if not os.path.isabs(abs_path):
        abs_path = os.path.join(CORE.config_dir, value)
    if not os.path.isfile(abs_path):
        raise cv.Invalid(
            f"model_path: file not found at {abs_path}\n"
            f"Place your .espdl model next to your YAML and use a relative path."
        )
    return value


def _posix(p):
    """Normalise a path to forward slashes so gcc -I flags work on Windows.

    cg.add_build_flag(f"-I{path}") goes through platformio.ini's build_flags
    which on Windows can mishandle backslash-laden paths (gcc treats `\\` as
    line-continuation in some contexts and `\d` as an escape in others).
    Using forward slashes is safe on every host OS - mingw and msys gcc
    accept them on Windows just like on Linux.
    """
    return p.replace("\\", "/")


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(YOLOv11Component),
        cv.Required(CONF_ESP32_CAMERA_ID): cv.use_id(ESP32Camera),
        cv.Optional(CONF_MODEL_PATH): _validate_model_path,
        cv.Optional(CONF_MODEL_ID): cv.use_id(cg.uint8),
        cv.Optional(CONF_SCORE_THRESHOLD, default=0.30): cv.float_range(min=0.0, max=1.0),
        cv.Optional(CONF_NMS_THRESHOLD, default=0.50): cv.float_range(min=0.0, max=1.0),
        cv.Optional(CONF_DETECTION_INTERVAL_MS, default=200): cv.int_range(min=50, max=10000),
        cv.Optional(CONF_MAX_DETECTIONS, default=10): cv.int_range(min=1, max=50),
        cv.Optional(CONF_INFERENCE_TASK_STACK_SIZE, default=8192): cv.int_range(min=4096, max=32768),
        cv.Optional(CONF_INFERENCE_TASK_PRIORITY, default=5): cv.int_range(min=1, max=10),
        cv.Optional(CONF_FRAME_WIDTH, default=320): cv.int_range(min=96, max=2560),
        cv.Optional(CONF_FRAME_HEIGHT, default=240): cv.int_range(min=96, max=1920),
        cv.Optional(CONF_ON_OBJECT_DETECTED): _TRIGGER_SCHEMA,
        cv.Optional(CONF_ON_DETECTION): _TRIGGER_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cam = await cg.get_variable(config[CONF_ESP32_CAMERA_ID])
    cg.add(var.set_camera(cam))

    cg.add(var.set_score_threshold(config[CONF_SCORE_THRESHOLD]))
    cg.add(var.set_nms_threshold(config[CONF_NMS_THRESHOLD]))
    cg.add(var.set_detection_interval_ms(config[CONF_DETECTION_INTERVAL_MS]))
    cg.add(var.set_max_detections(config[CONF_MAX_DETECTIONS]))
    cg.add(var.set_inference_task_stack_size(config[CONF_INFERENCE_TASK_STACK_SIZE]))
    cg.add(var.set_inference_task_priority(config[CONF_INFERENCE_TASK_PRIORITY]))
    cg.add(var.set_frame_width(config[CONF_FRAME_WIDTH]))
    cg.add(var.set_frame_height(config[CONF_FRAME_HEIGHT]))

    if CONF_MODEL_PATH in config:
        model_path = config[CONF_MODEL_PATH]
        if not os.path.isabs(model_path):
            model_path = os.path.join(CORE.config_dir, model_path)
        # Forward to the build script via a CPP define. Forward-slash the
        # path so gcc on Windows doesn't choke on backslashes in the
        # macro value.
        cg.add_build_flag(f'-DYOLOV11_USER_MODEL_PATH="{_posix(model_path)}"')

    if CONF_MODEL_ID in config:
        model_arr = await cg.get_variable(config[CONF_MODEL_ID])
        cg.add(var.set_model_buffer(model_arr, cg.RawExpression(f"sizeof({model_arr})")))
        cg.add_define("YOLOV11_MODEL_FROM_FILE")

    # ------------------------------------------------------------------
    # Build flags - ESP32-S3 specific
    # ------------------------------------------------------------------
    cg.add_build_flag("-DESP_DL_MODEL_YOLO11=1")
    cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32S3=1")

    cg.add_build_flag("-DCONFIG_COCO_DETECT_YOLO11N_S8_V1=1")
    cg.add_build_flag("-DCONFIG_DEFAULT_COCO_DETECT_MODEL=0")
    cg.add_build_flag("-DCONFIG_YOLO11_DETECT_S8_V1=1")
    cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_TYPE=0")

    cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_IN_FLASH_RODATA=1")
    cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_LOCATION=0")
    cg.add_build_flag("-DCONFIG_COCO_DETECT_MODEL_IN_FLASH_RODATA=1")
    cg.add_build_flag("-DCONFIG_COCO_DETECT_MODEL_LOCATION=0")

    # ------------------------------------------------------------------
    # ESP-DL include paths (S3 variants) - GLOBAL via PlatformIO build
    # flags so ESPHome's main src/ pass (yolov11_component.cpp,
    # yolo11_detect_inner.cpp, yolov11_text_sensor.cpp) finds the headers.
    #
    # IMPORTANT: ALL paths must be forward-slashed on Windows. gcc
    # accepts forward slashes on every platform, while a raw Windows
    # path like `C:\Users\foo\esp-dl\dl\base` written as `-IC:\Users\foo\
    # esp-dl\dl\base` after platformio.ini parsing has unpredictable
    # results: backslashes can be eaten as line-continuations or
    # interpreted as escape sequences (\d, \t, \n, \U, ...). Forward
    # slashes sidestep all of that.
    # ------------------------------------------------------------------
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    # Add the component's own dir + the parent dir so #include "..." for
    # sibling headers (yolo11_detect.hpp lives next to yolov11_component.cpp)
    # always works.
    cg.add_build_flag(f"-I{_posix(component_dir)}")

    esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
    if os.path.exists(esp_dl_dir):
        for inc in [
            "dl",
            "dl/tool/include",
            "dl/tool/isa/tie728",
            "dl/tool/isa/xtensa",
            "dl/tool/src",
            "dl/tensor/include",
            "dl/tensor/src",
            "dl/base",
            "dl/base/isa",
            "dl/base/isa/tie728",
            "dl/base/isa/xtensa",
            "dl/math/include",
            "dl/math/src",
            "dl/model/include",
            "dl/model/src",
            "dl/module/include",
            "dl/module/src",
            "fbs_loader/include",
            "fbs_loader/src",
            "vision/detect",
            "vision/image",
            "vision/image/isa",
        ]:
            inc_path = os.path.join(esp_dl_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{_posix(inc_path)}")

    # ------------------------------------------------------------------
    # Triggers (both yaml keys go through the same class)
    # ------------------------------------------------------------------
    triggers = []
    triggers.extend(config.get(CONF_ON_OBJECT_DETECTED, []))
    triggers.extend(config.get(CONF_ON_DETECTION, []))
    for conf in triggers:
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(
            trigger,
            [(cg.int_, "object_count"), (cg.std_string, "summary")],
            conf,
        )

    # ------------------------------------------------------------------
    # Build script (post: extra_scripts)
    # ------------------------------------------------------------------
    build_script = os.path.join(component_dir, "yolov11_build.py")
    if os.path.exists(build_script):
        cg.add_platformio_option("extra_scripts", [f"post:{_posix(build_script)}"])


# ============================================================================
# Action: yolov11.inference
# ============================================================================
INFERENCE_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(YOLOv11Component),
    }
)


@automation.register_action(
    "yolov11.inference", RunInferenceAction, INFERENCE_ACTION_SCHEMA, synchronous=True
)
async def run_inference_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
