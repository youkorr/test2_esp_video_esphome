"""
Pre-build script: adds ESP-DL include paths to projenv (project source env).
This runs before compilation so src/esphome/components/ files can find ESP-DL headers.
"""

import os
import sys

Import("env", "projenv")

script_dir = os.path.dirname(os.path.abspath(env.subst("$PROJECT_DIR")))

# Find esp-dl using shared resolver
# The script is at components/face_detection/esp_dl_includes.py
# esp_dl_path.py is at components/esp_dl_path.py
this_script = os.path.abspath(__file__) if '__file__' in dir() else None
if this_script:
    parent_components_dir = os.path.dirname(os.path.dirname(this_script))
else:
    parent_components_dir = None

# Also try the external_components path
external_components_dirs = []
for lib_dir in env.get("LIBSOURCE_DIRS", []):
    lib_dir_str = str(lib_dir)
    try:
        resolved = env.subst(lib_dir_str)
    except Exception:
        resolved = lib_dir_str
    for path in [resolved, lib_dir_str]:
        if os.path.exists(path):
            for name in os.listdir(path):
                candidate = os.path.join(path, name)
                if os.path.isdir(candidate):
                    # Check for esp-dl/dl/ or dl/ structure
                    esp_dl_sub = os.path.join(candidate, "esp-dl")
                    if os.path.isdir(esp_dl_sub) and os.path.exists(os.path.join(esp_dl_sub, "dl")):
                        external_components_dirs.append(esp_dl_sub)
                    elif "esp-dl" in name.lower() or "esp_dl" in name.lower():
                        if os.path.exists(os.path.join(candidate, "dl")):
                            external_components_dirs.append(candidate)

# Also try to find via esp_dl_path module
esp_dl_dir = None
try:
    if parent_components_dir:
        sys.path.insert(0, parent_components_dir)
    from esp_dl_path import find_esp_dl
    esp_dl_dir = find_esp_dl(env)
except Exception as e:
    print(f"[ESP-DL Pre] find_esp_dl failed: {e}")

if not esp_dl_dir and external_components_dirs:
    esp_dl_dir = external_components_dirs[0]

if esp_dl_dir and os.path.exists(esp_dl_dir):
    esp_dl_include_dirs = [
        "dl", "dl/tool/include", "dl/tool/isa/esp32p4", "dl/tool/isa/tie728",
        "dl/tool/isa/xtensa", "dl/tool/src", "dl/tensor/include", "dl/tensor/src",
        "dl/base", "dl/base/isa", "dl/base/isa/esp32p4", "dl/base/isa/tie728",
        "dl/base/isa/xtensa", "dl/math/include", "dl/math/src", "dl/model/include",
        "dl/model/src", "dl/module/include", "dl/module/src", "fbs_loader/include",
        "fbs_loader/lib/esp32p4", "fbs_loader/src", "vision/detect", "vision/image",
        "vision/image/isa", "vision/image/isa/esp32p4", "vision/recognition",
        "vision/classification",
    ]

    added = 0
    for inc_dir in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc_dir)
        if os.path.exists(inc_path):
            projenv.Append(CPPPATH=[inc_path])
            added += 1

    print(f"[ESP-DL Pre] Added {added} include paths to projenv from {esp_dl_dir}")
else:
    print("[ESP-DL Pre] WARNING: esp-dl not found, projenv includes not set")
