"""
LVGL v9.4 3D Texture Widget Implementation

The 3D texture widget displays textures mapped onto 3D surfaces.
This is an experimental widget for 3D rendering capabilities.

Note: This widget requires OpenGL backend and is rarely used in embedded systems.
"""

import esphome.config_validation as cv

from ..defines import (
    CONF_MAIN,
    CONF_SRC,
)
from ..helpers import lvgl_components_required
from ..lv_validation import lv_image
from ..lvcode import lv
from ..types import LvType
from . import Widget, WidgetType

CONF_3DTEXTURE = "tex3d"
CONF_ROTATION_X = "rotation_x"
CONF_ROTATION_Y = "rotation_y"
CONF_ROTATION_Z = "rotation_z"
CONF_SCALE = "scale"

lv_tex3d_t = LvType("lv_3dtexture_t")

# 3D Texture schema
TEX3D_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SRC): lv_image,
        cv.Optional(CONF_ROTATION_X, default=0): cv.float_,
        cv.Optional(CONF_ROTATION_Y, default=0): cv.float_,
        cv.Optional(CONF_ROTATION_Z, default=0): cv.float_,
        cv.Optional(CONF_SCALE, default=1.0): cv.float_,
    }
)


class Tex3DType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_3DTEXTURE,
            lv_tex3d_t,
            (CONF_MAIN,),
            TEX3D_SCHEMA,
            modify_schema={},
        )

    async def to_code(self, w: Widget, config):
        """Generate C++ code for 3D texture widget configuration"""
        lvgl_components_required.add(CONF_3DTEXTURE)

        # Set texture source
        src = await lv_image.process(config[CONF_SRC])
        lv.tex3d_set_src(w.obj, src)

        # Set rotation (if API supports it)
        # Note: The actual API for 3D textures in LVGL v9.4 may differ
        # This is a placeholder implementation

    def get_uses(self):
        """3D texture uses image component"""
        return ("img",)


tex3d_spec = Tex3DType()
