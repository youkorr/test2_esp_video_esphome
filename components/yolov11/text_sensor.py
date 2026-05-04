"""
text_sensor sub-platform for the yolov11 component.

YAML usage:

  text_sensor:
    - platform: yolov11
      detection:
        id: my_detection
        name: "current_detection"

The "detection" key publishes a string of the form:

    person:87,car:62,dog:55

(label:score%, comma-separated, max_detections entries).

It updates after every successful inference pass, no need to call any
action manually.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID, CONF_NAME

from . import YOLOv11Component, yolov11_ns

CONF_YOLOV11_ID = "yolov11_id"
CONF_DETECTION = "detection"

YOLOv11TextSensor = yolov11_ns.class_(
    "YOLOv11TextSensor", text_sensor.TextSensor, cg.Component
)

DETECTION_SCHEMA = text_sensor.text_sensor_schema(YOLOv11TextSensor).extend(
    {
        cv.GenerateID(): cv.declare_id(YOLOv11TextSensor),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_YOLOV11_ID): cv.use_id(YOLOv11Component),
        cv.Optional(CONF_DETECTION): DETECTION_SCHEMA,
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_YOLOV11_ID])

    if CONF_DETECTION in config:
        sub = config[CONF_DETECTION]
        var = await text_sensor.new_text_sensor(sub)
        await cg.register_component(var, sub)
        cg.add(parent.add_listener(var))
