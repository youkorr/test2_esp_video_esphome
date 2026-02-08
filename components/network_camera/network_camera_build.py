"""
Build script for network_camera component
Compiles esp_h264 decoder sources and links openh264 library
"""

import os
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[Network Camera] Build script running...")

# ========================================================================
# H.264 decoder: compile sources + link library
# ========================================================================
esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
if os.path.exists(esp_h264_dir):
    # Add all esp_h264 include paths
    h264_includes = [
        os.path.join(esp_h264_dir, "interface", "include"),
        os.path.join(esp_h264_dir, "port", "include"),
        os.path.join(esp_h264_dir, "port", "inc"),
        os.path.join(esp_h264_dir, "sw", "include"),
        os.path.join(esp_h264_dir, "hw", "include"),
        os.path.join(esp_h264_dir, "sw", "libs", "openh264_inc"),
        os.path.join(esp_h264_dir, "sw", "libs", "tinyh264_inc"),
        os.path.join(esp_h264_dir, "hw", "src"),
        os.path.join(esp_h264_dir, "hw", "hal", "esp32p4"),
        os.path.join(esp_h264_dir, "hw", "soc", "esp32p4"),
    ]
    for inc_path in h264_includes:
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    # ========================================================================
    # Compile esp_h264 decoder sources (previously done by esp_video_build.py)
    # ========================================================================
    esp_h264_decoder_sources = [
        "port/src/esp_h264_alloc.c",
        "port/src/esp_h264_cache.c",
        "sw/src/esp_h264_dec_sw.c",
        "sw/src/h264_color_convert.c",
        "interface/include/src/esp_h264_dec.c",
        "interface/include/src/esp_h264_dec_param.c",
        "interface/include/src/esp_h264_version.c",
    ]

    h264_objects = []
    for src in esp_h264_decoder_sources:
        src_path = os.path.join(esp_h264_dir, src)
        if os.path.exists(src_path):
            obj = env.Object(src_path)
            h264_objects.extend(obj)
            print(f"[Network Camera] + esp_h264/{src}")

    if h264_objects:
        h264_dec_lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libh264_decoder_nc"),
            h264_objects
        )
        env.Prepend(LIBS=[h264_dec_lib])
        print(f"[Network Camera] Created libh264_decoder_nc.a with decoder sources")

    # ========================================================================
    # Link H.264 libraries: openh264 (encoder/decoder) + tinyh264 (h264bsd decoder)
    # ========================================================================
    h264_lib_dir = os.path.join(esp_h264_dir, "sw", "libs", "esp32p4")
    openh264_lib = os.path.join(h264_lib_dir, "libopenh264.a")
    tinyh264_lib = os.path.join(h264_lib_dir, "libtinyh264.a")

    if os.path.exists(h264_lib_dir):
        env.Append(LIBPATH=[h264_lib_dir])

    if os.path.exists(openh264_lib):
        env.Append(LINKFLAGS=[
            "-Wl,--allow-multiple-definition",
            "-Wl,--whole-archive",
            openh264_lib,
            "-Wl,--no-whole-archive"
        ])
        print(f"[Network Camera] Linked openh264 (Baseline/Main/High profiles)")
    else:
        print(f"[Network Camera]  openh264 not found at {openh264_lib}")

    # tinyh264 provides h264bsd* symbols needed by esp_h264_dec_sw.c
    if os.path.exists(tinyh264_lib):
        env.Append(LIBS=["tinyh264"])
        print(f"[Network Camera] Linked tinyh264 (h264bsd decoder symbols)")
    else:
        print(f"[Network Camera]  tinyh264 not found at {tinyh264_lib}")
else:
    print(f"[Network Camera]  esp_h264 component not found")

print("[Network Camera] Build script completed")
