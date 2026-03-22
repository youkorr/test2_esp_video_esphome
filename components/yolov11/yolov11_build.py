"""
Build script for YOLOV11 component
Compiles ESP-DL sources for YOLO11 object detection.
Model embedding is handled by the file component separately.
"""

import os
import glob
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[YOLOV11] Build script running...")

# ========================================================================
# CONFIG defines
# ========================================================================
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32P4", "1"),
])

sources_to_add = []

# ========================================================================
# ESP-DL Sources
# ========================================================================
esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
if os.path.exists(esp_dl_dir):
    # Include directories
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

    print("[YOLOV11] ESP-DL includes added")

    # Source directories
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

    # Files to exclude
    esp_dl_exclude = [
        "dl_base_dotprod.cpp",
        "dl_image_jpeg.cpp",
        "dl_image_bmp.cpp",
        "dl_detect_msr_postprocessor.cpp",
        "dl_detect_mnp_postprocessor.cpp",
        "dl_pose_yolo11_postprocessor.cpp",
        "dl_detect_espdet_postprocessor.cpp",
        "dl_detect_pico_postprocessor.cpp",
    ]

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

    # dl/base sources
    dl_base_dir = os.path.join(esp_dl_dir, "dl", "base")
    if os.path.exists(dl_base_dir):
        for src_file in glob.glob(os.path.join(dl_base_dir, "*.cpp")):
            if os.path.basename(src_file) not in esp_dl_exclude:
                sources_to_add.append(src_file)
                sources_count["base"] += 1

    # ISA optimized files (ESP32P4)
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

    total = sum(sources_count.values())
    print(f"[YOLOV11] ESP-DL: {total} files "
          f"(base:{sources_count['base']} isa:{sources_count['isa']} "
          f"core:{sources_count['core']} vision:{sources_count['vision']})")

    # Prebuilt FBS library
    fbs_lib_dir = os.path.join(esp_dl_dir, "fbs_loader", "lib", "esp32p4")
    fbs_lib = os.path.join(fbs_lib_dir, "libfbs_model.a")
    if os.path.exists(fbs_lib):
        env.Append(LIBPATH=[fbs_lib_dir])
        env.Prepend(LIBS=["fbs_model"])
        print("[YOLOV11] Added libfbs_model.a")

# ========================================================================
# Stub files (dotprod, mbedTLS)
# ========================================================================
# Look for stubs in this component or in face_detection/yolo11_detection
stub_dirs = [
    component_dir,
    os.path.join(parent_components_dir, "yolo11_detection"),
    os.path.join(parent_components_dir, "face_detection"),
]

for stub_name in ["dl_base_dotprod_no_dsp.cpp", "mbedtls_aes_stub.c"]:
    found = False
    for stub_dir in stub_dirs:
        stub_path = os.path.join(stub_dir, stub_name)
        if os.path.exists(stub_path):
            sources_to_add.append(stub_path)
            print(f"[YOLOV11] + {stub_name}")
            found = True
            break
    if not found:
        print(f"[YOLOV11] Warning: {stub_name} not found")

env.Append(CPPPATH=[component_dir])

# ========================================================================
# Compile all sources into a static library
# ========================================================================
if sources_to_add:
    objects = []
    for src_file in sources_to_add:
        try:
            obj = env.Object(src_file)
            objects.extend(obj)
        except Exception as e:
            print(f"[YOLOV11] Failed to compile {os.path.basename(src_file)}: {e}")

    if objects:
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libyolov11"), objects
        )
        env.Prepend(LIBS=[lib])
        env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
        print(f"[YOLOV11] {len(sources_to_add)} source files compiled")
        print("[YOLOV11] libyolov11.a created")

print("[YOLOV11] Build script completed")
