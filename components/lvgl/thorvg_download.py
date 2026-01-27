"""
Auto-download ThorVG pour LVGL (comme esp_video_download.py)
Télécharge ThorVG depuis GitHub avec sparse checkout pour LVGL external
"""

import os
import subprocess
import logging
import sys

_LOGGER = logging.getLogger(__name__)


def download_thorvg(components_dir):
    """
    Télécharge ThorVG depuis GitHub vers components/thorvg/
    Méthode simple: git clone --depth=1 (comme esp_video_download.py)
    """
    thorvg_dir = os.path.join(components_dir, "thorvg")

    # Si ThorVG existe déjà, ne rien faire
    if os.path.exists(thorvg_dir) and os.path.isdir(thorvg_dir):
        thorvg_files = os.listdir(thorvg_dir)
        if len(thorvg_files) > 5:  # Si le dossier contient des fichiers
            _LOGGER.info("[ThorVG] Already present, skipping download")
            return True

    _LOGGER.info("[ThorVG] Downloading from GitHub...")
    _LOGGER.info("[ThorVG] Repository: https://github.com/thorvg/thorvg.git")
    _LOGGER.info("[ThorVG] Version: v0.15.16")
    _LOGGER.info("[ThorVG] Target: components/thorvg/")

    try:
        # Clone shallow du tag v0.15.16 (comme esp_video avec --depth=1)
        # Note: --branch avec un tag fonctionne pour checkout direct
        subprocess.run(
            ["git", "clone", "--depth=1", "--branch", "v0.15.16",
             "https://github.com/thorvg/thorvg.git", thorvg_dir],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=300  # 5 minutes max
        )

        _LOGGER.info("[ThorVG] ✅ Download complete!")
        _LOGGER.info(f"[ThorVG] Installed at: {thorvg_dir}")

        # Vérifier que les fichiers importants existent
        inc_dir = os.path.join(thorvg_dir, "inc")
        src_dir = os.path.join(thorvg_dir, "src")

        if not os.path.exists(inc_dir):
            _LOGGER.warning(f"[ThorVG] Warning: inc/ directory not found")
        if not os.path.exists(src_dir):
            _LOGGER.warning(f"[ThorVG] Warning: src/ directory not found")

        return True

    except subprocess.CalledProcessError as e:
        _LOGGER.error(f"[ThorVG] ❌ Download failed: {e}")
        if e.stdout:
            _LOGGER.error(f"[ThorVG] stdout: {e.stdout.decode('utf-8', errors='ignore')}")
        if e.stderr:
            _LOGGER.error(f"[ThorVG] stderr: {e.stderr.decode('utf-8', errors='ignore')}")

        # Nettoyer en cas d'échec
        if os.path.exists(thorvg_dir):
            import shutil
            shutil.rmtree(thorvg_dir, ignore_errors=True)

        return False

    except subprocess.TimeoutExpired:
        _LOGGER.error("[ThorVG] ❌ Download timeout (>5 minutes)")
        if os.path.exists(thorvg_dir):
            import shutil
            shutil.rmtree(thorvg_dir, ignore_errors=True)
        return False

    except Exception as e:
        _LOGGER.error(f"[ThorVG] ❌ Unexpected error: {e}")
        import traceback
        _LOGGER.error(traceback.format_exc())
        return False


def ensure_thorvg_present(components_dir):
    """
    S'assure que ThorVG est présent, le télécharge si nécessaire
    """
    thorvg_dir = os.path.join(components_dir, "thorvg")

    # Vérifier si ThorVG existe
    if os.path.exists(thorvg_dir) and os.path.isdir(thorvg_dir):
        # Vérifier qu'il contient des fichiers sources
        thorvg_files = os.listdir(thorvg_dir)
        if len(thorvg_files) > 5:
            _LOGGER.info("[ThorVG] Found existing installation")
            return True

    # ThorVG n'existe pas, le télécharger
    _LOGGER.info("[ThorVG] Not found, downloading...")
    return download_thorvg(components_dir)
