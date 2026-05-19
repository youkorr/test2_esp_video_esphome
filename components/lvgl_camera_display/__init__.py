import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["lvgl", "esp_cam_sensor"]
AUTO_LOAD = ["esp_cam_sensor"]

CONF_CAMERA_ID = "camera_id"
CONF_CANVAS_ID = "canvas_id"
CONF_UPDATE_INTERVAL = "update_interval"
CONF_FACE_DETECTION_ID = "face_detection_id"
CONF_YOLO11_DETECTION_ID = "yolo11_detection_id"
CONF_PEDESTRIAN_DETECTION_ID = "pedestrian_detection_id"

lvgl_camera_display_ns = cg.esphome_ns.namespace("lvgl_camera_display")
LVGLCameraDisplay = lvgl_camera_display_ns.class_("LVGLCameraDisplay", cg.Component)

esp_cam_sensor_ns = cg.esphome_ns.namespace("esp_cam_sensor")
EspCamSensor = esp_cam_sensor_ns.class_("MipiDSICamComponent", cg.Component)

face_detection_ns = cg.esphome_ns.namespace("face_detection")
FaceDetectionComponent = face_detection_ns.class_("FaceDetectionComponent", cg.Component)

yolo11_detection_ns = cg.esphome_ns.namespace("yolo11_detection")
YOLO11DetectionComponent = yolo11_detection_ns.class_("YOLO11DetectionComponent", cg.Component)

pedestrian_detection_ns = cg.esphome_ns.namespace("pedestrian_detection")
PedestrianDetectionComponent = pedestrian_detection_ns.class_("PedestrianDetectionComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LVGLCameraDisplay),
    cv.Required(CONF_CAMERA_ID): cv.use_id(EspCamSensor),
    cv.Required(CONF_CANVAS_ID): cv.string,
    cv.Optional(CONF_UPDATE_INTERVAL, default="33ms"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_FACE_DETECTION_ID): cv.use_id(FaceDetectionComponent),
    cv.Optional(CONF_YOLO11_DETECTION_ID): cv.use_id(YOLO11DetectionComponent),
    cv.Optional(CONF_PEDESTRIAN_DETECTION_ID): cv.use_id(PedestrianDetectionComponent),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))

    cg.add(var.set_canvas_id(config[CONF_CANVAS_ID]))

    update_interval_ms = config[CONF_UPDATE_INTERVAL].total_milliseconds
    cg.add(var.set_update_interval(int(update_interval_ms)))

    if CONF_FACE_DETECTION_ID in config:
        cg.add_define("USE_FACE_DETECTION")
        face_detect = await cg.get_variable(config[CONF_FACE_DETECTION_ID])
        cg.add(var.set_face_detection(face_detect))

    if CONF_YOLO11_DETECTION_ID in config:
        cg.add_define("USE_YOLO11_DETECTION")
        yolo11_detect = await cg.get_variable(config[CONF_YOLO11_DETECTION_ID])
        cg.add(var.set_yolo11_detection(yolo11_detect))

    if CONF_PEDESTRIAN_DETECTION_ID in config:
        cg.add_define("USE_PEDESTRIAN_DETECTION")
        ped_detect = await cg.get_variable(config[CONF_PEDESTRIAN_DETECTION_ID])
        cg.add(var.set_pedestrian_detection(ped_detect))
