"""
Build script for YOLOV11 component.
Uses shared ESP-DL compilation (esp_dl_build.py) to avoid duplicate libraries.
"""

import os
import sys
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

# Find esp-dl
sys.path.insert(0, parent_components_dir)
from esp_dl_path import find_esp_dl
esp_dl_dir = find_esp_dl(env, fallback_components_dir=parent_components_dir)

print(f"[YOLOV11] Build script running...")
print(f"[YOLOV11] ESP-DL: {esp_dl_dir}")

# CONFIG defines
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32P4", "1"),
])

# yolo11_detect component includes
yolo11_detect_dir = os.path.join(parent_components_dir, "yolo11_detect")
if os.path.exists(yolo11_detect_dir):
    env.Append(CPPPATH=[yolo11_detect_dir])

env.Append(CPPPATH=[component_dir])

# Shared ESP-DL compilation (compiles once, reused by all detection components)
from esp_dl_build import build_espdl
build_espdl(env, esp_dl_dir, isa_target="esp32p4")

print("[YOLOV11] Build script completed")
