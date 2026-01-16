"""
LVGL v9.4 Window Widget Implementation

The window widget provides modal window functionality with:
- Title bar/header
- Content area
- Header buttons (close, minimize, etc.)
- Draggable window
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

from ..defines import (
    CONF_BODY,
    CONF_HEADER,
    CONF_MAIN,
    CONF_SRC,
    CONF_TITLE,
    literal,
)
from ..helpers import lvgl_components_required
from ..lv_validation import lv_image, lv_int, lv_text
from ..lvcode import lv, lv_expr
from ..schemas import container_schema
from ..types import LvType, lv_obj_t
from . import Widget, WidgetType, add_widgets, set_obj_properties
from .button import lv_button_t
from .obj import obj_spec

CONF_WIN = "win"
CONF_HEADER_HEIGHT = "header_height"
CONF_HEADER_BUTTONS = "header_buttons"

lv_win_t = LvType("lv_win_t")

# Header button schema
HEADER_BUTTON_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(lv_button_t),
        cv.Optional(CONF_SRC): lv_image,
    }
)

# Window schema
WIN_SCHEMA = container_schema(
    obj_spec,
    {
        cv.Required(CONF_TITLE): lv_text,
        cv.Optional(CONF_HEADER_HEIGHT, default=40): lv_int,
        cv.Optional(CONF_HEADER_BUTTONS): cv.ensure_list(HEADER_BUTTON_SCHEMA),
    },
)


class WindowType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_WIN,
            lv_win_t,
            (CONF_MAIN, CONF_HEADER, CONF_BODY),
            WIN_SCHEMA,
            modify_schema={},
        )

    async def to_code(self, w: Widget, config):
        """Generate C++ code for window widget configuration"""
        lvgl_components_required.add(CONF_WIN)

        # Get window title
        title = await lv_text.process(config[CONF_TITLE])

        # Get header height
        header_height = await lv_int.process(config[CONF_HEADER_HEIGHT])

        # Create window with title and header height
        # Note: In LVGL v9.4, lv_win_create takes parent, title, and header_height
        lv.win_add_title(w.obj, title)

        # Add header buttons if specified
        if header_buttons := config.get(CONF_HEADER_BUTTONS):
            for button_conf in header_buttons:
                await self._add_header_button(w, button_conf)

        # Get content area for adding widgets
        # In LVGL v9.4, content area is accessed via lv_win_get_content()
        # Child widgets will be added to the content area automatically

    async def _add_header_button(self, w: Widget, button_conf):
        """Add a button to the window header"""
        button_id = button_conf.get(CONF_ID)

        if CONF_SRC in button_conf:
            # Button with icon
            icon = await lv_image.process(button_conf[CONF_SRC])
            if button_id:
                button_obj = cg.Pvariable(
                    button_id, lv_expr.win_add_button(w.obj, icon, literal("40"))
                )
            else:
                lv.win_add_button(w.obj, icon, literal("40"))
        else:
            # Button without icon
            if button_id:
                button_obj = cg.Pvariable(
                    button_id, lv_expr.win_add_button(w.obj, literal("NULL"), literal("40"))
                )
            else:
                lv.win_add_button(w.obj, literal("NULL"), literal("40"))

    def get_uses(self):
        """Window widget uses button and label"""
        return ("btn", "label")


win_spec = WindowType()
