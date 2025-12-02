import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID
from . import NetworkCamera, network_camera_ns

DEPENDENCIES = ["network_camera"]

NetworkCameraSwitch = network_camera_ns.class_(
    "NetworkCameraSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = switch.switch_schema(NetworkCameraSwitch).extend(
    {
        cv.GenerateID(): cv.declare_id(NetworkCameraSwitch),
        cv.Required("camera_id"): cv.use_id(NetworkCamera),
    }
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)

    camera = await cg.get_variable(config["camera_id"])
    cg.add(var.set_camera(camera))
