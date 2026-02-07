"""
Build script for simple_video_player component
Compiles esp_h264 decoder sources and links H264 decoder library
"""

import os
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[Simple Video Player] Build script running...")

# Define ESP32-P4 target to enable PPA hardware acceleration
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32P4", "1"),  # Enable ESP32-P4 PPA hardware for YUVRGB
])
env.Append(CCFLAGS=["-DCONFIG_IDF_TARGET_ESP32P4=1"])
env.Append(CXXFLAGS=["-DCONFIG_IDF_TARGET_ESP32P4=1"])
print("[Simple Video Player] Enabled CONFIG_IDF_TARGET_ESP32P4 (PPA hardware for YUVRGB)")

# ESPHome auto-discovers and compiles .cpp files in component directory
# No need for manual StaticLibrary compilation
print("[Simple Video Player] Note: yuv_rgb_convert*.cpp files auto-compiled by ESPHome")

# Explicitly compile yuv_rgb_lut.cpp to ensure it's included
yuv_lut_src = os.path.join(component_dir, "yuv_rgb_lut.cpp")
if os.path.exists(yuv_lut_src):
    print("[Simple Video Player] Compiling yuv_rgb_lut.cpp (lookup table YUVRGB)")
    env.StaticObject(yuv_lut_src)

# ========================================================================
# H.264 decoder: compile sources + link library
# ========================================================================
esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
if os.path.exists(esp_h264_dir):
    # ESP32-P4 specific optimizations for video decoding performance
    env.Append(CCFLAGS=[
        "-ffast-math",                  # Fast floating-point math (safe for video)
        "-ftree-vectorize",             # Enable auto-vectorization (use SIMD)
    ])
    env.Append(CXXFLAGS=[
        "-ffast-math",
        "-ftree-vectorize",
    ])
    print("[Simple Video Player] Enabled ESP32-P4 performance optimizations (vectorization, fast-math)")

    # Add esp_h264 include paths
    esp_h264_includes = [
        os.path.join(esp_h264_dir, "interface", "include"),
        os.path.join(esp_h264_dir, "port", "include"),
        os.path.join(esp_h264_dir, "port", "inc"),
        os.path.join(esp_h264_dir, "sw", "include"),
        os.path.join(esp_h264_dir, "sw", "src"),
        os.path.join(esp_h264_dir, "hw", "include"),
        os.path.join(esp_h264_dir, "sw", "libs", "openh264_inc"),
        os.path.join(esp_h264_dir, "sw", "libs", "tinyh264_inc"),
        os.path.join(esp_h264_dir, "hw", "src"),
        os.path.join(esp_h264_dir, "hw", "hal", "esp32p4"),
        os.path.join(esp_h264_dir, "hw", "soc", "esp32p4"),
    ]
    for inc_path in esp_h264_includes:
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    # ========================================================================
    # Compile esp_h264 decoder sources directly
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
            print(f"[Simple Video Player] + esp_h264/{src}")

    if h264_objects:
        h264_dec_lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libh264_decoder_svp"),
            h264_objects
        )
        env.Prepend(LIBS=[h264_dec_lib])
        print(f"[Simple Video Player] Created libh264_decoder_svp.a with decoder sources")

    # ========================================================================
    # Link H.264 library (OpenH264 preferred, tinyh264 fallback)
    # ========================================================================
    h264_lib_dir = os.path.join(esp_h264_dir, "sw", "libs", "esp32p4")

    # Use OpenH264 for FULL H.264 profile support (Baseline, Main, High)
    h264_lib = os.path.join(h264_lib_dir, "libopenh264.a")
    h264_lib_name = "openh264"

    if not os.path.exists(h264_lib):
        # Fallback to tinyh264 if openh264 not available
        h264_lib = os.path.join(h264_lib_dir, "libtinyh264.a")
        h264_lib_name = "tinyh264"
        print("[Simple Video Player]  WARNING: Using tinyh264 (ONLY Constrained Baseline)")

    if os.path.exists(h264_lib):
        lib_size_mb = os.path.getsize(h264_lib) / (1024 * 1024)
        print(f"[Simple Video Player] Using {h264_lib_name} decoder ({lib_size_mb:.1f} MB)")

        env.Append(LIBPATH=[h264_lib_dir])
        env.Append(LINKFLAGS=[
            "-Wl,--allow-multiple-definition",
            h264_lib,
        ])
        print(f"[Simple Video Player] Linked {h264_lib_name} decoder library")
    else:
        print(f"[Simple Video Player]  H.264 decoder library not found in {h264_lib_dir}")
else:
    print(f"[Simple Video Player]  esp_h264 component not found")

# ========================================================================
# esp_image_effects (esp_imgfx) REMOVED - buggy and slower than software LUT
# ========================================================================
# NOW USING: PPA hardware (ESP32-P4) + software LUT fallback
print("[Simple Video Player] YUVRGB: PPA hardware + software LUT (esp_imgfx removed)")

# ========================================================================
# Audio codec library - REMOVED (not working)
# ========================================================================
print("[Simple Video Player] Audio codec disabled (esp_audio_codec removed)")

print("[Simple Video Player] Build script completed")
