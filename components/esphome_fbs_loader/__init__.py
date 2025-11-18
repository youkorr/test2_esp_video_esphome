"""ESPHome FBS Loader Component.

This component provides integration for ESP-DL FlatBuffers model loader.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import pins

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = []

# Component namespace
esphome_fbs_loader_ns = cg.esphome_ns.namespace("esphome_fbs_loader")
ESPHomeFbsLoader = esphome_fbs_loader_ns.class_("ESPHomeFbsLoader", cg.Component)

# Model storage location enum
ModelLocation = esphome_fbs_loader_ns.enum("ModelLocation")
MODEL_LOCATIONS = {
    "flash_rodata": ModelLocation.FLASH_RODATA,
    "flash_partition": ModelLocation.FLASH_PARTITION,
    "sdcard": ModelLocation.SDCARD,
}

# Configuration keys
CONF_MODEL_PATH = "model_path"
CONF_MODEL_LOCATION = "model_location"
CONF_MODEL_NAME = "model_name"
CONF_MODEL_INDEX = "model_index"
CONF_ENCRYPTION_KEY = "encryption_key"
CONF_PARAM_COPY = "param_copy"

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ESPHomeFbsLoader),
    cv.Optional(CONF_MODEL_PATH): cv.string,
    cv.Optional(CONF_MODEL_LOCATION, default="flash_partition"): cv.enum(
        MODEL_LOCATIONS, lower=True
    ),
    cv.Optional(CONF_MODEL_NAME): cv.string,
    cv.Optional(CONF_MODEL_INDEX): cv.int_range(min=0),
    cv.Optional(CONF_ENCRYPTION_KEY): cv.All(
        cv.ensure_list(cv.hex_uint8_t), cv.Length(min=16, max=16)
    ),
    cv.Optional(CONF_PARAM_COPY, default=True): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    """Generate C++ code from YAML configuration."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set model path if provided
    if CONF_MODEL_PATH in config:
        cg.add(var.set_model_path(config[CONF_MODEL_PATH]))

    # Set model location
    cg.add(var.set_model_location(config[CONF_MODEL_LOCATION]))

    # Set model name if provided
    if CONF_MODEL_NAME in config:
        cg.add(var.set_model_name(config[CONF_MODEL_NAME]))

    # Set model index if provided
    if CONF_MODEL_INDEX in config:
        cg.add(var.set_model_index(config[CONF_MODEL_INDEX]))

    # Set encryption key if provided
    if CONF_ENCRYPTION_KEY in config:
        cg.add(var.set_encryption_key(config[CONF_ENCRYPTION_KEY]))

    # Set param_copy option
    cg.add(var.set_param_copy(config[CONF_PARAM_COPY]))

    # Add required build flags
    cg.add_build_flag("-DCONFIG_ESP_DL_FBS_LOADER_ENABLED=1")

    # Add include directories
    cg.add_library("esp-dl", None, "components/esp-dl")
    cg.add_library("esp-dl/fbs_loader", None, "components/esp-dl/fbs_loader")
