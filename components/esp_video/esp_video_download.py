"""
Auto-download des dépendances ESP-Video (comme LVGL 9.4)
Télécharge automatiquement esp_h264, esp_cam_sensor, esp_ipa, etc. depuis GitHub
"""

import os
import subprocess
import logging
import hashlib
import json

_LOGGER = logging.getLogger(__name__)

# Configuration des dépendances à télécharger
ESP_VIDEO_DEPENDENCIES = [
    {
        "name": "esp_h264",
        "repo": "https://github.com/espressif/esp-adf-libs.git",
        "tag": "f8baa69620b3e4cd2170098d57b1c2e7e47f9b7c",  # Version stable
        "sparse_paths": ["esp_h264"],
        "description": "Encodeur/décodeur H.264 (OpenH264 + TinyH264)"
    },
    {
        "name": "esp_cam_sensor",
        "repo": "https://github.com/espressif/esp-adf-libs.git",
        "tag": "f8baa69620b3e4cd2170098d57b1c2e7e47f9b7c",
        "sparse_paths": ["esp_cam_sensor"],
        "description": "Drivers caméra (OV5647, SC202CS, OV02C10)"
    },
    {
        "name": "esp_ipa",
        "repo": "https://github.com/espressif/esp-adf-libs.git",
        "tag": "f8baa69620b3e4cd2170098d57b1c2e7e47f9b7c",
        "sparse_paths": ["esp_ipa"],
        "description": "Image Processing Algorithms (AWB, denoise, sharpen)"
    },
    {
        "name": "esp_sccb_intf",
        "repo": "https://github.com/espressif/esp-adf-libs.git",
        "tag": "f8baa69620b3e4cd2170098d57b1c2e7e47f9b7c",
        "sparse_paths": ["esp_sccb_intf"],
        "description": "Interface I2C/SCCB pour caméras"
    },
]


def get_component_cache_dir():
    """Retourne le répertoire de cache pour les composants téléchargés"""
    # Utiliser le répertoire home de l'utilisateur pour le cache
    cache_dir = os.path.expanduser("~/.esphome/esp_video_cache")
    os.makedirs(cache_dir, exist_ok=True)
    return cache_dir


def get_download_state_file():
    """Retourne le fichier de state pour tracker les téléchargements"""
    cache_dir = get_component_cache_dir()
    return os.path.join(cache_dir, "download_state.json")


def load_download_state():
    """Charge l'état des téléchargements"""
    state_file = get_download_state_file()
    if os.path.exists(state_file):
        try:
            with open(state_file, 'r') as f:
                return json.load(f)
        except Exception as e:
            _LOGGER.warning(f"Could not load download state: {e}")
    return {}


def save_download_state(state):
    """Sauvegarde l'état des téléchargements"""
    state_file = get_download_state_file()
    try:
        with open(state_file, 'w') as f:
            json.dump(state, f, indent=2)
    except Exception as e:
        _LOGGER.warning(f"Could not save download state: {e}")


def component_hash(dep):
    """Calcule un hash unique pour une dépendance"""
    key = f"{dep['repo']}:{dep['tag']}:{','.join(dep['sparse_paths'])}"
    return hashlib.md5(key.encode()).hexdigest()[:8]


def is_component_downloaded(dep, target_dir):
    """Vérifie si un composant est déjà téléchargé et à jour"""
    if not os.path.exists(target_dir):
        return False

    # Vérifier si le répertoire contient des fichiers
    if not os.listdir(target_dir):
        return False

    # Vérifier le state file
    state = load_download_state()
    dep_hash = component_hash(dep)
    component_name = dep['name']

    if component_name in state:
        if state[component_name].get('hash') == dep_hash:
            # Même version, déjà téléchargé
            return True

    return False


