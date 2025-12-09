"""
Build script for Face Detection component
Compiles ESP-DL sources and embeds face detection/recognition models
"""

import os
import glob
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[Face Detection] Build script running...")

# ========================================================================
# Add CONFIG defines for detection models
# ========================================================================
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32P4", "1"),
])

# Human face detection configuration
env.Append(CPPDEFINES=[
    ("CONFIG_HUMAN_FACE_DETECT_MSR_S8_V1"),
    ("CONFIG_HUMAN_FACE_DETECT_MNP_S8_V1"),
    ("CONFIG_HUMAN_FACE_DETECT_MODEL_TYPE", "0"),
    ("CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA", "1"),
    ("CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION", "0"),
])

# Human face recognition configuration
env.Append(CPPDEFINES=[
    ("CONFIG_HUMAN_FACE_FEAT_MFN_S8_V1", "1"),
    ("CONFIG_HUMAN_FACE_FEAT_MODEL_TYPE", "0"),
    ("CONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_RODATA", "1"),
    ("CONFIG_HUMAN_FACE_FEAT_MODEL_LOCATION", "0"),
])

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
# Pack and Embed Face Detection Models
# ========================================================================
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
# Pack and Embed Face Recognition Model
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

# ========================================================================
# ESP-DL Sources
# ========================================================================
esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
if os.path.exists(esp_dl_dir):
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

    print(f"[Face Detection] ESP-DL includes added")

    esp_dl_source_dirs = [
        "dl", "dl/tool/include", "dl/tool/isa/esp32p4", "dl/tool/src",
        "dl/tensor/include", "dl/tensor/src", "dl/base", "dl/base/isa",
        "dl/base/isa/esp32p4", "dl/math/include", "dl/math/src", "dl/model/include",
        "dl/model/src", "dl/module/include", "dl/module/src", "fbs_loader/include",
        "fbs_loader/lib/esp32p4", "fbs_loader/src", "vision/detect", "vision/image",
        "vision/image/isa", "vision/image/isa/esp32p4", "vision/recognition",
        "vision/classification",
    ]

    esp_dl_exclude_files = [
        "dl_base_dotprod.cpp",
        "dl_image_jpeg.cpp",
        "dl_image_bmp.cpp",
    ]

    esp_dl_count = 0
    for src_dir in esp_dl_source_dirs:
        src_dir_path = os.path.join(esp_dl_dir, src_dir)
        if os.path.exists(src_dir_path):
            patterns = ['*.cpp', '*.c', '*.S']
            for pattern in patterns:
                files = glob.glob(os.path.join(src_dir_path, pattern))
                for src_file in files:
                    basename = os.path.basename(src_file)
                    if basename not in esp_dl_exclude_files:
                        sources_to_add.append(src_file)
                        esp_dl_count += 1

    print(f"[Face Detection] ESP-DL: {esp_dl_count} source files")

    # Add prebuilt FBS library
    fbs_lib_dir = os.path.join(esp_dl_dir, "fbs_loader", "lib", "esp32p4")
    fbs_lib = os.path.join(fbs_lib_dir, "libfbs_model.a")
    if os.path.exists(fbs_lib):
        env.Append(LIBPATH=[fbs_lib_dir])
        env.Prepend(LIBS=["fbs_model"])
        print("[Face Detection] Added libfbs_model.a")

    env.Append(CPPPATH=[component_dir])

# ========================================================================
# Copy stub files from lvgl_camera_display if they exist
# ========================================================================
lvgl_cam_dir = os.path.join(parent_components_dir, "lvgl_camera_display")

# Custom dotprod implementation
dotprod_src = os.path.join(lvgl_cam_dir, "dl_base_dotprod_no_dsp.cpp")
dotprod_dst = os.path.join(component_dir, "dl_base_dotprod_no_dsp.cpp")
if os.path.exists(dotprod_src) and not os.path.exists(dotprod_dst):
    import shutil
    shutil.copy(dotprod_src, dotprod_dst)
if os.path.exists(dotprod_dst):
    sources_to_add.append(dotprod_dst)
    print("[Face Detection] + dl_base_dotprod_no_dsp.cpp")

# mbedTLS stub
mbedtls_stub_src = os.path.join(lvgl_cam_dir, "mbedtls_aes_stub.c")
mbedtls_stub_dst = os.path.join(component_dir, "mbedtls_aes_stub.c")
if os.path.exists(mbedtls_stub_src) and not os.path.exists(mbedtls_stub_dst):
    import shutil
    shutil.copy(mbedtls_stub_src, mbedtls_stub_dst)
if os.path.exists(mbedtls_stub_dst):
    sources_to_add.append(mbedtls_stub_dst)
    print("[Face Detection] + mbedtls_aes_stub.c")

# mbedTLS header directory
mbedtls_header_src = os.path.join(lvgl_cam_dir, "mbedtls")
mbedtls_header_dst = os.path.join(component_dir, "mbedtls")
if os.path.exists(mbedtls_header_src) and not os.path.exists(mbedtls_header_dst):
    import shutil
    shutil.copytree(mbedtls_header_src, mbedtls_header_dst)

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
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libface_detection"),
            objects
        )
        env.Prepend(LIBS=[lib])
        env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
        print(f"[Face Detection] {len(sources_to_add)} source files compiled")
        print("[Face Detection] libface_detection.a created")

print("[Face Detection] Build script completed")
