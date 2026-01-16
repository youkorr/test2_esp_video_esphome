from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_DATE, CONF_ID, CONF_YEAR

from ..automation import action_to_code
from ..defines import CONF_ITEMS, CONF_MAIN, literal
from ..helpers import lvgl_components_required
from ..lv_validation import lv_int
from ..lvcode import lv, lv_add
from ..types import LvCompound, LvType, ObjUpdateAction
from . import Widget, WidgetType, get_widgets

CONF_CALENDAR = "calendar"
CONF_TODAY_DATE = "today_date"
CONF_SHOWED_DATE = "showed_date"
CONF_HIGHLIGHTED_DATES = "highlighted_dates"
CONF_MONTH = "month"
CONF_DAY = "day"

# Calendar returns selected date as year, month, day
lv_calendar_t = LvType(
    "LvCalendarType",
    parents=(LvCompound,),
    largs=[
        (cg.uint16, "year"),
        (cg.uint8, "month"),
        (cg.uint8, "day"),
    ],
    lvalue=lambda w: [
        w.var.get_selected_year(),
        w.var.get_selected_month(),
        w.var.get_selected_day(),
    ],
    has_on_value=True,
)


def date_schema(required=False):
    """Schema for date specification (year, month, day)"""
    return cv.Schema(
        {
            cv.Required(CONF_YEAR) if required else cv.Optional(CONF_YEAR): cv.int_range(
                min=1970, max=2099
            ),
            cv.Required(CONF_MONTH) if required else cv.Optional(CONF_MONTH): cv.int_range(
                min=1, max=12
            ),
            cv.Required(CONF_DAY) if required else cv.Optional(CONF_DAY): cv.int_range(
                min=1, max=31
            ),
        }
    )


CALENDAR_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_TODAY_DATE): date_schema(),
        cv.Optional(CONF_SHOWED_DATE): date_schema(),
        cv.Optional(CONF_HIGHLIGHTED_DATES): cv.ensure_list(date_schema(required=True)),
    }
)


class CalendarType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_CALENDAR,
            lv_calendar_t,
            (CONF_MAIN, CONF_ITEMS),
            CALENDAR_SCHEMA,
            modify_schema=CALENDAR_SCHEMA,
            lv_name="calendar",
        )

    async def to_code(self, w: Widget, config):
        """Generate code for calendar widget"""
        lvgl_components_required.add("CALENDAR")

        # Set today's date
        if today := config.get(CONF_TODAY_DATE):
            year = await lv_int.process(today.get(CONF_YEAR, 2024))
            month = await lv_int.process(today.get(CONF_MONTH, 1))
            day = await lv_int.process(today.get(CONF_DAY, 1))
            lv.calendar_set_today_date(w.obj, year, month, day)

        # Set showed date (initial display date)
        if showed := config.get(CONF_SHOWED_DATE):
            year = await lv_int.process(showed.get(CONF_YEAR, 2024))
            month = await lv_int.process(showed.get(CONF_MONTH, 1))
            day = await lv_int.process(showed.get(CONF_DAY, 1))
            lv.calendar_set_showed_date(w.obj, year, month, day)

        # Set highlighted dates
        if highlighted := config.get(CONF_HIGHLIGHTED_DATES):
            # Create array of highlighted dates
            dates_count = len(highlighted)
            if dates_count > 0:
                # Generate the highlighted dates array
                dates_array_id = cg.RawExpression(f"{w.obj}_highlighted_dates")
                dates_elements = []
                for date in highlighted:
                    year = date[CONF_YEAR]
                    month = date[CONF_MONTH]
                    day = date[CONF_DAY]
                    dates_elements.append(f"{{{year}, {month}, {day}}}")

                dates_array_str = "{" + ", ".join(dates_elements) + "}"
                dates_var = cg.RawExpression(
                    f"static lv_calendar_date_t {dates_array_id}[] = {dates_array_str}"
                )
                lv_add(dates_var)
                lv.calendar_set_highlighted_dates(
                    w.obj, dates_array_id, dates_count
                )

    def get_uses(self):
        return ("calendar",)


calendar_spec = CalendarType()


@automation.register_action(
    "lvgl.calendar.update",
    ObjUpdateAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(lv_calendar_t),
            cv.Optional(CONF_TODAY_DATE): date_schema(),
            cv.Optional(CONF_SHOWED_DATE): date_schema(),
            cv.Optional(CONF_HIGHLIGHTED_DATES): cv.ensure_list(date_schema(required=True)),
        }
    ),
)
async def calendar_update_to_code(config, action_id, template_arg, args):
    """Handle calendar update action"""
    widgets = await get_widgets(config)

    async def do_calendar_update(w: Widget):
        # Update today's date
        if today := config.get(CONF_TODAY_DATE):
            year = await lv_int.process(today.get(CONF_YEAR, 2024))
            month = await lv_int.process(today.get(CONF_MONTH, 1))
            day = await lv_int.process(today.get(CONF_DAY, 1))
            lv.calendar_set_today_date(w.obj, year, month, day)

        # Update showed date
        if showed := config.get(CONF_SHOWED_DATE):
            year = await lv_int.process(showed.get(CONF_YEAR, 2024))
            month = await lv_int.process(showed.get(CONF_MONTH, 1))
            day = await lv_int.process(showed.get(CONF_DAY, 1))
            lv.calendar_set_showed_date(w.obj, year, month, day)

        # Update highlighted dates
        if highlighted := config.get(CONF_HIGHLIGHTED_DATES):
            dates_count = len(highlighted)
            if dates_count > 0:
                # Generate the highlighted dates array
                dates_array_id = cg.RawExpression(f"{w.obj}_highlighted_dates_update")
                dates_elements = []
                for date in highlighted:
                    year = date[CONF_YEAR]
                    month = date[CONF_MONTH]
                    day = date[CONF_DAY]
                    dates_elements.append(f"{{{year}, {month}, {day}}}")

                dates_array_str = "{" + ", ".join(dates_elements) + "}"
                dates_var = cg.RawExpression(
                    f"static lv_calendar_date_t {dates_array_id}[] = {dates_array_str}"
                )
                lv_add(dates_var)
                lv.calendar_set_highlighted_dates(
                    w.obj, dates_array_id, dates_count
                )

    return await action_to_code(
        widgets, do_calendar_update, action_id, template_arg, args, config
    )
