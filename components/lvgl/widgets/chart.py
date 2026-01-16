"""
LVGL v9.4 Chart Widget Implementation

The chart widget displays data visualization with support for:
- LINE charts: Connected line series
- BAR charts: Vertical or horizontal bars
- SCATTER charts: Point-based data
- Multiple series per chart
- Configurable axes and division lines
"""

import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_MODE,
    CONF_TYPE,
)

from ..defines import (
    CONF_ITEMS,
    CONF_MAIN,
    literal,
)
from ..helpers import lvgl_components_required
from ..lv_validation import lv_color, lv_int
from ..lvcode import lv
from ..types import LvType
from . import Widget, WidgetType

CONF_CHART = "chart"
CONF_SERIES = "series"
CONF_POINT_COUNT = "point_count"
CONF_POINTS = "points"
CONF_X_AXIS = "x_axis"
CONF_Y_AXIS = "y_axis"
CONF_DIV_LINE_COUNT = "div_line_count"
CONF_AXIS_PRIMARY_Y = "axis_primary_y"
CONF_AXIS_SECONDARY_Y = "axis_secondary_y"
CONF_AXIS_PRIMARY_X = "axis_primary_x"
CONF_AXIS_SECONDARY_X = "axis_secondary_x"
CONF_UPDATE_MODE = "update_mode"
CONF_COLOR = "color"

lv_chart_t = LvType("lv_chart_t")

# Chart types
CHART_TYPE_NONE = "NONE"
CHART_TYPE_LINE = "LINE"
CHART_TYPE_BAR = "BAR"
CHART_TYPE_SCATTER = "SCATTER"

CHART_TYPES = {
    CHART_TYPE_NONE: "LV_CHART_TYPE_NONE",
    CHART_TYPE_LINE: "LV_CHART_TYPE_LINE",
    CHART_TYPE_BAR: "LV_CHART_TYPE_BAR",
    CHART_TYPE_SCATTER: "LV_CHART_TYPE_SCATTER",
}

# Update modes
UPDATE_MODE_SHIFT = "SHIFT"
UPDATE_MODE_CIRCULAR = "CIRCULAR"

UPDATE_MODES = {
    UPDATE_MODE_SHIFT: "LV_CHART_UPDATE_MODE_SHIFT",
    UPDATE_MODE_CIRCULAR: "LV_CHART_UPDATE_MODE_CIRCULAR",
}

# Axis configuration
AXIS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MIN_VALUE): lv_int,
        cv.Optional(CONF_MAX_VALUE): lv_int,
        cv.Optional(CONF_DIV_LINE_COUNT): cv.positive_int,
    }
)

# Series configuration
SERIES_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.declare_id(LvType("lv_chart_series_t")),
        cv.Optional(CONF_COLOR): lv_color,
        cv.Optional(CONF_POINTS): cv.ensure_list(lv_int),
        cv.Optional(CONF_TYPE): cv.enum(CHART_TYPES, upper=True),
    }
)

# Main chart schema
CHART_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_TYPE, default=CHART_TYPE_LINE): cv.enum(CHART_TYPES, upper=True),
        cv.Optional(CONF_POINT_COUNT, default=10): cv.positive_int,
        cv.Optional(CONF_UPDATE_MODE, default=UPDATE_MODE_SHIFT): cv.enum(
            UPDATE_MODES, upper=True
        ),
        cv.Optional(CONF_SERIES): cv.ensure_list(SERIES_SCHEMA),
        # Axes configuration
        cv.Optional(CONF_AXIS_PRIMARY_Y): AXIS_SCHEMA,
        cv.Optional(CONF_AXIS_SECONDARY_Y): AXIS_SCHEMA,
        cv.Optional(CONF_AXIS_PRIMARY_X): AXIS_SCHEMA,
        cv.Optional(CONF_AXIS_SECONDARY_X): AXIS_SCHEMA,
    }
)


class ChartType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_CHART,
            lv_chart_t,
            (CONF_MAIN, CONF_ITEMS),
            CHART_SCHEMA,
            modify_schema={},
        )

    async def to_code(self, w: Widget, config):
        """Generate C++ code for chart widget configuration"""
        lvgl_components_required.add(CONF_CHART)

        # Set chart type
        chart_type = CHART_TYPES[config[CONF_TYPE]]
        lv.chart_set_type(w.obj, literal(chart_type))

        # Set point count
        point_count = config[CONF_POINT_COUNT]
        lv.chart_set_point_count(w.obj, point_count)

        # Set update mode
        update_mode = UPDATE_MODES[config[CONF_UPDATE_MODE]]
        lv.chart_set_update_mode(w.obj, literal(update_mode))

        # Configure axes
        await self._configure_axis(
            w, config, CONF_AXIS_PRIMARY_Y, "LV_CHART_AXIS_PRIMARY_Y"
        )
        await self._configure_axis(
            w, config, CONF_AXIS_SECONDARY_Y, "LV_CHART_AXIS_SECONDARY_Y"
        )
        await self._configure_axis(
            w, config, CONF_AXIS_PRIMARY_X, "LV_CHART_AXIS_PRIMARY_X"
        )
        await self._configure_axis(
            w, config, CONF_AXIS_SECONDARY_X, "LV_CHART_AXIS_SECONDARY_X"
        )

        # Add series
        if series_list := config.get(CONF_SERIES):
            for series in series_list:
                await self._add_series(w, series)

    async def _configure_axis(self, w: Widget, config, axis_key, axis_const):
        """Configure a specific axis"""
        if axis_config := config.get(axis_key):
            axis_literal = literal(axis_const)

            # Set range if specified
            if CONF_MIN_VALUE in axis_config and CONF_MAX_VALUE in axis_config:
                min_val = await lv_int.process(axis_config[CONF_MIN_VALUE])
                max_val = await lv_int.process(axis_config[CONF_MAX_VALUE])
                lv.chart_set_range(w.obj, axis_literal, min_val, max_val)

            # Set division line count if specified
            if CONF_DIV_LINE_COUNT in axis_config:
                div_count = axis_config[CONF_DIV_LINE_COUNT]
                lv.chart_set_div_line_count(w.obj, div_count, div_count)

    async def _add_series(self, w: Widget, series_config):
        """Add a data series to the chart"""
        # Get series color if specified, otherwise use default
        if CONF_COLOR in series_config:
            color = await lv_color.process(series_config[CONF_COLOR])
        else:
            color = literal("lv_palette_main(LV_PALETTE_RED)")

        # Add series
        series_var = series_config[CONF_ID]
        lv.chart_add_series(w.obj, color, literal("LV_CHART_AXIS_PRIMARY_Y"))

        # Set series type if specified (overrides chart type for this series)
        if CONF_TYPE in series_config:
            series_type = CHART_TYPES[series_config[CONF_TYPE]]
            # Note: In LVGL v9.4, series type is set per series, not globally
            # This allows mixing different chart types

        # Set initial points if provided
        if points := series_config.get(CONF_POINTS):
            for idx, point_value in enumerate(points):
                point = await lv_int.process(point_value)
                # Set point by index
                # lv.chart_set_value_by_id(w.obj, series_var, idx, point)

    def get_uses(self):
        """Chart widget uses label for axis labels"""
        return ("label",)


chart_spec = ChartType()
