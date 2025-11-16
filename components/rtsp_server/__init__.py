import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import mipi_dsi_cam
from esphome.const import CONF_ID, CONF_PORT

DEPENDENCIES = ["mipi_dsi_cam", "network"]
AUTO_LOAD = []
CODEOWNERS = ["@youkorr"]

rtsp_server_ns = cg.esphome_ns.namespace("rtsp_server")
RTSPServer = rtsp_server_ns.class_("RTSPServer", cg.Component)

CONF_CAMERA_ID = "camera_id"
CONF_STREAM_PATH = "stream_path"
CONF_RTP_PORT = "rtp_port"
CONF_RTCP_PORT = "rtcp_port"
CONF_BITRATE = "bitrate"
CONF_GOP = "gop"
CONF_QP_MIN = "qp_min"
CONF_QP_MAX = "qp_max"
CONF_MAX_CLIENTS = "max_clients"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(RTSPServer),
    cv.Required(CONF_CAMERA_ID): cv.use_id(mipi_dsi_cam.MipiDSICamComponent),
    cv.Optional(CONF_PORT, default=554): cv.port,
    cv.Optional(CONF_STREAM_PATH, default="/stream"): cv.string,
    cv.Optional(CONF_RTP_PORT, default=5004): cv.port,
    cv.Optional(CONF_RTCP_PORT, default=5005): cv.port,
    cv.Optional(CONF_BITRATE, default=2000000): cv.int_range(min=100000, max=10000000),
    cv.Optional(CONF_GOP, default=30): cv.int_range(min=1, max=120),
    cv.Optional(CONF_QP_MIN, default=10): cv.int_range(min=0, max=51),
    cv.Optional(CONF_QP_MAX, default=40): cv.int_range(min=0, max=51),
    cv.Optional(CONF_MAX_CLIENTS, default=3): cv.int_range(min=1, max=5),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Get camera reference
    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))

    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_stream_path(config[CONF_STREAM_PATH]))
    cg.add(var.set_rtp_port(config[CONF_RTP_PORT]))
    cg.add(var.set_rtcp_port(config[CONF_RTCP_PORT]))
    cg.add(var.set_bitrate(config[CONF_BITRATE]))
    cg.add(var.set_gop(config[CONF_GOP]))
    cg.add(var.set_qp_min(config[CONF_QP_MIN]))
    cg.add(var.set_qp_max(config[CONF_QP_MAX]))
    cg.add(var.set_max_clients(config[CONF_MAX_CLIENTS]))
