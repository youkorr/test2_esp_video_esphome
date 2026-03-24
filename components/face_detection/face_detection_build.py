"""
Build script for Face Detection component
Embeds face detection/recognition models and compiles optimized ESP-DL sources
"""

import os
import sys
import glob
Import("env")

# Resolve component directory by finding this script's own path from extra_scripts.
# __init__.py sets extra_scripts to "post:<absolute_path>/face_detection_build.py",
# so we can parse it to recover the real component directory.
component_dir = None
parent_components_dir = None

# Method 1: parse extra_scripts to find our own absolute path
try:
    extra_scripts = env.GetProjectOption("extra_scripts", [])
    for es in extra_scripts:
        es_str = str(es).strip()
        if "face_detection_build.py" in es_str:
            script_path = es_str.replace("post:", "").strip()
            component_dir = os.path.dirname(os.path.abspath(script_path))
            parent_components_dir = os.path.dirname(component_dir)
            print(f"[Face Detection] Resolved path from extra_scripts: {component_dir}")
            break
except Exception as e:
    print(f"[Face Detection] extra_scripts parse failed: {e}")

# Method 2: custom platformio option (fallback)
if not component_dir:
    try:
        parent_components_dir = env.GetProjectOption("custom_face_detection_components_dir")
        component_dir = os.path.join(parent_components_dir, "face_detection")
        print(f"[Face Detection] Resolved path from custom option: {component_dir}")
    except:
        pass

# Method 3: Dir('.') fallback
if not component_dir:
    script_dir = Dir('.').srcnode().abspath
    component_dir = script_dir
    parent_components_dir = os.path.dirname(component_dir)
    print(f"[Face Detection] WARNING: Using Dir('.') fallback: {component_dir}")

print(f"[Face Detection] component_dir = {component_dir}")
print(f"[Face Detection] parent_components_dir = {parent_components_dir}")

# Verify directories exist
for check_name in ["human_face_detect", "human_face_recognition", "esp_dl_path.py"]:
    check_path = os.path.join(parent_components_dir, check_name)
    print(f"[Face Detection]   {check_name}: {'EXISTS' if os.path.exists(check_path) else 'MISSING'} ({check_path})")

# Find esp-dl (downloaded by PlatformIO lib_deps or local)
sys.path.insert(0, parent_components_dir)
from esp_dl_path import find_esp_dl
esp_dl_resolved_dir = find_esp_dl(env, fallback_components_dir=parent_components_dir)

print("[Face Detection] Build script running...")

# Detect which models this component needs from build flags.
# Multiple model defines can coexist (e.g. face_detection + yolov11 components).
# For THIS component's build script, we care about ESP_DL_MODEL_FACE_RECOGNITION.
has_face_recognition = False
has_yolo11 = False
has_pose = False
cpp_defines = env.get('CPPDEFINES', [])
for define in cpp_defines:
    if isinstance(define, tuple):
        key, val = define
    else:
        key = define
        val = None

    if key == "ESP_DL_MODEL_FACE_RECOGNITION":
        has_face_recognition = True
    elif key == "ESP_DL_MODEL_YOLO11":
        has_yolo11 = True
    elif key == "ESP_DL_MODEL_POSE_DETECTION":
        has_pose = True

# This is the face_detection build script, so default to face_recognition
# unless ONLY other model types are defined
if has_face_recognition:
    model_type = "face_recognition"
elif has_yolo11:
    model_type = "yolo11"
elif has_pose:
    model_type = "pose_detection"
else:
    model_type = "face_recognition"  # default

print(f"[Face Detection] Model type: {model_type} (face_recognition={has_face_recognition}, yolo11={has_yolo11}, pose={has_pose})")

# ========================================================================
# Add CONFIG defines for detection models
# ========================================================================
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32P4", "1"),
])

# Human face detection configuration
# Default: flash rodata mode (override with model_location: sdcard in YAML)
# Only add defaults if not already defined (to avoid redefinition warnings)
existing_defines = [define[0] if isinstance(define, tuple) else define for define in env.get("CPPDEFINES", [])]

if "CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA" not in existing_defines:
    env.Append(CPPDEFINES=[
        ("CONFIG_HUMAN_FACE_DETECT_MSRMNP_S8_V1", "1"),
        ("CONFIG_HUMAN_FACE_DETECT_MSR_S8_V1", "1"),
        ("CONFIG_HUMAN_FACE_DETECT_MNP_S8_V1", "1"),
        ("CONFIG_HUMAN_FACE_DETECT_MODEL_TYPE", "0"),
        ("CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA", "1"),
        ("CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD", "0"),
        ("CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION", "0"),
    ])
    print("[Face Detection] Added default face detection CONFIG defines (flash rodata mode)")
else:
    print("[Face Detection] Face detection CONFIG defines already set by YAML (skipping defaults)")

