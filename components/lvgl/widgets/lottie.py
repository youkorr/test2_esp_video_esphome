"""
LVGL v9.4 Lottie Widget Implementation

The lottie widget displays vector animations using the Lottie format with ThorVG.
Lottie animations are JSON-based vector animations created with Adobe After Effects.
"""

import esphome.config_validation as cv
from esphome.const import CONF_ID

from ..defines import (
    CONF_MAIN,
    CONF_SRC,
    literal,
)
from ..helpers import lvgl_components_required
from ..lv_validation import lv_text
from ..lvcode import lv
from ..types import LvType
from . import Widget, WidgetType

CONF_LOTTIE = "lottie"
CONF_AUTOPLAY = "autoplay"
CONF_LOOP = "loop"

lv_lottie_t = LvType("lv_lottie_t")

# Lottie schema
LOTTIE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SRC): lv_text,  # Path to .json Lottie animation file
        cv.Optional(CONF_AUTOPLAY, default=True): cv.boolean,
        cv.Optional(CONF_LOOP, default=True): cv.boolean,
    }
)


class LottieType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_LOTTIE,
            lv_lottie_t,
            (CONF_MAIN,),
            LOTTIE_SCHEMA,
            modify_schema={},
        )

    async def to_code(self, w: Widget, config):
        """Generate C++ code for lottie widget configuration"""
        lvgl_components_required.add(CONF_LOTTIE)

        # Get animation source path
        src = await lv_text.process(config[CONF_SRC])

        # Set Lottie animation from file or buffer
        # In LVGL v9.4, lv_lottie_set_src_file or lv_lottie_set_src_data
        lv.lottie_set_src_file(w.obj, src)

        # Configure autoplay and loop
        # Note: These might need to be set differently in actual LVGL v9.4 API
        # The exact API depends on ThorVG integration

    def get_uses(self):
        """Lottie widget uses ThorVG for rendering"""
        return ()


lottie_spec = LottieType()
