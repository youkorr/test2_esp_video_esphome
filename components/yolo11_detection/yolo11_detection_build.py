"""
Build script for YOLO11 Detection component
Embeds the YOLO11 model in flash rodata.
ESP-DL sources are compiled separately by the yolov11 build system.
"""

import os
import sys
import glob
import subprocess
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

# Find esp-dl (downloaded by PlatformIO lib_deps or local)
sys.path.insert(0, parent_components_dir)
from esp_dl_path import find_esp_dl
esp_dl_resolved_dir = find_esp_dl(env, fallback_components_dir=parent_components_dir)

print("[YOLO11 Detection] Build script running...")

# ========================================================================
# Helper function for caching
# ========================================================================
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
# Add include paths for ESP-DL headers
# ========================================================================
esp_dl_dir = esp_dl_resolved_dir
if os.path.exists(esp_dl_dir):
    esp_dl_include_dirs = [
        "dl", "dl/tool/include", "dl/tool/isa/esp32p4",
        "dl/tool/src", "dl/tensor/include", "dl/tensor/src",
        "dl/base", "dl/base/isa", "dl/base/isa/esp32p4",
        "dl/math/include", "dl/math/src", "dl/model/include",
        "dl/model/src", "dl/module/include", "dl/module/src",
        "fbs_loader/include", "fbs_loader/lib/esp32p4", "fbs_loader/src",
        "vision/detect", "vision/image", "vision/image/isa",
        "vision/image/isa/esp32p4",
    ]

    for inc_dir in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc_dir)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    print("[YOLO11 Detection] ESP-DL include paths added")

# ========================================================================
# Embed YOLO11 Detection Model in flash rodata
# ========================================================================
sources_to_add = []

yolo11_detect_dir = os.path.join(parent_components_dir, "yolo11_detect")
if os.path.exists(yolo11_detect_dir):
    models_dir = os.path.join(yolo11_detect_dir, "models", "p4")
    pack_script = os.path.join(yolo11_detect_dir, "pack_model.py")

    if os.path.exists(models_dir):
        yolo11_model = os.path.join(models_dir, "yolo11_detect_s8_v1.espdl")

        if os.path.exists(yolo11_model):
            embed_c_file = os.path.join(component_dir, "yolo11_detect_espdl_embed.c")

            if os.path.exists(pack_script) and needs_rebuild(embed_c_file, [yolo11_model]):
                # Try pack_model.py first
                print("[YOLO11 Detection] Packing yolo11_detect model...")
                packed_model = os.path.join(component_dir, "yolo11_detect.espdl")
                try:
                    cmd = [
                        "python3", pack_script,
                        "--model_path", yolo11_model,
                        "--out_file", packed_model
                    ]
                    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
                    if result.returncode == 0 and os.path.exists(packed_model):
                        model_file = packed_model
                    else:
                        print(f"[YOLO11 Detection] pack_model.py failed, using raw model")
                        model_file = yolo11_model
                except Exception as e:
                    print(f"[YOLO11 Detection] pack_model.py error: {e}, using raw model")
                    model_file = yolo11_model

                # Generate C embed file
                with open(model_file, 'rb') as f:
                    model_data = f.read()

                c_content = '// Auto-generated - embedded yolo11_detect model\n'
                c_content += '#include <stddef.h>\n'
                c_content += '#include <stdint.h>\n\n'
                c_content += '__attribute__((aligned(16)))\n'
                c_content += 'const uint8_t _binary_yolo11_detect_espdl_start[] = {\n'

                for i in range(0, len(model_data), 16):
                    chunk = model_data[i:i+16]
                    hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
                    c_content += f'    {hex_bytes},\n'

                c_content += '};\n\n'
                c_content += f'const uint8_t *_binary_yolo11_detect_espdl_end = '
                c_content += f'_binary_yolo11_detect_espdl_start + {len(model_data)};\n'
                c_content += f'const size_t _binary_yolo11_detect_espdl_size = {len(model_data)};\n'

                with open(embed_c_file, 'w') as f:
                    f.write(c_content)
                print(f"[YOLO11 Detection] Model embedded: {len(model_data)} bytes -> {embed_c_file}")

            elif needs_rebuild(embed_c_file, [yolo11_model]):
                # No pack_model.py, embed raw model directly
                print("[YOLO11 Detection] Embedding raw model (no pack_model.py)...")
                with open(yolo11_model, 'rb') as f:
                    model_data = f.read()

                c_content = '// Auto-generated - embedded yolo11_detect model (raw)\n'
                c_content += '#include <stddef.h>\n'
                c_content += '#include <stdint.h>\n\n'
                c_content += '__attribute__((aligned(16)))\n'
                c_content += 'const uint8_t _binary_yolo11_detect_espdl_start[] = {\n'

                for i in range(0, len(model_data), 16):
                    chunk = model_data[i:i+16]
                    hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
                    c_content += f'    {hex_bytes},\n'

                c_content += '};\n\n'
                c_content += f'const uint8_t *_binary_yolo11_detect_espdl_end = '
                c_content += f'_binary_yolo11_detect_espdl_start + {len(model_data)};\n'
                c_content += f'const size_t _binary_yolo11_detect_espdl_size = {len(model_data)};\n'

                with open(embed_c_file, 'w') as f:
                    f.write(c_content)
                print(f"[YOLO11 Detection] Raw model embedded: {len(model_data)} bytes")
            else:
                print("[YOLO11 Detection] Model embed cached (skip)")

            if os.path.exists(embed_c_file):
                sources_to_add.append(embed_c_file)
                print(f"[YOLO11 Detection] + yolo11_detect_espdl_embed.c")
        else:
            print(f"[YOLO11 Detection] ERROR: Model not found: {yolo11_model}")
    else:
        print(f"[YOLO11 Detection] ERROR: Models dir not found: {models_dir}")
else:
    print(f"[YOLO11 Detection] ERROR: yolo11_detect dir not found: {yolo11_detect_dir}")

# ========================================================================
# Compile embedded model source
# ========================================================================
if sources_to_add:
    objects = []
    for src_file in sources_to_add:
        try:
            obj = env.Object(src_file)
            objects.extend(obj)
        except Exception as e:
            print(f"[YOLO11 Detection] Failed to compile {os.path.basename(src_file)}: {e}")

    if objects:
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libyolo11_model_embed"),
            objects
        )
        env.Prepend(LIBS=[lib])
        env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
        print(f"[YOLO11 Detection] libyolo11_model_embed.a created")
else:
    print("[YOLO11 Detection] WARNING: No model embedded!")

env.Append(CPPPATH=[component_dir])
print("[YOLO11 Detection] Build script completed")
