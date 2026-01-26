"""
SVG File Component for ESPHome

Embeds SVG vector graphics files into firmware Flash ROM.
Similar to image: component but for SVG images.

Usage:
  svg_file:
    - id: wifi_icon
      file: icons/wifi.svg
    - id: battery_icon
      file: icons/battery.svg

  lvgl:
    widgets:
      - image:
          src: wifi_icon  # Reference the embedded SVG
"""
from pathlib import Path
import xml.etree.ElementTree as ET

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_FILE, CONF_ID, CONF_RAW_DATA_ID
from esphome.core import CORE, HexInt

DOMAIN = "svg_file"
CODEOWNERS = ["@youkorr"]

svg_file_ns = cg.esphome_ns.namespace("svg_file")
SvgFile = svg_file_ns.class_("SvgFile")

CONFIG_SCHEMA = cv.All(
    cv.ensure_list,
    [
        cv.Schema(
            {
                cv.Required(CONF_ID): cv.declare_id(SvgFile),
                cv.Required(CONF_FILE): cv.file_,
                cv.GenerateID(CONF_RAW_DATA_ID): cv.declare_id(cg.uint8),
            }
        )
    ],
)


def validate_svg_file(config):
    """Validate that the file is valid SVG XML"""
    path = Path(config[CONF_FILE])
    if not path.is_file():
        raise cv.Invalid(f"Could not find SVG file {path}")

    if not path.suffix.lower() in [".svg", ".svgz"]:
        raise cv.Invalid(f"SVG file must be a .svg or .svgz file, got {path.suffix}")

    try:
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
            # Basic SVG XML validation
            if "<svg" not in content.lower():
                raise cv.Invalid(f"File {path} doesn't appear to be a valid SVG (missing <svg> tag)")
            # Try to parse XML
            ET.fromstring(content)
    except ET.ParseError as e:
        raise cv.Invalid(f"Could not parse SVG XML file {path}: {e}")
    except UnicodeDecodeError:
        # Might be compressed .svgz - allow it
        pass

    return config


async def to_code(config):
    cg.add_define("USE_SVG_FILE")

    for conf in config:
        conf = validate_svg_file(conf)
        path = Path(conf[CONF_FILE])

        # Read the SVG file as text
        with open(path, "r", encoding="utf-8") as f:
            svg_str = f.read()

        # Convert string to byte array
        svg_bytes = svg_str.encode("utf-8")
        rhs = [HexInt(x) for x in svg_bytes]

        # Generate progmem array
        prog_arr = cg.progmem_array(conf[CONF_RAW_DATA_ID], rhs)

        # Create SvgFile object
        var = cg.new_Pvariable(conf[CONF_ID], prog_arr, len(svg_bytes))

        # Store metadata for LVGL integration
        if DOMAIN not in CORE.data:
            CORE.data[DOMAIN] = {}
        CORE.data[DOMAIN][conf[CONF_ID].id] = {
            "size": len(svg_bytes),
            "path": str(path),
        }
