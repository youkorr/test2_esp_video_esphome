"""
LVGL v9.4 Lottie Widget Implementation

The lottie widget displays vector animations using the Lottie format with ThorVG.
Lottie animations are JSON-based vector animations created with Adobe After Effects.

Based on LVGL 9.4 official API:
https://docs.lvgl.io/master/details/widgets/lottie.html
"""

import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID
from esphome.core import CORE

from esphome.cpp_generator import RawExpression, RawStatement

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


def lottie_src(value):
    """
    Validate lottie source: can be either:
    - A file path string (e.g., "/sdcard/animations/loading.json")
    - A lottie_file ID (e.g., my_lottie_animation)

    Note: LVGL Lottie uses direct fopen(), not VFS driver, so use full paths like:
    "/sdcard/..." instead of "S:/..."
    """
    # If it's a string, treat it as a file path
    if isinstance(value, str):
        return lv_text(value)

    # Otherwise, treat it as a lottie_file ID
    return cv.use_id(cg.esphome_ns.namespace("lottie_file").class_("LottieFile"))(value)


# Lottie schema
LOTTIE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_SRC): lottie_src,  # Path to .json file OR lottie_file ID (can be set later via on_load)
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
        - lv_lottie_set_src_file(obj, path) - Load from file (SD card)
        - lv_lottie_set_src_data(obj, data, size) - Load from memory (embedded)
        - lv_lottie_get_anim(obj) - Get LVGL animation object for control

        By default, Lottie animations run infinitely at 60FPS.

        CRITICAL: Lottie widgets require a buffer allocated with proper 64-byte alignment.
        We use lv_draw_buf_create() which ensures alignment for ESP32-P4 PSRAM/cache.
        """
        lvgl_components_required.add(CONF_LOTTIE)

        # Get buffer dimensions (default to widget size if not specified)
        buffer_width = config.get(CONF_BUFFER_WIDTH)
        buffer_height = config.get(CONF_BUFFER_HEIGHT)

        # Generate buffer allocation code
        # CRITICAL: Use lv_draw_buf_create() to ensure 64-byte alignment for ESP32-P4
        # ThorVG requires ARGB8888_PREMULTIPLIED format
        # NOTE: Buffer must be allocated AFTER widget is created and sized
        if buffer_width and buffer_height:
            # User specified buffer size - use it directly
            buf_w = await lv_int.process(buffer_width)
            buf_h = await lv_int.process(buffer_height)

            with LocalVariable("lottie_draw_buf", "lv_draw_buf_t *") as draw_buf_var:
                # Calculate aligned stride for ESP32-P4 (64-byte alignment)
                # ARGB8888 = 4 bytes per pixel, align to 64 bytes
                cg.add(RawStatement(
                    f'uint32_t stride_{buf_w} = ({buf_w} * 4 + 63) & ~63;  // Align to 64 bytes\n'
                    f'{draw_buf_var} = lv_draw_buf_create({buf_w}, {buf_h}, '
                    f'LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED, stride_{buf_w});'
                ))

                # Check allocation success
                cg.add(RawStatement(
                    f'if ({draw_buf_var} == nullptr) {{\n'
                    f'  ESP_LOGE("lvgl.lottie", "Failed to allocate draw buffer: %dx%d", {buf_w}, {buf_h});\n'
                    f'}} else {{\n'
                    f'  ESP_LOGI("lvgl.lottie", "Allocated draw buffer: %dx%d (64-byte aligned)", '
                    f'{draw_buf_var}->header.w, {draw_buf_var}->header.h);\n'
                    f'  lv_lottie_set_draw_buf({w.obj}, {draw_buf_var});\n'
                    f'}}'
                ))
        else:
            # Use widget dimensions - need to check they're valid
            # Generate inline code without LocalVariable to avoid pointer issues
            cg.add(RawStatement(
                f'{{\n'
                f'  int32_t lottie_w = lv_obj_get_content_width({w.obj});\n'
                f'  int32_t lottie_h = lv_obj_get_content_height({w.obj});\n'
                f'  if (lottie_w > 0 && lottie_h > 0) {{\n'
                f'    // Calculate aligned stride for ESP32-P4 (64-byte alignment)\n'
                f'    // ARGB8888 = 4 bytes per pixel, align to 64 bytes\n'
                f'    uint32_t lottie_stride = (lottie_w * 4 + 63) & ~63;\n'
                f'    lv_draw_buf_t *lottie_draw_buf = lv_draw_buf_create(lottie_w, lottie_h, '
                f'LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED, lottie_stride);\n'
                f'    if (lottie_draw_buf == nullptr) {{\n'
                f'      ESP_LOGE("lvgl.lottie", "Failed to allocate draw buffer: %dx%d", lottie_w, lottie_h);\n'
                f'    }} else {{\n'
                f'      ESP_LOGI("lvgl.lottie", "Allocated draw buffer: %dx%d (64-byte aligned)", '
                f'lottie_draw_buf->header.w, lottie_draw_buf->header.h);\n'
                f'      lv_lottie_set_draw_buf({w.obj}, lottie_draw_buf);\n'
                f'    }}\n'
                f'  }} else {{\n'
                f'    ESP_LOGW("lvgl.lottie", "Widget has invalid dimensions: %dx%d. Set width/height or buffer_width/buffer_height.", '
                f'lottie_w, lottie_h);\n'
                f'  }}\n'
                f'}}'
            ))

        # Get animation source (file path or embedded ID) - may be None if set via on_load
        src = config.get(CONF_SRC)

        # Only load source if provided (can be set later via on_load/widget.update)
        if src is not None and isinstance(src, str):
            # File path (/sdcard/...) - use lv_lottie_set_src_file()
            # Note: Lottie uses direct fopen(), so use full paths like "/sdcard/..."
            src_path = await lv_text.process(src)

            # Debug: Log file path being loaded
            cg.add(RawStatement(f'ESP_LOGI("lvgl.lottie", "Loading Lottie: %s", {src_path});'))

            # Load the Lottie animation from file
            # LVGL will handle file errors gracefully
            lv.lottie_set_src_file(w.obj, src_path)
        elif src is not None:
            # Embedded lottie_file - use lv_lottie_set_src_data()
            lottie_file = await cg.get_variable(src)

            # Debug: Log embedded loading
            cg.add(RawStatement(f'ESP_LOGI("lvgl.lottie", "Loading embedded Lottie: %u bytes", {lottie_file}->get_size());'))
            # Load from embedded Flash ROM data
            lv.lottie_set_src_data(
                w.obj,
                RawExpression(f"(const char*){lottie_file}->get_data()"),
                RawExpression(f"{lottie_file}->get_size()")
            )
        # else: src is None - will be set later via on_load or widget.update

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
