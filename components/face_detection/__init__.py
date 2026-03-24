import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation
from esphome.core import CORE
import os

DEPENDENCIES = ["esp_cam_sensor"]
AUTO_LOAD = ["esp_cam_sensor"]

CONF_CAMERA_ID = "camera_id"
CONF_CANVAS_ID = "canvas_id"
CONF_MODEL_TYPE = "model_type"
CONF_SCORE_THRESHOLD = "score_threshold"
CONF_NMS_THRESHOLD = "nms_threshold"
CONF_RECOGNITION_ENABLED = "recognition_enabled"
CONF_FACE_DB_PATH = "face_db_path"
CONF_RECOGNITION_THRESHOLD = "recognition_threshold"
CONF_ON_FACE_DETECTED = "on_face_detected"
CONF_ON_FACE_RECOGNIZED = "on_face_recognized"
CONF_DETECTION_INTERVAL = "detection_interval"
CONF_DRAW_ENABLED = "draw_enabled"
CONF_MODEL_LOCATION = "model_location"
CONF_MODEL_PATH = "model_path"

# Available model types
MODEL_TYPE_FACE = "face_recognition"
MODEL_TYPE_YOLO11 = "yolo11"
MODEL_TYPE_POSE = "pose_detection"

# Model location types
MODEL_LOCATION_FLASH = "flash_rodata"
MODEL_LOCATION_SDCARD = "sdcard"

face_detection_ns = cg.esphome_ns.namespace("face_detection")
FaceDetectionComponent = face_detection_ns.class_("FaceDetectionComponent", cg.Component)

# Triggers
FaceDetectedTrigger = face_detection_ns.class_("FaceDetectedTrigger", automation.Trigger.template(cg.int_))
FaceRecognizedTrigger = face_detection_ns.class_("FaceRecognizedTrigger", automation.Trigger.template(cg.int_, cg.float_))

# Actions
EnrollFaceAction = face_detection_ns.class_("EnrollFaceAction", automation.Action)
EnrollFaceWithNameAction = face_detection_ns.class_("EnrollFaceWithNameAction", automation.Action)
SetFaceNameAction = face_detection_ns.class_("SetFaceNameAction", automation.Action)
DeleteFaceAction = face_detection_ns.class_("DeleteFaceAction", automation.Action)
ClearAllFacesAction = face_detection_ns.class_("ClearAllFacesAction", automation.Action)

