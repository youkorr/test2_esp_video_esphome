import esphome.config_validation as cv
from esphome.const import CONF_ITEMS, CONF_TEXT, CONF_TYPE

from ..defines import (
    CONF_MAIN,
    CONF_SCROLLBAR,
    CONF_SRC,
    literal,
)
from ..helpers import lvgl_components_required
from ..lv_validation import lv_image, lv_text
from ..lvcode import lv
from ..types import LvCompound
from . import Widget, WidgetType

CONF_LIST = "list"
CONF_LIST_BUTTON = "list_button"
CONF_LIST_TEXT = "list_text"

lv_list_t = LvCompound("lv_list_t")

# Item types
ITEM_TYPE_BUTTON = "button"
ITEM_TYPE_TEXT = "text"

# Schema for list items
LIST_ITEM_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TYPE): cv.one_of(ITEM_TYPE_BUTTON, ITEM_TYPE_TEXT, lower=True),
        cv.Required(CONF_TEXT): lv_text,
        cv.Optional(CONF_SRC): lv_image,  # Optional icon for buttons
    }
)

LIST_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ITEMS): cv.ensure_list(LIST_ITEM_SCHEMA),
    }
)


class ListType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_LIST,
            lv_list_t,
            (CONF_MAIN, CONF_SCROLLBAR),
            LIST_SCHEMA,
            modify_schema=LIST_SCHEMA,
        )

    async def to_code(self, w: Widget, config):
        lvgl_components_required.add(CONF_LIST)

        # Add list items
        if items := config.get(CONF_ITEMS):
            for item in items:
                item_type = item[CONF_TYPE]
                text_value = await lv_text.process(item[CONF_TEXT])

                if item_type == ITEM_TYPE_BUTTON:
                    # Check if icon is provided
                    if CONF_SRC in item:
                        icon_value = await lv_image.process(item[CONF_SRC])
                        # Add button with icon
                        lv.list_add_button(w.obj, icon_value, text_value)
                    else:
                        # Add button without icon (NULL for icon parameter)
                        lv.list_add_button(w.obj, literal("NULL"), text_value)
                else:  # ITEM_TYPE_TEXT
                    # Add text item
                    lv.list_add_text(w.obj, text_value)

    def get_uses(self):
        return ("btn", "label")


list_spec = ListType()
