"""
ESP-Video component for ESPHome (v2.0.0)

Supports two I2C modes:
  - shared:    reuse ESPHome's I2C bus  (i2c_id)
  - dedicated: camera owns its I2C bus  (sda_pin + scl_pin)

The dedicated mode avoids extracting the internal I2C handle from
ESPHome's IDFI2CBus class, which broke with ESPHome 2026.5 / ESP-IDF 6.0.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID, CONF_I2C_ID
from esphome.core import CORE
import os
import logging

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = []

esp_video_ns = cg.esphome_ns.namespace("esp_video")
ESPVideoComponent = esp_video_ns.class_("ESPVideoComponent", cg.Component)

# Configuration keys
CONF_ENABLE_JPEG = "enable_jpeg"
CONF_ENABLE_ISP = "enable_isp"
CONF_USE_HEAP_ALLOCATOR = "use_heap_allocator"
CONF_XCLK_PIN = "xclk_pin"
CONF_XCLK_FREQ = "xclk_freq"
CONF_ENABLE_XCLK_INIT = "enable_xclk_init"

# Dedicated-mode keys
CONF_SDA_PIN = "sda_pin"
CONF_SCL_PIN = "scl_pin"
CONF_I2C_PORT = "i2c_port"
CONF_I2C_FREQ = "i2c_freq"

NO_CLOCK = -1


def parse_gpio_pin(value):
    """Parse a GPIO pin specified as 'GPIO36', 36, -1, or 'NO_CLOCK'."""
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        if value == "-1" or value.upper() == "NO_CLOCK":
            return NO_CLOCK
        if value.upper().startswith("GPIO"):
            try:
                return int(value[4:])
            except ValueError:
                raise cv.Invalid(f"Invalid GPIO format: {value}")
        try:
            return int(value)
        except ValueError:
            raise cv.Invalid(f"Invalid GPIO format: {value}")
    raise cv.Invalid(f"Invalid pin type: {type(value)}")


def validate_esp_video_config(config):
    """Ensure exactly one I2C mode is configured."""
    has_i2c_id = CONF_I2C_ID in config
    has_pins = CONF_SDA_PIN in config and CONF_SCL_PIN in config
    has_any_pin = CONF_SDA_PIN in config or CONF_SCL_PIN in config

    if has_i2c_id and has_pins:
        raise cv.Invalid(
            "Cannot use both 'i2c_id' (shared mode) and 'sda_pin'/'scl_pin' "
            "(dedicated mode) at the same time. Choose one."
        )
    if not has_i2c_id and not has_pins:
        raise cv.Invalid(
            "Either 'i2c_id' (shared mode) or 'sda_pin' + 'scl_pin' "
            "(dedicated mode) must be provided.\n"
            "  Shared:    i2c_id: my_i2c_bus\n"
            "  Dedicated: sda_pin: 8\n"
            "             scl_pin: 9"
        )
    if has_any_pin and not has_pins:
        raise cv.Invalid("Both 'sda_pin' and 'scl_pin' are required in dedicated mode.")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(ESPVideoComponent),

        # ---- Shared-bus mode ----
        cv.Optional(CONF_I2C_ID): cv.use_id(i2c.I2CBus),
        cv.Optional(CONF_I2C_PORT): cv.int_range(min=0, max=3),

        # ---- Dedicated mode ----
        cv.Optional(CONF_SDA_PIN): cv.int_range(min=0, max=48),
        cv.Optional(CONF_SCL_PIN): cv.int_range(min=0, max=48),
        cv.Optional(CONF_I2C_FREQ, default=400000): cv.int_range(
            min=100000, max=1000000
        ),

        # ---- Common ----
        cv.Optional(CONF_ENABLE_JPEG, default=True): cv.boolean,
        cv.Optional(CONF_ENABLE_ISP, default=True): cv.boolean,
        cv.Optional(CONF_USE_HEAP_ALLOCATOR, default=True): cv.boolean,
        cv.Optional(CONF_XCLK_PIN, default="GPIO36"): cv.Any(
            cv.string, cv.int_range(min=-1, max=48)
        ),
        cv.Optional(CONF_XCLK_FREQ, default=24000000): cv.int_range(
            min=1000000, max=40000000
        ),
        cv.Optional(CONF_ENABLE_XCLK_INIT, default=False): cv.boolean,
    }).extend(cv.COMPONENT_SCHEMA),
    validate_esp_video_config,
)


async def to_code(config):
    if not CORE.using_esp_idf:
        raise cv.Invalid(
            "ESP-Video requires the esp-idf framework. "
            "Add 'framework: type: esp-idf' to your configuration."
        )

    # ---- Auto-download dependencies ----
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    from .esp_video_download import ensure_esp_video_dependencies
    try:
        ensure_esp_video_dependencies(parent_components_dir)
    except Exception as e:
        logging.warning(
            f"[ESP-Video] Auto-download failed: {e}\n"
            f"If components are already present locally, this is OK."
        )

    # ---- Component variable ----
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # ---- I2C mode selection ----
    if CONF_I2C_ID in config:
        # Shared mode: reuse ESPHome's I2C bus
        i2c_bus = await cg.get_variable(config[CONF_I2C_ID])
        cg.add(var.set_i2c_bus(i2c_bus))
        if CONF_I2C_PORT in config:
            cg.add(var.set_i2c_port(config[CONF_I2C_PORT]))
        logging.info("[ESP-Video] I2C mode: shared (i2c_id)")
    else:
        # Dedicated mode: camera manages its own I2C
        sda = config[CONF_SDA_PIN]
        scl = config[CONF_SCL_PIN]
        port = config.get(CONF_I2C_PORT, 0)
        freq = config[CONF_I2C_FREQ]

        cg.add(var.set_init_sccb(True))
        cg.add(var.set_sda_pin(cg.RawExpression(f"static_cast<gpio_num_t>({sda})")))
        cg.add(var.set_scl_pin(cg.RawExpression(f"static_cast<gpio_num_t>({scl})")))
        cg.add(var.set_sccb_port(port))
        cg.add(var.set_sccb_freq(freq))
        logging.info(
            f"[ESP-Video] I2C mode: dedicated "
            f"(SDA={sda}, SCL={scl}, port={port}, freq={freq})"
        )

    # ---- XCLK ----
    xclk_pin = parse_gpio_pin(config[CONF_XCLK_PIN])
    cg.add(var.set_xclk_pin(
        cg.RawExpression(f"static_cast<gpio_num_t>({xclk_pin})")
    ))
    cg.add(var.set_xclk_freq(config[CONF_XCLK_FREQ]))
    cg.add(var.set_enable_xclk_init(config[CONF_ENABLE_XCLK_INIT]))

    # ---- Include paths ----
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)
    includes_found = False

    for inc in ["include", "private_include", "src"]:
        inc_path = os.path.join(component_dir, inc)
        if os.path.exists(inc_path):
            cg.add_build_flag(f"-I{inc_path}")
            includes_found = True

    esp_cam_sensor_dir = os.path.join(parent_components_dir, "esp_cam_sensor")
    if os.path.exists(esp_cam_sensor_dir):
        for inc in [
            "include",
            "sensor/ov5647/include",
            "sensor/sc202cs/include",
            "sensor/ov02c10/include",
            "src",
            "src/driver_spi",
            "src/driver_cam",
        ]:
            inc_path = os.path.join(esp_cam_sensor_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")
                includes_found = True

    esp_ipa_dir = os.path.join(parent_components_dir, "esp_ipa")
    if os.path.exists(esp_ipa_dir):
        for inc in ["include", "src"]:
            inc_path = os.path.join(esp_ipa_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")
                includes_found = True

    esp_sccb_intf_dir = os.path.join(parent_components_dir, "esp_sccb_intf")
    if os.path.exists(esp_sccb_intf_dir):
        for inc in ["include", "interface", "sccb_i2c/include"]:
            inc_path = os.path.join(esp_sccb_intf_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")
                includes_found = True

    if not includes_found:
        logging.warning(
            "[ESP-Video] No include directories found! "
            "Check ESP-Video component structure."
        )

    # ---- Build flags ----
    flags = [
        "-DCONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1",
        "-DCONFIG_IDF_TARGET_ESP32P4=1",
        "-DCONFIG_SOC_I2C_SUPPORTED=1",
    ]

    # SC202CS
    flags.extend([
        "-DCONFIG_CAMERA_SC202CS=1",
        "-DCONFIG_CAMERA_SC202CS_AUTO_DETECT=1",
        "-DCONFIG_CAMERA_SC202CS_AUTO_DETECT_MIPI_INTERFACE_SENSOR=1",
        "-DCONFIG_CAMERA_SC202CS_ABSOLUTE_GAIN_LIMIT=63008",
        "-DCONFIG_CAMERA_SC202CS_ANA_GAIN_PRIORITY=0",
        "-DCONFIG_CAMERA_SC202CS_DIG_GAIN_PRIORITY=1",
        "-DCONFIG_CAMERA_SC202CS_MAX_SUPPORT=1",
    ])

    # OV5647
    flags.extend([
        "-DCONFIG_CAMERA_OV5647=1",
        "-DCONFIG_CAMERA_OV5647_AUTO_DETECT=1",
        "-DCONFIG_CAMERA_OV5647_AUTO_DETECT_MIPI_INTERFACE_SENSOR=1",
        "-DCONFIG_CAMERA_OV5647_CSI_LINESYNC_ENABLE=0",
        "-DCONFIG_CAMERA_OV5647_MIPI_IF_FORMAT_INDEX_DEFAULT=0",
        "-DCONFIG_CAMERA_OV5647_DEFAULT_IPA_JSON_CONFIGURATION_FILE=0",
    ])

    # OV02C10
    flags.extend([
        "-DCONFIG_CAMERA_OV02C10=1",
        "-DCONFIG_CAMERA_OV02C10_AUTO_DETECT=1",
        "-DCONFIG_CAMERA_OV02C10_AUTO_DETECT_MIPI_INTERFACE_SENSOR=1",
        "-DCONFIG_CAMERA_OV02C10_ABSOLUTE_GAIN_LIMIT=16000",
        "-DCONFIG_CAMERA_OV02C10_ANA_GAIN_PRIORITY=1",
        "-DCONFIG_CAMERA_OV02C10_DIG_GAIN_PRIORITY=0",
        "-DCONFIG_CAMERA_OV02C10_CSI_LINESYNC_ENABLE=0",
        "-DCONFIG_CAMERA_OV02C10_MIPI_IF_FORMAT_INDEX_DEFAULT=0",
        "-DCONFIG_CAMERA_OV02C10_MAX_SUPPORT=1",
        "-DCONFIG_CAMERA_OV02C10_DEFAULT_IPA_JSON_CONFIGURATION_FILE=1",
    ])

    if config[CONF_ENABLE_ISP]:
        flags.extend([
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1",
            "-DESP_VIDEO_ISP_ENABLED=1",
        ])

    if config[CONF_USE_HEAP_ALLOCATOR]:
        flags.append("-DCONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1")

    if config[CONF_ENABLE_JPEG]:
        flags.extend([
            "-DCONFIG_ESP_VIDEO_ENABLE_JPEG_VIDEO_DEVICE=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE=1",
            "-DESP_VIDEO_JPEG_ENABLED=1",
        ])

    for flag in flags:
        cg.add_build_flag(flag)

    for flag in [
        "-Wno-unused-function",
        "-Wno-unused-variable",
        "-Wno-missing-field-initializers",
    ]:
        cg.add_build_flag(flag)

    # ---- Build script ----
    build_script_path = os.path.join(component_dir, "esp_video_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
    else:
        raise cv.Invalid(f"Build script not found: {build_script_path}")