esp_cam_sensor_ns = cg.esphome_ns.namespace("esp_cam_sensor")
MipiDsiCam = esp_cam_sensor_ns.class_("MipiDSICamComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(FaceDetectionComponent),
    cv.Required(CONF_CAMERA_ID): cv.use_id(MipiDsiCam),
    cv.Optional(CONF_CANVAS_ID): cv.string,
    cv.Optional(CONF_MODEL_TYPE, default=MODEL_TYPE_FACE): cv.one_of(
        MODEL_TYPE_FACE, MODEL_TYPE_YOLO11, MODEL_TYPE_POSE, lower=True
    ),
    cv.Optional(CONF_SCORE_THRESHOLD, default=0.3): cv.float_range(min=0.0, max=1.0),
    cv.Optional(CONF_NMS_THRESHOLD, default=0.5): cv.float_range(min=0.0, max=1.0),
    cv.Optional(CONF_DETECTION_INTERVAL, default=8): cv.int_range(min=1, max=600),
    cv.Optional(CONF_DRAW_ENABLED, default=True): cv.boolean,
    cv.Optional(CONF_RECOGNITION_ENABLED, default=False): cv.boolean,
    cv.Optional(CONF_FACE_DB_PATH, default="/sdcard/faces.db"): cv.string,
    cv.Optional(CONF_RECOGNITION_THRESHOLD, default=0.7): cv.float_range(min=0.0, max=1.0),
    cv.Optional(CONF_MODEL_LOCATION, default=MODEL_LOCATION_FLASH): cv.one_of(
        MODEL_LOCATION_FLASH, MODEL_LOCATION_SDCARD, lower=True
    ),
    cv.Optional(CONF_MODEL_PATH): cv.string,
    cv.Optional(CONF_ON_FACE_DETECTED): automation.validate_automation({
        cv.GenerateID(): cv.declare_id(FaceDetectedTrigger),
    }),
    cv.Optional(CONF_ON_FACE_RECOGNIZED): automation.validate_automation({
        cv.GenerateID(): cv.declare_id(FaceRecognizedTrigger),
    }),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))

    # Set model type build flag for conditional compilation
    model_type = config[CONF_MODEL_TYPE]
    if model_type == MODEL_TYPE_FACE:
        cg.add_build_flag("-DESP_DL_MODEL_FACE_RECOGNITION=1")
    elif model_type == MODEL_TYPE_YOLO11:
        cg.add_build_flag("-DESP_DL_MODEL_YOLO11=1")
    elif model_type == MODEL_TYPE_POSE:
        cg.add_build_flag("-DESP_DL_MODEL_POSE_DETECTION=1")

    if CONF_CANVAS_ID in config:
        cg.add(var.set_canvas_id(config[CONF_CANVAS_ID]))

    cg.add(var.set_score_threshold(config[CONF_SCORE_THRESHOLD]))
    cg.add(var.set_nms_threshold(config[CONF_NMS_THRESHOLD]))
    cg.add(var.set_detection_interval(config[CONF_DETECTION_INTERVAL]))
    cg.add(var.set_draw_enabled(config[CONF_DRAW_ENABLED]))

    if config[CONF_RECOGNITION_ENABLED]:
        cg.add(var.set_recognition_enabled(True))
        cg.add(var.set_face_db_path(config[CONF_FACE_DB_PATH]))
        cg.add(var.set_recognition_threshold(config[CONF_RECOGNITION_THRESHOLD]))

    # Setup automations
    for conf in config.get(CONF_ON_FACE_DETECTED, []):
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(trigger, [(cg.int_, "face_count")], conf)

    for conf in config.get(CONF_ON_FACE_RECOGNIZED, []):
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(trigger, [(cg.int_, "face_id"), (cg.float_, "similarity")], conf)

    # Add build flags for face detection models
    cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MSRMNP_S8_V1=1")
    cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_TYPE=0")
    cg.add_build_flag("-DCONFIG_IDF_TARGET_ESP32P4=1")

    # Model location configuration
    model_location = config.get(CONF_MODEL_LOCATION, MODEL_LOCATION_FLASH)

    if model_location == MODEL_LOCATION_SDCARD:
        # SD card mode
        cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD=1")
        cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=0")
        cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION=2")

        # Pass SD card path to C++ component
        if CONF_MODEL_PATH in config:
            cg.add(var.set_sdcard_model_path(cg.RawExpression(f'"{config[CONF_MODEL_PATH]}"')))
        else:
            # Default SD card path
            cg.add(var.set_sdcard_model_path(cg.RawExpression('"/sdcard"')))
    else:
        # Flash rodata mode (default)
        cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=1")
        cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD=0")
        cg.add_build_flag("-DCONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION=0")

    # Add build flags for face recognition if enabled
    if config[CONF_RECOGNITION_ENABLED]:
        cg.add_build_flag("-DCONFIG_HUMAN_FACE_FEAT_MFN_S8_V1=1")
        cg.add_build_flag("-DCONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_RODATA=1")
        cg.add_build_flag("-DCONFIG_HUMAN_FACE_FEAT_MODEL_TYPE=0")
        cg.add_build_flag("-DCONFIG_HUMAN_FACE_FEAT_MODEL_LOCATION=0")

    # Add include paths
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    # Add human_face_detect include path
    human_face_detect_dir = os.path.join(parent_components_dir, "human_face_detect")
    if os.path.exists(human_face_detect_dir):
        cg.add_build_flag(f"-I{human_face_detect_dir}")

    # Add human_face_recognition include path
    human_face_recognition_dir = os.path.join(parent_components_dir, "human_face_recognition")
    if os.path.exists(human_face_recognition_dir):
        cg.add_build_flag(f"-I{human_face_recognition_dir}")

    # ESP-DL: download via PlatformIO lib_deps
    cg.add_library("esp-dl", None, "https://github.com/espressif/esp-dl.git#v3.2.3")

    # Prevent PlatformIO LDF from auto-compiling esp-dl as a regular library.
    # Our build script manually compiles only the esp-dl sources we need.
    cg.add_platformio_option("lib_ignore", ["esp-dl"])

    # Add ESP-DL include paths (try local and PlatformIO locations)
    esp_dl_include_subdirs = [
        "dl", "dl/tool/include", "dl/tool/isa/esp32p4",
        "dl/tool/src", "dl/tensor/include", "dl/tensor/src",
        "dl/base", "dl/base/isa", "dl/base/isa/esp32p4",
        "dl/math/include", "dl/math/src", "dl/model/include",
        "dl/model/src", "dl/module/include", "dl/module/src",
        "fbs_loader/include", "fbs_loader/lib/esp32p4", "fbs_loader/src",
        "vision/detect", "vision/image", "vision/image/isa",
        "vision/image/isa/esp32p4", "vision/recognition",
        "vision/classification",
    ]

    # Try local components/esp-dl/ first
    esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
    if os.path.exists(esp_dl_dir):
        for subdir in esp_dl_include_subdirs:
            inc_path = os.path.join(esp_dl_dir, subdir)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")

    # Also add PlatformIO libdeps paths (for HA/Docker builds)
    build_path = CORE.build_path
    pioenv = CORE.name
    esp_dl_candidates = [
        os.path.join(str(build_path), ".piolibdeps", pioenv, "esp-dl"),
        os.path.join(str(build_path), ".piolibdeps", pioenv, "esp-dl", "esp-dl"),
    ]
    for esp_dl_base in esp_dl_candidates:
        for subdir in esp_dl_include_subdirs:
            cg.add_build_flag(f"-I{esp_dl_base}/{subdir}")

    # Build script for compiling ESP-DL sources and embedding models
    build_script_path = os.path.join(component_dir, "face_detection_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])


# Action schemas
CONF_NAME = "name"
CONF_FACE_ID = "face_id"

ENROLL_FACE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(FaceDetectionComponent),
})