# Human face recognition configuration
if "CONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_RODATA" not in existing_defines:
    env.Append(CPPDEFINES=[
        ("CONFIG_HUMAN_FACE_FEAT_MFN_S8_V1", "1"),
        ("CONFIG_HUMAN_FACE_FEAT_MODEL_TYPE", "0"),
        ("CONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_RODATA", "1"),
        ("CONFIG_HUMAN_FACE_FEAT_MODEL_LOCATION", "0"),
    ])
    print("[Face Detection] Added default face recognition CONFIG defines")
else:
    print("[Face Detection] Face recognition CONFIG defines already set")

print("[Face Detection] CONFIG defines added")

sources_to_add = []

# ========================================================================
# Helper function for caching
# ========================================================================
import subprocess

def needs_rebuild(output_file, input_files):
    """Check if output_file needs to be rebuilt."""
    if not os.path.exists(output_file):
        return True
    output_mtime = os.path.getmtime(output_file)
    for input_file in input_files:
        if os.path.exists(input_file):
            if os.path.getmtime(input_file) > output_mtime:
                return True
    return False

# ========================================================================
# Pack and Embed Face Detection Models (ONLY for face_recognition model)
# ========================================================================
if model_type == "face_recognition":
    human_face_detect_dir = os.path.join(parent_components_dir, "human_face_detect")
    if os.path.exists(human_face_detect_dir):
        models_dir = os.path.join(human_face_detect_dir, "models", "p4")
        pack_script = os.path.join(human_face_detect_dir, "pack_model.py")

        if os.path.exists(models_dir) and os.path.exists(pack_script):
            msr_model = os.path.join(models_dir, "human_face_detect_msr_s8_v1.espdl")
            mnp_model = os.path.join(models_dir, "human_face_detect_mnp_s8_v1.espdl")

            if os.path.exists(msr_model) and os.path.exists(mnp_model):
                packed_model = os.path.join(component_dir, "human_face_detect.espdl")
                embed_c_file = os.path.join(component_dir, "human_face_detect_espdl_embed.c")

                if needs_rebuild(embed_c_file, [msr_model, mnp_model, pack_script]):
                    print("[Face Detection] Packing human_face_detect models...")
                    try:
                        cmd = [
                            "python3", pack_script,
                            "--model_path", msr_model, mnp_model,
                            "--out_file", packed_model
                        ]
                        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
                        if result.returncode == 0 and os.path.exists(packed_model):
                            with open(packed_model, 'rb') as f:
                                model_data = f.read()

                            c_content = '''// Auto-generated - embedded human_face_detect model
#include <stddef.h>
#include <stdint.h>

__attribute__((aligned(16)))
const uint8_t _binary_human_face_detect_espdl_start[] = {
'''
                            for i in range(0, len(model_data), 16):
                                chunk = model_data[i:i+16]
                                hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
                                c_content += f'    {hex_bytes},\n'

                            c_content += f'''}};

const uint8_t *_binary_human_face_detect_espdl_end = _binary_human_face_detect_espdl_start + {len(model_data)};
const size_t _binary_human_face_detect_espdl_size = {len(model_data)};
'''
                            with open(embed_c_file, 'w') as f:
                                f.write(c_content)
                            print(f"[Face Detection] Model embedded: {len(model_data)} bytes")
                    except Exception as e:
                        print(f"[Face Detection] Error packing models: {e}")
                else:
                    print("[Face Detection] human_face_detect models cached (skip)")

                if os.path.exists(embed_c_file):
                    sources_to_add.append(embed_c_file)

        env.Append(CPPPATH=[human_face_detect_dir])
        human_face_sources = ["human_face_detect.cpp"]
        for src in human_face_sources:
            src_path = os.path.join(human_face_detect_dir, src)
            if os.path.exists(src_path):
                sources_to_add.append(src_path)
                print(f"[Face Detection] + {src}")

    # ========================================================================
    # Pack and Embed Face Recognition Model (ONLY for face_recognition model)
    # ========================================================================
    human_face_recognition_dir = os.path.join(parent_components_dir, "human_face_recognition")
    if os.path.exists(human_face_recognition_dir):
        env.Append(CPPPATH=[human_face_recognition_dir])

        models_dir = os.path.join(human_face_recognition_dir, "models", "p4")
        pack_script = os.path.join(human_face_recognition_dir, "pack_model.py")

        if os.path.exists(models_dir) and os.path.exists(pack_script):
            mfn_model = os.path.join(models_dir, "human_face_feat_mfn_s8_v1.espdl")

            if os.path.exists(mfn_model):
                packed_model = os.path.join(component_dir, "human_face_feat.espdl")
                embed_c_file = os.path.join(component_dir, "human_face_feat_espdl_embed.c")

                if needs_rebuild(embed_c_file, [mfn_model, pack_script]):
                    print("[Face Detection] Packing human_face_recognition model...")
                    try:
                        cmd = [
                            "python3", pack_script,
                            "--model_path", mfn_model,
                            "--out_file", packed_model
                        ]
                        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
                        if result.returncode == 0 and os.path.exists(packed_model):
                            with open(packed_model, 'rb') as f:
                                model_data = f.read()

                            c_content = '''// Auto-generated - embedded human_face_feat model
#include <stddef.h>
#include <stdint.h>

__attribute__((aligned(16)))
const uint8_t _binary_human_face_feat_mfn_s8_v1_espdl_start[] = {
'''
                            for i in range(0, len(model_data), 16):
                                chunk = model_data[i:i+16]
                                hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
                                c_content += f'    {hex_bytes},\n'

                            c_content += f'''}};

const uint8_t *_binary_human_face_feat_mfn_s8_v1_espdl_end = _binary_human_face_feat_mfn_s8_v1_espdl_start + {len(model_data)};
const size_t _binary_human_face_feat_mfn_s8_v1_espdl_size = {len(model_data)};
'''
                            with open(embed_c_file, 'w') as f:
                                f.write(c_content)
                            print(f"[Face Detection] Recognition model embedded: {len(model_data)} bytes")
                    except Exception as e:
                        print(f"[Face Detection] Error packing recognition model: {e}")
                else:
                    print("[Face Detection] human_face_recognition model cached (skip)")

                if os.path.exists(embed_c_file):
                    sources_to_add.append(embed_c_file)

        recognition_sources = ["human_face_recognition.cpp"]
        for src in recognition_sources:
            src_path = os.path.join(human_face_recognition_dir, src)
            if os.path.exists(src_path):
                sources_to_add.append(src_path)
                print(f"[Face Detection] + {src}")
