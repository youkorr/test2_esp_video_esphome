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
    Patch ThorVG files pour résoudre le conflit avec std::identity (C++20)
    GCC 14.2.0 introduit std::identity qui entre en conflit avec tvg::identity
    """
    # Liste des fichiers à patcher
    files_to_patch = [
        os.path.join(thorvg_dir, "src", "renderer", "sw_engine", "tvgSwFill.cpp"),
        os.path.join(thorvg_dir, "src", "loaders", "svg", "tvgSvgSceneBuilder.cpp"),
        os.path.join(thorvg_dir, "src", "loaders", "lottie", "tvgLottieBuilder.cpp"),
    ]

    patched_count = 0
    for file_path in files_to_patch:
        if not os.path.exists(file_path):
            _LOGGER.warning(f"[ThorVG Patch] File not found: {file_path}")
            continue

        try:
            # Lire le fichier
            with open(file_path, "r", encoding="utf-8") as f:
                content = f.read()

            # Vérifier si déjà patché
            if "tvg::identity" in content:
                continue

            # Patcher toutes les occurrences de identity() pour utiliser tvg::identity()
            # Patterns à patcher:
            # - !identity((const Matrix*)   (conditional checks)
            # - identity((const Matrix*)    (function calls with cast)
            # - identity(&matrix)           (direct variable calls)
            modified = False

            if "!identity((const Matrix*)" in content:
                content = content.replace(
                    "!identity((const Matrix*)",
                    "!tvg::identity((const Matrix*)"
                )
                modified = True

            if " identity((const Matrix*)" in content:
                content = content.replace(
                    " identity((const Matrix*)",
                    " tvg::identity((const Matrix*)"
                )
                modified = True

            if "identity(&" in content:
                content = content.replace(
                    "identity(&",
                    "tvg::identity(&"
                )
                modified = True

            if modified:
                # Écrire le fichier patché
                with open(file_path, "w", encoding="utf-8") as f:
                    f.write(content)
                patched_count += 1

        except Exception as e:
            _LOGGER.error(f"[ThorVG Patch] Failed to patch {os.path.basename(file_path)}: {e}")
            return False

    if patched_count > 0:
        _LOGGER.info(f"[ThorVG Patch] Successfully patched {patched_count} files for identity() conflict")
        _LOGGER.info("[ThorVG Patch] Fixed namespace conflict with C++20 std::identity")

    return True
