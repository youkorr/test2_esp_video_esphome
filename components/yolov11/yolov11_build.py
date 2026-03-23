"""
Build script for YOLOV11 component
Sets up include paths for ESP-DL headers.
ESP-DL sources are compiled once in libface_detection.a (face_detection component).
Model embedding is handled by the file component separately.
"""

import os
import sys
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

# Find esp-dl (downloaded by PlatformIO lib_deps or local)
sys.path.insert(0, parent_components_dir)
from esp_dl_path import find_esp_dl
esp_dl_resolved_dir = find_esp_dl(env, fallback_components_dir=parent_components_dir)

print("[YOLOV11] Build script running...")

# ========================================================================
# CONFIG defines
# ========================================================================
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32P4", "1"),
])

# ========================================================================
# ESP-DL Include paths only (sources are in libface_detection.a)
# ========================================================================
esp_dl_dir = esp_dl_resolved_dir
if os.path.exists(esp_dl_dir):
    esp_dl_include_dirs = [
        "dl", "dl/tool/include", "dl/tool/isa/esp32p4", "dl/tool/isa/tie728",
        "dl/tool/isa/xtensa", "dl/tool/src", "dl/tensor/include", "dl/tensor/src",
        "dl/base", "dl/base/isa", "dl/base/isa/esp32p4", "dl/base/isa/tie728",
        "dl/base/isa/xtensa", "dl/math/include", "dl/math/src", "dl/model/include",
        "dl/model/src", "dl/module/include", "dl/module/src", "fbs_loader/include",
        "fbs_loader/lib/esp32p4", "fbs_loader/src", "vision/detect", "vision/image",
        "vision/image/isa", "vision/image/isa/esp32p4",
    ]

    for inc_dir in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc_dir)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    print("[YOLOV11] ESP-DL includes added (sources shared via libface_detection.a)")

env.Append(CPPPATH=[component_dir])

print("[YOLOV11] Build script completed (no duplicate ESP-DL compilation)")
