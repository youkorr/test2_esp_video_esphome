"""
Build script for Face Detection component
Embeds face detection/recognition models and compiles ESP-DL core sources.
Wrapper files at component root handle face-specific sources (auto-compiled by ESPHome).
"""

import os
import sys
import glob
import subprocess
Import("env")

# ========================================================================
# Resolve component directory
# ========================================================================
component_dir = None
parent_components_dir = None

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

if not component_dir:
    script_dir = Dir('.').srcnode().abspath
    component_dir = script_dir
    parent_components_dir = os.path.dirname(component_dir)
    print(f"[Face Detection] WARNING: Using Dir('.') fallback: {component_dir}")

print(f"[Face Detection] component_dir = {component_dir}")
print(f"[Face Detection] parent_components_dir = {parent_components_dir}")

# ========================================================================
# Find esp-dl (local components/esp-dl/ or PlatformIO libdeps)
# ========================================================================
sys.path.insert(0, parent_components_dir)
from esp_dl_path import find_esp_dl
esp_dl_dir = find_esp_dl(env, fallback_components_dir=parent_components_dir)
print(f"[Face Detection] ESP-DL: {esp_dl_dir}")

# ========================================================================
# Detect model type from build flags
# ========================================================================
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

if has_face_recognition:
    model_type = "face_recognition"
elif has_yolo11:
    model_type = "yolo11"
elif has_pose:
    model_type = "pose_detection"
else:
    model_type = "face_recognition"

print(f"[Face Detection] Model type: {model_type}")

# ========================================================================
# Add CONFIG defines
# ========================================================================
env.Append(CPPDEFINES=[("CONFIG_IDF_TARGET_ESP32P4", "1")])

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

if "CONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_RODATA" not in existing_defines:
    env.Append(CPPDEFINES=[
        ("CONFIG_HUMAN_FACE_FEAT_MFN_S8_V1", "1"),
        ("CONFIG_HUMAN_FACE_FEAT_MODEL_TYPE", "0"),
        ("CONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_RODATA", "1"),
        ("CONFIG_HUMAN_FACE_FEAT_MODEL_LOCATION", "0"),
    ])

# ========================================================================
# Helper
# ========================================================================
def needs_rebuild(output_file, input_files):
    if not os.path.exists(output_file):
        return True
    output_mtime = os.path.getmtime(output_file)
    for input_file in input_files:
        if os.path.exists(input_file) and os.path.getmtime(input_file) > output_mtime:
            return True
    return False

# ========================================================================
# Pack and Embed Models (generates _embed.c files at component root)
# These .c files are auto-compiled by ESPHome since they're in the component dir.
# ========================================================================
sources_to_add = []

if model_type == "face_recognition":
    # Human face detection model
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
                            c_content = '// Auto-generated - embedded human_face_detect model\n'
                            c_content += '#include <stddef.h>\n#include <stdint.h>\n\n'
                            c_content += '__attribute__((aligned(16)))\nconst uint8_t _binary_human_face_detect_espdl_start[] = {\n'
                            for i in range(0, len(model_data), 16):
                                chunk = model_data[i:i+16]
                                hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
                                c_content += f'    {hex_bytes},\n'
                            c_content += '};\n\n'
                            c_content += f'const uint8_t *_binary_human_face_detect_espdl_end = _binary_human_face_detect_espdl_start + {len(model_data)};\n'
                            c_content += f'const size_t _binary_human_face_detect_espdl_size = {len(model_data)};\n'
                            with open(embed_c_file, 'w') as f:
                                f.write(c_content)
                            print(f"[Face Detection] Model embedded: {len(model_data)} bytes")
                    except Exception as e:
                        print(f"[Face Detection] Error packing models: {e}")
                else:
                    print("[Face Detection] human_face_detect models cached (skip)")

        env.Append(CPPPATH=[human_face_detect_dir])

    # Human face recognition model
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
                            c_content = '// Auto-generated - embedded human_face_feat model\n'
                            c_content += '#include <stddef.h>\n#include <stdint.h>\n\n'
                            c_content += '__attribute__((aligned(16)))\nconst uint8_t _binary_human_face_feat_mfn_s8_v1_espdl_start[] = {\n'
                            for i in range(0, len(model_data), 16):
                                chunk = model_data[i:i+16]
                                hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
                                c_content += f'    {hex_bytes},\n'
                            c_content += '};\n\n'
                            c_content += f'const uint8_t *_binary_human_face_feat_mfn_s8_v1_espdl_end = _binary_human_face_feat_mfn_s8_v1_espdl_start + {len(model_data)};\n'
                            c_content += f'const size_t _binary_human_face_feat_mfn_s8_v1_espdl_size = {len(model_data)};\n'
                            with open(embed_c_file, 'w') as f:
                                f.write(c_content)
                            print(f"[Face Detection] Recognition model embedded: {len(model_data)} bytes")
                    except Exception as e:
                        print(f"[Face Detection] Error packing recognition model: {e}")
                else:
                    print("[Face Detection] human_face_recognition model cached (skip)")

