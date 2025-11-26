"""
Script de build PlatformIO pour LVGL Camera Display avec détection
Ajoute les composants ESP-DL pour la détection faciale et piétonne
"""

import os
Import("env")

# Obtenir le répertoire du composant
script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

# Liste de tous les fichiers sources à compiler
sources_to_add = []
include_paths = []

print("[LVGL Camera Display] Adding ESP-DL detection components...")

# ========================================================================
# ESP-DL Component (deep learning library)
# ========================================================================
esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")

# ESP-DL include directories (from CMakeLists.txt)
esp_dl_includes = [
    "dl",
    "dl/tool/include",
    "dl/tensor/include",
    "dl/base",
    "dl/base/isa",
    "dl/math/include",
    "dl/model/include",
    "dl/module/include",
    "fbs_loader/include",
    "vision/detect",
    "vision/image",
    "vision/image/isa",
    "vision/recognition",
    "vision/classification",
    "audio/common",
    "audio/speech_features",
]

# ESP32-P4 specific includes
esp_dl_includes.extend([
    "dl/tool/isa/esp32p4",
    "dl/base/isa/esp32p4",
    "vision/image/isa/esp32p4",
])

# ESP-DL source directories (from CMakeLists.txt)
esp_dl_src_dirs = [
    "dl/tool/src",
    "dl/tensor/src",
    "dl/base",
    "dl/math/src",
    "dl/model/src",
    "dl/module/src",
    "fbs_loader/src",
    "vision/detect",
    "vision/image",
    "vision/recognition",
    "vision/classification",
    "audio/common",
    "audio/speech_features",
    "dl/tool/isa/esp32p4",
    "dl/base/isa/esp32p4",
    "vision/image/isa/esp32p4",
]

if os.path.exists(esp_dl_dir):
    # Add include paths
    for inc in esp_dl_includes:
        inc_path = os.path.join(esp_dl_dir, inc)
        if os.path.exists(inc_path):
            include_paths.append(inc_path)

    # Add all .c and .cpp files from source directories
    for src_dir in esp_dl_src_dirs:
        src_path = os.path.join(esp_dl_dir, src_dir)
        if os.path.exists(src_path):
            # Find all .c files
            for root, dirs, files in os.walk(src_path):
                for file in files:
                    if file.endswith('.c') or file.endswith('.cpp'):
                        file_path = os.path.join(root, file)
                        sources_to_add.append(file_path)

    # Add prebuilt FBS model library for ESP32-P4
    fbs_lib_path = os.path.join(esp_dl_dir, "fbs_loader/lib/esp32p4/libfbs_model.a")
    if os.path.exists(fbs_lib_path):
        fbs_lib_dir = os.path.dirname(fbs_lib_path)
        env.Append(LIBPATH=[fbs_lib_dir])
        env.Append(LIBS=["fbs_model"])
        print(f"[LVGL Camera Display] 📚 ESP-DL FBS library added")

    print(f"[LVGL Camera Display] ✓ ESP-DL: {len([s for s in sources_to_add if 'esp-dl' in s])} source files")

# ========================================================================
# Human Face Detect Component
# ========================================================================
human_face_detect_dir = os.path.join(parent_components_dir, "human_face_detect")

if os.path.exists(human_face_detect_dir):
    # Add include path
    include_paths.append(human_face_detect_dir)

    # Add source files
    human_face_sources = ["human_face_detect.cpp"]
    for src in human_face_sources:
        src_path = os.path.join(human_face_detect_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)

    print(f"[LVGL Camera Display] ✓ Human Face Detect component added")

# ========================================================================
# Pedestrian Detect Component
# ========================================================================
pedestrian_detect_dir = os.path.join(parent_components_dir, "pedestrian_detect")

if os.path.exists(pedestrian_detect_dir):
    # Add include path
    include_paths.append(pedestrian_detect_dir)

    # Add source files
    pedestrian_sources = ["pedestrian_detect.cpp"]
    for src in pedestrian_sources:
        src_path = os.path.join(pedestrian_detect_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)

    print(f"[LVGL Camera Display] ✓ Pedestrian Detect component added")

# ========================================================================
# Apply include paths and compile sources
# ========================================================================
if include_paths:
    env.Append(CPPPATH=include_paths)
    print(f"[LVGL Camera Display] 📁 {len(include_paths)} include paths added")

if sources_to_add:
    # Compile source files
    objects = []
    for src_file in sources_to_add:
        obj = env.Object(src_file)
        objects.extend(obj)

    # Create static library
    lib = env.StaticLibrary(
        os.path.join("$BUILD_DIR", "liblvgl_camera_display_detect"),
        objects
    )

    # Add library to linkage
    env.Prepend(LIBS=[lib])

    print(f"[LVGL Camera Display] ✓ {len(sources_to_add)} detection source files compiled")
    print(f"[LVGL Camera Display] ✓ liblvgl_camera_display_detect.a created")

# Add compiler flags for C++ and optimization
env.Append(CXXFLAGS=["-std=c++17", "-fno-rtti"])
env.Append(CCFLAGS=["-O3", "-ffast-math"])

print("[LVGL Camera Display] Detection components ready!")
