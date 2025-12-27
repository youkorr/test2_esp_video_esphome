"""
Build script for simple_video_player component
Links optimized H264 decoder and audio codec libraries
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
# Link optimized H.264 decoder library (tinyh264)
# ========================================================================
esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
if os.path.exists(esp_h264_dir):
    # Dual-task H.264 flags are defined in esp_h264/__init__.py to avoid redefinition warnings
    print("[Simple Video Player] Using H.264 dual-task config from esp_h264 component")

    # ESP32-P4 specific optimizations for video decoding performance
    # Use -Os (optimize for size) instead of -O3 to reduce flash usage
    # Still enable critical performance flags for video decoding
    env.Append(CCFLAGS=[
        "-ffast-math",                  # Fast floating-point math (safe for video)
        "-ftree-vectorize",             # Enable auto-vectorization (use SIMD)
    ])
    env.Append(CXXFLAGS=[
        "-ffast-math",
        "-ftree-vectorize",
    ])
    print("[Simple Video Player] Enabled ESP32-P4 performance optimizations (vectorization, fast-math)")

    # Add esp_h264 include paths for compiling wrapper code
    esp_h264_includes = [
        os.path.join(esp_h264_dir, "interface", "include"),
        os.path.join(esp_h264_dir, "port", "include"),
        os.path.join(esp_h264_dir, "port", "inc"),
        os.path.join(esp_h264_dir, "sw", "include"),
        os.path.join(esp_h264_dir, "sw", "src"),
        os.path.join(esp_h264_dir, "hw", "include"),
    ]
    for inc_path in esp_h264_includes:
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    # NOTE: esp_h264_dec_sw.c is now compiled in esp_video_build.py with DUAL_TASK flags
    # This follows Espressif's official approach (CMakeLists.txt)
    # No need to compile a separate wrapper here!
    print("[Simple Video Player]  esp_h264_dec_sw.c compiled by esp_video_build.py with DUAL_TASK")

    # DEPRECATED: Old wrapper approach (now handled by esp_video_build.py)
    # esp_h264_dec_sw_c = os.path.join(esp_h264_dir, "sw", "src", "esp_h264_dec_sw.c")
    h264_wrapper_sources = []

    if False:  # Disabled: wrapper compilation now in esp_video_build.py
        pass
        # print(f"[Simple Video Player] Compiling esp_h264_dec_sw.c with DUAL_TASK flags...")

    # Create static library from wrapper sources
    if h264_wrapper_sources:
        # CRITICAL: Explicitly compile objects with DUAL_TASK flags
        # Use UNIQUE target name to force recompilation (avoid SCons cache)
        print("[Simple Video Player]  EXPLICITLY compiling with -DCONFIG_ESP_H264_DUAL_TASK=1")
        wrapper_objects = []
        for src in h264_wrapper_sources:
            # Use existing environment defines (from esp_h264/__init__.py)
            # No need to redefine flags here - they're inherited from global environment
            existing_defines = list(env.get('CPPDEFINES', []))

            # Use UNIQUE target name to force recompilation (avoid SCons cached version)
            src_basename = os.path.basename(src).replace('.c', '_dual_task.o')
            target_path = os.path.join(env['PROJECT_BUILD_DIR'], src_basename)

            # Compile with inherited flags from global environment
            obj = env.Object(
                target=target_path,
                source=src,
                CPPDEFINES=existing_defines
            )

            # Force SCons to ALWAYS rebuild this file (never use cache)
            env.AlwaysBuild(obj)
            env.NoCache(obj)

            wrapper_objects.extend(obj)
            print(f"[Simple Video Player] Compiling {os.path.basename(src)} {src_basename} (CCFLAGS + CPPDEFINES)")

        # Create library from explicitly compiled objects
        h264_wrapper_lib = env.StaticLibrary(
            target=os.path.join(env['PROJECT_BUILD_DIR'], "libh264_wrapper_dual"),
            source=wrapper_objects
        )

        # CRITICAL: Get the actual file path from the library node
        wrapper_lib_file = h264_wrapper_lib[0]  # StaticLibrary returns a list with one element
        wrapper_lib_path = str(wrapper_lib_file.get_abspath())

        # Add library to build dependencies
        env.Prepend(LIBS=[h264_wrapper_lib])
        env.Prepend(LIBPATH=[env['PROJECT_BUILD_DIR']])

        # ULTRA AGGRESSIVE: Force linker to use OUR symbols by making them undefined first
        # Then link our library FIRST so it resolves them
        env.Prepend(LINKFLAGS=[
            "-Wl,--undefined=esp_h264_dec_sw_new",  # Force this symbol to be resolved
            "-Wl,--whole-archive",                   # Include ALL symbols from our lib
            wrapper_lib_path,
            "-Wl,--no-whole-archive"
        ])

        print("[Simple Video Player] ULTRA AGGRESSIVE linking: --undefined + --whole-archive")

        print("[Simple Video Player] Created libh264_wrapper_dual.a with DUAL_TASK enabled")
        print("[Simple Video Player] AGGRESSIVE linking: --allow-multiple-definition + --whole-archive")
        print(f"[Simple Video Player]   Library: {wrapper_lib_path}")
        print("[Simple Video Player]   Our DUAL_TASK symbols MUST override ESP-Video!")

    # Add esp_h264 library path for ESP32-P4
    h264_lib_dir = os.path.join(esp_h264_dir, "sw", "libs", "esp32p4")

    # CRITICAL: Use tinyh264 for ESP32 dual-task support!
    # OpenH264 uses generic C++ threading (WelsTaskThread) which is SLOWER
    # TinyH264 uses ESP32-optimized dual-task (espCreateFilterTask) which is FASTER
    h264_lib = os.path.join(h264_lib_dir, "libtinyh264.a")
    h264_lib_name = "tinyh264"

    if not os.path.exists(h264_lib):
        # Fallback to openh264 if tinyh264 not available
        # NOTE: OpenH264 does NOT support ESP32 dual-task optimization!
        h264_lib = os.path.join(h264_lib_dir, "libopenh264.a")
        h264_lib_name = "openh264"
        print("[Simple Video Player]  WARNING: Using openh264 (no ESP32 dual-task support)")
        print("[Simple Video Player]     TinyH264 not found - performance will be degraded")

    if os.path.exists(h264_lib):
        print(f"[Simple Video Player] Found {h264_lib_name} library: {h264_lib}")

        # Add library path
        env.Append(LIBPATH=[h264_lib_dir])

        # Add include paths for decoder
        if h264_lib_name == "tinyh264":
            h264_inc = os.path.join(esp_h264_dir, "sw", "libs", "tinyh264_inc")
        else:  # openh264
            h264_inc = os.path.join(esp_h264_dir, "sw", "libs", "openh264_inc")

        if os.path.exists(h264_inc):
            env.Append(CPPPATH=[h264_inc])
            print(f"[Simple Video Player] Added {h264_lib_name} include path")

        # Link pre-compiled library AFTER our wrapper library (linked with Prepend above)
        # The linker will use our esp_h264_dec_sw.o (with DUAL_TASK flags)
        # and skip the version in the pre-compiled library
        env.Append(LINKFLAGS=[h264_lib])

        print(f"[Simple Video Player] Linked optimized {h264_lib_name} decoder library")
        print("[Simple Video Player]   Wrapper symbols from libh264_wrapper_dual.a take precedence")
        print("[Simple Video Player]   This should reduce H.264 decode time from ~60ms to ~20-30ms")
    else:
        print(f"[Simple Video Player]  H.264 decoder library not found in {h264_lib_dir}")
else:
    print(f"[Simple Video Player]  esp_h264 component not found")

# ========================================================================
# esp_image_effects (esp_imgfx) REMOVED - buggy and slower than software LUT
# ========================================================================
# NOW USING: PPA hardware (ESP32-P4) + software LUT fallback
# esp_imgfx was causing 42ms delays instead of expected 3-5ms
print("[Simple Video Player] YUVRGB: PPA hardware + software LUT (esp_imgfx removed)")

# ========================================================================
# Link audio codec library (esp_audio_codec)
# ========================================================================
audio_codec_dir = os.path.join(parent_components_dir, "esp_audio_codec")
if os.path.exists(audio_codec_dir):
    # Add audio codec library path for ESP32-P4
    audio_lib_dir = os.path.join(audio_codec_dir, "lib", "esp32p4")

    if os.path.exists(audio_lib_dir):
        print(f"[Simple Video Player] Found audio codec libraries in: {audio_lib_dir}")

        # Add library path
        env.Append(LIBPATH=[audio_lib_dir])

        # Add include paths for esp_audio_codec headers
        audio_includes = [
            os.path.join(audio_codec_dir, "include"),
            os.path.join(audio_codec_dir, "include", "decoder"),
            os.path.join(audio_codec_dir, "include", "decoder", "impl"),
            os.path.join(audio_codec_dir, "include", "codec"),
            os.path.join(audio_codec_dir, "include", "simple_dec"),
        ]
        for inc_path in audio_includes:
            if os.path.exists(inc_path):
                env.Append(CPPPATH=[inc_path])
        print(f"[Simple Video Player] Added esp_audio_codec include paths")

        # Force linking with --whole-archive for audio codec libraries
        audio_codec_lib = os.path.join(audio_lib_dir, "libesp_audio_codec.a")
        audio_simple_dec_lib = os.path.join(audio_lib_dir, "libesp_audio_simple_dec.a")

        if os.path.exists(audio_codec_lib) and os.path.exists(audio_simple_dec_lib):
            env.Append(LINKFLAGS=[
                "-Wl,--whole-archive",
                audio_codec_lib,
                audio_simple_dec_lib,
                "-Wl,--no-whole-archive"
            ])
            print("[Simple Video Player] Linked esp_audio_codec libraries (with --whole-archive)")
            print("[Simple Video Player]   AAC audio decoding should now be available")
        else:
            print(f"[Simple Video Player]  One or more audio codec libraries not found")
            print(f"[Simple Video Player]     Looking for: {audio_codec_lib}")
            print(f"[Simple Video Player]     Looking for: {audio_simple_dec_lib}")
    else:
        print(f"[Simple Video Player]  Audio codec libraries not found at {audio_lib_dir}")
else:
    print(f"[Simple Video Player]  esp_audio_codec component not found")

print("[Simple Video Player] Build script completed")
