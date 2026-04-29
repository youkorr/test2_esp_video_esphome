"""
Build script for YOLO11 Detection component
Compiles optimized ESP-DL sources for YOLO11 object detection
"""

import os
import glob
import shutil
import sys
Import("env")

# In a SCons post-script `Dir('.')` is the project's build dir, NOT this
# file's location. Use __file__ so we always resolve paths relative to the
# script itself.
try:
    component_dir = os.path.dirname(os.path.abspath(__file__))
except NameError:
    # Fallback for very old SCons that doesn't set __file__
    component_dir = Dir('.').srcnode().abspath
parent_components_dir = os.path.dirname(component_dir)

print("[YOLO11 Detection] Build script running...")
print(f"[YOLO11 Detection] component_dir         = {component_dir}")
print(f"[YOLO11 Detection] parent_components_dir = {parent_components_dir}")
print("[YOLO11 Detection] Model type: yolo11")

# ========================================================================
# Add CONFIG defines for YOLO11 detection
# ========================================================================
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32P4", "1"),
])

print("[YOLO11 Detection] CONFIG defines added")

sources_to_add = []

# ========================================================================
# ESP-DL Sources - Optimized for YOLO11 only
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
        "vision/image/isa", "vision/image/isa/esp32p4",
    ]

    for inc_dir in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc_dir)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    print("[YOLO11 Detection] ESP-DL includes added")

    # ESP-DL source files - YOLO11 only
    esp_dl_source_dirs = [
        "dl/tensor/src",
        "dl/model/src",
        "dl/module/src",
        "dl/tool/src",
        "dl/math/src",
        "fbs_loader/src",
        "vision/image",
        "vision/detect",  # Detection base (includes YOLO11)
    ]

    print("[YOLO11 Detection] Including: vision/detect (YOLO11 only)")

    # Files to exclude
    esp_dl_exclude = [
        "dl_base_dotprod.cpp",       # Use custom implementation
        "dl_image_jpeg.cpp",         # JPEG not used
        "dl_image_bmp.cpp",          # BMP not used
        "dl_detect_msr_postprocessor.cpp",
        "dl_detect_mnp_postprocessor.cpp",
        "dl_pose_yolo11_postprocessor.cpp",
        "dl_detect_espdet_postprocessor.cpp",
        "dl_detect_pico_postprocessor.cpp",
    ]

    print("[YOLO11 Detection] Excluding: face/pose/espdet/pico postprocessors (YOLO11 only)")

    sources_count = {"base": 0, "isa": 0, "core": 0, "vision": 0}

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

    dl_base_dir = os.path.join(esp_dl_dir, "dl", "base")
    if os.path.exists(dl_base_dir):
        for src_file in glob.glob(os.path.join(dl_base_dir, "*.cpp")):
            if os.path.basename(src_file) not in esp_dl_exclude:
                sources_to_add.append(src_file)
                sources_count["base"] += 1

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
    print(f"[YOLO11 Detection] ESP-DL: {esp_dl_total} files (base:{sources_count['base']} isa:{sources_count['isa']} core:{sources_count['core']} vision:{sources_count['vision']})")

    fbs_lib_dir = os.path.join(esp_dl_dir, "fbs_loader", "lib", "esp32p4")
    fbs_lib = os.path.join(fbs_lib_dir, "libfbs_model.a")
    if os.path.exists(fbs_lib):
        env.Append(LIBPATH=[fbs_lib_dir])
        env.Prepend(LIBS=["fbs_model"])
        print("[YOLO11 Detection] Added libfbs_model.a")

# ========================================================================
# Embed YOLO11 Detection Model
# ------------------------------------------------------------------------
# We always (re)generate the embed C file from scratch. The previous
# implementation tried to cache it via mtime checks, which masked failures
# when the file wasn't actually present in the build dir SCons enumerated.
# ========================================================================
sdcard_mode = False

for define in env.get("CPPDEFINES", []):
    if isinstance(define, tuple) and define[0] == "CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD":
        sdcard_mode = (define[1] == "1")
        break
    elif define == "CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD":
        sdcard_mode = True
        break

if not sdcard_mode:
    build_flags = env.get("BUILD_FLAGS", [])
    for flag in build_flags:
        if "CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD" in str(flag):
            sdcard_mode = True
            break

if sdcard_mode:
    print("[YOLO11 Detection] SD card mode enabled - skipping model embedding")
else:
    print("[YOLO11 Detection] Flash rodata mode - embedding model in firmware")

yolo11_detect_dir = os.path.join(parent_components_dir, "yolo11_detect")
print(f"[YOLO11 Detection] yolo11_detect_dir = {yolo11_detect_dir} "
      f"(exists={os.path.exists(yolo11_detect_dir)})")

