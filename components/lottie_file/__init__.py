"""
Lottie File Component for ESPHome

Embeds Lottie animation JSON files into firmware Flash ROM.
Similar to image: component but for Lottie animations.

Usage:
  lottie_file:
    - id: loading_animation
      file: animations/loading.json
    - id: success_animation
      file: animations/success.json

  lvgl:
    widgets:
      - lottie:
          src: loading_animation  # Reference the embedded animation
"""
from pathlib import Path
import json

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_FILE, CONF_ID, CONF_RAW_DATA_ID
from esphome.core import CORE, HexInt

DOMAIN = "lottie_file"
CODEOWNERS = ["@youkorr"]

lottie_file_ns = cg.esphome_ns.namespace("lottie_file")
LottieFile = lottie_file_ns.class_("LottieFile")

CONFIG_SCHEMA = cv.All(
    cv.ensure_list,
    [
        cv.Schema(
            {
                cv.Required(CONF_ID): cv.declare_id(LottieFile),
                cv.Required(CONF_FILE): cv.file_,
                cv.GenerateID(CONF_RAW_DATA_ID): cv.declare_id(cg.uint8),
            }
        )
    ],
)


def validate_lottie_file(config):
    """Validate that the file is valid JSON"""
    path = Path(config[CONF_FILE])
    if not path.is_file():
        raise cv.Invalid(f"Could not find Lottie file {path}")

    if not path.suffix.lower() == ".json":
        raise cv.Invalid(f"Lottie file must be a .json file, got {path.suffix}")

    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
            # Basic Lottie JSON validation
            if "v" not in data:  # Version field
                raise cv.Invalid(f"File {path} doesn't appear to be a valid Lottie JSON (missing 'v' field)")
    except json.JSONDecodeError as e:
        raise cv.Invalid(f"Could not parse Lottie JSON file {path}: {e}")

    return config


async def to_code(config):
    cg.add_define("USE_LOTTIE_FILE")

    for conf in config:
        conf = validate_lottie_file(conf)
        path = Path(conf[CONF_FILE])

        # Read the JSON file as text
        with open(path, "r", encoding="utf-8") as f:
            json_str = f.read()

        # Remove whitespace to save Flash space (optional - keep formatting for debugging)
        # json_str = json.dumps(json.loads(json_str), separators=(',', ':'))

        # Convert string to byte array
        json_bytes = json_str.encode("utf-8")
        rhs = [HexInt(x) for x in json_bytes]

        # Generate progmem array
        prog_arr = cg.progmem_array(conf[CONF_RAW_DATA_ID], rhs)

        # Create LottieFile object
        var = cg.new_Pvariable(conf[CONF_ID], prog_arr, len(json_bytes))

        # Store metadata for LVGL integration
        if DOMAIN not in CORE.data:
            CORE.data[DOMAIN] = {}
        CORE.data[DOMAIN][conf[CONF_ID].id] = {
            "size": len(json_bytes),
            "path": str(path),
        }
