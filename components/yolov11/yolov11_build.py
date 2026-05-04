"""
Build script for the ESP32-S3 yolov11 ESPHome component.

Pulls in the ESP-DL sources for ESP32-S3 and embeds the YOLO11 model
as flash rodata. The yolo11_detect inner wrapper (.cpp + .hpp) lives
INSIDE the yolov11/ component now, so it is auto-compiled by ESPHome's
main src/ pass and we don't have to add it from a sibling directory.
"""

import os
import glob
import sys

Import("env")  # noqa: F821 (provided by SCons / PlatformIO)


# Resolve path to this file. PlatformIO post-scripts have __file__ set.
try:
    component_dir = os.path.dirname(os.path.abspath(__file__))
except NameError:
    component_dir = Dir('.').srcnode().abspath  # noqa: F821

parent_components_dir = os.path.dirname(component_dir)

print("[YOLOv11 S3 Build] Build script running...")
print(f"[YOLOv11 S3 Build] component_dir         = {component_dir}")
print(f"[YOLOv11 S3 Build] parent_components_dir = {parent_components_dir}")

# ---------------------------------------------------------------------------
# CONFIG defines for ESP32-S3
# ---------------------------------------------------------------------------
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32S3", "1"),
])
print("[YOLOv11 S3 Build] CONFIG_IDF_TARGET_ESP32S3=1")

sources_to_add = []

# ---------------------------------------------------------------------------
# ESP-DL Sources - YOLO11 only, ESP32-S3 ISA
# ---------------------------------------------------------------------------
esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
if os.path.exists(esp_dl_dir):
    esp_dl_include_dirs = [
        "dl",
        "dl/tool/include",
        "dl/tool/isa/esp32s3",
        "dl/tool/isa/tie728",
        "dl/tool/isa/xtensa",
        "dl/tool/src",
        "dl/tensor/include",
        "dl/tensor/src",
        "dl/base",
        "dl/base/isa",
        "dl/base/isa/esp32s3",
        "dl/base/isa/tie728",
        "dl/base/isa/xtensa",
        "dl/math/include",
        "dl/math/src",
        "dl/model/include",
        "dl/model/src",
        "dl/module/include",
        "dl/module/src",
        "fbs_loader/include",
        "fbs_loader/lib/esp32s3",
        "fbs_loader/src",
        "vision/detect",
        "vision/image",
        "vision/image/isa",
        "vision/image/isa/esp32s3",
    ]
    for inc in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])
    print("[YOLOv11 S3 Build] ESP-DL include paths added")

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
    esp_dl_exclude = [
        # We provide a no-DSP dotprod ourselves
        "dl_base_dotprod.cpp",
        # JPEG/BMP not used here
        "dl_image_jpeg.cpp",
        "dl_image_bmp.cpp",
        # Other detection variants not needed
        "dl_detect_msr_postprocessor.cpp",
        "dl_detect_mnp_postprocessor.cpp",
        "dl_pose_yolo11_postprocessor.cpp",
        "dl_detect_espdet_postprocessor.cpp",
        "dl_detect_pico_postprocessor.cpp",
    ]

    counts = {"base": 0, "isa": 0, "core": 0, "vision": 0}
    for src_dir in esp_dl_source_dirs:
        src_dir_path = os.path.join(esp_dl_dir, src_dir)
        if not os.path.exists(src_dir_path):
            continue
        if src_dir.startswith("vision/"):
            for src_file in glob.glob(
                os.path.join(src_dir_path, "**", "*.cpp"), recursive=True
            ):
                if os.path.basename(src_file) in esp_dl_exclude:
                    continue
                sources_to_add.append(src_file)
                counts["vision"] += 1
        else:
            for src_file in glob.glob(os.path.join(src_dir_path, "*.cpp")):
                if os.path.basename(src_file) in esp_dl_exclude:
                    continue
                sources_to_add.append(src_file)
                counts["core"] += 1

    dl_base_dir = os.path.join(esp_dl_dir, "dl", "base")
    for src_file in glob.glob(os.path.join(dl_base_dir, "*.cpp")):
        if os.path.basename(src_file) in esp_dl_exclude:
            continue
        sources_to_add.append(src_file)
        counts["base"] += 1

    # ESP32-S3 ISA - assembly + cpp
    s3_isa_dirs = [
        ("dl/base/isa/esp32s3", "*.S"),
        ("dl/base/isa/esp32s3", "*.cpp"),
        ("dl/tool/isa/esp32s3", "*.S"),
        ("dl/tool/isa/esp32s3", "*.cpp"),
        ("vision/image/isa/esp32s3", "*.S"),
        ("vision/image/isa/esp32s3", "*.cpp"),
    ]
    for sub, pattern in s3_isa_dirs:
        path = os.path.join(esp_dl_dir, sub)
        if not os.path.exists(path):
            continue
        for f in glob.glob(os.path.join(path, pattern)):
            sources_to_add.append(f)
            counts["isa"] += 1

    print(f"[YOLOv11 S3 Build] ESP-DL: base:{counts['base']} "
          f"isa:{counts['isa']} core:{counts['core']} vision:{counts['vision']}")

    # Prebuilt FBS lib for S3
    fbs_lib_dir = os.path.join(esp_dl_dir, "fbs_loader", "lib", "esp32s3")
    fbs_lib = os.path.join(fbs_lib_dir, "libfbs_model.a")
    if os.path.exists(fbs_lib):
        env.Append(LIBPATH=[fbs_lib_dir])
        env.Prepend(LIBS=["fbs_model"])
        print("[YOLOv11 S3 Build] Linked libfbs_model.a (S3)")

