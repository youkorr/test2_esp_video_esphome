"""LVGL v9.4 Scale Widget Implementation for ESPHome

The scale widget is a versatile component for displaying measurement scales in various orientations.
It replaces the obsolete meter widget from LVGL v8.x and provides more flexibility.

Supported features:
- Multiple scale modes: horizontal, vertical, round (inner/outer)
- Configurable tick marks (major and minor)
- Value range configuration
- Colored sections for different value ranges
- Label rotation and formatting
- Parts: MAIN (background), INDICATOR (major ticks), ITEMS (minor ticks)
"""

from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_COLOR,
    CONF_COUNT,
    CONF_ID,
    CONF_ITEMS,
    CONF_LENGTH,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_MODE,
    CONF_RANGE_FROM,
    CONF_RANGE_TO,
    CONF_ROTATION,
    CONF_VALUE,
    CONF_WIDTH,
)
from esphome.cpp_types import nullptr

from .. import set_obj_properties
from ..automation import action_to_code
from ..defines import (
    CONF_ANIMATED,
    CONF_END_VALUE,
    CONF_INDICATOR,
    CONF_MAIN,
    CONF_START_VALUE,
    CONF_TICKS,
    LV_OBJ_FLAG,
    LV_PART,
    LV_SCALE_MODE,
    literal,
)
from ..helpers import add_lv_use, lvgl_components_required
from ..lv_validation import (
    LV_OPA,
    animated,
    get_end_value,
    get_start_value,
    lv_angle_degrees,
    lv_bool,
    lv_color,
    lv_float,
    lv_int,
    opacity,
    size,
)
from ..lvcode import lv, lv_add, lv_obj
from ..types import LvNumber, ObjUpdateAction
from . import NumberType, Widget, get_widgets

# Configuration keys
CONF_ANGLE_RANGE = "angle_range"
CONF_COLOR_END = "color_end"
CONF_COLOR_START = "color_start"
CONF_LABEL_GAP = "label_gap"
CONF_LABEL_SHOW = "label_show"
CONF_MAJOR = "major"
CONF_RADIAL_OFFSET = "radial_offset"
CONF_SCALE = "scale"
CONF_SECTION = "section"
CONF_SECTIONS = "sections"
CONF_STRIDE = "stride"
CONF_TEXT_SRC = "text_src"
CONF_POST_FIX = "post_fix"
CONF_PRE_FIX = "pre_fix"

DEFAULT_LABEL_GAP = 10  # Default label gap for major ticks

# Scale section schema for colored ranges
SECTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LvNumber("lv_scale_section_t")),
        cv.Optional(CONF_START_VALUE): lv_float,
        cv.Optional(CONF_END_VALUE): lv_float,
        cv.Optional(CONF_RANGE_FROM): lv_int,
        cv.Optional(CONF_RANGE_TO): lv_int,
        cv.Optional(CONF_COLOR, default=0): lv_color,
        cv.Optional(CONF_WIDTH, default=4): cv.positive_int,
    }
).add_extra(cv.has_at_most_one_key(CONF_START_VALUE, CONF_RANGE_FROM))

# Tick configuration schema
TICK_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_COUNT, default=12): cv.positive_int,
        cv.Optional(CONF_WIDTH, default=2): cv.positive_int,
        cv.Optional(CONF_LENGTH, default=10): size,
        cv.Optional(CONF_COLOR, default=0x808080): lv_color,
        cv.Optional(CONF_RADIAL_OFFSET, default=0): size,
        cv.Optional(CONF_MAJOR): cv.Schema(
            {
                cv.Optional(CONF_STRIDE, default=3): cv.positive_int,
                cv.Optional(CONF_WIDTH, default=5): size,
                cv.Optional(CONF_LENGTH, default="15%"): size,
                cv.Optional(CONF_COLOR, default=0): lv_color,
                cv.Optional(CONF_RADIAL_OFFSET, default=0): size,
                cv.Optional(CONF_LABEL_GAP, default=4): size,
                cv.Optional(CONF_LABEL_SHOW, default=True): lv_bool,
            }
        ),
    }
)

# Main scale widget schema
SCALE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_VALUE): lv_float,
        cv.Optional(CONF_MIN_VALUE, default=0): lv_int,
        cv.Optional(CONF_MAX_VALUE, default=100): lv_int,
        cv.Optional(
            CONF_MODE, default="ROUND_OUTER"
        ): LV_SCALE_MODE.one_of,  # horizontal_top, horizontal_bottom, vertical_left, vertical_right, round_inner, round_outer
        cv.Optional(CONF_ROTATION, default=0): lv_angle_degrees,
        cv.Optional(CONF_ANGLE_RANGE, default=270): lv_angle_degrees,
        cv.Optional(CONF_TICKS): TICK_SCHEMA,
        cv.Optional(CONF_SECTIONS): cv.ensure_list(SECTION_SCHEMA),
        cv.Optional(CONF_ANIMATED, default=True): animated,
    }
)

