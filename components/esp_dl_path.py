"""
ESP-DL path resolver for PlatformIO builds.
Finds esp-dl downloaded by PlatformIO lib_deps from GitHub.
"""

import os


def find_esp_dl(env, fallback_components_dir=None):
    """Find esp-dl directory in PlatformIO libdeps or local components.

    PlatformIO downloads lib_deps to: .pio/libdeps/<env>/esp-dl/
    The actual component is in the esp-dl/ subdirectory of the repo.

    Args:
        env: PlatformIO SCons environment
        fallback_components_dir: Optional fallback to components/esp-dl/

    Returns:
        Path to the esp-dl component directory (containing dl/, vision/, etc.)
    """
    # 1. Try PlatformIO libdeps directory
    try:
        project_dir = env["PROJECT_DIR"]
        pioenv = env["PIOENV"]
        libdeps_dir = os.path.join(project_dir, ".pio", "libdeps", pioenv)

        if os.path.exists(libdeps_dir):
            for name in os.listdir(libdeps_dir):
                if "esp-dl" in name.lower() or "esp_dl" in name.lower():
                    # GitHub repo has esp-dl/ subdirectory containing the component
                    candidate = os.path.join(libdeps_dir, name, "esp-dl")
                    if os.path.isdir(candidate) and os.path.exists(os.path.join(candidate, "dl")):
                        print(f"[ESP-DL] Found in PlatformIO libdeps: {candidate}")
                        return candidate

                    # Maybe the component is directly at root level
                    candidate = os.path.join(libdeps_dir, name)
                    if os.path.exists(os.path.join(candidate, "dl")):
                        print(f"[ESP-DL] Found in PlatformIO libdeps: {candidate}")
                        return candidate
    except (KeyError, OSError) as e:
        print(f"[ESP-DL] Could not search PlatformIO libdeps: {e}")

    # 2. Try ESPHome managed_components directory
    try:
        project_dir = env["PROJECT_DIR"]
        managed_dir = os.path.join(project_dir, "managed_components", "espressif__esp-dl")
        if os.path.isdir(managed_dir) and os.path.exists(os.path.join(managed_dir, "dl")):
            print(f"[ESP-DL] Found in managed_components: {managed_dir}")
            return managed_dir
    except (KeyError, OSError):
        pass

    # 3. Fallback to local components/esp-dl/
    if fallback_components_dir:
        local_dir = os.path.join(fallback_components_dir, "esp-dl")
        if os.path.isdir(local_dir) and os.path.exists(os.path.join(local_dir, "dl")):
            print(f"[ESP-DL] Found locally: {local_dir}")
            return local_dir

    raise FileNotFoundError(
        "[ESP-DL] esp-dl not found! Add to your YAML config:\n"
        "  esphome:\n"
        "    libraries:\n"
        '      - https://github.com/espressif/esp-dl.git#v3.2.3\n'
        "  Or place esp-dl manually in components/esp-dl/"
    )


def get_esp_dl_include_dirs(esp_dl_dir, isa_target="esp32p4"):
    """Return list of include directories for esp-dl.

    Args:
        esp_dl_dir: Path to esp-dl component root
        isa_target: ISA target (esp32p4, tie728, xtensa)

    Returns:
        List of existing include directory paths
    """
    dirs = [
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

    result = []
    for d in dirs:
        path = os.path.join(esp_dl_dir, d)
        if os.path.exists(path):
            result.append(path)
    return result
