"""
LVGL v9.4 Menu Widget Implementation

The menu widget provides hierarchical navigation with:
- Multiple pages/screens
- Header with title and back button
- Main content area
- Sidebar support
- Breadcrumb navigation
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MODE

from ..defines import (
    CONF_BODY,
    CONF_HEADER,
    CONF_MAIN,
    CONF_PAGE,
    CONF_SIDEBAR,
    CONF_TITLE,
    literal,
)
from ..helpers import lvgl_components_required
from ..lv_validation import lv_bool, lv_text, size
from ..lvcode import lv, lv_expr
from ..schemas import container_schema
from ..types import LvType, lv_obj_t
from . import Widget, WidgetType, add_widgets, set_obj_properties
from .obj import obj_spec

CONF_MENU = "menu"
CONF_PAGES = "pages"
CONF_ROOT_BACK_BUTTON = "root_back_button"
CONF_SIDEBAR_PAGE = "sidebar_page"

lv_menu_t = LvType("lv_menu_t")
lv_menu_page_t = LvType("lv_menu_page_t")

# Menu modes
MENU_MODE_ROOT = "ROOT"
MENU_MODE_HEADER = "HEADER"
MENU_MODE_SIDEBAR = "SIDEBAR"

MENU_MODES = {
    MENU_MODE_ROOT: "LV_MENU_ROOT_BACK_BTN_DISABLED",
    MENU_MODE_HEADER: "LV_MENU_HEADER_TOP_FIXED",
    MENU_MODE_SIDEBAR: "LV_MENU_HEADER_TOP_UNFIXED",
}

# Menu page schema
MENU_PAGE_SCHEMA = container_schema(
    obj_spec,
    {
        cv.GenerateID(): cv.declare_id(lv_menu_page_t),
        cv.Optional(CONF_TITLE): lv_text,
    },
)

# Main menu schema
MENU_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PAGES): cv.ensure_list(MENU_PAGE_SCHEMA),
        cv.Optional(CONF_ROOT_BACK_BUTTON, default=False): lv_bool,
        cv.Optional(CONF_MODE, default=MENU_MODE_HEADER): cv.enum(
            MENU_MODES, upper=True
        ),
        cv.Optional(CONF_SIDEBAR_PAGE): cv.use_id(lv_menu_page_t),
    }
)


class MenuType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_MENU,
            lv_menu_t,
            (CONF_MAIN, CONF_HEADER, CONF_SIDEBAR),
            MENU_SCHEMA,
            modify_schema={},
        )

    async def to_code(self, w: Widget, config):
        """Generate C++ code for menu widget configuration"""
        lvgl_components_required.add(CONF_MENU)

        # Create menu pages
        if pages := config.get(CONF_PAGES):
            for page_conf in pages:
                await self._create_page(w, page_conf)

        # Set root back button mode
        if config.get(CONF_ROOT_BACK_BUTTON):
            lv.menu_set_mode_root_back_btn(w.obj, literal("LV_MENU_ROOT_BACK_BTN_ENABLED"))
        else:
            lv.menu_set_mode_root_back_btn(w.obj, literal("LV_MENU_ROOT_BACK_BTN_DISABLED"))

        # Set sidebar page if specified
        if sidebar_page_id := config.get(CONF_SIDEBAR_PAGE):
            sidebar_page = cg.get_variable(sidebar_page_id)
            lv.menu_set_sidebar_page(w.obj, sidebar_page)

    async def _create_page(self, w: Widget, page_conf):
        """Create a menu page with optional title and content"""
        page_id = page_conf[CONF_ID]

        # Create the menu page
        if CONF_TITLE in page_conf:
            title = await lv_text.process(page_conf[CONF_TITLE])
            page_obj = cg.Pvariable(page_id, lv_expr.menu_page_create(w.obj, title))
        else:
            page_obj = cg.Pvariable(page_id, lv_expr.menu_page_create(w.obj, literal("NULL")))

        # Create widget wrapper for the page
        page_widget = Widget.create(page_id, page_obj, obj_spec, page_conf)

        # Set page properties and add child widgets
        await set_obj_properties(page_widget, page_conf)
        await add_widgets(page_widget, page_conf)

    def get_uses(self):
        """Menu widget uses button and label for navigation"""
        return ("btn", "label")


menu_spec = MenuType()
