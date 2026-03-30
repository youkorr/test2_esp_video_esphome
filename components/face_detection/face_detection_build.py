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
# Find esp-dl (try local first, then download cache, then esp_dl_path)
# ========================================================================
sys.path.insert(0, parent_components_dir)
esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
if not os.path.isdir(esp_dl_dir) or not os.path.exists(os.path.join(esp_dl_dir, "dl")):
    # Not local — will be downloaded by build_espdl() below
    esp_dl_dir = None
else:
    print(f"[Face Detection] ESP-DL (local): {esp_dl_dir}")

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
# Shared ESP-DL compilation (compiles once, reused by all detection components)
# Wrapper files at component root (auto-compiled by ESPHome) handle:
#   - human_face_detect_wrapper.cpp -> human_face_detect.cpp
#   - human_face_recognition_wrapper.cpp -> human_face_recognition.cpp
#   - dl_base_dotprod_no_dsp.cpp -> custom dotprod (no DSP)
#   - mbedtls_aes_stub.c -> AES stubs
#   - human_face_detect_espdl_embed.c -> detection model data
#   - human_face_feat_espdl_embed.c -> recognition model data
# ========================================================================
from esp_dl_build import build_espdl
esp_dl_dir = build_espdl(env, esp_dl_dir=esp_dl_dir, isa_target="esp32p4")
print(f"[Face Detection] ESP-DL: {esp_dl_dir}")

env.Append(CPPPATH=[component_dir])

# ========================================================================
# Compile face-specific model embed sources (not shared)
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
            os.path.join("$BUILD_DIR", "libface_model_embed"),
            objects
        )
        env.Prepend(LIBS=[lib])
        if '-Wl,--start-group' not in env.get('_LIBFLAGS', ''):
            env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
        print(f"[Face Detection] {len(sources_to_add)} model files compiled into libface_model_embed.a")

print("[Face Detection] Build script completed")
