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
CONF_MODEL_VARIANT = "model_variant"

# Model location types
MODEL_LOCATION_FLASH = "flash_rodata"
MODEL_LOCATION_SDCARD = "sdcard"

# Model variants
MODEL_VARIANT_S8_V1 = "s8_v1"
MODEL_VARIANT_S8_V2 = "s8_v2"
MODEL_VARIANT_S8_V3 = "s8_v3"
MODEL_VARIANT_320_S8_V3 = "320_s8_v3"

MODEL_VARIANT_INDEX = {
    MODEL_VARIANT_S8_V1: 0,
    MODEL_VARIANT_S8_V2: 1,
    MODEL_VARIANT_S8_V3: 2,
    MODEL_VARIANT_320_S8_V3: 3,
}

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
    cv.Optional(CONF_MODEL_VARIANT, default=MODEL_VARIANT_S8_V1): cv.one_of(
        *MODEL_VARIANT_INDEX.keys(), lower=True
    ),
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

    for conf in config.get(CONF_ON_OBJECT_DETECTED, []):
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(trigger, [(cg.int_, "object_count")], conf)

    cg.add_build_flag("-DESP_DL_MODEL_YOLO11=1")
    cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32P4=1")

    variant = config[CONF_MODEL_VARIANT]
    variant_index = MODEL_VARIANT_INDEX[variant]

    # Map pour COCO_DETECT
    variant_flag_map = {
        MODEL_VARIANT_S8_V1:       "CONFIG_COCO_DETECT_YOLO11N_S8_V1",
        MODEL_VARIANT_S8_V2:       "CONFIG_COCO_DETECT_YOLO11N_S8_V2",
        MODEL_VARIANT_S8_V3:       "CONFIG_COCO_DETECT_YOLO11N_S8_V3",
        MODEL_VARIANT_320_S8_V3:   "CONFIG_COCO_DETECT_YOLO11N_320_S8_V3",
    }
    
    # Map pour YOLO11_DETECT (La correction majeure)
    yolo_variant_flag_map = {
        MODEL_VARIANT_S8_V1:       "CONFIG_YOLO11_DETECT_S8_V1",
        MODEL_VARIANT_S8_V2:       "CONFIG_YOLO11_DETECT_S8_V2",
        MODEL_VARIANT_S8_V3:       "CONFIG_YOLO11_DETECT_S8_V3",
        MODEL_VARIANT_320_S8_V3:   "CONFIG_YOLO11_DETECT_320_S8_V3",
    }

    cg.add_build_flag(f"-D{variant_flag_map[variant]}=1")
    cg.add_build_flag(f"-DCONFIG_DEFAULT_COCO_DETECT_MODEL={variant_index}")
    cg.add_build_flag(f"-DCONFIG_YOLO11_DETECT_MODEL_TYPE={variant_index}")
    cg.add_build_flag(f"-D{yolo_variant_flag_map[variant]}=1")

    model_location = config.get(CONF_MODEL_LOCATION, MODEL_LOCATION_FLASH)
    if model_location == MODEL_LOCATION_SDCARD:
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_IN_SDCARD=1")
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_IN_FLASH_RODATA=0")
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_LOCATION=2")
        cg.add_build_flag("-DCONFIG_COCO_DETECT_MODEL_IN_SDCARD=1")
        cg.add_build_flag("-DCONFIG_COCO_DETECT_MODEL_LOCATION=2")
        sd_path = config.get(CONF_MODEL_PATH, "/sdcard")
        cg.add(var.set_sdcard_model_path(cg.RawExpression(f'"{sd_path}"')))
    else:
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_IN_FLASH_RODATA=1")
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_IN_SDCARD=0")
        cg.add_build_flag("-DCONFIG_YOLO11_DETECT_MODEL_LOCATION=0")
        cg.add_build_flag("-DCONFIG_COCO_DETECT_MODEL_IN_FLASH_RODATA=1")
        cg.add_build_flag("-DCONFIG_COCO_DETECT_MODEL_LOCATION=0")

    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    yolo11_detect_dir = os.path.join(parent_components_dir, "yolo11_detect")
    if os.path.exists(yolo11_detect_dir):
        cg.add_build_flag(f"-I{yolo11_detect_dir}")

    esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
    if os.path.exists(esp_dl_dir):
        esp_dl_includes = [
            "dl", "dl/tool/include", "dl/tool/isa/esp32p4", "dl/tool/src",
            "dl/tensor/include", "dl/tensor/src", "dl/base", "dl/base/isa",
            "dl/base/isa/esp32p4", "dl/math/include", "dl/math/src",
            "dl/model/include", "dl/model/src", "dl/module/include",
            "dl/module/src", "fbs_loader/include", "fbs_loader/lib/esp32p4",
            "fbs_loader/src", "vision/detect", "vision/image",
            "vision/image/isa", "vision/image/isa/esp32p4",
        ]
        for inc in esp_dl_includes:
            inc_path = os.path.join(esp_dl_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")

    build_script_path = os.path.join(component_dir, "yolo11_detection_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
