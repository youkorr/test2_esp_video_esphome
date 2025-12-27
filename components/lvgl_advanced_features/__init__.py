"""
LVGL Advanced Features Component
Enables ThorVG, SVG, Lottie, and other advanced LVGL V9 features for ESPHome.

This component adds build flags to enable advanced LVGL features that are
disabled by default in ESPHome's LVGL configuration.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["lvgl"]
CODEOWNERS = ["@youkorr"]

lvgl_advanced_features_ns = cg.esphome_ns.namespace("lvgl_advanced_features")
LVGLAdvancedFeatures = lvgl_advanced_features_ns.class_("LVGLAdvancedFeatures", cg.Component)

# Configuration options
CONF_THORVG = "thorvg"
CONF_SVG = "svg"
CONF_LOTTIE = "lottie"
CONF_FREETYPE = "freetype"
CONF_RLOTTIE = "rlottie"
CONF_FFMPEG = "ffmpeg"
CONF_LIBPNG = "libpng"
CONF_LIBJPEG_TURBO = "libjpeg_turbo"
CONF_GIF = "gif"
CONF_BMP = "bmp"
CONF_QRCODE = "qrcode"
CONF_BARCODE = "barcode"
CONF_IME_PINYIN = "ime_pinyin"

# ThorVG options
CONF_THORVG_INTERNAL = "internal"
CONF_THORVG_EXTERNAL = "external"

# Performance options
CONF_DRAW_SW_COMPLEX = "draw_sw_complex"
CONF_DRAW_SW_ASM = "draw_sw_asm"
CONF_SHADOW_CACHE_SIZE = "shadow_cache_size"
CONF_IMG_CACHE_SIZE = "img_cache_size"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LVGLAdvancedFeatures),

    # Vector Graphics
    cv.Optional(CONF_THORVG): cv.Schema({
        cv.Optional(CONF_THORVG_INTERNAL, default=True): cv.boolean,
        cv.Optional(CONF_THORVG_EXTERNAL, default=False): cv.boolean,
    }),
    cv.Optional(CONF_SVG, default=False): cv.boolean,
    cv.Optional(CONF_LOTTIE, default=False): cv.boolean,

    # Font rendering
    cv.Optional(CONF_FREETYPE, default=False): cv.boolean,
    cv.Optional(CONF_RLOTTIE, default=False): cv.boolean,

    # Media support
    cv.Optional(CONF_FFMPEG, default=False): cv.boolean,
    cv.Optional(CONF_LIBPNG, default=False): cv.boolean,
    cv.Optional(CONF_LIBJPEG_TURBO, default=False): cv.boolean,
    cv.Optional(CONF_GIF, default=False): cv.boolean,
    cv.Optional(CONF_BMP, default=False): cv.boolean,

    # Widgets
    cv.Optional(CONF_QRCODE, default=False): cv.boolean,
    cv.Optional(CONF_BARCODE, default=False): cv.boolean,
    cv.Optional(CONF_IME_PINYIN, default=False): cv.boolean,

    # Performance
    cv.Optional(CONF_DRAW_SW_COMPLEX, default=True): cv.boolean,
    cv.Optional(CONF_DRAW_SW_ASM, default=False): cv.one_of("none", "neon", "helium"),
    cv.Optional(CONF_SHADOW_CACHE_SIZE, default=0): cv.int_range(min=0, max=1024),
    cv.Optional(CONF_IMG_CACHE_SIZE, default=0): cv.int_range(min=0, max=1024),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # ThorVG configuration
    if CONF_THORVG in config:
        thorvg_cfg = config[CONF_THORVG]
        if thorvg_cfg.get(CONF_THORVG_INTERNAL, False):
            cg.add_define("LV_USE_THORVG_INTERNAL", "1")
            cg.add_build_flag("-DLV_USE_THORVG_INTERNAL=1")
            cg.add(var.set_thorvg_internal(True))
        if thorvg_cfg.get(CONF_THORVG_EXTERNAL, False):
            cg.add_define("LV_USE_THORVG_EXTERNAL", "1")
            cg.add_build_flag("-DLV_USE_THORVG_EXTERNAL=1")
            cg.add(var.set_thorvg_external(True))

    # SVG support (requires ThorVG)
    if config.get(CONF_SVG, False):
        cg.add_define("LV_USE_SVG", "1")
        cg.add_build_flag("-DLV_USE_SVG=1")
        cg.add(var.set_svg_enabled(True))

    # Lottie support (requires ThorVG)
    if config.get(CONF_LOTTIE, False):
        cg.add_define("LV_USE_LOTTIE", "1")
        cg.add_build_flag("-DLV_USE_LOTTIE=1")
        cg.add(var.set_lottie_enabled(True))

    # FreeType font rendering
    if config.get(CONF_FREETYPE, False):
        cg.add_define("LV_USE_FREETYPE", "1")
        cg.add_build_flag("-DLV_USE_FREETYPE=1")
        cg.add(var.set_freetype_enabled(True))

    # RLottie (alternative to ThorVG for Lottie)
    if config.get(CONF_RLOTTIE, False):
        cg.add_define("LV_USE_RLOTTIE", "1")
        cg.add_build_flag("-DLV_USE_RLOTTIE=1")
        cg.add(var.set_rlottie_enabled(True))

    # FFmpeg support
    if config.get(CONF_FFMPEG, False):
        cg.add_define("LV_USE_FFMPEG", "1")
        cg.add_build_flag("-DLV_USE_FFMPEG=1")
        cg.add(var.set_ffmpeg_enabled(True))

    # Image format support
    if config.get(CONF_LIBPNG, False):
        cg.add_define("LV_USE_LIBPNG", "1")
        cg.add_build_flag("-DLV_USE_LIBPNG=1")
        cg.add(var.set_libpng_enabled(True))

    if config.get(CONF_LIBJPEG_TURBO, False):
        cg.add_define("LV_USE_LIBJPEG_TURBO", "1")
        cg.add_build_flag("-DLV_USE_LIBJPEG_TURBO=1")
        cg.add(var.set_libjpeg_turbo_enabled(True))

    if config.get(CONF_GIF, False):
        cg.add_define("LV_USE_GIF", "1")
        cg.add_build_flag("-DLV_USE_GIF=1")
        cg.add(var.set_gif_enabled(True))

    if config.get(CONF_BMP, False):
        cg.add_define("LV_USE_BMP", "1")
        cg.add_build_flag("-DLV_USE_BMP=1")
        cg.add(var.set_bmp_enabled(True))

    # Widgets
    if config.get(CONF_QRCODE, False):
        cg.add_define("LV_USE_QRCODE", "1")
        cg.add_build_flag("-DLV_USE_QRCODE=1")
        cg.add(var.set_qrcode_enabled(True))

    if config.get(CONF_BARCODE, False):
        cg.add_define("LV_USE_BARCODE", "1")
        cg.add_build_flag("-DLV_USE_BARCODE=1")
        cg.add(var.set_barcode_enabled(True))

    if config.get(CONF_IME_PINYIN, False):
        cg.add_define("LV_USE_IME_PINYIN", "1")
        cg.add_build_flag("-DLV_USE_IME_PINYIN=1")
        cg.add(var.set_ime_pinyin_enabled(True))

    # Performance optimizations
    if config.get(CONF_DRAW_SW_COMPLEX, False):
        cg.add_define("LV_DRAW_SW_COMPLEX", "1")
        cg.add_build_flag("-DLV_DRAW_SW_COMPLEX=1")

    asm_type = config.get(CONF_DRAW_SW_ASM, "none")
    if asm_type == "neon":
        cg.add_define("LV_DRAW_SW_ASM", "LV_DRAW_SW_ASM_NEON")
        cg.add_build_flag("-DLV_DRAW_SW_ASM=LV_DRAW_SW_ASM_NEON")
    elif asm_type == "helium":
        cg.add_define("LV_DRAW_SW_ASM", "LV_DRAW_SW_ASM_HELIUM")
        cg.add_build_flag("-DLV_DRAW_SW_ASM=LV_DRAW_SW_ASM_HELIUM")

    if config.get(CONF_SHADOW_CACHE_SIZE, 0) > 0:
        size = config[CONF_SHADOW_CACHE_SIZE]
        cg.add_define("LV_SHADOW_CACHE_SIZE", str(size))
        cg.add_build_flag(f"-DLV_SHADOW_CACHE_SIZE={size}")

    if config.get(CONF_IMG_CACHE_SIZE, 0) > 0:
        size = config[CONF_IMG_CACHE_SIZE]
        cg.add_define("LV_IMG_CACHE_SIZE", str(size))
        cg.add_build_flag(f"-DLV_IMG_CACHE_SIZE={size}")