# Schema for updating scale widget
SCALE_MODIFY_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_VALUE): lv_float,
        cv.Optional(CONF_MIN_VALUE): lv_int,
        cv.Optional(CONF_MAX_VALUE): lv_int,
        cv.Optional(CONF_MODE): LV_SCALE_MODE.one_of,
        cv.Optional(CONF_ROTATION): lv_angle_degrees,
        cv.Optional(CONF_ANGLE_RANGE): lv_angle_degrees,
        cv.Optional(CONF_ANIMATED, default=True): animated,
    }
)


class ScaleType(NumberType):
    """
    LVGL Scale widget type.

    The scale widget provides a versatile way to display measurement scales in different orientations.
    It supports horizontal, vertical, and circular (round) layouts with configurable tick marks,
    labels, and colored sections.
    """

    def __init__(self):
        super().__init__(
            CONF_SCALE,
            LvNumber("lv_scale_t"),
            parts=(CONF_MAIN, CONF_INDICATOR, CONF_ITEMS),
            schema=SCALE_SCHEMA,
            modify_schema=SCALE_MODIFY_SCHEMA,
        )

    @property
    def animated(self):
        return True

    async def to_code(self, w: Widget, config):
        """Generate code for scale widget configuration."""
        lvgl_components_required.add(CONF_SCALE)
        add_lv_use(CONF_SCALE)

        # Set scale mode
        if CONF_MODE in config:
            mode = config[CONF_MODE]
            lv.scale_set_mode(w.obj, literal(mode))

        # Set range (min/max values)
        if CONF_MIN_VALUE in config or CONF_MAX_VALUE in config:
            min_val = config.get(CONF_MIN_VALUE, 0)
            max_val = config.get(CONF_MAX_VALUE, 100)
            lv.scale_set_range(w.obj, min_val, max_val)

        # Set rotation (for round modes)
        if CONF_ROTATION in config:
            rotation = await lv_angle_degrees.process(config[CONF_ROTATION])
            lv.scale_set_rotation(w.obj, rotation)

        # Set angle range (for round modes)
        if CONF_ANGLE_RANGE in config:
            angle_range = await lv_angle_degrees.process(config[CONF_ANGLE_RANGE])
            lv.scale_set_angle_range(w.obj, angle_range)

        # Configure ticks
        if ticks := config.get(CONF_TICKS):
            # Set total tick count
            lv.scale_set_total_tick_count(w.obj, ticks[CONF_COUNT])

            # Style minor ticks (ITEMS part)
            lv_obj.set_style_length(
                w.obj, await size.process(ticks[CONF_LENGTH]), LV_PART.ITEMS
            )
            lv_obj.set_style_line_width(
                w.obj, await size.process(ticks[CONF_WIDTH]), LV_PART.ITEMS
            )
            lv_obj.set_style_line_color(
                w.obj, await lv_color.process(ticks[CONF_COLOR]), LV_PART.ITEMS
            )
            lv_obj.set_style_radial_offset(
                w.obj,
                await size.process(ticks[CONF_RADIAL_OFFSET]),
                LV_PART.ITEMS,
            )

            # Configure major ticks if specified
            if major := ticks.get(CONF_MAJOR):
                # Set major tick frequency
                lv.scale_set_major_tick_every(w.obj, major[CONF_STRIDE])

                # Enable or disable labels
                label_show = major.get(CONF_LABEL_SHOW, True)
                lv.scale_set_label_show(w.obj, label_show)

                # Style major ticks (INDICATOR part)
                lv_obj.set_style_length(
                    w.obj, await size.process(major[CONF_LENGTH]), LV_PART.INDICATOR
                )
                lv_obj.set_style_line_width(
                    w.obj, await size.process(major[CONF_WIDTH]), LV_PART.INDICATOR
                )
                lv_obj.set_style_line_color(
                    w.obj, await lv_color.process(major[CONF_COLOR]), LV_PART.INDICATOR
                )
                lv_obj.set_style_radial_offset(
                    w.obj,
                    await size.process(major[CONF_RADIAL_OFFSET]),
                    LV_PART.INDICATOR,
                )

                # Set label gap (distance from scale)
                label_gap = await size.process(major[CONF_LABEL_GAP])
                if isinstance(label_gap, int):
                    label_gap -= DEFAULT_LABEL_GAP
                lv_obj.set_style_pad_radial(w.obj, label_gap, LV_PART.INDICATOR)
            else:
                # No major ticks
                lv.scale_set_major_tick_every(w.obj, 0)
        else:
            # No ticks at all
            lv.scale_set_total_tick_count(w.obj, 0)

        # Add colored sections
        if sections := config.get(CONF_SECTIONS):
            for section_conf in sections:
                section_id = section_conf[CONF_ID]

                # Determine start and end values
                start_value = await get_start_value(section_conf) or section_conf.get(
                    CONF_RANGE_FROM, config.get(CONF_MIN_VALUE, 0)
                )
                end_value = await get_end_value(section_conf) or section_conf.get(
                    CONF_RANGE_TO, config.get(CONF_MAX_VALUE, 100)
                )

                # Create section
                section_var = cg.Pvariable(
                    section_id, lv.scale_add_section(w.obj)
                )

                # Set section range
                lv.scale_section_set_range(section_var, start_value, end_value)

                # Set section style
                if CONF_COLOR in section_conf:
                    color = await lv_color.process(section_conf[CONF_COLOR])
                    lv_obj.set_style_line_color(section_var, color, LV_PART.INDICATOR)

                if CONF_WIDTH in section_conf:
                    width = section_conf[CONF_WIDTH]
                    lv_obj.set_style_line_width(section_var, width, LV_PART.INDICATOR)

        # Set initial value if provided
        value = await get_start_value(config)
        if value is not None:
            # Note: Scale widget doesn't have a direct value setter in LVGL 9.4
            # This would typically be used with indicators/needles that point to values
            # Store the value for potential use with custom indicators
            pass


