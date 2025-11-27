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
# Pack and Embed Detection Models
# ========================================================================
import subprocess

# Pack human_face_detect models
human_face_detect_dir = os.path.join(parent_components_dir, "human_face_detect")
if os.path.exists(human_face_detect_dir):
    print("[LVGL Camera Display] 📦 Packing human_face_detect models...")

    models_dir = os.path.join(human_face_detect_dir, "models", "p4")
    pack_script = os.path.join(human_face_detect_dir, "pack_model.py")

    if os.path.exists(models_dir) and os.path.exists(pack_script):
        # Model files to pack
        msr_model = os.path.join(models_dir, "human_face_detect_msr_s8_v1.espdl")
        mnp_model = os.path.join(models_dir, "human_face_detect_mnp_s8_v1.espdl")

        if os.path.exists(msr_model) and os.path.exists(mnp_model):
            # Output packed model
            packed_model = os.path.join(component_dir, "human_face_detect.espdl")

            # Run pack_model.py
            try:
                cmd = [
                    "python3", pack_script,
                    "--model_path", msr_model, mnp_model,
                    "--out_file", packed_model
                ]
                result = subprocess.run(cmd, capture_output=True, text=True, check=False)
                if result.returncode == 0 and os.path.exists(packed_model):
                    print(f"[LVGL Camera Display] ✅ Models packed: {os.path.basename(packed_model)}")

                    # Create C file with embedded binary data
                    embed_c_file = os.path.join(component_dir, "human_face_detect_espdl_embed.c")

                    # Read binary data
                    with open(packed_model, 'rb') as f:
                        model_data = f.read()

                    # Generate C array
                    c_content = f'''// Auto-generated file - embedded human_face_detect model
#include <stddef.h>
#include <stdint.h>

__attribute__((aligned(16)))
const uint8_t _binary_human_face_detect_espdl_start[] = {{
'''
                    # Write bytes in rows of 16
                    for i in range(0, len(model_data), 16):
                        chunk = model_data[i:i+16]
                        hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
                        c_content += f'    {hex_bytes},\n'

                    c_content += f'''}};

const uint8_t *_binary_human_face_detect_espdl_end = _binary_human_face_detect_espdl_start + {len(model_data)};
const size_t _binary_human_face_detect_espdl_size = {len(model_data)};
'''

                    # Write C file
                    with open(embed_c_file, 'w') as f:
                        f.write(c_content)

                    # Add to sources
                    sources_to_add.append(embed_c_file)
                    print(f"[LVGL Camera Display] ✅ Model embedded: {len(model_data)} bytes")
                else:
                    error_msg = result.stderr if result.stderr else "Unknown error"
                    print(f"[LVGL Camera Display] ⚠️  Failed to pack models: {error_msg}")
            except Exception as e:
                print(f"[LVGL Camera Display] ⚠️  Error packing models: {e}")
        else:
            print(f"[LVGL Camera Display] ⚠️  Model files not found in {models_dir}")
    else:
        print(f"[LVGL Camera Display] ⚠️  pack_model.py or models dir not found")

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
# Pack and Embed Pedestrian Detection Model
# ========================================================================
pedestrian_detect_dir = os.path.join(parent_components_dir, "pedestrian_detect")
if os.path.exists(pedestrian_detect_dir):
    print("[LVGL Camera Display] 📦 Packing pedestrian_detect model...")

    models_dir = os.path.join(pedestrian_detect_dir, "models", "p4")
    pack_script = os.path.join(pedestrian_detect_dir, "pack_model.py")

    if os.path.exists(models_dir) and os.path.exists(pack_script):
        # Model file to pack
        pico_model = os.path.join(models_dir, "pedestrian_detect_pico_s8_v1.espdl")

        if os.path.exists(pico_model):
            # Output packed model
            packed_model = os.path.join(component_dir, "pedestrian_detect.espdl")

            # Run pack_model.py
            try:
                cmd = [
                    "python3", pack_script,
                    "--model_path", pico_model,
                    "--out_file", packed_model
                ]
                result = subprocess.run(cmd, capture_output=True, text=True, check=False)
                if result.returncode == 0 and os.path.exists(packed_model):
                    print(f"[LVGL Camera Display] ✅ Model packed: {os.path.basename(packed_model)}")

                    # Create C file with embedded binary data
                    embed_c_file = os.path.join(component_dir, "pedestrian_detect_espdl_embed.c")

                    # Read binary data
                    with open(packed_model, 'rb') as f:
                        model_data = f.read()

                    # Generate C array
                    c_content = f'''// Auto-generated file - embedded pedestrian_detect model
#include <stddef.h>
#include <stdint.h>

__attribute__((aligned(16)))
const uint8_t _binary_pedestrian_detect_espdl_start[] = {{
'''
                    # Write bytes in rows of 16
                    for i in range(0, len(model_data), 16):
                        chunk = model_data[i:i+16]
                        hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
                        c_content += f'    {hex_bytes},\n'

                    c_content += f'''}};

const uint8_t *_binary_pedestrian_detect_espdl_end = _binary_pedestrian_detect_espdl_start + {len(model_data)};
const size_t _binary_pedestrian_detect_espdl_size = {len(model_data)};
'''

                    # Write C file
                    with open(embed_c_file, 'w') as f:
                        f.write(c_content)

                    # Add to sources
                    sources_to_add.append(embed_c_file)
                    print(f"[LVGL Camera Display] ✅ Model embedded: {len(model_data)} bytes")
                else:
                    error_msg = result.stderr if result.stderr else "Unknown error"
                    print(f"[LVGL Camera Display] ⚠️  Failed to pack pedestrian model: {error_msg}")
            except Exception as e:
                print(f"[LVGL Camera Display] ⚠️  Error packing pedestrian model: {e}")
        else:
            print(f"[LVGL Camera Display] ⚠️  Pedestrian model file not found in {models_dir}")
    else:
        print(f"[LVGL Camera Display] ⚠️  pack_model.py or models dir not found for pedestrian_detect")

    # Add pedestrian_detect source
    pedestrian_sources = ["pedestrian_detect.cpp"]
    for src in pedestrian_sources:
        src_path = os.path.join(pedestrian_detect_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[LVGL Camera Display] + {src}")

# ========================================================================
# Custom dotprod implementation (without ESP-DSP dependency)
# ========================================================================
# Add our custom dotprod implementation that doesn't require ESP-DSP
dotprod_no_dsp = os.path.join(component_dir, "dl_base_dotprod_no_dsp.cpp")
if os.path.exists(dotprod_no_dsp):
    sources_to_add.append(dotprod_no_dsp)
    print("[LVGL Camera Display] + dl_base_dotprod_no_dsp.cpp (ESP-DSP free version)")

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

    # Files to exclude (missing external dependencies)
    esp_dl_exclude_files = [
        "dl_base_dotprod.cpp",       # Requires esp_dsp.h (ESP-DSP library)
        "dl_image_jpeg.cpp",         # Requires ESP-JPEG library
        "dl_image_bmp.cpp",          # May have dependencies we don't need
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
                    # Check if file should be excluded
                    basename = os.path.basename(src_file)
                    if basename not in esp_dl_exclude_files:
                        sources_to_add.append(src_file)
                        esp_dl_count += 1
                    else:
                        print(f"[LVGL Camera Display] ⊘ Excluded: {basename}")
        else:
            print(f"[LVGL Camera Display] ⚠️  Directory not found: {src_dir}")

    print(f"[LVGL Camera Display] ✓ ESP-DL: {esp_dl_count} source files from {len(esp_dl_source_dirs)} directories")

    # Add prebuilt FBS model library (required for FlatBuffers model loading)
    # IMPORTANT: Must be linked BEFORE our library to resolve symbols
    fbs_lib_dir = os.path.join(esp_dl_dir, "fbs_loader", "lib", "esp32p4")
    fbs_lib = os.path.join(fbs_lib_dir, "libfbs_model.a")
    if os.path.exists(fbs_lib):
        env.Append(LIBPATH=[fbs_lib_dir])
        # Use Prepend to ensure fbs_model is linked early
        env.Prepend(LIBS=["fbs_model"])
        print(f"[LVGL Camera Display] ✓ ESP-DL: Added libfbs_model.a (FlatBuffers loader)")
    else:
        print(f"[LVGL Camera Display] ⚠️  libfbs_model.a not found at {fbs_lib}")

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

        # Link both our library and libfbs_model.a with --whole-archive
        # This ensures all symbols are included, resolving circular dependencies
        fbs_lib_path = os.path.join(parent_components_dir, "esp-dl", "fbs_loader", "lib", "esp32p4", "libfbs_model.a")

        if os.path.exists(fbs_lib_path):
            # Pass the File nodes directly to LINKFLAGS for proper resolution
            lib_node = lib[0]  # SCons File node
            fbs_lib_node = env.File(fbs_lib_path)  # Convert to File node

            # Add both libraries with --whole-archive
            env.Append(LINKFLAGS=[
                "-Wl,--whole-archive",
                lib_node,
                fbs_lib_node,
                "-Wl,--no-whole-archive"
            ])

            print(f"[LVGL Camera Display] ✓ {len(sources_to_add)} source files compiled")
            print(f"[LVGL Camera Display] ✓ liblvgl_camera_display_detect.a created")
            print(f"[LVGL Camera Display] ✓ Both libraries linked with --whole-archive")
        else:
            print(f"[LVGL Camera Display] ⚠️  libfbs_model.a not found at {fbs_lib_path}")
            # Fallback: just add our library normally
            env.Prepend(LIBS=[lib])
else:
    print("[LVGL Camera Display] ⚠️  No sources to compile")

print("[LVGL Camera Display] Build script completed")
