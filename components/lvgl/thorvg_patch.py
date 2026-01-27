"""
Patch ThorVG v0.15.16 pour compatibilité LVGL 9.4.0
Ajoute TVG_BLEND_METHOD_SRCOVER manquant dans l'enum
"""

import os
import logging

_LOGGER = logging.getLogger(__name__)


def patch_thorvg_for_lvgl(thorvg_dir):
    """
    Patch thorvg_capi.h pour ajouter TVG_BLEND_METHOD_SRCOVER
    LVGL 9.4.0 a ajouté cette constante dans sa version bundled
    """
    thorvg_capi_h = os.path.join(thorvg_dir, "src", "bindings", "capi", "thorvg_capi.h")

    if not os.path.exists(thorvg_capi_h):
        _LOGGER.warning(f"[ThorVG Patch] File not found: {thorvg_capi_h}")
        return False

    try:
        # Lire le fichier
        with open(thorvg_capi_h, "r", encoding="utf-8") as f:
            content = f.read()

        # Vérifier si déjà patché
        if "TVG_BLEND_METHOD_SRCOVER" in content:
            _LOGGER.info("[ThorVG Patch] Already patched, skipping")
            return True

        # Trouver l'enum et ajouter SRCOVER après OVERLAY
        original = """    TVG_BLEND_METHOD_OVERLAY,           ///< Combines Multiply and Screen blend modes. (2 * S * D) if (2 * D < Da), otherwise (Sa * Da) - 2 * (Da - S) * (Sa - D)
    TVG_BLEND_METHOD_DARKEN,"""

        patched = """    TVG_BLEND_METHOD_OVERLAY,           ///< Combines Multiply and Screen blend modes. (2 * S * D) if (2 * D < Da), otherwise (Sa * Da) - 2 * (Da - S) * (Sa - D)
    TVG_BLEND_METHOD_SRCOVER,           ///< Replace the bottom layer with the top layer. (Added for LVGL 9.4.0 compatibility)
    TVG_BLEND_METHOD_DARKEN,"""

        if original not in content:
            _LOGGER.error("[ThorVG Patch] Pattern not found, ThorVG version may have changed")
            return False

        # Appliquer le patch
        content = content.replace(original, patched)

        # Écrire le fichier patché
        with open(thorvg_capi_h, "w", encoding="utf-8") as f:
            f.write(content)

        _LOGGER.info("[ThorVG Patch] Successfully patched thorvg_capi.h")
        _LOGGER.info("[ThorVG Patch] Added TVG_BLEND_METHOD_SRCOVER for LVGL 9.4.0")

        return True

    except Exception as e:
        _LOGGER.error(f"[ThorVG Patch] Failed to patch thorvg_capi.h: {e}")
        import traceback
        _LOGGER.error(traceback.format_exc())
        return False


def patch_thorvg_identity_conflict(thorvg_dir):
    """
    Patch tvgSwFill.cpp pour résoudre le conflit avec std::identity (C++20)
    GCC 14.2.0 introduit std::identity qui entre en conflit avec tvg::identity
    """
    tvgSwFill_cpp = os.path.join(thorvg_dir, "src", "renderer", "sw_engine", "tvgSwFill.cpp")

    if not os.path.exists(tvgSwFill_cpp):
        _LOGGER.warning(f"[ThorVG Patch] File not found: {tvgSwFill_cpp}")
        return False

    try:
        # Lire le fichier
        with open(tvgSwFill_cpp, "r", encoding="utf-8") as f:
            content = f.read()

        # Vérifier si déjà patché
        if "tvg::identity" in content:
            _LOGGER.info("[ThorVG Patch] tvgSwFill.cpp already patched (identity conflict)")
            return True

        # Patcher les deux occurrences de identity() pour utiliser tvg::identity()
        # Line 224: bool isTransformation = !identity((const Matrix*)(&gradTransform));
        # Line 293: bool isTransformation = !identity((const Matrix*)(&gradTransform));

        content = content.replace(
            "!identity((const Matrix*)",
            "!tvg::identity((const Matrix*)"
        )

        # Écrire le fichier patché
        with open(tvgSwFill_cpp, "w", encoding="utf-8") as f:
            f.write(content)

        _LOGGER.info("[ThorVG Patch] Successfully patched tvgSwFill.cpp")
        _LOGGER.info("[ThorVG Patch] Fixed identity() namespace conflict with C++20 std::identity")

        return True

    except Exception as e:
        _LOGGER.error(f"[ThorVG Patch] Failed to patch tvgSwFill.cpp: {e}")
        import traceback
        _LOGGER.error(traceback.format_exc())
        return False
