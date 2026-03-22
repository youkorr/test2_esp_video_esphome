import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID, CONF_NAME

from .. import yolov11_ns, YOLOV11Component

CONF_YOLOV11_ID = "yolov11_id"
CONF_DETECTION = "detection"

DEPENDENCIES = ["yolov11"]

yolov11_text_sensor_ns = yolov11_ns
YOLOV11TextSensor = yolov11_text_sensor_ns.class_(
    "YOLOV11TextSensor", text_sensor.TextSensor, cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_DETECTION): text_sensor.text_sensor_schema(
            YOLOV11TextSensor
        ).extend(
            {
                cv.GenerateID(CONF_YOLOV11_ID): cv.use_id(YOLOV11Component),
            }
        ),
    }
)


async def to_code(config):
    if CONF_DETECTION in config:
        conf = config[CONF_DETECTION]
        var = await text_sensor.new_text_sensor(conf)
        await cg.register_component(var, conf)

        parent = await cg.get_variable(conf[CONF_YOLOV11_ID])
        cg.add(var.set_yolov11(parent))