else:
    print(f"[Face Detection] Skipping human_face_detect and human_face_recognition (model_type={model_type})")

# ========================================================================
# ESP-DL Sources - Optimized: only essential files
# ========================================================================
esp_dl_dir = esp_dl_resolved_dir
if os.path.exists(esp_dl_dir):
    # Add include directories
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

    esp_dl_paths = []
    for inc_dir in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc_dir)
        if os.path.exists(inc_path):
            esp_dl_paths.append(inc_path)

    env.Append(CPPPATH=esp_dl_paths)

    print(f"[Face Detection] ESP-DL includes added ({len(esp_dl_paths)} paths)")

    # ESP-DL source files - based on selected model type
    # Core directories (always needed)
    esp_dl_source_dirs = [
        "dl/tensor/src",
        "dl/model/src",
        "dl/module/src",
        "dl/tool/src",
        "dl/math/src",
        "fbs_loader/src",
        "vision/image",          # Image processing (required)
        "vision/detect",         # Detection base (always needed, even for face recognition)
    ]

    # Add model-specific directories
    if model_type == "face_recognition":
        esp_dl_source_dirs.append("vision/recognition")  # Face recognition
        print("[Face Detection] Including: vision/recognition + vision/detect (face recognition)")
    elif model_type == "yolo11":
        print("[Face Detection] Including: vision/detect (YOLO11)")
    elif model_type == "pose_detection":
        print("[Face Detection] Including: vision/detect (pose detection)")

    # Files to exclude (conditionally based on model type)
    esp_dl_exclude = [
        "dl_base_dotprod.cpp",       # Use custom implementation
        "dl_image_jpeg.cpp",         # JPEG not used
        "dl_image_bmp.cpp",          # BMP not used
    ]

    # Exclude ONLY YOLO/pose-specific files if not needed
    # NOTE: MSR/MNP postprocessors are needed for face detection!
    if model_type == "face_recognition":
        esp_dl_exclude.extend([
            "dl_detect_yolo11_postprocessor.cpp",   # YOLO11 only
            "dl_pose_yolo11_postprocessor.cpp",     # Pose only
            "dl_detect_espdet_postprocessor.cpp",   # EspDet only
            "dl_detect_pico_postprocessor.cpp",     # Pico only
        ])
        print("[Face Detection] Excluding: YOLO11/EspDet/Pico postprocessors (keeping MSR/MNP for face detection)")
    elif model_type == "yolo11":
        # Exclude pose-specific files when using YOLO11
        esp_dl_exclude.extend([
            "dl_pose_yolo11_postprocessor.cpp",
            "dl_detect_msr_postprocessor.cpp",      # Face detection specific
            "dl_detect_mnp_postprocessor.cpp",      # Face detection specific
        ])
        print("[Face Detection] Excluding: pose/face detection postprocessors (using YOLO11)")
    elif model_type == "pose_detection":
        # Exclude face detection postprocessors for pose
        esp_dl_exclude.extend([
            "dl_detect_msr_postprocessor.cpp",      # Face detection specific
            "dl_detect_mnp_postprocessor.cpp",      # Face detection specific
        ])
        print("[Face Detection] Excluding: face detection postprocessors (using pose detection)")

    # Count files by category for better visibility
    sources_count = {"base": 0, "isa": 0, "core": 0, "vision": 0}

    # Add sources from specific directories
    for src_dir in esp_dl_source_dirs:
        src_dir_path = os.path.join(esp_dl_dir, src_dir)
        if os.path.exists(src_dir_path):
            # Use recursive glob for ALL vision/* directories to get files in subdirectories
            if src_dir.startswith("vision/"):
                pattern = os.path.join(src_dir_path, "**", "*.cpp")
                for src_file in glob.glob(pattern, recursive=True):
                    if os.path.basename(src_file) not in esp_dl_exclude:
                        sources_to_add.append(src_file)
                        sources_count["vision"] += 1
            else:
                for src_file in glob.glob(os.path.join(src_dir_path, "*.cpp")):
                    if os.path.basename(src_file) not in esp_dl_exclude:
                        sources_to_add.append(src_file)
                        sources_count["core"] += 1

    # Add ALL dl/base/*.cpp files (required for neural network operations)
    dl_base_dir = os.path.join(esp_dl_dir, "dl", "base")
    if os.path.exists(dl_base_dir):
        for src_file in glob.glob(os.path.join(dl_base_dir, "*.cpp")):
            if os.path.basename(src_file) not in esp_dl_exclude:
                sources_to_add.append(src_file)
                sources_count["base"] += 1

    # Add ESP32P4 ISA files (optimized assembly for ESP32P4)
    isa_dirs = [
        ("dl/base/isa/esp32p4", "*.S"),
        ("dl/base/isa/esp32p4", "*.cpp"),
        ("dl/tool/isa/esp32p4", "*.S"),
        ("vision/image/isa/esp32p4", "*.S"),
    ]

    for isa_dir, pattern in isa_dirs:
        isa_path = os.path.join(esp_dl_dir, isa_dir)
        if os.path.exists(isa_path):
            for asm_file in glob.glob(os.path.join(isa_path, pattern)):
                sources_to_add.append(asm_file)
                sources_count["isa"] += 1

    esp_dl_total = sum(sources_count.values())
    print(f"[Face Detection] ESP-DL: {esp_dl_total} files (base:{sources_count['base']} isa:{sources_count['isa']} core:{sources_count['core']} vision:{sources_count['vision']})")

    # Add prebuilt FBS library
    fbs_lib_dir = os.path.join(esp_dl_dir, "fbs_loader", "lib", "esp32p4")
    fbs_lib = os.path.join(fbs_lib_dir, "libfbs_model.a")
    if os.path.exists(fbs_lib):
        env.Append(LIBPATH=[fbs_lib_dir])
        env.Prepend(LIBS=["fbs_model"])
        print("[Face Detection] Added libfbs_model.a")