# ---------------------------------------------------------------------------
# yolo11_detect inner wrapper now lives INSIDE this component
# (yolov11/yolo11_detect.hpp + yolov11/yolo11_detect_inner.cpp), so we
# don't need to pull anything from the sibling yolo11_detect/ directory.
# We only need to keep the include path for the .hpp visible to ESPHome's
# main src/ build, and that's already handled because the file is in
# the component's own dir.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Embed model in flash rodata (Option C)
# Only when YOLOV11_MODEL_FROM_FILE is NOT defined.
# Also silently skip if the file already exists with the same model
# (the embed C file lives in component_dir which gets re-used across
# rebuilds).
# ---------------------------------------------------------------------------
def has_define(name):
    for d in env.get("CPPDEFINES", []):
        if isinstance(d, (tuple, list)) and d and d[0] == name:
            return True
        if isinstance(d, str) and d.split("=", 1)[0] == name:
            return True
    return False

model_from_file = has_define("YOLOV11_MODEL_FROM_FILE")
if not model_from_file:
    candidates = []
    own_dir = os.path.join(component_dir, "models")
    if os.path.isdir(own_dir):
        candidates += sorted(glob.glob(os.path.join(own_dir, "*.espdl")))
    # Fallback: the model in the sibling yolo11_detect/ external_components
    # directory. ESPHome syncs the whole repo's components/ folder, so this
    # path exists at build time even though yolo11_detect itself is not
    # listed in the user's `components: [...]`.
    fallback = os.path.join(parent_components_dir, "yolo11_detect", "models",
                            "p4", "yolo11_detect_s8_v1.espdl")
    if not candidates and os.path.exists(fallback):
        candidates.append(fallback)

    if not candidates:
        sys.exit("[YOLOv11 S3 Build] FATAL: no .espdl model found for embedding. "
                 "Drop a .espdl in components/yolov11/models/.")

    model_path = candidates[0]
    embed_c = os.path.join(component_dir, "yolov11_model_embed.c")

    print(f"[YOLOv11 S3 Build] Embedding model: {model_path}")
    with open(model_path, "rb") as f:
        data = f.read()
    lines = [
        "// Auto-generated YOLOv11 model blob",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "__attribute__((aligned(16)))",
        "const uint8_t _binary_yolo11_detect_espdl_start[] = {",
    ]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines += [
        "};",
        "",
        f"const uint8_t *const _binary_yolo11_detect_espdl_end = "
        f"_binary_yolo11_detect_espdl_start + {len(data)};",
        f"const size_t _binary_yolo11_detect_espdl_size = {len(data)};",
        "",
    ]
    with open(embed_c, "w") as f:
        f.write("\n".join(lines))
    # ESPHome auto-compiles every .c in the component dir, so this gets
    # picked up by the main src/ pass. Don't add it to sources_to_add to
    # avoid double-compilation.
    print(f"[YOLOv11 S3 Build] Embedded {len(data)} bytes -> "
          f"{os.path.basename(embed_c)} (auto-compiled by main pass)")
else:
    print("[YOLOv11 S3 Build] YOLOV11_MODEL_FROM_FILE set - skipping flash embed")

# ---------------------------------------------------------------------------
# Local stub helpers: dotprod (no DSP) + mbedtls AES stub
# These need to be added to OUR static lib because they reference
# ESP-DL internals; ESPHome's main src/ pass doesn't have those on
# its include path automatically.
# ---------------------------------------------------------------------------
def _add_or_fallback(local_name, fallback_dir):
    local = os.path.join(component_dir, local_name)
    if os.path.exists(local):
        sources_to_add.append(local)
        print(f"[YOLOv11 S3 Build] + {local_name} (local)")
        return
    fb = os.path.join(parent_components_dir, fallback_dir, local_name)
    if os.path.exists(fb):
        sources_to_add.append(fb)
        print(f"[YOLOv11 S3 Build] + {local_name} (from {fallback_dir}/)")

_add_or_fallback("dl_base_dotprod_no_dsp.cpp", "yolo11_detection")
_add_or_fallback("mbedtls_aes_stub.c", "face_detection")

env.Append(CPPPATH=[component_dir])

# ---------------------------------------------------------------------------
# Compile + link as static library
# ---------------------------------------------------------------------------
if sources_to_add:
    objects = []
    for src in sources_to_add:
        try:
            objects.extend(env.Object(src))
        except Exception as e:
            print(f"[YOLOv11 S3 Build] Failed to compile {os.path.basename(src)}: {e}")

    if objects:
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libyolov11_s3"),
            objects,
        )
        env.Prepend(LIBS=[lib])
        env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
        print(f"[YOLOv11 S3 Build] {len(sources_to_add)} sources -> libyolov11_s3.a")

print("[YOLOv11 S3 Build] Build script completed")
