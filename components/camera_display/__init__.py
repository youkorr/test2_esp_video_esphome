"""
camera_display - Waveshare-style camera display for ESPHome

Based on Waveshare ESP32-P4 Camera implementation with DMA architecture:
Camera DMA → Triple Buffer → LVGL Canvas (25-30 FPS expected)

Example YAML:
    camera_display:
      id: my_camera
      sensor_name: sc202cs
      width: 800
      height: 600
      buffer_count: 2
      cpu_core: 0
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import lvgl
from esphome.const import (
    CONF_ID,
    CONF_WIDTH,
    CONF_HEIGHT,
)

# Define namespace
camera_display_ns = cg.esphome_ns.namespace("camera_display")
CameraDisplay = camera_display_ns.class_("CameraDisplay", cg.Component)

# Custom config keys
CONF_SENSOR_NAME = "sensor_name"
CONF_BUFFER_COUNT = "buffer_count"
CONF_CPU_CORE = "cpu_core"
CONF_PIXEL_FORMAT = "pixel_format"
CONF_MIRROR_X = "mirror_x"
CONF_MIRROR_Y = "mirror_y"
CONF_CANVAS_ID = "canvas_id"

# Pixel format validation
PIXEL_FORMATS = ["RGB565", "RGB888", "RAW8", "GRAY"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CameraDisplay),
        cv.Required(CONF_SENSOR_NAME): cv.string,
        cv.Required(CONF_WIDTH): cv.int_range(min=160, max=1920),
        cv.Required(CONF_HEIGHT): cv.int_range(min=120, max=1200),
        cv.Optional(CONF_BUFFER_COUNT, default=2): cv.int_range(min=2, max=6),
        cv.Optional(CONF_CPU_CORE, default=0): cv.int_range(min=0, max=1),
        cv.Optional(CONF_PIXEL_FORMAT, default="RGB565"): cv.one_of(
            *PIXEL_FORMATS, upper=True
        ),
        cv.Optional(CONF_MIRROR_X, default=False): cv.boolean,
        cv.Optional(CONF_MIRROR_Y, default=False): cv.boolean,
        cv.Optional(CONF_CANVAS_ID): cv.use_id(lvgl.LvglComponent),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    """Generate C++ code from YAML config"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set camera configuration
    cg.add(var.set_sensor_name(config[CONF_SENSOR_NAME]))
    cg.add(var.set_width(config[CONF_WIDTH]))
    cg.add(var.set_height(config[CONF_HEIGHT]))
    cg.add(var.set_buffer_count(config[CONF_BUFFER_COUNT]))
    cg.add(var.set_cpu_core(config[CONF_CPU_CORE]))
    cg.add(var.set_pixel_format(config[CONF_PIXEL_FORMAT]))
    cg.add(var.set_mirror_x(config[CONF_MIRROR_X]))
    cg.add(var.set_mirror_y(config[CONF_MIRROR_Y]))

    # Canvas will be set via Lambda or on_load (see examples)
    # We can't set it here because LVGL canvas objects are created at runtime

    # Add dependencies
    cg.add_library("ESP32 Camera", None)  # System library
