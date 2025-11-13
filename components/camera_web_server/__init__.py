import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import mipi_dsi_cam
from esphome.const import CONF_ID, CONF_PORT

DEPENDENCIES = ["mipi_dsi_cam"]
AUTO_LOAD = []
CODEOWNERS = ["@youkorr"]

camera_web_server_ns = cg.esphome_ns.namespace("camera_web_server")
CameraWebServer = camera_web_server_ns.class_("CameraWebServer", cg.Component)

CONF_CAMERA_ID = "camera_id"
CONF_ENABLE_STREAM = "enable_stream"
CONF_ENABLE_SNAPSHOT = "enable_snapshot"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(CameraWebServer),
    cv.Required(CONF_CAMERA_ID): cv.use_id(mipi_dsi_cam.MipiDSICamComponent),
    cv.Optional(CONF_PORT, default=8080): cv.port,
    cv.Optional(CONF_ENABLE_STREAM, default=True): cv.boolean,
    cv.Optional(CONF_ENABLE_SNAPSHOT, default=True): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Obtenir la référence à la caméra
    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))

    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_enable_stream(config[CONF_ENABLE_STREAM]))
    cg.add(var.set_enable_snapshot(config[CONF_ENABLE_SNAPSHOT]))