if not sdcard_mode:
    if not os.path.exists(yolo11_detect_dir):
        sys.exit(f"[YOLO11 Detection] FATAL: yolo11_detect dir not found at {yolo11_detect_dir}")

    yolo11_model = os.path.join(yolo11_detect_dir, "models", "p4",
                                "yolo11_detect_s8_v1.espdl")
    print(f"[YOLO11 Detection] model file = {yolo11_model} "
          f"(exists={os.path.exists(yolo11_model)})")

    if not os.path.exists(yolo11_model):
        sys.exit(f"[YOLO11 Detection] FATAL: model file missing at {yolo11_model}")

    embed_c_file = os.path.join(component_dir, "yolo11_detect_espdl_embed.c")

    print(f"[YOLO11 Detection] Embedding {os.path.basename(yolo11_model)} -> {embed_c_file}")
    with open(yolo11_model, "rb") as f:
        model_data = f.read()

    c_lines = [
        "// Auto-generated - embedded yolo11_detect model",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "__attribute__((aligned(16)))",
        "const uint8_t _binary_yolo11_detect_espdl_start[] = {",
    ]
    for i in range(0, len(model_data), 16):
        chunk = model_data[i:i + 16]
        c_lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    c_lines.append("};")
    c_lines.append("")
    c_lines.append(
        f"const uint8_t *const _binary_yolo11_detect_espdl_end = "
        f"_binary_yolo11_detect_espdl_start + {len(model_data)};"
    )
    c_lines.append(
        f"const size_t _binary_yolo11_detect_espdl_size = {len(model_data)};"
    )
    c_lines.append("")

    with open(embed_c_file, "w") as f:
        f.write("\n".join(c_lines))

    if not os.path.exists(embed_c_file):
        sys.exit(f"[YOLO11 Detection] FATAL: embed C file was not created at {embed_c_file}")

    print(f"[YOLO11 Detection] Model embedded: {len(model_data)} bytes -> "
          f"{os.path.basename(embed_c_file)}")
    sources_to_add.append(embed_c_file)

# ========================================================================
# Add yolo11_detect wrapper sources
# ========================================================================
if os.path.exists(yolo11_detect_dir):
    env.Append(CPPPATH=[yolo11_detect_dir])
    yolo11_sources = ["yolo11_detect.cpp"]
    for src in yolo11_sources:
        src_path = os.path.join(yolo11_detect_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[YOLO11 Detection] + {src}")

# ========================================================================
# Add local stub files (if needed)
# ========================================================================
dotprod_file = os.path.join(component_dir, "dl_base_dotprod_no_dsp.cpp")
if os.path.exists(dotprod_file):
    sources_to_add.append(dotprod_file)
    print("[YOLO11 Detection] + dl_base_dotprod_no_dsp.cpp")
else:
    face_detection_dir = os.path.join(parent_components_dir, "face_detection")
    dotprod_file_alt = os.path.join(face_detection_dir, "dl_base_dotprod_no_dsp.cpp")
    if os.path.exists(dotprod_file_alt):
        sources_to_add.append(dotprod_file_alt)
        print("[YOLO11 Detection] + dl_base_dotprod_no_dsp.cpp (from face_detection)")

mbedtls_stub = os.path.join(component_dir, "mbedtls_aes_stub.c")
if os.path.exists(mbedtls_stub):
    sources_to_add.append(mbedtls_stub)
    print("[YOLO11 Detection] + mbedtls_aes_stub.c")
else:
    face_detection_dir = os.path.join(parent_components_dir, "face_detection")
    mbedtls_stub_alt = os.path.join(face_detection_dir, "mbedtls_aes_stub.c")
    if os.path.exists(mbedtls_stub_alt):
        sources_to_add.append(mbedtls_stub_alt)
        print("[YOLO11 Detection] + mbedtls_aes_stub.c (from face_detection)")

env.Append(CPPPATH=[component_dir])

# ========================================================================
# Compile sources
# ========================================================================
embed_in_sources = any("yolo11_detect_espdl_embed.c" in s for s in sources_to_add)
print(f"[YOLO11 Detection] embed file in sources_to_add: {embed_in_sources}")

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
            os.path.join("$BUILD_DIR", "libyolo11_detection"),
            objects
        )
        env.Prepend(LIBS=[lib])
        env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
        print(f"[YOLO11 Detection] {len(sources_to_add)} source files compiled")
        print("[YOLO11 Detection] libyolo11_detection.a created")

print("[YOLO11 Detection] Build script completed")