def download_component_sparse(dep, target_dir):
    """
    Télécharge un composant depuis esp-adf-libs.
    Équivalent à `cg.add_library()` mais pour des repos Git.

    Stratégie :
    1. Clone le repo complet dans un cache (une seule fois)
    2. Copie seulement les composants nécessaires vers target_dir
    """
    import shutil

    component_name = dep['name']
    repo_url = dep['repo']
    tag = dep['tag']
    sparse_paths = dep['sparse_paths']

    _LOGGER.info(f"📥 Downloading {component_name}...")

    # Répertoire de cache pour le clone complet
    cache_dir = get_component_cache_dir()
    repo_cache_dir = os.path.join(cache_dir, "esp-adf-libs")

    try:
        # Étape 1: Cloner le repo dans le cache (si pas déjà fait)
        if not os.path.exists(repo_cache_dir):
            _LOGGER.info(f"   Cloning esp-adf-libs to cache...")
            subprocess.run(
                ["git", "clone", "--depth=1", repo_url, repo_cache_dir],
                check=True,
                capture_output=True,
                text=True
            )
            _LOGGER.info(f"   ✓ Repository cloned to cache")
        else:
            # Pull les dernières modifications
            _LOGGER.debug(f"   Using cached repository")
            try:
                subprocess.run(
                    ["git", "-C", repo_cache_dir, "pull", "--depth=1"],
                    check=False,  # Pas critique si ça échoue
                    capture_output=True,
                    text=True,
                    timeout=10
                )
            except:
                pass  # Ignorer les erreurs de pull

        # Étape 2: Copier le composant depuis le cache vers target_dir
        for sparse_path in sparse_paths:
            src_path = os.path.join(repo_cache_dir, sparse_path)

            if not os.path.exists(src_path):
                _LOGGER.error(f"   ✗ Component path not found in repo: {sparse_path}")
                return False

            # Supprimer le target existant
            if os.path.exists(target_dir):
                shutil.rmtree(target_dir)

            # Copier le composant
            _LOGGER.info(f"   Copying {sparse_path} to {os.path.basename(target_dir)}...")
            shutil.copytree(src_path, target_dir, dirs_exist_ok=True)
            _LOGGER.info(f"   ✓ Copied successfully")

        # Sauvegarder le state
        state = load_download_state()
        state[component_name] = {
            'hash': component_hash(dep),
            'tag': tag,
            'repo': repo_url,
        }
        save_download_state(state)

        _LOGGER.info(f"✅ {component_name} ready")
        return True

    except subprocess.CalledProcessError as e:
        _LOGGER.error(f"❌ Failed to download {component_name}")
        if e.stdout:
            _LOGGER.debug(f"   stdout: {e.stdout}")
        if e.stderr:
            _LOGGER.debug(f"   stderr: {e.stderr}")
        return False
    except Exception as e:
        _LOGGER.error(f"❌ Unexpected error downloading {component_name}: {e}")
        import traceback
        _LOGGER.debug(traceback.format_exc())
        return False


def ensure_esp_video_dependencies(components_dir):
    """
    S'assure que toutes les dépendances ESP-Video sont téléchargées.
    Fonctionne comme `cg.add_library("lvgl/lvgl", "9.4.0")`.

    Args:
        components_dir: Répertoire parent où installer les composants

    Returns:
        True si tout est OK, False sinon
    """
    _LOGGER.info("=" * 60)
    _LOGGER.info("ESP-Video Auto-Download (like LVGL 9.4)")
    _LOGGER.info("=" * 60)

    all_ok = True
    downloaded_count = 0
    cached_count = 0

    for dep in ESP_VIDEO_DEPENDENCIES:
        component_name = dep['name']
        target_dir = os.path.join(components_dir, component_name)

        _LOGGER.info(f"📦 {component_name}: {dep['description']}")

        # Vérifier si déjà téléchargé
        if is_component_downloaded(dep, target_dir):
            _LOGGER.info(f"   ✓ Already downloaded (cached)")
            cached_count += 1
            continue

        # Télécharger
        if download_component_sparse(dep, target_dir):
            downloaded_count += 1
        else:
            _LOGGER.error(f"   ✗ Download failed!")
            all_ok = False

    _LOGGER.info("=" * 60)
    if all_ok:
        if downloaded_count > 0:
            _LOGGER.info(f"✅ Downloaded {downloaded_count} component(s)")
        if cached_count > 0:
            _LOGGER.info(f"📦 Used {cached_count} cached component(s)")
        _LOGGER.info("✅ All ESP-Video dependencies ready!")
    else:
        _LOGGER.error("❌ Some dependencies failed to download")

    _LOGGER.info("=" * 60)

    return all_ok


def clean_esp_video_cache():
    """Nettoie le cache des composants téléchargés (pour debug)"""
    cache_dir = get_component_cache_dir()
    state_file = get_download_state_file()

    if os.path.exists(state_file):
        os.remove(state_file)
        _LOGGER.info(f"Cleaned download state: {state_file}")

    _LOGGER.info(f"Cache directory: {cache_dir}")


# Pour utilisation en ligne de commande (debug)
if __name__ == "__main__":
    import sys
    logging.basicConfig(level=logging.INFO)

    if len(sys.argv) > 1 and sys.argv[1] == "clean":
        clean_esp_video_cache()
    else:
        # Test download
        test_dir = os.path.join(os.path.dirname(__file__), "..", "..")
        ensure_esp_video_dependencies(test_dir)
