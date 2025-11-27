"""
Script de build PlatformIO pour LVGL Camera Display avec détection
Compile les sources nécessaires pour la détection faciale et piétonne
"""

import os
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
# ESP-DL Sources (only what we need for detection)
# ========================================================================
esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
if os.path.exists(esp_dl_dir):
    # Only compile specific files needed for detection, avoiding esp_dsp.h dependencies
    esp_dl_sources = [
        # Detection base
        "vision/detect/dl_detect_base.cpp",
        "vision/detect/dl_detect_postprocessor.cpp",
        "vision/detect/dl_detect_pico_postprocessor.cpp",
        "vision/detect/dl_detect_mnp_postprocessor.cpp",
        "vision/detect/dl_detect_msr_postprocessor.cpp",

        # Image processing (for draw_hollow_rectangle and ImageTransformer)
        "vision/image/dl_image_draw.cpp",
        "vision/image/dl_image_preprocessor.cpp",
        "vision/image/dl_image_process.cpp",

        # Model loading
        "dl/model/src/dl_model_base.cpp",

        # Tensor operations (basic)
        "dl/tensor/src/dl_tensor_base.cpp",

        # Tool functions
        "dl/tool/src/dl_tool.cpp",
        "dl/tool/isa/esp32p4/dl_esp32p4_memcpy.S",
    ]

    for src in esp_dl_sources:
        src_path = os.path.join(esp_dl_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
        else:
            print(f"[LVGL Camera Display] ⚠️  Source not found: {src}")

    print(f"[LVGL Camera Display] ✓ ESP-DL: {len([s for s in sources_to_add if 'esp-dl' in s])} source files")

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
