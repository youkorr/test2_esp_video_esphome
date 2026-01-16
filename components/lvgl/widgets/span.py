"""
LVGL v9.4 Spangroup Widget Implementation

The spangroup widget displays text with multiple styles (spans) within the same label.
Each span can have different font, color, and decoration.
"""

import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MODE, CONF_TEXT

from ..defines import (
    CONF_MAIN,
    literal,
)
from ..helpers import lvgl_components_required
from ..lv_validation import lv_color, lv_text
from ..lvcode import lv, lv_expr
from ..types import LvType
from . import Widget, WidgetType

CONF_SPANGROUP = "spangroup"
CONF_SPANS = "spans"
CONF_TEXT_COLOR = "text_color"
CONF_TEXT_FONT = "text_font"
CONF_TEXT_DECOR = "text_decor"

lv_spangroup_t = LvType("lv_spangroup_t")
lv_span_t = LvType("lv_span_t")

# Span modes
SPAN_MODE_FIXED = "FIXED"
SPAN_MODE_EXPAND = "EXPAND"
SPAN_MODE_BREAK = "BREAK"

SPAN_MODES = {
    SPAN_MODE_FIXED: "LV_SPAN_MODE_FIXED",
    SPAN_MODE_EXPAND: "LV_SPAN_MODE_EXPAND",
    SPAN_MODE_BREAK: "LV_SPAN_MODE_BREAK",
}

# Text decorations
TEXT_DECOR_NONE = "NONE"
TEXT_DECOR_UNDERLINE = "UNDERLINE"
TEXT_DECOR_STRIKETHROUGH = "STRIKETHROUGH"

TEXT_DECORS = {
    TEXT_DECOR_NONE: "LV_TEXT_DECOR_NONE",
    TEXT_DECOR_UNDERLINE: "LV_TEXT_DECOR_UNDERLINE",
    TEXT_DECOR_STRIKETHROUGH: "LV_TEXT_DECOR_STRIKETHROUGH",
}

# Span schema
SPAN_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TEXT): lv_text,
        cv.Optional(CONF_TEXT_COLOR): lv_color,
        cv.Optional(CONF_TEXT_FONT): cv.string,
        cv.Optional(CONF_TEXT_DECOR): cv.enum(TEXT_DECORS, upper=True),
    }
)

# Spangroup schema
SPANGROUP_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MODE, default=SPAN_MODE_BREAK): cv.enum(SPAN_MODES, upper=True),
        cv.Optional(CONF_SPANS): cv.ensure_list(SPAN_SCHEMA),
    }
)


class SpangroupType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_SPANGROUP,
            lv_spangroup_t,
            (CONF_MAIN,),
            SPANGROUP_SCHEMA,
            modify_schema={},
        )

    async def to_code(self, w: Widget, config):
        """Generate C++ code for spangroup widget configuration"""
        lvgl_components_required.add(CONF_SPANGROUP)

        # Set span mode
        mode = SPAN_MODES[config[CONF_MODE]]
        lv.spangroup_set_mode(w.obj, literal(mode))

        # Add spans
        if spans := config.get(CONF_SPANS):
            for span_conf in spans:
                await self._add_span(w, span_conf)

    async def _add_span(self, w: Widget, span_conf):
        """Add a text span with styling"""
        # Create new span
        span_var = lv_expr.spangroup_new_span(w.obj)

        # Set span text
        text = await lv_text.process(span_conf[CONF_TEXT])
        lv.span_set_text(span_var, text)

        # Set span color if specified
        if CONF_TEXT_COLOR in span_conf:
            color = await lv_color.process(span_conf[CONF_TEXT_COLOR])
            lv.style_set_text_color(lv_expr.span_get_style(span_var), color)

        # Set span font if specified
        if CONF_TEXT_FONT in span_conf:
            # Font handling would need proper font reference
            pass

        # Set text decoration if specified
        if CONF_TEXT_DECOR in span_conf:
            decor = TEXT_DECORS[span_conf[CONF_TEXT_DECOR]]
            lv.style_set_text_decor(lv_expr.span_get_style(span_var), literal(decor))

    def get_uses(self):
        """Spangroup uses label component"""
        return ("label",)


spangroup_spec = SpangroupType()
