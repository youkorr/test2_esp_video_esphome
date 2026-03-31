"""
Shared ESP-DL download + compilation.
Called by detection component build scripts (face_detection, yolov11, yolo11_detection).

Downloads esp-dl via sparse git checkout (only dl/, fbs_loader/, vision/).
Compiles ESP-DL sources ONCE into a single shared libespdl_shared.a.
Subsequent calls detect the existing library and skip.
"""

import os
import glob
import subprocess

# Marker to prevent double compilation within the same SCons run
_ESPDL_BUILT_KEY = "_ESPDL_SHARED_LIB_BUILT"
_ESPDL_DIR_KEY = "_ESPDL_SHARED_DIR"

ESP_DL_REPO = "https://github.com/espressif/esp-dl.git"
ESP_DL_TAG = "v3.2.3"

# Only these top-level dirs are needed for detection/recognition
SPARSE_DIRS = ["dl", "fbs_loader", "vision"]

# Dirs to exclude from compilation
EXCLUDE_DIRS = {"audio", "examples", "docs", "test", "speech"}

# Files to exclude from compilation
EXCLUDE_FILES = {
    "dl_base_dotprod.cpp",   # Requires esp_dsp.h — replaced by dl_base_dotprod_no_dsp.cpp
    "dl_image_jpeg.cpp",     # JPEG not needed
    "dl_image_bmp.cpp",      # BMP not needed
}


def _resolve_esp_dl_root(path):
    """Handle nested esp-dl/esp-dl/ repo structure. Returns path containing dl/."""
    if os.path.isdir(os.path.join(path, "dl")):
        return path
    inner = os.path.join(path, "esp-dl")
    if os.path.isdir(os.path.join(inner, "dl")):
        return inner
    return path


def _download_esp_dl(download_dir):
    """Download esp-dl via sparse checkout (only dl/, fbs_loader/, vision/).
    Falls back to full clone + delete if sparse checkout fails.

    Returns path to esp-dl root (containing dl/, vision/, etc.)
    """
    # Check both direct and nested structure
    resolved = _resolve_esp_dl_root(download_dir)
    if os.path.isdir(resolved) and os.path.exists(os.path.join(resolved, "dl")):
        print(f"[ESP-DL Download] Already present: {resolved}")
        return resolved

    print(f"[ESP-DL Download] Downloading esp-dl {ESP_DL_TAG} (sparse: {SPARSE_DIRS})...")

    # Try sparse checkout first
    try:
        os.makedirs(download_dir, exist_ok=True)
        subprocess.run(["git", "init"], cwd=download_dir,
                        capture_output=True, check=True)
        subprocess.run(["git", "remote", "add", "origin", ESP_DL_REPO],
                        cwd=download_dir, capture_output=True, check=True)
        subprocess.run(["git", "config", "core.sparseCheckout", "true"],
                        cwd=download_dir, capture_output=True, check=True)

        sparse_file = os.path.join(download_dir, ".git", "info", "sparse-checkout")
        os.makedirs(os.path.dirname(sparse_file), exist_ok=True)
        with open(sparse_file, "w") as f:
            for d in SPARSE_DIRS:
                f.write(f"{d}/\n")

        result = subprocess.run(
            ["git", "fetch", "--depth=1", "origin", f"tags/{ESP_DL_TAG}"],
            cwd=download_dir, capture_output=True, text=True, timeout=120
        )
        if result.returncode == 0:
            subprocess.run(["git", "checkout", "FETCH_HEAD"],
                            cwd=download_dir, capture_output=True, check=True)
            resolved = _resolve_esp_dl_root(download_dir)
            if os.path.exists(os.path.join(resolved, "dl")):
                print(f"[ESP-DL Download] Sparse checkout OK: {resolved}")
                return resolved

        print(f"[ESP-DL Download] Sparse checkout failed, trying full clone...")
    except Exception as e:
        print(f"[ESP-DL Download] Sparse checkout error: {e}, trying full clone...")

    # Fallback: full shallow clone
    import shutil
    if os.path.exists(download_dir):
        shutil.rmtree(download_dir)

    try:
        subprocess.run(
            ["git", "clone", "--depth=1", "--branch", ESP_DL_TAG, ESP_DL_REPO, download_dir],
            capture_output=True, text=True, timeout=300, check=True
        )
        resolved = _resolve_esp_dl_root(download_dir)
        # Remove unnecessary dirs to save space
        for unwanted in ["audio", "examples", "docs", "test"]:
            unwanted_path = os.path.join(resolved, unwanted)
            if os.path.exists(unwanted_path):
                shutil.rmtree(unwanted_path)
                print(f"[ESP-DL Download] Removed {unwanted}/")

        print(f"[ESP-DL Download] Full clone OK: {resolved}")
        return resolved
    except Exception as e:
        print(f"[ESP-DL Download] ERROR: {e}")
        raise FileNotFoundError(f"Failed to download esp-dl: {e}")


