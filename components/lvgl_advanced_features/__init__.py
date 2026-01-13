"""
LVGL Advanced Features Component
Enables ThorVG, SVG, Lottie, and other advanced LVGL features for ESPHome.

Compatible with LVGL v8 and v9:
- v8: PNG, JPEG, GIF, BMP, QRCode, FreeType, Performance opts
- v9: All v8 features + ThorVG, SVG, Lottie, Barcode, FFmpeg

This component adds build flags to enable advanced LVGL features that are
disabled by default in ESPHome's LVGL configuration.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
import logging

_LOGGER = logging.getLogger(__name__)

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
    cv.Optional(CONF_DRAW_SW_ASM, default="none"): cv.one_of("none", "neon", "helium"),
    cv.Optional(CONF_SHADOW_CACHE_SIZE, default=0): cv.int_range(min=0, max=1024),
    cv.Optional(CONF_IMG_CACHE_SIZE, default=0): cv.int_range(min=0, max=1024),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    _LOGGER.info("LVGL Advanced Features: Configuring for LVGL v8/v9 compatibility")

    # ========================================
    # LVGL V9 ONLY FEATURES
    # ========================================
    # These features will be enabled but require LVGL v9 to actually work
    # The component will log warnings if used with LVGL v8

    # ThorVG configuration (v9 only)
    if CONF_THORVG in config:
        thorvg_cfg = config[CONF_THORVG]
        if thorvg_cfg.get(CONF_THORVG_INTERNAL, False):
            cg.add_build_flag("-DLV_USE_VECTOR_GRAPHIC=1")
            cg.add_build_flag("-DLV_USE_THORVG_INTERNAL=1")
            cg.add(var.set_thorvg_internal(True))
            _LOGGER.info("  ThorVG Internal: ENABLED (requires LVGL v9)")
        if thorvg_cfg.get(CONF_THORVG_EXTERNAL, False):
            cg.add_build_flag("-DLV_USE_VECTOR_GRAPHIC=1")
            cg.add_build_flag("-DLV_USE_THORVG_EXTERNAL=1")
            cg.add(var.set_thorvg_external(True))
            _LOGGER.info("  ThorVG External: ENABLED (requires LVGL v9)")

    # SVG support (v9 only - requires ThorVG)
    if config.get(CONF_SVG, False):
        cg.add_build_flag("-DLV_USE_VECTOR_GRAPHIC=1")
        cg.add_build_flag("-DLV_USE_SVG=1")
        cg.add(var.set_svg_enabled(True))
        _LOGGER.info("  SVG: ENABLED (requires LVGL v9 + ThorVG)")

    # Lottie support (v9 only - requires ThorVG)
    if config.get(CONF_LOTTIE, False):
        cg.add_build_flag("-DLV_USE_VECTOR_GRAPHIC=1")
        cg.add_build_flag("-DLV_USE_LOTTIE=1")
        cg.add(var.set_lottie_enabled(True))
        _LOGGER.info("  Lottie: ENABLED (requires LVGL v9 + ThorVG)")

    # ========================================
    # LVGL V8 AND V9 COMPATIBLE FEATURES
    # ========================================
    # These features work with both LVGL v8 and v9

    # FreeType font rendering (v8+v9 compatible)
    if config.get(CONF_FREETYPE, False):
        cg.add_build_flag("-DLV_USE_FREETYPE=1")
        cg.add(var.set_freetype_enabled(True))
        _LOGGER.info("  FreeType: ENABLED (compatible v8+v9)")

    # RLottie (alternative to ThorVG for Lottie) - experimental
    if config.get(CONF_RLOTTIE, False):
        cg.add_build_flag("-DLV_USE_RLOTTIE=1")
        cg.add(var.set_rlottie_enabled(True))
        _LOGGER.info("  RLottie: ENABLED (experimental)")

    # FFmpeg support (v9 only)
    if config.get(CONF_FFMPEG, False):
        cg.add_build_flag("-DLV_USE_FFMPEG=1")
        cg.add(var.set_ffmpeg_enabled(True))
        _LOGGER.info("  FFmpeg: ENABLED (requires LVGL v9)")

    # Image format support (v8+v9 compatible)
    if config.get(CONF_LIBPNG, False):
        cg.add_build_flag("-DLV_USE_LIBPNG=1")
        cg.add(var.set_libpng_enabled(True))
        _LOGGER.info("  LibPNG: ENABLED (compatible v8+v9)")

    if config.get(CONF_LIBJPEG_TURBO, False):
        cg.add_build_flag("-DLV_USE_LIBJPEG_TURBO=1")
        cg.add(var.set_libjpeg_turbo_enabled(True))
        _LOGGER.info("  LibJPEG Turbo: ENABLED (compatible v8+v9)")

    if config.get(CONF_GIF, False):
        cg.add_build_flag("-DLV_USE_GIF=1")
        cg.add(var.set_gif_enabled(True))
        _LOGGER.info("  GIF: ENABLED (compatible v8+v9)")

    if config.get(CONF_BMP, False):
        cg.add_build_flag("-DLV_USE_BMP=1")
        cg.add(var.set_bmp_enabled(True))
        _LOGGER.info("  BMP: ENABLED (compatible v8+v9)")

    # Widgets
    if config.get(CONF_QRCODE, False):
        cg.add_build_flag("-DLV_USE_QRCODE=1")
        cg.add(var.set_qrcode_enabled(True))
        _LOGGER.info("  QRCode: ENABLED (compatible v8+v9)")

    if config.get(CONF_BARCODE, False):
        cg.add_build_flag("-DLV_USE_BARCODE=1")
        cg.add(var.set_barcode_enabled(True))
        _LOGGER.info("  Barcode: ENABLED (requires LVGL v9)")

    if config.get(CONF_IME_PINYIN, False):
        cg.add_build_flag("-DLV_USE_IME_PINYIN=1")
        cg.add(var.set_ime_pinyin_enabled(True))
        _LOGGER.info("  IME Pinyin: ENABLED (v9 feature)")

    # ========================================
    # PERFORMANCE OPTIMIZATIONS (v8+v9 compatible)
    # ========================================
    if config.get(CONF_DRAW_SW_COMPLEX, False):
        cg.add_build_flag("-DLV_DRAW_SW_COMPLEX=1")
        _LOGGER.info("  Draw SW Complex: ENABLED (compatible v8+v9)")

    asm_type = config.get(CONF_DRAW_SW_ASM, "none")
    if asm_type == "neon":
        cg.add_build_flag("-DLV_DRAW_SW_ASM=LV_DRAW_SW_ASM_NEON")
        _LOGGER.info("  ASM Optimization: NEON (ARM)")
    elif asm_type == "helium":
        cg.add_build_flag("-DLV_DRAW_SW_ASM=LV_DRAW_SW_ASM_HELIUM")
        _LOGGER.info("  ASM Optimization: HELIUM (ARM v8.1-M)")

    if config.get(CONF_SHADOW_CACHE_SIZE, 0) > 0:
        size = config[CONF_SHADOW_CACHE_SIZE]
        cg.add_build_flag(f"-DLV_SHADOW_CACHE_SIZE={size}")
        _LOGGER.info(f"  Shadow Cache: {size} bytes")

    if config.get(CONF_IMG_CACHE_SIZE, 0) > 0:
        size = config[CONF_IMG_CACHE_SIZE]
        cg.add_build_flag(f"-DLV_IMG_CACHE_SIZE={size}")
        _LOGGER.info(f"  Image Cache: {size} bytes")

    _LOGGER.info("LVGL Advanced Features: Configuration complete!")
