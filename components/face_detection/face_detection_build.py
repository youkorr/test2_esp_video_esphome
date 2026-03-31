"""
Build script for Face Detection component
Embeds face detection/recognition models and compiles optimized ESP-DL sources
"""

import os
import glob
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[Face Detection] Build script running...")

# Detect model type from build flags
model_type = "face_recognition"  # default
cpp_defines = env.get('CPPDEFINES', [])
for define in cpp_defines:
    if isinstance(define, tuple):
        key, val = define
    else:
        key = define
        val = None

    if key == "ESP_DL_MODEL_YOLO11":
        model_type = "yolo11"
        break
    elif key == "ESP_DL_MODEL_POSE_DETECTION":
        model_type = "pose_detection"
        break
    elif key == "ESP_DL_MODEL_FACE_RECOGNITION":
        model_type = "face_recognition"

print(f"[Face Detection] Model type: {model_type}")

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
esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
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

    for inc_dir in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc_dir)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    print("[Face Detection] ESP-DL includes added")

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
# Add local stub files (already in face_detection component)
# ========================================================================
# Custom dotprod implementation (no DSP version)
dotprod_file = os.path.join(component_dir, "dl_base_dotprod_no_dsp.cpp")
if os.path.exists(dotprod_file):
    sources_to_add.append(dotprod_file)
    print("[Face Detection] + dl_base_dotprod_no_dsp.cpp")

# mbedTLS stub (if exists)
mbedtls_stub = os.path.join(component_dir, "mbedtls_aes_stub.c")
if os.path.exists(mbedtls_stub):
    sources_to_add.append(mbedtls_stub)
    print("[Face Detection] + mbedtls_aes_stub.c")

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