# ========================================================================
# ESP-DL Sources + Postprocessors + Recognition
# This script compiles: ESP-DL core, vision/detect, vision/recognition
# Wrapper files at component root (auto-compiled by ESPHome) handle:
#   - human_face_detect_wrapper.cpp -> human_face_detect.cpp
#   - human_face_recognition_wrapper.cpp -> human_face_recognition.cpp
#   - dl_base_dotprod_no_dsp.cpp -> custom dotprod (no DSP)
#   - mbedtls_aes_stub.c -> AES stubs
#   - human_face_detect_espdl_embed.c -> detection model data
#   - human_face_feat_espdl_embed.c -> recognition model data
# ========================================================================

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

# ESP-DL source directories
# Core + vision/detect + vision/recognition (compiled in build script)
# human_face_detect.cpp and human_face_recognition.cpp are handled by
# wrapper files at component root (auto-compiled by ESPHome)
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

# Add recognition sources if face recognition is enabled
if has_face_recognition:
    esp_dl_source_dirs.append("vision/recognition")
    print("[Face Detection] Including: vision/recognition")

# Files to exclude
esp_dl_exclude = [
    "dl_base_dotprod.cpp",       # Replaced by custom dl_base_dotprod_no_dsp.cpp (at component root)
    "dl_image_jpeg.cpp",         # JPEG not used
    "dl_image_bmp.cpp",          # BMP not used
]

# Exclude unused postprocessors
if not has_yolo11:
    esp_dl_exclude.append("dl_detect_yolo11_postprocessor.cpp")
if not has_pose:
    esp_dl_exclude.append("dl_pose_yolo11_postprocessor.cpp")
if not has_face_recognition:
    esp_dl_exclude.append("dl_detect_msr_postprocessor.cpp")
    esp_dl_exclude.append("dl_detect_mnp_postprocessor.cpp")
esp_dl_exclude.append("dl_detect_espdet_postprocessor.cpp")
esp_dl_exclude.append("dl_detect_pico_postprocessor.cpp")

sources_count = {"base": 0, "isa": 0, "core": 0, "vision": 0}

# Add core sources
for src_dir in esp_dl_source_dirs:
    src_dir_path = os.path.join(esp_dl_dir, src_dir)
    if os.path.exists(src_dir_path):
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

# Add dl/base/*.cpp (except dotprod - replaced by our custom version at component root)
dl_base_dir = os.path.join(esp_dl_dir, "dl", "base")
if os.path.exists(dl_base_dir):
    for src_file in glob.glob(os.path.join(dl_base_dir, "*.cpp")):
        if os.path.basename(src_file) not in esp_dl_exclude:
            sources_to_add.append(src_file)
            sources_count["base"] += 1

# Add ESP32P4 ISA files (optimized assembly)
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

env.Append(CPPPATH=[component_dir])

# ========================================================================
# Compile ESP-DL core sources into static library
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
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libface_detection"),
            objects
        )
        env.Append(LINKFLAGS=["-Wl,--start-group"])
        env.Prepend(LIBS=[lib])
        env.Append(LINKFLAGS=["-Wl,--end-group"])
        env.Append(PIOBUILDFILES=objects)

        print(f"[Face Detection] {len(sources_to_add)} source files compiled into libface_detection.a")

print("[Face Detection] Build script completed")
