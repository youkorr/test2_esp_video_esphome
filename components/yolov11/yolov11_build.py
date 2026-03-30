"""
Build script for YOLOV11 component.
Compiles ESP-DL sources (dl/, fbs_loader/, vision/) needed for YOLO11 inference.
Excludes audio/, examples/, docs/, test/, speech/.
"""

import os
import sys
import glob
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

# Find esp-dl (downloaded by PlatformIO lib_deps or local)
sys.path.insert(0, parent_components_dir)
from esp_dl_path import find_esp_dl
esp_dl_dir = find_esp_dl(env, fallback_components_dir=parent_components_dir)

print(f"[YOLOV11] Build script running...")
print(f"[YOLOV11] ESP-DL: {esp_dl_dir}")

# ========================================================================
# CONFIG defines
# ========================================================================
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32P4", "1"),
])

# ========================================================================
# ESP-DL Include paths
# ========================================================================
esp_dl_include_dirs = [
    "dl", "dl/tool/include", "dl/tool/isa/esp32p4", "dl/tool/isa/tie728",
    "dl/tool/isa/xtensa", "dl/tool/src", "dl/tensor/include", "dl/tensor/src",
    "dl/base", "dl/base/isa", "dl/base/isa/esp32p4", "dl/base/isa/tie728",
    "dl/base/isa/xtensa", "dl/math/include", "dl/math/src", "dl/model/include",
    "dl/model/src", "dl/module/include", "dl/module/src", "fbs_loader/include",
    "fbs_loader/lib/esp32p4", "fbs_loader/src", "vision/detect", "vision/image",
    "vision/image/isa", "vision/image/isa/esp32p4",
]

inc_count = 0
for inc_dir in esp_dl_include_dirs:
    inc_path = os.path.join(esp_dl_dir, inc_dir)
    if os.path.exists(inc_path):
        env.Append(CPPPATH=[inc_path])
        inc_count += 1
print(f"[YOLOV11] ESP-DL: {inc_count} include paths added")

# yolo11_detect component includes
yolo11_detect_dir = os.path.join(parent_components_dir, "yolo11_detect")
if os.path.exists(yolo11_detect_dir):
    env.Append(CPPPATH=[yolo11_detect_dir])

env.Append(CPPPATH=[component_dir])

# ========================================================================
# ESP-DL Source compilation
# Only compile dl/, fbs_loader/, vision/ (exclude audio/, examples/, etc.)
# ========================================================================
sources_to_add = []

esp_dl_source_dirs = [
    "dl/tensor/src",
    "dl/model/src",
    "dl/module/src",
    "dl/tool/src",
    "dl/math/src",
    "fbs_loader/src",
    "vision/image",
    "vision/detect",
]

esp_dl_exclude_dirs = {"audio", "examples", "docs", "test", "speech"}

esp_dl_exclude_files = {
    "dl_image_jpeg.cpp",
    "dl_image_bmp.cpp",
}

if os.path.exists(esp_dl_dir):
    for src_dir in esp_dl_source_dirs:
        src_dir_path = os.path.join(esp_dl_dir, src_dir)
        if not os.path.exists(src_dir_path):
            continue
        if src_dir.startswith("vision/"):
            pattern = os.path.join(src_dir_path, "**", "*.cpp")
            for src_file in glob.glob(pattern, recursive=True):
                fname = os.path.basename(src_file)
                rel = os.path.relpath(src_file, esp_dl_dir)
                parts = set(rel.split(os.sep))
                if parts & esp_dl_exclude_dirs:
                    continue
                if fname not in esp_dl_exclude_files:
                    sources_to_add.append(src_file)
        else:
            for src_file in glob.glob(os.path.join(src_dir_path, "*.cpp")):
                fname = os.path.basename(src_file)
                if fname not in esp_dl_exclude_files:
                    sources_to_add.append(src_file)

    # dl/base/*.cpp
    dl_base_dir = os.path.join(esp_dl_dir, "dl", "base")
    if os.path.exists(dl_base_dir):
        for src_file in glob.glob(os.path.join(dl_base_dir, "*.cpp")):
            sources_to_add.append(src_file)

    # ISA-specific assembly/source for ESP32-P4
    for isa_dir in ["dl/base/isa/esp32p4", "dl/tool/isa/esp32p4",
                     "vision/image/isa/esp32p4"]:
        isa_path = os.path.join(esp_dl_dir, isa_dir)
        if os.path.exists(isa_path):
            for ext in ["*.cpp", "*.S", "*.c"]:
                for src_file in glob.glob(os.path.join(isa_path, ext)):
                    sources_to_add.append(src_file)

    # Pre-compiled static library (fbs_loader)
    fbs_lib = os.path.join(esp_dl_dir, "fbs_loader", "lib", "esp32p4", "libfbs_model.a")
    if os.path.exists(fbs_lib):
        env.Append(LIBPATH=[os.path.dirname(fbs_lib)])
        env.Append(LIBS=["fbs_model"])
        print("[YOLOV11] + libfbs_model.a")

    print(f"[YOLOV11] ESP-DL: {len(sources_to_add)} source files to compile")
else:
    print("[YOLOV11] WARNING: esp-dl not found, skipping source compilation")

# ========================================================================
# Compile ESP-DL sources into static library
# ========================================================================
if sources_to_add:
    objects = []
    for src_file in sources_to_add:
        try:
            obj = env.Object(src_file)
            objects.extend(obj)
        except Exception as e:
            print(f"[YOLOV11] Failed to compile {os.path.basename(src_file)}: {e}")

    if objects:
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libyolov11_espdl"),
            objects
        )
        env.Prepend(LIBS=[lib])
        env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
        print(f"[YOLOV11] libyolov11_espdl.a created ({len(objects)} objects)")
else:
    print("[YOLOV11] WARNING: No ESP-DL sources compiled!")

print("[YOLOV11] Build script completed")
