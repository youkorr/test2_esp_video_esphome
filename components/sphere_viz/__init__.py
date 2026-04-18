from esphome import codegen as cg, config_validation as cv
from esphome.components.lvgl.types import lv_page_t
from esphome.const import (
    CONF_COLOR,
    CONF_HEIGHT,
    CONF_ID,
    CONF_MODE,
    CONF_WIDTH,
)

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["lvgl"]

sphere_viz_ns = cg.esphome_ns.namespace("sphere_viz")
SphereViz = sphere_viz_ns.class_("SphereViz", cg.Component)
SphereMode = sphere_viz_ns.enum("SphereMode")

CONF_X = "x"
CONF_Y = "y"
CONF_FPS = "fps"
CONF_PARTICLES = "particles"
CONF_MERIDIANS = "meridians"
CONF_PARALLELS = "parallels"
CONF_RADIUS = "radius"
CONF_PAGE_ID = "page_id"

MODES = {
    "WIREFRAME": SphereMode.MODE_WIREFRAME,
    "PARTICLES": SphereMode.MODE_PARTICLES,
}

CONFIG_SCHEMA = cv.ensure_list(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SphereViz),
            cv.Optional(CONF_PAGE_ID): cv.use_id(lv_page_t),
            cv.Optional(CONF_WIDTH, default=400): cv.int_range(64, 1024),
            cv.Optional(CONF_HEIGHT, default=400): cv.int_range(64, 1024),
            cv.Optional(CONF_X, default=0): cv.int_,
            cv.Optional(CONF_Y, default=0): cv.int_,
            cv.Optional(CONF_MODE, default="WIREFRAME"): cv.enum(MODES, upper=True),
            cv.Optional(CONF_FPS, default=30): cv.int_range(5, 60),
            cv.Optional(CONF_COLOR, default=0x00FFAA): cv.hex_int,
            cv.Optional(CONF_PARTICLES, default=600): cv.int_range(50, 4000),
            cv.Optional(CONF_MERIDIANS, default=12): cv.int_range(4, 32),
            cv.Optional(CONF_PARALLELS, default=8): cv.int_range(2, 20),
            cv.Optional(CONF_RADIUS, default=0): cv.int_,
        }
    ).extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)

        cg.add(var.set_geometry(conf[CONF_X], conf[CONF_Y], conf[CONF_WIDTH], conf[CONF_HEIGHT]))
        cg.add(var.set_mode(conf[CONF_MODE]))
        cg.add(var.set_fps(conf[CONF_FPS]))
        cg.add(var.set_color(conf[CONF_COLOR]))
        cg.add(var.set_particle_count(conf[CONF_PARTICLES]))
        cg.add(var.set_meridians(conf[CONF_MERIDIANS]))
        cg.add(var.set_parallels(conf[CONF_PARALLELS]))
        cg.add(var.set_radius(conf[CONF_RADIUS]))

        if CONF_PAGE_ID in conf:
            page = await cg.get_variable(conf[CONF_PAGE_ID])
            cg.add(var.set_parent(page.obj))
