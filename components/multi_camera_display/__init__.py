import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["network_camera"]
CODEOWNERS = ["@esphome"]

CONF_CAMERAS = "cameras"
CONF_CANVAS_ID = "canvas_id"
CONF_CAMERA_ID = "camera_id"

multi_camera_display_ns = cg.esphome_ns.namespace("multi_camera_display")
MultiCameraDisplay = multi_camera_display_ns.class_("MultiCameraDisplay", cg.Component)

# Import network_camera namespace directly
network_camera_ns = cg.esphome_ns.namespace("network_camera")
NetworkCamera = network_camera_ns.class_("NetworkCamera", cg.Component)

CAMERA_SCHEMA = cv.Schema({
    cv.Required(CONF_CAMERA_ID): cv.use_id(NetworkCamera),
})

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MultiCameraDisplay),
    cv.Required(CONF_CANVAS_ID): cv.string,
    cv.Required(CONF_CAMERAS): cv.ensure_list(CAMERA_SCHEMA),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Add cameras
    for camera_conf in config[CONF_CAMERAS]:
        camera = await cg.get_variable(camera_conf[CONF_CAMERA_ID])
        cg.add(var.add_camera(camera))
