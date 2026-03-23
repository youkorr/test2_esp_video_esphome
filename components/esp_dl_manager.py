"""
ESP-DL Download Manager
Downloads esp-dl from GitHub if not present locally.
Used by build scripts (face_detection, yolo11_detection, etc.)
"""

import os
import tarfile
import shutil
import urllib.request
import tempfile

# ESP-DL version and source
ESP_DL_VERSION = "v3.2.3"
ESP_DL_GITHUB_URL = f"https://github.com/espressif/esp-dl/archive/refs/tags/{ESP_DL_VERSION}.tar.gz"
ESP_DL_ARCHIVE_PREFIX = f"esp-dl-{ESP_DL_VERSION.lstrip('v')}"  # e.g. "esp-dl-3.2.3"

# Marker file to track downloaded version
VERSION_FILE = ".esp_dl_version"


def get_esp_dl_dir(components_dir):
    """Return the path to the esp-dl directory."""
    return os.path.join(components_dir, "esp-dl")


def is_esp_dl_available(components_dir):
    """Check if esp-dl is available and matches the expected version."""
    esp_dl_dir = get_esp_dl_dir(components_dir)
    if not os.path.exists(esp_dl_dir):
        return False

    # Check version marker
    version_file = os.path.join(esp_dl_dir, VERSION_FILE)
    if os.path.exists(version_file):
        with open(version_file, 'r') as f:
            installed_version = f.read().strip()
        if installed_version == ESP_DL_VERSION:
            return True
        print(f"[ESP-DL Manager] Version mismatch: installed={installed_version}, expected={ESP_DL_VERSION}")
        return False

    # Directory exists but no version file - assume it's valid (manual install)
    # Check for key subdirectories
    key_dirs = ["dl", "vision", "fbs_loader"]
    for d in key_dirs:
        if not os.path.exists(os.path.join(esp_dl_dir, d)):
            return False
    return True


def download_esp_dl(components_dir):
    """Download and extract esp-dl from GitHub."""
    esp_dl_dir = get_esp_dl_dir(components_dir)

    print(f"[ESP-DL Manager] Downloading esp-dl {ESP_DL_VERSION} from GitHub...")
    print(f"[ESP-DL Manager] URL: {ESP_DL_GITHUB_URL}")

    with tempfile.TemporaryDirectory() as tmp_dir:
        archive_path = os.path.join(tmp_dir, "esp-dl.tar.gz")

        # Download
        try:
            urllib.request.urlretrieve(ESP_DL_GITHUB_URL, archive_path)
        except Exception as e:
            raise RuntimeError(f"[ESP-DL Manager] Failed to download esp-dl: {e}")

        print(f"[ESP-DL Manager] Downloaded, extracting...")

        # Extract
        with tarfile.open(archive_path, 'r:gz') as tar:
            tar.extractall(path=tmp_dir)

        # The archive extracts to esp-dl-3.2.3/esp-dl/ (component is in subdirectory)
        extracted_component = os.path.join(tmp_dir, ESP_DL_ARCHIVE_PREFIX, "esp-dl")
        if not os.path.exists(extracted_component):
            # Fallback: maybe the structure is flat
            extracted_component = os.path.join(tmp_dir, ESP_DL_ARCHIVE_PREFIX)

        if not os.path.exists(extracted_component):
            raise RuntimeError(
                f"[ESP-DL Manager] Expected directory not found after extraction. "
                f"Contents: {os.listdir(tmp_dir)}"
            )

        # Remove old esp-dl if exists
        if os.path.exists(esp_dl_dir):
            print(f"[ESP-DL Manager] Removing old esp-dl directory...")
            shutil.rmtree(esp_dl_dir)

        # Move to components/esp-dl/
        shutil.copytree(extracted_component, esp_dl_dir)

        # Write version marker
        version_file = os.path.join(esp_dl_dir, VERSION_FILE)
        with open(version_file, 'w') as f:
            f.write(ESP_DL_VERSION)

    print(f"[ESP-DL Manager] esp-dl {ESP_DL_VERSION} installed to {esp_dl_dir}")
    return esp_dl_dir


def ensure_esp_dl(components_dir):
    """Ensure esp-dl is available. Download if necessary.

    Call this from build scripts:
        import sys
        sys.path.insert(0, os.path.dirname(__file__))
        from esp_dl_manager import ensure_esp_dl
        esp_dl_dir = ensure_esp_dl(parent_components_dir)
    """
    if is_esp_dl_available(components_dir):
        esp_dl_dir = get_esp_dl_dir(components_dir)
        print(f"[ESP-DL Manager] esp-dl found at {esp_dl_dir}")
        return esp_dl_dir

    return download_esp_dl(components_dir)
