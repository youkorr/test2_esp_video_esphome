import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation
from esphome.core import CORE
import os
import subprocess
import logging

_LOGGER = logging.getLogger(__name__)

ESP_DL_REPO = "https://github.com/espressif/esp-dl.git"
ESP_DL_TAG = "v3.2.3"
ESP_DL_SPARSE_DIRS = ["dl", "fbs_loader", "vision"]

ESP_DL_INCLUDE_SUBDIRS = [
    "dl", "dl/tool/include", "dl/tool/isa/esp32p4",
    "dl/tool/src", "dl/tensor/include", "dl/tensor/src",
    "dl/base", "dl/base/isa", "dl/base/isa/esp32p4",
    "dl/math/include", "dl/math/src", "dl/model/include",
    "dl/model/src", "dl/module/include", "dl/module/src",
    "fbs_loader/include", "fbs_loader/lib/esp32p4", "fbs_loader/src",
    "vision/detect", "vision/image", "vision/image/isa",
    "vision/image/isa/esp32p4",
]


def _resolve_esp_dl_root(path):
    """Handle nested esp-dl/esp-dl/ repo structure. Returns path containing dl/."""
    if os.path.isdir(os.path.join(path, "dl")):
        return path
    inner = os.path.join(path, "esp-dl")
    if os.path.isdir(os.path.join(inner, "dl")):
        return inner
    return path


def _ensure_esp_dl(build_path):
    """Download esp-dl if not already present. Returns path to esp-dl root."""
    download_dir = os.path.join(str(build_path), ".espdl_cache", "esp-dl")

    # Already downloaded? Check both direct and nested structure
    resolved = _resolve_esp_dl_root(download_dir)
    marker = os.path.join(resolved, "dl", "base")
    if os.path.isdir(marker):
        _LOGGER.info("[ESP-DL] Already present: %s", resolved)
        return resolved

    _LOGGER.info("[ESP-DL] Downloading esp-dl %s to %s ...", ESP_DL_TAG, download_dir)

    # Try sparse checkout
    try:
        import shutil
        if os.path.exists(download_dir):
            shutil.rmtree(download_dir)
        os.makedirs(download_dir, exist_ok=True)

        subprocess.run(["git", "init"], cwd=download_dir, capture_output=True, check=True)
        subprocess.run(["git", "remote", "add", "origin", ESP_DL_REPO],
                        cwd=download_dir, capture_output=True, check=True)
        subprocess.run(["git", "config", "core.sparseCheckout", "true"],
                        cwd=download_dir, capture_output=True, check=True)

        sparse_file = os.path.join(download_dir, ".git", "info", "sparse-checkout")
        os.makedirs(os.path.dirname(sparse_file), exist_ok=True)
        with open(sparse_file, "w") as f:
            for d in ESP_DL_SPARSE_DIRS:
                f.write(f"{d}/\n")

        result = subprocess.run(
            ["git", "fetch", "--depth=1", "origin", f"tags/{ESP_DL_TAG}"],
            cwd=download_dir, capture_output=True, text=True, timeout=120
        )
        if result.returncode == 0:
            subprocess.run(["git", "checkout", "FETCH_HEAD"],
                            cwd=download_dir, capture_output=True, check=True)
            resolved = _resolve_esp_dl_root(download_dir)
            if os.path.isdir(os.path.join(resolved, "dl", "base")):
                _LOGGER.info("[ESP-DL] Sparse checkout OK: %s", resolved)
                return resolved

        _LOGGER.warning("[ESP-DL] Sparse checkout failed, trying full clone...")
    except Exception as e:
        _LOGGER.warning("[ESP-DL] Sparse checkout error: %s", e)

    # Fallback: full shallow clone
    import shutil
    if os.path.exists(download_dir):
        shutil.rmtree(download_dir)
    try:
        subprocess.run(
            ["git", "clone", "--depth=1", "--branch", ESP_DL_TAG,
             ESP_DL_REPO, download_dir],
            capture_output=True, text=True, timeout=300, check=True
        )
        resolved = _resolve_esp_dl_root(download_dir)
        for unwanted in ["audio", "examples", "docs", "test"]:
            p = os.path.join(resolved, unwanted)
            if os.path.exists(p):
                shutil.rmtree(p)
        _LOGGER.info("[ESP-DL] Full clone OK: %s", resolved)
        return resolved
    except Exception as e:
        raise RuntimeError(f"[ESP-DL] Failed to download esp-dl: {e}")

