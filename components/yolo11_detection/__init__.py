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


def _ensure_esp_dl(build_path):
    """Download esp-dl if not already present. Returns path to esp-dl root."""
    download_dir = os.path.join(str(build_path), ".espdl_cache", "esp-dl")
    marker = os.path.join(download_dir, "dl", "base")
    if os.path.isdir(marker):
        _LOGGER.info("[ESP-DL] Already present: %s", download_dir)
        return download_dir

    _LOGGER.info("[ESP-DL] Downloading esp-dl %s ...", ESP_DL_TAG)
    import shutil
    if os.path.exists(download_dir):
        shutil.rmtree(download_dir)
    os.makedirs(download_dir, exist_ok=True)

    # Try sparse checkout first
    try:
        subprocess.run(["git", "init"], cwd=download_dir, capture_output=True, check=True)
        subprocess.run(["git", "remote", "add", "origin", ESP_DL_REPO],
                        cwd=download_dir, capture_output=True, check=True)
        subprocess.run(["git", "config", "core.sparseCheckout", "true"],
                        cwd=download_dir, capture_output=True, check=True)
        sparse_file = os.path.join(download_dir, ".git", "info", "sparse-checkout")
        os.makedirs(os.path.dirname(sparse_file), exist_ok=True)
        with open(sparse_file, "w") as f:
            for d in ["dl", "fbs_loader", "vision"]:
                f.write(f"{d}/\n")
        r = subprocess.run(["git", "fetch", "--depth=1", "origin", f"tags/{ESP_DL_TAG}"],
                            cwd=download_dir, capture_output=True, text=True, timeout=120)
        if r.returncode == 0:
            subprocess.run(["git", "checkout", "FETCH_HEAD"],
                            cwd=download_dir, capture_output=True, check=True)
            if os.path.isdir(marker):
                _LOGGER.info("[ESP-DL] Sparse checkout OK")
                return download_dir
    except Exception as e:
        _LOGGER.warning("[ESP-DL] Sparse failed: %s", e)

    # Fallback: full clone
    if os.path.exists(download_dir):
        shutil.rmtree(download_dir)
    subprocess.run(
        ["git", "clone", "--depth=1", "--branch", ESP_DL_TAG, ESP_DL_REPO, download_dir],
        capture_output=True, text=True, timeout=300, check=True
    )
    for d in ["audio", "examples", "docs", "test"]:
        p = os.path.join(download_dir, d)
        if os.path.exists(p):
            shutil.rmtree(p)
    _LOGGER.info("[ESP-DL] Full clone OK")
    return download_dir

CONF_ESP32_CAMERA_ID = "esp32_camera_id"
CONF_CAMERA_ID = "camera_id"
CONF_CANVAS_ID = "canvas_id"
CONF_SCORE_THRESHOLD = "score_threshold"
CONF_NMS_THRESHOLD = "nms_threshold"
CONF_DETECTION_INTERVAL = "detection_interval"
CONF_DRAW_ENABLED = "draw_enabled"
CONF_ON_OBJECT_DETECTED = "on_object_detected"
CONF_DETECT_CLASSES = "detect_classes"

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


# COCO class name -> index mapping for YAML config
COCO_CLASSES = {
    "person": 0, "bicycle": 1, "car": 2, "motorcycle": 3, "airplane": 4,
    "bus": 5, "train": 6, "truck": 7, "boat": 8, "traffic light": 9,
    "fire hydrant": 10, "stop sign": 11, "parking meter": 12, "bench": 13,
    "bird": 14, "cat": 15, "dog": 16, "horse": 17, "sheep": 18, "cow": 19,
    "elephant": 20, "bear": 21, "zebra": 22, "giraffe": 23, "backpack": 24,
    "umbrella": 25, "handbag": 26, "tie": 27, "suitcase": 28, "frisbee": 29,
    "skis": 30, "snowboard": 31, "sports ball": 32, "kite": 33,
    "baseball bat": 34, "baseball glove": 35, "skateboard": 36,
    "surfboard": 37, "tennis racket": 38, "bottle": 39, "wine glass": 40,
    "cup": 41, "fork": 42, "knife": 43, "spoon": 44, "bowl": 45,
    "banana": 46, "apple": 47, "sandwich": 48, "orange": 49, "broccoli": 50,
    "carrot": 51, "hot dog": 52, "pizza": 53, "donut": 54, "cake": 55,
    "chair": 56, "couch": 57, "potted plant": 58, "bed": 59,
    "dining table": 60, "toilet": 61, "tv": 62, "laptop": 63, "mouse": 64,
    "remote": 65, "keyboard": 66, "cell phone": 67, "microwave": 68,
    "oven": 69, "toaster": 70, "sink": 71, "refrigerator": 72, "book": 73,
    "clock": 74, "vase": 75, "scissors": 76, "teddy bear": 77,
    "hair drier": 78, "toothbrush": 79,
}


def validate_detect_classes(value):
    """Validate detect_classes: accepts class names (str) or indices (int)."""
    if not isinstance(value, list):
        raise cv.Invalid("detect_classes must be a list")
    indices = []
    for item in value:
        if isinstance(item, int):
            if item < 0 or item > 79:
                raise cv.Invalid(f"Class index {item} out of range (0-79)")
            indices.append(item)
        elif isinstance(item, str):
            name = item.lower().strip()
            if name in COCO_CLASSES:
                indices.append(COCO_CLASSES[name])
            else:
                raise cv.Invalid(
                    f"Unknown class '{item}'. Valid: {', '.join(sorted(COCO_CLASSES.keys()))}"
                )
        else:
            raise cv.Invalid(f"Invalid class specifier: {item}")
    return indices


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
        cv.Optional(CONF_DETECT_CLASSES, default=[]): validate_detect_classes,
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

    # Class filtering
    detect_classes = config.get(CONF_DETECT_CLASSES, [])
    if detect_classes:
        for cls_id in detect_classes:
            cg.add(var.add_detect_class(cls_id))

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

    # ESP-DL: download NOW (during codegen) so -I paths exist at compile time.
    esp_dl_dir = _ensure_esp_dl(CORE.build_path)
    esp_dl_inc = [
        "dl", "dl/tool/include", f"dl/tool/isa/{isa_target}",
        "dl/tool/src", "dl/tensor/include", "dl/tensor/src",
        "dl/base", "dl/base/isa", f"dl/base/isa/{isa_target}",
        "dl/math/include", "dl/math/src", "dl/model/include",
        "dl/model/src", "dl/module/include", "dl/module/src",
        "fbs_loader/include", f"fbs_loader/lib/{isa_target}", "fbs_loader/src",
        "vision/detect", "vision/image", "vision/image/isa",
        f"vision/image/isa/{isa_target}",
    ]
    for subdir in esp_dl_inc:
        cg.add_build_flag(f"-I{os.path.join(esp_dl_dir, subdir)}")

    # Build script for model embedding + ESP-DL source compilation
    build_script_path = os.path.join(component_dir, "yolo11_detection_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