# ========================================================================
# Add local stub files (in extra/ subfolder to avoid ESPHome auto-compile)
# ========================================================================
extra_dir = os.path.join(component_dir, "extra")

# Custom dotprod implementation (no DSP version)
dotprod_file = os.path.join(extra_dir, "dl_base_dotprod_no_dsp.cpp")
if os.path.exists(dotprod_file):
    sources_to_add.append(dotprod_file)
    print("[Face Detection] + extra/dl_base_dotprod_no_dsp.cpp")

# mbedTLS stub (if exists)
mbedtls_stub = os.path.join(extra_dir, "mbedtls_aes_stub.c")
if os.path.exists(mbedtls_stub):
    sources_to_add.append(mbedtls_stub)
    print("[Face Detection] + extra/mbedtls_aes_stub.c")

env.Append(CPPPATH=[component_dir])

# ========================================================================
# Compile sources
# ========================================================================
if sources_to_add:
    objects = []
    for src_file in sources_to_add:
        try:
            obj = env.Object(src_file)
            objects.extend(obj)
        except Exception as e:
            print(f"[Face Detection] Failed to compile {os.path.basename(src_file)}: {e}")

    if objects:
        # Create static library
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libface_detection"),
            objects
        )

        # Add library with proper linking flags for circular dependencies
        env.Append(LINKFLAGS=["-Wl,--start-group"])
        env.Prepend(LIBS=[lib])
        env.Append(LINKFLAGS=["-Wl,--end-group"])

        # Also add objects directly to ensure they're linked
        env.Append(PIOBUILDFILES=objects)

        print(f"[Face Detection] {len(sources_to_add)} source files compiled")
        print(f"[Face Detection] {len(objects)} object files created")
        print("[Face Detection] libface_detection.a created and linked")

print("[Face Detection] Build script completed")
