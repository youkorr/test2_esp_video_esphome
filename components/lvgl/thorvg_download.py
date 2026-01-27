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
    Utilise git sparse-checkout pour n'avoir que les sources nécessaires
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
    _LOGGER.info("[ThorVG] Target: components/thorvg/")

    try:
        # Créer le dossier temporaire
        os.makedirs(thorvg_dir, exist_ok=True)

        # Initialiser un repo git
        subprocess.run(
            ["git", "init"],
            cwd=thorvg_dir,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        # Ajouter le remote
        subprocess.run(
            ["git", "remote", "add", "origin", "https://github.com/thorvg/thorvg.git"],
            cwd=thorvg_dir,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        # Activer sparse-checkout
        subprocess.run(
            ["git", "config", "core.sparseCheckout", "true"],
            cwd=thorvg_dir,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        # Configurer les paths à télécharger (tout le repo)
        sparse_file = os.path.join(thorvg_dir, ".git", "info", "sparse-checkout")
        os.makedirs(os.path.dirname(sparse_file), exist_ok=True)
        with open(sparse_file, "w") as f:
            f.write("/*\n")  # Tout télécharger

        _LOGGER.info("[ThorVG] Fetching v0.15.16...")

        # Fetch la version spécifique (v0.15.16)
        subprocess.run(
            ["git", "fetch", "--depth=1", "origin", "v0.15.16"],
            cwd=thorvg_dir,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        # Checkout la version
        subprocess.run(
            ["git", "checkout", "v0.15.16"],
            cwd=thorvg_dir,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        _LOGGER.info("[ThorVG] ✅ Download complete!")
        _LOGGER.info(f"[ThorVG] Installed at: {thorvg_dir}")

        return True

    except subprocess.CalledProcessError as e:
        _LOGGER.error(f"[ThorVG] ❌ Download failed: {e}")
        _LOGGER.error(f"[ThorVG] stdout: {e.stdout}")
        _LOGGER.error(f"[ThorVG] stderr: {e.stderr}")

        # Nettoyer en cas d'échec
        if os.path.exists(thorvg_dir):
            import shutil
            shutil.rmtree(thorvg_dir, ignore_errors=True)

        return False

    except Exception as e:
        _LOGGER.error(f"[ThorVG] ❌ Unexpected error: {e}")
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
