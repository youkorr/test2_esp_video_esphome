"""
Build script for Pedestrian Detection component
Embeds pedestrian detection model and compiles detection-specific sources
ESP-DL core is provided by the esp_dl component
"""

import os
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[Pedestrian Detection] Build script running...")

# ========================================================================
# Add CONFIG defines for detection models
# ========================================================================
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32P4", "1"),
])

# Pedestrian detection configuration
env.Append(CPPDEFINES=[
    ("CONFIG_PEDESTRIAN_DETECT_PICO_S8_V1", "1"),
    ("CONFIG_PEDESTRIAN_DETECT_MODEL_TYPE", "0"),
    ("CONFIG_PEDESTRIAN_DETECT_MODEL_IN_FLASH_RODATA", "1"),
    ("CONFIG_PEDESTRIAN_DETECT_MODEL_LOCATION", "0"),
])

print("[Pedestrian Detection] CONFIG defines added")

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
# Pack and Embed Pedestrian Detection Model
# ========================================================================
pedestrian_detect_dir = os.path.join(parent_components_dir, "pedestrian_detect")
if os.path.exists(pedestrian_detect_dir):
    models_dir = os.path.join(pedestrian_detect_dir, "models", "p4")
    pack_script = os.path.join(pedestrian_detect_dir, "pack_model.py")

    if os.path.exists(models_dir) and os.path.exists(pack_script):
        pico_model = os.path.join(models_dir, "pedestrian_detect_pico_s8_v1.espdl")

        if os.path.exists(pico_model):
            packed_model = os.path.join(component_dir, "pedestrian_detect.espdl")
            embed_c_file = os.path.join(component_dir, "pedestrian_detect_espdl_embed.c")

            if needs_rebuild(embed_c_file, [pico_model, pack_script]):
                print("[Pedestrian Detection] Packing pedestrian_detect model...")
                try:
                    cmd = [
                        "python3", pack_script,
                        "--model_path", pico_model,
                        "--out_file", packed_model
                    ]
                    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
                    if result.returncode == 0 and os.path.exists(packed_model):
                        with open(packed_model, 'rb') as f:
                            model_data = f.read()

                        c_content = '''// Auto-generated - embedded pedestrian_detect model
#include <stddef.h>
#include <stdint.h>

__attribute__((aligned(16)))
const uint8_t _binary_pedestrian_detect_espdl_start[] = {
'''
                        for i in range(0, len(model_data), 16):
                            chunk = model_data[i:i+16]
                            hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
                            c_content += f'    {hex_bytes},\n'

                        c_content += f'''}};

const uint8_t *_binary_pedestrian_detect_espdl_end = _binary_pedestrian_detect_espdl_start + {len(model_data)};
const size_t _binary_pedestrian_detect_espdl_size = {len(model_data)};
'''
                        with open(embed_c_file, 'w') as f:
                            f.write(c_content)
                        print(f"[Pedestrian Detection] Model embedded: {len(model_data)} bytes")
                except Exception as e:
                    print(f"[Pedestrian Detection] Error packing model: {e}")
            else:
                print("[Pedestrian Detection] pedestrian_detect model cached (skip)")

            if os.path.exists(embed_c_file):
                sources_to_add.append(embed_c_file)

    env.Append(CPPPATH=[pedestrian_detect_dir])
    pedestrian_sources = ["pedestrian_detect.cpp"]
    for src in pedestrian_sources:
        src_path = os.path.join(pedestrian_detect_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[Pedestrian Detection] + {src}")

env.Append(CPPPATH=[component_dir])

# ========================================================================
# Compile sources (only detection-specific, ESP-DL is in esp_dl component)
# ========================================================================
if sources_to_add:
    objects = []
    for src_file in sources_to_add:
        try:
            obj = env.Object(src_file)
            objects.extend(obj)
        except Exception as e:
            print(f"[Pedestrian Detection] Failed to compile {os.path.basename(src_file)}: {e}")

    if objects:
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libpedestrian_detection"),
            objects
        )
        env.Prepend(LIBS=[lib])
        print(f"[Pedestrian Detection] {len(sources_to_add)} source files compiled")
        print("[Pedestrian Detection] libpedestrian_detection.a created")

print("[Pedestrian Detection] Build script completed")
