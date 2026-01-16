import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_ROW, CONF_TEXT, CONF_WIDTH

from ..defines import (
    CONF_COLUMN,
    CONF_ITEMS,
    CONF_MAIN,
)
from ..helpers import lvgl_components_required
from ..lv_validation import lv_int, lv_text
from ..lvcode import lv
from ..types import LvType
from . import Widget, WidgetType

CONF_TABLE = "table"
CONF_ROW_COUNT = "row_count"
CONF_COLUMN_COUNT = "column_count"
CONF_COLUMNS = "columns"
CONF_CELLS = "cells"

# Define the table type
lv_table_t = LvType("lv_table_t")

# Schema for individual cells
CELL_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ROW): cv.positive_int,
        cv.Required(CONF_COLUMN): cv.positive_int,
        cv.Required(CONF_TEXT): lv_text,
    }
)

# Schema for column configuration
COLUMN_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.positive_int,
        cv.Required(CONF_WIDTH): cv.positive_int,
    }
)

# Main table schema
TABLE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ROW_COUNT): cv.positive_int,
        cv.Optional(CONF_COLUMN_COUNT): cv.positive_int,
        cv.Optional(CONF_CELLS): cv.ensure_list(CELL_SCHEMA),
        cv.Optional(CONF_COLUMNS): cv.ensure_list(COLUMN_SCHEMA),
    }
)

# Update schema (same as main schema for table)
TABLE_UPDATE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ROW_COUNT): cv.positive_int,
        cv.Optional(CONF_COLUMN_COUNT): cv.positive_int,
        cv.Optional(CONF_CELLS): cv.ensure_list(CELL_SCHEMA),
        cv.Optional(CONF_COLUMNS): cv.ensure_list(COLUMN_SCHEMA),
    }
)


class TableType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_TABLE,
            lv_table_t,
            (CONF_MAIN, CONF_ITEMS),
            TABLE_SCHEMA,
            modify_schema=TABLE_UPDATE_SCHEMA,
        )

    async def to_code(self, w: Widget, config):
        """Generate code for table widget"""
        lvgl_components_required.add("TABLE")

        # Set row count
        if row_count := config.get(CONF_ROW_COUNT):
            row_count_val = await lv_int.process(row_count)
            lv.table_set_row_count(w.obj, row_count_val)

        # Set column count
        if column_count := config.get(CONF_COLUMN_COUNT):
            column_count_val = await lv_int.process(column_count)
            lv.table_set_column_count(w.obj, column_count_val)

        # Configure column widths
        if columns := config.get(CONF_COLUMNS):
            for col_conf in columns:
                col_id = col_conf[CONF_ID]
                col_width = col_conf[CONF_WIDTH]
                lv.table_set_column_width(w.obj, col_id, col_width)

        # Set cell values
        if cells := config.get(CONF_CELLS):
            for cell_conf in cells:
                row = cell_conf[CONF_ROW]
                col = cell_conf[CONF_COLUMN]
                text = await lv_text.process(cell_conf[CONF_TEXT])
                lv.table_set_cell_value(w.obj, row, col, text)

    def get_uses(self):
        return ("label",)


table_spec = TableType()