def get_esp_dl_dir(env):
    """Get or download esp-dl. Returns path to esp-dl root."""
    # Return cached path if already resolved
    cached = env.get(_ESPDL_DIR_KEY)
    if cached:
        return cached

    # Try to find existing esp-dl
    project_dir = env.get("PROJECT_DIR", "")

    # 1. Check PlatformIO libdeps (if someone still uses add_library)
    pioenv = env.get("PIOENV", "")
    for base in [
        os.path.join(project_dir, ".piolibdeps", pioenv, "esp-dl"),
        os.path.join(project_dir, ".pio", "libdeps", pioenv, "esp-dl"),
    ]:
        resolved = _resolve_esp_dl_root(base)
        if os.path.isdir(resolved) and os.path.exists(os.path.join(resolved, "dl")):
            print(f"[ESP-DL] Found in libdeps: {resolved}")
            env[_ESPDL_DIR_KEY] = resolved
            return resolved

    # 2. Download to project build dir
    download_dir = os.path.join(project_dir, ".espdl_cache", "esp-dl")
    result = _download_esp_dl(download_dir)
    env[_ESPDL_DIR_KEY] = result
    return result


def build_espdl(env, esp_dl_dir=None, isa_target="esp32p4"):
    """Compile ESP-DL sources into a shared static library.

    Args:
        env: PlatformIO SCons environment
        esp_dl_dir: Path to esp-dl root (if None, auto-downloads)
        isa_target: ISA target (esp32p4, tie728, xtensa)

    Returns:
        Path to esp-dl directory.
    """
    # Skip if already built in this SCons run
    if env.get(_ESPDL_BUILT_KEY, False):
        esp_dl_dir = env.get(_ESPDL_DIR_KEY, esp_dl_dir)
        print("[ESP-DL Shared] Already compiled in this build, skipping")
        return esp_dl_dir

    # Get or download esp-dl
    if esp_dl_dir is None or not os.path.exists(esp_dl_dir):
        esp_dl_dir = get_esp_dl_dir(env)

    if not os.path.exists(esp_dl_dir):
        print(f"[ESP-DL Shared] ERROR: esp-dl not found at {esp_dl_dir}")
        return esp_dl_dir

    env[_ESPDL_DIR_KEY] = esp_dl_dir

    # ====================================================================
    # Include paths
    # ====================================================================
    esp_dl_include_dirs = [
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

    inc_count = 0
    for inc_dir in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc_dir)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])
            inc_count += 1
    print(f"[ESP-DL Shared] {inc_count} include paths added")

    # ====================================================================
    # Source files (only dl/, fbs_loader/, vision/)
    # ====================================================================
    esp_dl_source_dirs = [
        "dl/tensor/src",
        "dl/model/src",
        "dl/module/src",
        "dl/tool/src",
        "dl/math/src",
        "fbs_loader/src",
        "vision/image",
        "vision/detect",
        "vision/recognition",
    ]

    sources = []

    for src_dir in esp_dl_source_dirs:
        src_dir_path = os.path.join(esp_dl_dir, src_dir)
        if not os.path.exists(src_dir_path):
            continue
        if src_dir.startswith("vision/"):
            for src_file in glob.glob(os.path.join(src_dir_path, "**", "*.cpp"), recursive=True):
                fname = os.path.basename(src_file)
                rel = os.path.relpath(src_file, esp_dl_dir)
                parts = set(rel.split(os.sep))
                if parts & EXCLUDE_DIRS:
                    continue
                if fname not in EXCLUDE_FILES:
                    sources.append(src_file)
        else:
            for src_file in glob.glob(os.path.join(src_dir_path, "*.cpp")):
                fname = os.path.basename(src_file)
                if fname not in EXCLUDE_FILES:
                    sources.append(src_file)

    # dl/base/*.cpp (exclude dl_base_dotprod.cpp — needs esp_dsp.h)
    dl_base_dir = os.path.join(esp_dl_dir, "dl", "base")
    if os.path.exists(dl_base_dir):
        for src_file in glob.glob(os.path.join(dl_base_dir, "*.cpp")):
            fname = os.path.basename(src_file)
            if fname not in EXCLUDE_FILES:
                sources.append(src_file)

    # ISA-specific assembly/source
    for isa_dir in [f"dl/base/isa/{isa_target}", f"dl/tool/isa/{isa_target}",
                     f"vision/image/isa/{isa_target}"]:
        isa_path = os.path.join(esp_dl_dir, isa_dir)
        if os.path.exists(isa_path):
            for ext in ["*.cpp", "*.S", "*.c"]:
                for src_file in glob.glob(os.path.join(isa_path, ext)):
                    sources.append(src_file)

    # Pre-compiled static library (fbs_loader)
    fbs_lib = os.path.join(esp_dl_dir, "fbs_loader", "lib", isa_target, "libfbs_model.a")
    if os.path.exists(fbs_lib):
        env.Append(LIBPATH=[os.path.dirname(fbs_lib)])
        env.Append(LIBS=["fbs_model"])
        print("[ESP-DL Shared] + libfbs_model.a")

    print(f"[ESP-DL Shared] {len(sources)} source files to compile")

    # ====================================================================
    # Compile into shared static library
    # ====================================================================
    if sources:
        objects = []
        for src_file in sources:
            try:
                obj = env.Object(src_file)
                objects.extend(obj)
            except Exception as e:
                print(f"[ESP-DL Shared] Failed: {os.path.basename(src_file)}: {e}")

        if objects:
            lib = env.StaticLibrary(
                os.path.join("$BUILD_DIR", "libespdl_shared"),
                objects
            )
            env.Prepend(LIBS=[lib])
            if '-Wl,--start-group' not in env.get('_LIBFLAGS', ''):
                env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
            print(f"[ESP-DL Shared] libespdl_shared.a created ({len(objects)} objects)")

    # Mark as built
    env[_ESPDL_BUILT_KEY] = True
    return esp_dl_dir