CONF_ESP32_CAMERA_ID = "esp32_camera_id"
CONF_CAMERA_ID = "camera_id"
CONF_CANVAS_ID = "canvas_id"
CONF_MODEL_ID = "model_id"
CONF_MODEL_TYPE = "model_type"
CONF_SCORE_THRESHOLD = "score_threshold"
CONF_NMS_THRESHOLD = "nms_threshold"
CONF_CLASS_LABELS = "class_labels"
CONF_DETECT_CLASSES = "detect_classes"
CONF_DETECTION_INTERVAL = "detection_interval"
CONF_DRAW_ENABLED = "draw_enabled"

# Supported model types
MODEL_TYPE_YOLO11 = "yolo11"
MODEL_TYPE_YOLO26N = "yolo26n"

# 80 classes COCO par défaut
DEFAULT_COCO_LABELS = [
    "person", "bicycle", "car", "cat", "dog", "bus", "train",

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
            cv.Optional(CONF_CANVAS_ID): cv.string,
            cv.Required(CONF_MODEL_ID): cv.use_id(FileData),
            cv.Optional(CONF_MODEL_TYPE, default=MODEL_TYPE_YOLO11): cv.one_of(
                MODEL_TYPE_YOLO11, MODEL_TYPE_YOLO26N, lower=True
            ),
            cv.Optional(CONF_SCORE_THRESHOLD, default=0.25): cv.float_range(
                min=0.0, max=1.0
            ),
            cv.Optional(CONF_NMS_THRESHOLD, default=0.7): cv.float_range(
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

    # Canvas ID (for lvgl_camera_display integration)
    if CONF_CANVAS_ID in config:
        cg.add(var.set_canvas_id(config[CONF_CANVAS_ID]))
    # Set model
    model = await cg.get_variable(config[CONF_MODEL_ID])
    cg.add(var.set_model(model))

    # Model type: yolo11 (default) or yolo26n
    model_type = config[CONF_MODEL_TYPE]
    if model_type == MODEL_TYPE_YOLO26N:
        cg.add(var.set_model_type(cg.RawExpression("yolov11::MODEL_TYPE_YOLO26N")))

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
    if CONF_CAMERA_ID in config:
        cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32P4=1")

    # Include paths
    component_dir = os.path.dirname(os.path.abspath(__file__))
    parent_components_dir = os.path.dirname(component_dir)

    # yolo11_detect includes
    yolo11_detect_dir = os.path.join(parent_components_dir, "yolo11_detect")
    if os.path.exists(yolo11_detect_dir):
        cg.add_build_flag(f"-I{yolo11_detect_dir}")

    # ESP-DL: download NOW (during codegen) so -I paths exist at compile time.
    # No cg.add_library — we manage esp-dl ourselves to avoid PlatformIO
    # auto-compiling it (which fails due to missing esp_dsp.h).
    esp_dl_dir = _ensure_esp_dl(CORE.build_path)
    _LOGGER.info("[YOLOV11] ESP-DL dir: %s", esp_dl_dir)
    _LOGGER.info("[YOLOV11] dl_model_base.hpp exists: %s",
                 os.path.exists(os.path.join(esp_dl_dir, "dl", "model", "include", "dl_model_base.hpp")))
    for subdir in ESP_DL_INCLUDE_SUBDIRS:
        cg.add_build_flag(f"-I{os.path.join(esp_dl_dir, subdir)}")

    # Register build script
    build_script = os.path.join(component_dir, "yolov11_build.py")
    if os.path.exists(build_script):
        cg.add_platformio_option(
            "extra_scripts", [f"post:{build_script}"]
        )
