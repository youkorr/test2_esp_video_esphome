"""
ESP-DL path resolver for PlatformIO builds.
Finds esp-dl downloaded by PlatformIO lib_deps from GitHub.
"""

import os


def _check_esp_dl_dir(candidate):
    """Check if a directory contains esp-dl (has dl/ subdirectory)."""
    if os.path.isdir(candidate) and os.path.exists(os.path.join(candidate, "dl")):
        return candidate
    # esp-dl repo has esp-dl/ subdirectory containing the actual component
    sub = os.path.join(candidate, "esp-dl")
    if os.path.isdir(sub) and os.path.exists(os.path.join(sub, "dl")):
        return sub
    return None


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

        print(f"[ESP-DL] PROJECT_DIR={project_dir} PIOENV={pioenv}")
        print(f"[ESP-DL] Searching libdeps: {libdeps_dir}")

        if os.path.exists(libdeps_dir):
            entries = os.listdir(libdeps_dir)
            print(f"[ESP-DL] libdeps contents: {entries}")
            for name in entries:
                if "esp-dl" in name.lower() or "esp_dl" in name.lower():
                    candidate = os.path.join(libdeps_dir, name)
                    print(f"[ESP-DL] Checking candidate: {candidate}")
                    if os.path.isdir(candidate):
                        sub_entries = os.listdir(candidate)
                        print(f"[ESP-DL]   Contents: {sub_entries}")
                    result = _check_esp_dl_dir(candidate)
                    if result:
                        print(f"[ESP-DL] Found in PlatformIO libdeps: {result}")
                        return result
        else:
            print(f"[ESP-DL] libdeps dir does not exist: {libdeps_dir}")
    except (KeyError, OSError) as e:
        print(f"[ESP-DL] Could not search PlatformIO libdeps: {e}")

    # 2. Search ALL directories in libdeps (PlatformIO may rename the folder)
    try:
        project_dir = env["PROJECT_DIR"]
        pioenv = env["PIOENV"]
        libdeps_dir = os.path.join(project_dir, ".pio", "libdeps", pioenv)

        if os.path.exists(libdeps_dir):
            for name in os.listdir(libdeps_dir):
                candidate = os.path.join(libdeps_dir, name)
                if os.path.isdir(candidate):
                    result = _check_esp_dl_dir(candidate)
                    if result:
                        print(f"[ESP-DL] Found (by structure) in libdeps: {result}")
                        return result
    except (KeyError, OSError):
        pass

    # 3. Try LIBSOURCE_DIRS from PlatformIO env (resolve SCons variables)
    try:
        lib_dirs = env.get("LIBSOURCE_DIRS", [])
        if lib_dirs:
            print(f"[ESP-DL] Checking LIBSOURCE_DIRS: {lib_dirs}")
        for lib_dir in lib_dirs:
            lib_dir_str = str(lib_dir)
            # Resolve PlatformIO variables like $PROJECT_LIBDEPS_DIR/$PIOENV
            try:
                lib_dir_resolved = env.subst(lib_dir_str)
            except Exception:
                lib_dir_resolved = lib_dir_str
            for path_to_check in [lib_dir_resolved, lib_dir_str]:
                if os.path.exists(path_to_check):
                    entries = os.listdir(path_to_check)
                    print(f"[ESP-DL] LIBSOURCE_DIR {path_to_check} contents: {entries}")
                    for name in entries:
                        candidate = os.path.join(path_to_check, name)
                        if os.path.isdir(candidate):
                            result = _check_esp_dl_dir(candidate)
                            if result:
                                print(f"[ESP-DL] Found in LIBSOURCE_DIRS: {result}")
                                return result
    except (KeyError, OSError, TypeError):
        pass

    # 4. Try PROJECT_LIBDEPS_DIR directly
    try:
        project_libdeps = env.subst("$PROJECT_LIBDEPS_DIR")
        pioenv = env["PIOENV"]
        if project_libdeps and os.path.exists(project_libdeps):
            print(f"[ESP-DL] PROJECT_LIBDEPS_DIR={project_libdeps}")
            # Check <libdeps>/<env>/
            env_libdeps = os.path.join(project_libdeps, pioenv)
            # Also check <libdeps>/ directly
            for check_dir in [env_libdeps, project_libdeps]:
                if os.path.exists(check_dir):
                    entries = os.listdir(check_dir)
                    print(f"[ESP-DL] Checking {check_dir}: {entries}")
                    for name in entries:
                        candidate = os.path.join(check_dir, name)
                        if os.path.isdir(candidate):
                            result = _check_esp_dl_dir(candidate)
                            if result:
                                print(f"[ESP-DL] Found in PROJECT_LIBDEPS_DIR: {result}")
                                return result
    except (KeyError, OSError):
        pass

    # 5. Try ESPHome managed_components directory
    try:
        project_dir = env["PROJECT_DIR"]
        managed_dir = os.path.join(project_dir, "managed_components", "espressif__esp-dl")
        if os.path.isdir(managed_dir) and os.path.exists(os.path.join(managed_dir, "dl")):
            print(f"[ESP-DL] Found in managed_components: {managed_dir}")
            return managed_dir
    except (KeyError, OSError):
        pass

    # 6. Deep search: /data, project dir, common PlatformIO paths
    try:
        project_dir = env["PROJECT_DIR"]
        search_roots = set()
        search_roots.add(os.path.join(project_dir, ".pio"))
        search_roots.add(os.path.join(os.path.expanduser("~"), ".platformio"))
        # ESPHome Docker uses /data and /piolibs
        for extra in ["/data", "/piolibs"]:
            if os.path.exists(extra):
                search_roots.add(extra)

        print(f"[ESP-DL] Deep search in: {search_roots}")
        for search_path in search_roots:
            if not os.path.exists(search_path):
                continue
            for root, dirs, files in os.walk(search_path):
                depth = root[len(search_path):].count(os.sep)
                if depth > 4:
                    dirs.clear()
                    continue
                if "dl" in dirs and os.path.exists(os.path.join(root, "dl", "base")):
                    print(f"[ESP-DL] Found by deep search: {root}")
                    return root
    except (KeyError, OSError):
        pass

    # 7. Fallback to local components/esp-dl/
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
