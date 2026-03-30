"""
Build script for YOLOV11 component.
Uses shared ESP-DL download + compilation (esp_dl_build.py).
"""

import os
import sys
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[YOLOV11] Build script running...")

# CONFIG defines
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32P4", "1"),
])

# yolo11_detect component includes
yolo11_detect_dir = os.path.join(parent_components_dir, "yolo11_detect")
if os.path.exists(yolo11_detect_dir):
    env.Append(CPPPATH=[yolo11_detect_dir])

env.Append(CPPPATH=[component_dir])

# Shared ESP-DL download + compilation
# (downloads esp-dl via sparse git checkout, compiles once, reused by all)
sys.path.insert(0, parent_components_dir)
from esp_dl_build import build_espdl
esp_dl_dir = build_espdl(env, isa_target="esp32p4")
print(f"[YOLOV11] ESP-DL: {esp_dl_dir}")

print("[YOLOV11] Build script completed")
