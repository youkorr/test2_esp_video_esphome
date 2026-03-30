"""
ESP-DL downloader for ESPHome codegen phase.
Downloads esp-dl via git sparse checkout BEFORE compilation starts,
so that -I include paths point to real directories.

Called from __init__.py of detection components.
"""

import os
import subprocess
import logging

_LOGGER = logging.getLogger(__name__)

ESP_DL_REPO = "https://github.com/espressif/esp-dl.git"
ESP_DL_TAG = "v3.2.3"
SPARSE_DIRS = ["dl", "fbs_loader", "vision"]


def _run_git(args, cwd, timeout=120):
    """Run git command, return (success, stdout)."""
    try:
        result = subprocess.run(
            ["git"] + args, cwd=cwd,
            capture_output=True, text=True, timeout=timeout
        )
        return result.returncode == 0, result.stdout
    except Exception as e:
        _LOGGER.warning("git %s failed: %s", " ".join(args), e)
        return False, ""


def download_esp_dl(build_path):
    """Download esp-dl to build_path/.espdl_cache/esp-dl/.

    Uses sparse checkout to only get dl/, fbs_loader/, vision/.
    Falls back to full shallow clone if sparse fails.
    Skips download if already present.

    Args:
        build_path: ESPHome build path (e.g. /data/build/p4mini)

    Returns:
        Path to esp-dl root (containing dl/, vision/, etc.)
    """
    download_dir = os.path.join(str(build_path), ".espdl_cache", "esp-dl")

    # Already downloaded?
    if os.path.isdir(download_dir) and os.path.exists(os.path.join(download_dir, "dl")):
        _LOGGER.info("ESP-DL already present: %s", download_dir)
        return download_dir

    _LOGGER.info("Downloading ESP-DL %s (sparse: %s)...", ESP_DL_TAG, SPARSE_DIRS)

    # Try sparse checkout
    try:
        os.makedirs(download_dir, exist_ok=True)
        _run_git(["init"], cwd=download_dir)
        _run_git(["remote", "add", "origin", ESP_DL_REPO], cwd=download_dir)
        _run_git(["config", "core.sparseCheckout", "true"], cwd=download_dir)

        sparse_file = os.path.join(download_dir, ".git", "info", "sparse-checkout")
        os.makedirs(os.path.dirname(sparse_file), exist_ok=True)
        with open(sparse_file, "w") as f:
            for d in SPARSE_DIRS:
                f.write(f"{d}/\n")

        ok, _ = _run_git(
            ["fetch", "--depth=1", "origin", f"tags/{ESP_DL_TAG}"],
            cwd=download_dir, timeout=120
        )
        if ok:
            ok2, _ = _run_git(["checkout", "FETCH_HEAD"], cwd=download_dir)
            if ok2 and os.path.exists(os.path.join(download_dir, "dl")):
                _LOGGER.info("ESP-DL sparse checkout OK: %s", download_dir)
                return download_dir

        _LOGGER.warning("Sparse checkout failed, trying full clone...")
    except Exception as e:
        _LOGGER.warning("Sparse checkout error: %s, trying full clone...", e)

    # Fallback: full shallow clone
    import shutil
    if os.path.exists(download_dir):
        shutil.rmtree(download_dir)

    try:
        subprocess.run(
            ["git", "clone", "--depth=1", "--branch", ESP_DL_TAG,
             ESP_DL_REPO, download_dir],
            capture_output=True, text=True, timeout=300, check=True
        )
        # Remove unnecessary dirs
        for unwanted in ["audio", "examples", "docs", "test"]:
            unwanted_path = os.path.join(download_dir, unwanted)
            if os.path.exists(unwanted_path):
                shutil.rmtree(unwanted_path)

        _LOGGER.info("ESP-DL full clone OK: %s", download_dir)
        return download_dir
    except Exception as e:
        _LOGGER.error("Failed to download ESP-DL: %s", e)
        raise


def get_include_flags(esp_dl_dir, isa_target="esp32p4"):
    """Return list of -I flags for esp-dl include directories.

    Args:
        esp_dl_dir: Path to esp-dl root
        isa_target: ISA target (esp32p4, tie728, xtensa)

    Returns:
        List of -I flag strings
    """
    subdirs = [
        "dl", "dl/tool/include", f"dl/tool/isa/{isa_target}",
        "dl/tool/src", "dl/tensor/include", "dl/tensor/src",
        "dl/base", "dl/base/isa", f"dl/base/isa/{isa_target}",
        "dl/math/include", "dl/math/src", "dl/model/include",
        "dl/model/src", "dl/module/include", "dl/module/src",
        "fbs_loader/include", f"fbs_loader/lib/{isa_target}", "fbs_loader/src",
        "vision/detect", "vision/image", "vision/image/isa",
        f"vision/image/isa/{isa_target}", "vision/recognition",
        "vision/classification",
    ]
    flags = []
    for subdir in subdirs:
        path = os.path.join(esp_dl_dir, subdir)
        flags.append(f"-I{path}")
    return flags
