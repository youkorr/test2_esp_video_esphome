import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import (
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from . import UsbMediaStorage, CONF_USB_MEDIA_STORAGE_ID

DEPENDENCIES = ["usb_media_storage"]

CONF_USB_STATUS = "usb_status"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_USB_MEDIA_STORAGE_ID): cv.use_id(UsbMediaStorage),
    cv.Optional(CONF_USB_STATUS): text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
}

async def to_code(config):
    usb_component = await cg.get_variable(config[CONF_USB_MEDIA_STORAGE_ID])

    if CONF_USB_STATUS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_USB_STATUS])
        cg.add(usb_component.set_usb_status_text_sensor(sens))
