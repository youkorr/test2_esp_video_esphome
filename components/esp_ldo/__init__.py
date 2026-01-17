import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@youkorr"]  # ESP LDO component for ESP32

esp_ldo_ns = cg.esphome_ns.namespace("esp_ldo")
EspLdo = esp_ldo_ns.class_("EspLdo", cg.Component)

CONF_CHANNEL_ID = "channel_id"
CONF_VOLTAGE = "voltage"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EspLdo),
        cv.Required(CONF_CHANNEL_ID): cv.int_range(min=0, max=7),
        cv.Required(CONF_VOLTAGE): cv.All(
            cv.voltage, cv.int_range(min=500, max=3300)
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_channel_id(config[CONF_CHANNEL_ID]))
    # Convert voltage to millivolts
    voltage_mv = int(config[CONF_VOLTAGE] * 1000)
    cg.add(var.set_voltage_mv(voltage_mv))
