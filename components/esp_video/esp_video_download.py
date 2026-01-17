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

# Configuration des dépendances
# NOTE: esp_cam_sensor, esp_ipa, et esp_sccb_intf sont des composants ESP-IDF
# disponibles sur le Component Registry ou GitHub, mais pas dans esp-adf-libs.
# Pour l'instant, on vérifie juste qu'ils existent localement.

ESP_VIDEO_DEPENDENCIES = [
    {
        "name": "esp_h264",
        "repo": "https://github.com/espressif/esp-adf-libs.git",
        "tag": "master",  # Utiliser master pour la dernière version
        "sparse_paths": ["esp_h264"],
        "description": "Encodeur/décodeur H.264 (OpenH264 + TinyH264)",
        "required": True
    },
    {
        "name": "esp_cam_sensor",
        "repo": None,  # Composant ESP-IDF - doit être présent localement
        "tag": None,
        "sparse_paths": [],
        "description": "Drivers caméra (OV5647, SC202CS, OV02C10)",
        "required": True
    },
    {
        "name": "esp_ipa",
        "repo": None,  # Composant ESP-IDF - doit être présent localement
        "tag": None,
        "sparse_paths": [],
        "description": "Image Processing Algorithms (AWB, denoise, sharpen)",
        "required": True
    },
    {
        "name": "esp_sccb_intf",
        "repo": None,  # Composant ESP-IDF - doit être présent localement
        "tag": None,
        "sparse_paths": [],
        "description": "Interface I2C/SCCB pour caméras",
        "required": True
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
    try:
        dir_contents = os.listdir(target_dir)
        if not dir_contents:
            return False
    except Exception:
        return False

    # Si le répertoire existe et contient des fichiers, c'est OK
    # (même si le state file n'existe pas - compatibilité avec composants existants)
    _LOGGER.debug(f"Component {dep['name']} found locally at {target_dir}")

    # Vérifier le state file pour la version (optionnel)
    state = load_download_state()
    dep_hash = component_hash(dep)
    component_name = dep['name']

    if component_name in state:
        if state[component_name].get('hash') == dep_hash:
            _LOGGER.debug(f"  → Version matches (hash: {dep_hash})")
        else:
            _LOGGER.debug(f"  → Different version in state, but keeping local component")

    # Retourner True si le composant existe localement (peu importe le state)
    return True


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
    S'assure que toutes les dépendances ESP-Video sont présentes.
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
    local_count = 0
    missing_components = []

    for dep in ESP_VIDEO_DEPENDENCIES:
        component_name = dep['name']
        target_dir = os.path.join(components_dir, component_name)
        has_repo = dep['repo'] is not None

        _LOGGER.info(f"📦 {component_name}: {dep['description']}")

        # Vérifier si déjà présent localement
        if is_component_downloaded(dep, target_dir):
            _LOGGER.info(f"   ✓ Found locally")
            local_count += 1
            continue

        # Si pas de repo, on ne peut pas télécharger
        if not has_repo:
            _LOGGER.warning(f"   ⚠️ Not found locally and no download source available")
            if dep.get('required', True):
                missing_components.append(component_name)
                all_ok = False
            continue

        # Télécharger depuis le repo
        _LOGGER.info(f"   Downloading from {dep['repo']}...")
        if download_component_sparse(dep, target_dir):
            downloaded_count += 1
        else:
            _LOGGER.error(f"   ✗ Download failed!")
            if dep.get('required', True):
                missing_components.append(component_name)
                all_ok = False

    _LOGGER.info("=" * 60)
    if all_ok:
        if downloaded_count > 0:
            _LOGGER.info(f"✅ Downloaded {downloaded_count} component(s)")
        if local_count > 0:
            _LOGGER.info(f"📦 Found {local_count} local component(s)")
        _LOGGER.info("✅ All ESP-Video dependencies ready!")
    else:
        _LOGGER.error(f"❌ Missing required components: {', '.join(missing_components)}")
        _LOGGER.error(f"   Please ensure these components are in: {components_dir}/")

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
