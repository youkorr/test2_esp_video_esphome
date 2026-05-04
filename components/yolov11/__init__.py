"""
ESPHome component: yolov11
--------------------------
YOLO11 object detection for ESP32-S3 boards using the standard
`esp32_camera` component (DVP/parallel) instead of the MIPI-CSI
`esp_cam_sensor` we use on the ESP32-P4.

Two model sources are supported:

  - flash_rodata (default): the .espdl model file is embedded in the
    firmware at build time, like the existing `yolo11_detection`.
    Pros: zero-runtime-overhead load. Cons: model size eats firmware.

  - file: the model lives on the filesystem (typical use case: the
    `file:` platform from jesserockz/esphome-components stores a
    file in a flash partition and exposes it as a buffer at runtime).
    Pros: model can be replaced without reflashing. Cons: a few ms
    of extra setup time.

Camera input must be RGB565 - JPEG is not supported here because
the ESP32-S3 has no hardware JPEG decoder and software decode would
crash inference under 5 fps. Set `pixel_format: rgb565` on your
esp32_camera block.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation
import os

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32_camera"]

# ----- yaml keys -----
CONF_ESP32_CAMERA_ID = "esp32_camera_id"
CONF_MODEL_ID = "model_id"
CONF_SCORE_THRESHOLD = "score_threshold"
CONF_NMS_THRESHOLD = "nms_threshold"
CONF_DETECTION_INTERVAL_MS = "detection_interval_ms"
CONF_ON_OBJECT_DETECTED = "on_object_detected"
CONF_INFERENCE_TASK_STACK_SIZE = "inference_task_stack_size"
CONF_INFERENCE_TASK_PRIORITY = "inference_task_priority"
CONF_MAX_DETECTIONS = "max_detections"

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

# ----- jesserockz/esphome-components file: -----
# Reference is optional. We hold the buffer pointer and length at runtime
# via a function call into the file component. To stay loose-coupled, we
# only require that the user gives an id and we store the lookup.
file_ns = cg.esphome_ns.namespace("file")
FileComponent = file_ns.class_("File", cg.Component)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(YOLOv11Component),
        cv.Required(CONF_ESP32_CAMERA_ID): cv.use_id(ESP32Camera),
        # If model_id is not provided, the component falls back to the
        # flash-rodata embedded model (Option C). The build script
        # embeds the file pointed to by yolov11_build.py's MODEL_PATH.
        cv.Optional(CONF_MODEL_ID): cv.use_id(FileComponent),
        cv.Optional(CONF_SCORE_THRESHOLD, default=0.30): cv.float_range(min=0.0, max=1.0),
        cv.Optional(CONF_NMS_THRESHOLD, default=0.50): cv.float_range(min=0.0, max=1.0),
        cv.Optional(CONF_DETECTION_INTERVAL_MS, default=200): cv.int_range(min=50, max=10000),
        cv.Optional(CONF_MAX_DETECTIONS, default=10): cv.int_range(min=1, max=50),
        cv.Optional(CONF_INFERENCE_TASK_STACK_SIZE, default=8192): cv.int_range(min=4096, max=32768),
        cv.Optional(CONF_INFERENCE_TASK_PRIORITY, default=5): cv.int_range(min=1, max=10),
        cv.Optional(CONF_ON_OBJECT_DETECTED): automation.validate_automation(
            {
                cv.GenerateID(): cv.declare_id(ObjectDetectedTrigger),
            }
        ),
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

    if CONF_MODEL_ID in config:
        # Custom model loaded from `file:` at runtime
        file_var = await cg.get_variable(config[CONF_MODEL_ID])
        cg.add(var.set_model_from_file(file_var))
        cg.add_define("YOLOV11_MODEL_FROM_FILE")
    else:
        # Flash-rodata embedded model. The build script generates the
        # _binary_yolov11_model_espdl_start symbol from the .espdl in
        # components/yolov11/models/.
        cg.add_define("YOLOV11_MODEL_FROM_FLASH")

    # ------------------------------------------------------------------
    # Build flags - ESP32-S3 specific
    # ------------------------------------------------------------------
    cg.add_build_flag("-DESP_DL_MODEL_YOLO11=1")
    cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32S3=1")

    # The default coco_detect Kconfig variant used on S3
    cg.add_build_flag("-DCONFIG_COCO_DETECT_YOLO11N_S8_V1=1")
    cg.add_build_flag("-DCONFIG_DEFAULT_COCO_DETECT_MODEL=0")
    cg.add_build_flag("-DCONFIG_YOLO11_DETECT_S8_V1=1")
    cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_TYPE=0")

    # Model location: when using `file:` we still build the wrapper in
    # "in-memory" mode (location=0 is flash_rodata, but the data pointer
    # is overridden at runtime - see set_model_from_file in the cpp).
    cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_IN_FLASH_RODATA=1")
    cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_LOCATION=0")
    cg.add_build_flag("-DCONFIG_COCO_DETECT_MODEL_IN_FLASH_RODATA=1")
    cg.add_build_flag("-DCONFIG_COCO_DETECT_MODEL_LOCATION=0")

    # ------------------------------------------------------------------
    # ESP-DL include paths (S3 variants)
    # ------------------------------------------------------------------
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
    if os.path.exists(esp_dl_dir):
        for inc in [
            "dl",
            "dl/tool/include",
            "dl/tool/isa/esp32s3",
            "dl/tool/src",
            "dl/tensor/include",
            "dl/tensor/src",
            "dl/base",
            "dl/base/isa",
            "dl/base/isa/esp32s3",
            "dl/math/include",
            "dl/math/src",
            "dl/model/include",
            "dl/model/src",
            "dl/module/include",
            "dl/module/src",
            "fbs_loader/include",
            "fbs_loader/lib/esp32s3",
            "fbs_loader/src",
            "vision/detect",
            "vision/image",
            "vision/image/isa",
            "vision/image/isa/esp32s3",
        ]:
            inc_path = os.path.join(esp_dl_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")

    # ------------------------------------------------------------------
    # Triggers
    # ------------------------------------------------------------------
    for conf in config.get(CONF_ON_OBJECT_DETECTED, []):
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(
            trigger,
            [(cg.int_, "object_count"), (cg.std_string, "summary")],
            conf,
        )

    # ------------------------------------------------------------------
    # Build script (post: extra_scripts) - compiles ESP-DL S3 sources
    # and embeds the flash-rodata model when needed.
    # ------------------------------------------------------------------
    build_script = os.path.join(component_dir, "yolov11_build.py")
    if os.path.exists(build_script):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script}"])


# ============================================================================
# Action: yolov11.inference
# Force a one-shot inference on the latest camera frame.
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