scale_spec = ScaleType()


@automation.register_action(
    "lvgl.scale.update",
    ObjUpdateAction,
    SCALE_MODIFY_SCHEMA.extend(
        {
            cv.Required(CONF_ID): cv.use_id(LvNumber("lv_scale_t")),
        }
    ),
)
async def scale_update_to_code(config, action_id, template_arg, args):
    """Handle scale update actions."""
    widgets = await get_widgets(config)

    async def update_scale(w: Widget):
        if CONF_MODE in config:
            mode = config[CONF_MODE]
            lv.scale_set_mode(w.obj, literal(mode))

        if CONF_MIN_VALUE in config or CONF_MAX_VALUE in config:
            min_val = config.get(CONF_MIN_VALUE, w.type.get_min(w.config))
            max_val = config.get(CONF_MAX_VALUE, w.type.get_max(w.config))
            lv.scale_set_range(w.obj, min_val, max_val)

        if CONF_ROTATION in config:
            rotation = await lv_angle_degrees.process(config[CONF_ROTATION])
            lv.scale_set_rotation(w.obj, rotation)

        if CONF_ANGLE_RANGE in config:
            angle_range = await lv_angle_degrees.process(config[CONF_ANGLE_RANGE])
            lv.scale_set_angle_range(w.obj, angle_range)

    return await action_to_code(widgets, update_scale, action_id, template_arg, args, config)


@automation.register_action(
    "lvgl.scale.section.update",
    ObjUpdateAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(LvNumber("lv_scale_section_t")),
            cv.Optional(CONF_START_VALUE): lv_float,
            cv.Optional(CONF_END_VALUE): lv_float,
            cv.Optional(CONF_RANGE_FROM): lv_int,
            cv.Optional(CONF_RANGE_TO): lv_int,
            cv.Optional(CONF_COLOR): lv_color,
            cv.Optional(CONF_WIDTH): cv.positive_int,
        }
    ).add_extra(cv.has_at_most_one_key(CONF_START_VALUE, CONF_RANGE_FROM)),
)
async def section_update_to_code(config, action_id, template_arg, args):
    """Handle scale section update actions."""
    widgets = await get_widgets(config)

    async def update_section(w: Widget):
        # Update section range
        start_value = await get_start_value(config)
        end_value = await get_end_value(config)

        if start_value is not None and end_value is not None:
            lv.scale_section_set_range(w.obj, start_value, end_value)
        elif start_value is not None:
            # If only start value, use it as both start and end
            lv.scale_section_set_range(w.obj, start_value, start_value)

        # Update section style
        if CONF_COLOR in config:
            color = await lv_color.process(config[CONF_COLOR])
            lv_obj.set_style_line_color(w.obj, color, LV_PART.INDICATOR)

        if CONF_WIDTH in config:
            width = config[CONF_WIDTH]
            lv_obj.set_style_line_width(w.obj, width, LV_PART.INDICATOR)

    return await action_to_code(widgets, update_section, action_id, template_arg, args, config)
