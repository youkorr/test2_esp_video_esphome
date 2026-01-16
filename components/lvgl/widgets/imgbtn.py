"""
LVGL v9.4 Image Button Widget Implementation

The image button widget is a button that displays images for different states
(released, pressed, disabled, checked).
"""

import esphome.config_validation as cv
from esphome.const import CONF_STATE

from ..defines import (
    CONF_MAIN,
    CONF_SRC,
)
from ..helpers import lvgl_components_required
from ..lv_validation import lv_image
from ..lvcode import lv, literal
from ..types import LvType
from . import Widget, WidgetType

CONF_IMGBTN = "imgbtn"
CONF_SRC_RELEASED = "src_released"
CONF_SRC_PRESSED = "src_pressed"
CONF_SRC_DISABLED = "src_disabled"
CONF_SRC_CHECKED_RELEASED = "src_checked_released"
CONF_SRC_CHECKED_PRESSED = "src_checked_pressed"
CONF_SRC_CHECKED_DISABLED = "src_checked_disabled"

lv_imgbtn_t = LvType("lv_imgbtn_t")

# Image button states
IMGBTN_STATE_RELEASED = "LV_IMGBTN_STATE_RELEASED"
IMGBTN_STATE_PRESSED = "LV_IMGBTN_STATE_PRESSED"
IMGBTN_STATE_DISABLED = "LV_IMGBTN_STATE_DISABLED"
IMGBTN_STATE_CHECKED_RELEASED = "LV_IMGBTN_STATE_CHECKED_RELEASED"
IMGBTN_STATE_CHECKED_PRESSED = "LV_IMGBTN_STATE_CHECKED_PRESSED"
IMGBTN_STATE_CHECKED_DISABLED = "LV_IMGBTN_STATE_CHECKED_DISABLED"

# Image button schema
IMGBTN_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_SRC_RELEASED): lv_image,
        cv.Optional(CONF_SRC_PRESSED): lv_image,
        cv.Optional(CONF_SRC_DISABLED): lv_image,
        cv.Optional(CONF_SRC_CHECKED_RELEASED): lv_image,
        cv.Optional(CONF_SRC_CHECKED_PRESSED): lv_image,
        cv.Optional(CONF_SRC_CHECKED_DISABLED): lv_image,
    }
)


class ImageButtonType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_IMGBTN,
            lv_imgbtn_t,
            (CONF_MAIN,),
            IMGBTN_SCHEMA,
            modify_schema={},
        )

    async def to_code(self, w: Widget, config):
        """Generate C++ code for image button widget configuration"""
        lvgl_components_required.add(CONF_IMGBTN)

        # Set images for different states
        state_configs = [
            (CONF_SRC_RELEASED, IMGBTN_STATE_RELEASED),
            (CONF_SRC_PRESSED, IMGBTN_STATE_PRESSED),
            (CONF_SRC_DISABLED, IMGBTN_STATE_DISABLED),
            (CONF_SRC_CHECKED_RELEASED, IMGBTN_STATE_CHECKED_RELEASED),
            (CONF_SRC_CHECKED_PRESSED, IMGBTN_STATE_CHECKED_PRESSED),
            (CONF_SRC_CHECKED_DISABLED, IMGBTN_STATE_CHECKED_DISABLED),
        ]

        for conf_key, state_const in state_configs:
            if conf_key in config:
                img = await lv_image.process(config[conf_key])
                # Set image for specific state
                lv.imgbtn_set_src(w.obj, literal(state_const), img, literal("NULL"), literal("NULL"))

    def get_uses(self):
        """Image button uses image component"""
        return ("img",)


imgbtn_spec = ImageButtonType()
