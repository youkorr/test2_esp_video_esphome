"""
Script de build PlatformIO pour LVGL Camera Display avec détection
Compile les sources nécessaires pour la détection faciale et piétonne
"""

import os
import glob
Import("env")

# Obtenir le répertoire du composant
script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[LVGL Camera Display] Build script running...")

# Liste des sources à compiler
sources_to_add = []

# ========================================================================
# Human Face Detect Component
# ========================================================================
human_face_detect_dir = os.path.join(parent_components_dir, "human_face_detect")
if os.path.exists(human_face_detect_dir):
    human_face_sources = ["human_face_detect.cpp"]
    for src in human_face_sources:
        src_path = os.path.join(human_face_detect_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[LVGL Camera Display] + {src}")

# ========================================================================
# Pedestrian Detect Component
# ========================================================================
pedestrian_detect_dir = os.path.join(parent_components_dir, "pedestrian_detect")
if os.path.exists(pedestrian_detect_dir):
    pedestrian_sources = ["pedestrian_detect.cpp"]
    for src in pedestrian_sources:
        src_path = os.path.join(pedestrian_detect_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[LVGL Camera Display] + {src}")

# ========================================================================
# ESP-DL Sources (compile by directories like CMakeLists.txt)
# ========================================================================
esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
if os.path.exists(esp_dl_dir):
    # Add ESP-DL include directories (from CMakeLists.txt)
    esp_dl_include_dirs = [
        "dl",
        "dl/tool/include",
        "dl/tensor/include",
        "dl/base",
        "dl/base/isa",
        "dl/base/isa/esp32p4",
        "dl/math/include",
        "dl/model/include",
        "dl/module/include",
        "fbs_loader/include",
        "vision/detect",
        "vision/image",
        "vision/image/isa",
        "vision/image/isa/esp32p4",
        "vision/recognition",
        "vision/classification",
    ]

    for inc_dir in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc_dir)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    print(f"[LVGL Camera Display] ✓ ESP-DL: Added {len(esp_dl_include_dirs)} include directories")

    # Based on ESP-DL CMakeLists.txt, compile all sources from these directories
    # This ensures all dependencies are resolved
    esp_dl_source_dirs = [
        # Core DL components
        "dl/tool/src",
        "dl/tool/isa/esp32p4",       # ESP32-P4 optimized assembly
        "dl/tensor/src",
        "dl/base",                    # All base operations
        "dl/base/isa/esp32p4",       # ESP32-P4 optimized base operations
        "dl/math/src",
        "dl/model/src",
        "dl/module/src",
        "fbs_loader/src",

        # Vision components (needed for detection)
        "vision/detect",              # Detection algorithms
        "vision/image",               # Image processing
        "vision/image/isa/esp32p4",  # ESP32-P4 optimized image operations
    ]

    esp_dl_count = 0
    for src_dir in esp_dl_source_dirs:
        src_dir_path = os.path.join(esp_dl_dir, src_dir)
        if os.path.exists(src_dir_path):
            # Find all .cpp, .c, and .S files in this directory (non-recursive)
            patterns = ['*.cpp', '*.c', '*.S']
            for pattern in patterns:
                files = glob.glob(os.path.join(src_dir_path, pattern))
                for src_file in files:
                    sources_to_add.append(src_file)
                    esp_dl_count += 1
        else:
            print(f"[LVGL Camera Display] ⚠️  Directory not found: {src_dir}")

    print(f"[LVGL Camera Display] ✓ ESP-DL: {esp_dl_count} source files from {len(esp_dl_source_dirs)} directories")

# ========================================================================
# Compile sources
# ========================================================================
if sources_to_add:
    # Compile source files
    objects = []
    for src_file in sources_to_add:
        try:
            obj = env.Object(src_file)
            objects.extend(obj)
        except Exception as e:
            print(f"[LVGL Camera Display] ⚠️  Failed to compile {os.path.basename(src_file)}: {e}")

    if objects:
        # Create static library
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "liblvgl_camera_display_detect"),
            objects
        )

        # Add library to linkage
        env.Prepend(LIBS=[lib])

        print(f"[LVGL Camera Display] ✓ {len(sources_to_add)} source files compiled")
        print(f"[LVGL Camera Display] ✓ liblvgl_camera_display_detect.a created")
else:
    print("[LVGL Camera Display] ⚠️  No sources to compile")

print("[LVGL Camera Display] Build script completed")
