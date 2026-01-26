"""
LVGL v9.4 Lottie Widget Implementation

The lottie widget displays vector animations using the Lottie format with ThorVG.
Lottie animations are JSON-based vector animations created with Adobe After Effects.

Based on LVGL 9.4 official API:
https://docs.lvgl.io/master/details/widgets/lottie.html
"""

import esphome.config_validation as cv
from esphome.const import CONF_ID

from esphome.cpp_generator import RawExpression

from ..defines import (
    CONF_MAIN,
    CONF_SRC,
    literal,
)
from ..helpers import lvgl_components_required
from ..lv_validation import lv_int, lv_text
from ..lvcode import lv, LocalVariable
from ..types import LvType
from . import Widget, WidgetType

CONF_LOTTIE = "lottie"
CONF_AUTOPLAY = "autoplay"
CONF_LOOP = "loop"
CONF_BUFFER_WIDTH = "buffer_width"
CONF_BUFFER_HEIGHT = "buffer_height"

lv_lottie_t = LvType("lv_lottie_t")

# Lottie schema
LOTTIE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SRC): lv_text,  # Path to .json Lottie animation file
        cv.Optional(CONF_AUTOPLAY, default=True): cv.boolean,
        cv.Optional(CONF_LOOP, default=True): cv.boolean,
        cv.Optional(CONF_BUFFER_WIDTH): lv_int,  # Buffer width (default: widget width)
        cv.Optional(CONF_BUFFER_HEIGHT): lv_int,  # Buffer height (default: widget height)
    }
)

LOTTIE_MODIFY_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_SRC): lv_text,
    }
)


class LottieType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_LOTTIE,
            lv_lottie_t,
            (CONF_MAIN,),
            LOTTIE_SCHEMA,
            modify_schema=LOTTIE_MODIFY_SCHEMA,
        )

    async def to_code(self, w: Widget, config):
        """
        Generate C++ code for lottie widget configuration

        LVGL 9.4 Lottie API:
        - lv_lottie_set_src_file(obj, path) - Load animation from file
        - lv_lottie_set_buffer(obj, w, h, buf) - Set render buffer
        - lv_lottie_get_anim(obj) - Get LVGL animation object for control

        By default, Lottie animations run infinitely at 60FPS.
        Use lv_lottie_get_animation() to customize playback.
        """
        lvgl_components_required.add(CONF_LOTTIE)

        # Get animation source path
        src = await lv_text.process(config[CONF_SRC])

        # LVGL 9.4: Load Lottie animation from file
        lv.lottie_set_src_file(w.obj, src)

        # Control playback via LVGL animation object
        autoplay = config.get(CONF_AUTOPLAY, True)
        loop = config.get(CONF_LOOP, True)

        # Get the animation object to control playback
        # lv_anim_t * anim = lv_lottie_get_anim(obj)
        if not autoplay or not loop:
            # Create local variable for animation object
            with LocalVariable("lottie_anim", "lv_anim_t *",
                             lv.lottie_get_anim(w.obj)) as anim_obj:
                if not autoplay:
                    # Pause the animation (will need manual start)
                    lv.anim_del(anim_obj, literal("NULL"))
                elif not loop:
                    # Set animation to play once (not infinite loop)
                    # By default LVGL animations repeat infinitely
                    # Setting repeat count to 1 means play once
                    lv.anim_set_repeat_count(anim_obj, 1)

    def get_uses(self):
        """Lottie widget requires ThorVG for rendering"""
        return ()


lottie_spec = LottieType()
