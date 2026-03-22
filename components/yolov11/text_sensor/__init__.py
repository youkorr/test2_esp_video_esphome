import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID, CONF_NAME

from .. import yolov11_ns, YOLOV11Component

CONF_YOLOV11_ID = "yolov11_id"
CONF_DETECTION_ID = "detection_id"
CONF_DETECTION_BB = "detection_bb"

DEPENDENCIES = ["yolov11"]

YOLOV11DetectionClassSensor = yolov11_ns.class_(
    "YOLOV11DetectionClassSensor", text_sensor.TextSensor, cg.Component
)
YOLOV11DetectionBBSensor = yolov11_ns.class_(
    "YOLOV11DetectionBBSensor", text_sensor.TextSensor, cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_YOLOV11_ID): cv.use_id(YOLOV11Component),
        cv.Optional(CONF_DETECTION_ID): text_sensor.text_sensor_schema(
            YOLOV11DetectionClassSensor
        ),
        cv.Optional(CONF_DETECTION_BB): text_sensor.text_sensor_schema(
            YOLOV11DetectionBBSensor
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_YOLOV11_ID])

    if CONF_DETECTION_ID in config:
        conf = config[CONF_DETECTION_ID]
        var = await text_sensor.new_text_sensor(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_yolov11(parent))

    if CONF_DETECTION_BB in config:
        conf = config[CONF_DETECTION_BB]
        var = await text_sensor.new_text_sensor(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_yolov11(parent))
