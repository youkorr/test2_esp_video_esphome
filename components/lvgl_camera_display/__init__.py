import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["lvgl", "mipi_dsi_cam"]
AUTO_LOAD = ["mipi_dsi_cam"]

CONF_CAMERA_ID = "camera_id"
CONF_CANVAS_ID = "canvas_id"
CONF_UPDATE_INTERVAL = "update_interval"
CONF_FACE_DETECTOR = "face_detector"

lvgl_camera_display_ns = cg.esphome_ns.namespace("lvgl_camera_display")
LVGLCameraDisplay = lvgl_camera_display_ns.class_("LVGLCameraDisplay", cg.Component)

mipi_dsi_cam_ns = cg.esphome_ns.namespace("mipi_dsi_cam")
# Utiliser le nom réel de la classe C++ (MipiDSICamComponent)
MipiDsiCam = mipi_dsi_cam_ns.class_("MipiDSICamComponent", cg.Component)

human_face_detect_ns = cg.esphome_ns.namespace("human_face_detect")
HumanFaceDetect = human_face_detect_ns.class_("HumanFaceDetectComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LVGLCameraDisplay),
    cv.Required(CONF_CAMERA_ID): cv.use_id(MipiDsiCam),
    cv.Required(CONF_CANVAS_ID): cv.string,
    cv.Optional(CONF_UPDATE_INTERVAL, default="33ms"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_FACE_DETECTOR): cv.use_id(HumanFaceDetect),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))

    update_interval_ms = config[CONF_UPDATE_INTERVAL].total_milliseconds
    cg.add(var.set_update_interval(int(update_interval_ms)))

    # Optional face detector
    if CONF_FACE_DETECTOR in config:
        face_detector = await cg.get_variable(config[CONF_FACE_DETECTOR])
        cg.add(var.set_face_detector(face_detector))