ENROLL_FACE_WITH_NAME_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(FaceDetectionComponent),
    cv.Required(CONF_NAME): cv.templatable(cv.string),
})

SET_FACE_NAME_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(FaceDetectionComponent),
    cv.Required(CONF_FACE_ID): cv.templatable(cv.int_),
    cv.Required(CONF_NAME): cv.templatable(cv.string),
})

DELETE_FACE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(FaceDetectionComponent),
    cv.Required(CONF_FACE_ID): cv.templatable(cv.int_),
})

CLEAR_ALL_FACES_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(FaceDetectionComponent),
})


@automation.register_action("face_detection.enroll", EnrollFaceAction, ENROLL_FACE_ACTION_SCHEMA, synchronous=True)
async def enroll_face_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("face_detection.enroll_with_name", EnrollFaceWithNameAction, ENROLL_FACE_WITH_NAME_ACTION_SCHEMA, synchronous=True)
async def enroll_face_with_name_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_NAME], args, cg.std_string)
    cg.add(var.set_name(template_))
    return var


@automation.register_action("face_detection.set_name", SetFaceNameAction, SET_FACE_NAME_ACTION_SCHEMA, synchronous=True)
async def set_face_name_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_id = await cg.templatable(config[CONF_FACE_ID], args, cg.int_)
    cg.add(var.set_face_id(template_id))
    template_name = await cg.templatable(config[CONF_NAME], args, cg.std_string)
    cg.add(var.set_name(template_name))
    return var


@automation.register_action("face_detection.delete", DeleteFaceAction, DELETE_FACE_ACTION_SCHEMA, synchronous=True)
async def delete_face_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_FACE_ID], args, cg.int_)
    cg.add(var.set_face_id(template_))
    return var


@automation.register_action("face_detection.clear_all", ClearAllFacesAction, CLEAR_ALL_FACES_ACTION_SCHEMA, synchronous=True)
async def clear_all_faces_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
