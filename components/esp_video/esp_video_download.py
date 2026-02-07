"""
Auto-download des dépendances ESP-Video (comme LVGL 9.4)
Télécharge automatiquement esp_cam_sensor, esp_ipa, etc. depuis GitHub
Avec barre de progression visuelle comme PlatformIO
"""

import os
import subprocess
import logging
import hashlib
import json
import sys
import time
import threading

_LOGGER = logging.getLogger(__name__)


class ProgressBar:
    """Barre de progression comme PlatformIO"""

    def __init__(self, total=100, width=40, prefix=""):
        self.total = total
        self.width = width
        self.prefix = prefix
        self.current = 0
        self._lock = threading.Lock()

    def update(self, current):
        """Met à jour la progression"""
        with self._lock:
            self.current = min(current, self.total)
            self._render()

    def _render(self):
        """Affiche la barre de progression"""
        percent = int((self.current / self.total) * 100)
        filled = int((self.current / self.total) * self.width)
        bar = '#' * filled + '-' * (self.width - filled)

        # Format comme PlatformIO: "Downloading  [####---]  XX%"
        # Afficher seulement les jalons importants (0%, 25%, 50%, 75%, 100%)
        if percent in [0, 25, 50, 75, 100] or self.current >= self.total:
            _LOGGER.info(f'{self.prefix}  [{bar}]  {percent:3d}%')

    def finish(self):
        """Termine la barre de progression"""
        self.update(self.total)

# Configuration des dépendances
# NOTE: esp_cam_sensor, esp_ipa, et esp_sccb_intf sont des composants ESP-IDF
# disponibles sur le Component Registry ou GitHub, mais pas dans esp-adf-libs.
# Pour l'instant, on vérifie juste qu'ils existent localement.

ESP_VIDEO_DEPENDENCIES = [
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
    Télécharge un composant depuis esp-adf-libs avec barre de progression.
    Équivalent à `cg.add_library()` mais pour des repos Git.

    Stratégie :
    1. Clone le repo complet dans un cache (une seule fois) - avec progression
    2. Copie seulement les composants nécessaires vers target_dir - avec progression
    """
    import shutil

    component_name = dep['name']
    repo_url = dep['repo']
    tag = dep['tag']
    sparse_paths = dep['sparse_paths']

    _LOGGER.info(f"Installing {component_name}")

    # Répertoire de cache pour le clone complet
    cache_dir = get_component_cache_dir()
    repo_cache_dir = os.path.join(cache_dir, "esp-adf-libs")

    try:
        # Étape 1: Cloner le repo dans le cache (si pas déjà fait)
        if not os.path.exists(repo_cache_dir):
            # Afficher la progression du téléchargement
            progress = ProgressBar(total=100, width=40, prefix="Downloading")
            progress.update(0)

            # Fonction pour simuler la progression pendant le clone
            def update_clone_progress():
                for i in range(0, 100, 5):
                    progress.update(i)
                    time.sleep(0.1)

            # Lancer le thread de progression
            progress_thread = threading.Thread(target=update_clone_progress, daemon=True)
            progress_thread.start()

            # Clone Git en arrière-plan
            result = subprocess.run(
                ["git", "clone", "--depth=1", "--progress", repo_url, repo_cache_dir],
                check=True,
                capture_output=True,
                text=True
            )

            # Finir la barre de progression
            progress.finish()

        else:
            # Déjà en cache, pas besoin de re-télécharger
            _LOGGER.info("Using cached repository")

        # Étape 2: Copier le composant depuis le cache vers target_dir
        for sparse_path in sparse_paths:
            src_path = os.path.join(repo_cache_dir, sparse_path)

            if not os.path.exists(src_path):
                _LOGGER.error(f"Component path not found in repo: {sparse_path}")
                return False

            # Supprimer le target existant
            if os.path.exists(target_dir):
                shutil.rmtree(target_dir)

            # Afficher la progression du unpacking/copie
            progress = ProgressBar(total=100, width=40, prefix="Unpacking")
            progress.update(0)

            # Fonction pour mettre à jour la progression pendant la copie
            def copy_with_progress(src, dst):
                """Copie avec progression"""
                # Compter le nombre total de fichiers
                total_files = sum(len(files) for _, _, files in os.walk(src))
                copied_files = 0

                def copy_function(src_file, dst_file):
                    nonlocal copied_files
                    shutil.copy2(src_file, dst_file)
                    copied_files += 1
                    percent = int((copied_files / max(total_files, 1)) * 100)
                    progress.update(percent)

                # Copier avec la fonction custom
                shutil.copytree(src, dst, copy_function=copy_function, dirs_exist_ok=True)

            # Copier avec progression
            copy_with_progress(src_path, target_dir)
            progress.finish()

        # Sauvegarder le state
        state = load_download_state()
        state[component_name] = {
            'hash': component_hash(dep),
            'tag': tag,
            'repo': repo_url,
        }
        save_download_state(state)

        return True

    except subprocess.CalledProcessError as e:
        _LOGGER.error(f"Failed to download {component_name}")
        if e.stdout:
            _LOGGER.debug(f"stdout: {e.stdout}")
        if e.stderr:
            _LOGGER.debug(f"stderr: {e.stderr}")
        return False
    except Exception as e:
        _LOGGER.error(f"Unexpected error downloading {component_name}: {e}")
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
    all_ok = True
    downloaded_count = 0
    local_count = 0
    missing_components = []

    for dep in ESP_VIDEO_DEPENDENCIES:
        component_name = dep['name']
        target_dir = os.path.join(components_dir, component_name)
        has_repo = dep['repo'] is not None

        # Vérifier si déjà présent localement
        if is_component_downloaded(dep, target_dir):
            # Silencieux si déjà présent (comme PlatformIO)
            local_count += 1
            continue

        # Si pas de repo, on ne peut pas télécharger
        if not has_repo:
            _LOGGER.warning(f"Component {component_name} not found locally and no download source available")
            if dep.get('required', True):
                missing_components.append(component_name)
                all_ok = False
            continue

        # Télécharger depuis le repo (avec barre de progression)
        if download_component_sparse(dep, target_dir):
            downloaded_count += 1
        else:
            _LOGGER.error(f"Failed to download {component_name}")
            if dep.get('required', True):
                missing_components.append(component_name)
                all_ok = False

    # Afficher un résumé seulement si des composants ont été téléchargés
    if downloaded_count > 0:
        _LOGGER.info(f"Downloaded {downloaded_count} component(s)")

    if not all_ok:
        _LOGGER.error(f"Missing required components: {', '.join(missing_components)}")
        _LOGGER.error(f"Please ensure these components are in: {components_dir}/")

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
