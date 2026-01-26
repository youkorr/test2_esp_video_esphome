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
        os.path.join(esp_h264_dir, "hw", "hal", "esp32p4"),  # HAL headers
        os.path.join(esp_h264_dir, "hw", "soc", "esp32p4"),  # SOC structures
        os.path.join(esp_h264_dir, "hw", "src"),             # HW sources
    ]
    for inc_path in esp_h264_includes:
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    # Compile HAL sources needed by hardware encoder (included in libesp_video_full.a)
    # These must be compiled here because libesp_video_full.a references them
    h264_hal_sources = [
        os.path.join(esp_h264_dir, "hw", "hal", "esp32p4", "h264_hal.c"),
        os.path.join(esp_h264_dir, "hw", "hal", "esp32p4", "h264_dma_hal.c"),
    ]
    hal_objects = []
    for hal_src in h264_hal_sources:
        if os.path.exists(hal_src):
            obj = env.Object(hal_src)
            hal_obj = obj[0] if isinstance(obj, list) else obj
            hal_objects.append(hal_obj)
            print(f"[Simple Video Player] Compiling {os.path.basename(hal_src)} (HAL for HW encoder)")

    # Create library containing HAL objects and link it first to resolve symbols
    if hal_objects:
        h264_hal_lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libh264_hal_esp32p4"),
            hal_objects
        )
        env.Prepend(LIBS=[h264_hal_lib])
        print(f"[Simple Video Player] Created libh264_hal_esp32p4.a with {len(hal_objects)} HAL objects")

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

    # CRITICAL: Use OpenH264 for FULL H.264 profile support (Baseline, Main, High)!
    # This is REQUIRED for security cameras which often use High Profile.
    # Trade-off: OpenH264 is ~20-30% slower than tinyh264 but supports ALL profiles.
    #
    # TinyH264: Faster (ESP32 dual-task) but ONLY Constrained Baseline
    # OpenH264: Slower (generic threading) but supports Baseline/Main/High/High10/etc
    #
    # Security cameras compatibility > performance optimization
    h264_lib = os.path.join(h264_lib_dir, "libopenh264.a")
    h264_lib_name = "openh264"

    if not os.path.exists(h264_lib):
        # Fallback to tinyh264 if openh264 not available
        # NOTE: TinyH264 ONLY supports Constrained Baseline (security cameras won't work!)
        h264_lib = os.path.join(h264_lib_dir, "libtinyh264.a")
        h264_lib_name = "tinyh264"
        print("[Simple Video Player]  WARNING: Using tinyh264 (ONLY Constrained Baseline)")
        print("[Simple Video Player]     OpenH264 not found - High Profile videos will FAIL")

    if os.path.exists(h264_lib):
        lib_size_mb = os.path.getsize(h264_lib) / (1024 * 1024)
        print(f"[Simple Video Player] ========================================")
        print(f"[Simple Video Player] Using {h264_lib_name} decoder library")
        print(f"[Simple Video Player]   Path: {h264_lib}")
        print(f"[Simple Video Player]   Size: {lib_size_mb:.1f} MB")
        if h264_lib_name == "openh264":
            print(f"[Simple Video Player]   Profiles: Baseline, Main, High, High10, High422, High444")
            print(f"[Simple Video Player]   Note: ~20-30% slower but supports ALL H.264 profiles")
        else:
            print(f"[Simple Video Player]   Profiles: Constrained Baseline ONLY")
            print(f"[Simple Video Player]   Note: Faster but security cameras may not work")
        print(f"[Simple Video Player] ========================================")

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
# Audio codec library (AAC decoder)
# ========================================================================
# esp_audio_codec is available in /components/esp_audio_codec
# Try multiple locations (external build dir, then source project dir)
esp_audio_codec_dir = None

# Try location 1: parent_components_dir (external build directory)
candidate = os.path.join(parent_components_dir, "esp_audio_codec")
if os.path.exists(candidate):
    esp_audio_codec_dir = candidate
    print(f"[Simple Video Player] Found esp_audio_codec in external build: {candidate}")

# Try location 2: Navigate up from script_dir to find project root
if not esp_audio_codec_dir:
    # script_dir is usually .../components/simple_video_player
    # Go up 2 levels to get project root, then components/esp_audio_codec
    project_root = os.path.dirname(os.path.dirname(script_dir))
    candidate = os.path.join(project_root, "components", "esp_audio_codec")
    if os.path.exists(candidate):
        esp_audio_codec_dir = candidate
        print(f"[Simple Video Player] Found esp_audio_codec in project source: {candidate}")

# Try location 3: Absolute fallback path (if running from known location)
if not esp_audio_codec_dir:
    candidate = "/home/user/test2_esp_video_esphome/components/esp_audio_codec"
    if os.path.exists(candidate):
        esp_audio_codec_dir = candidate
        print(f"[Simple Video Player] Found esp_audio_codec in fallback path: {candidate}")

if esp_audio_codec_dir:
    print("[Simple Video Player] AAC audio codec ENABLED (USE_ESP_AUDIO_CODEC=1)")

    # Add esp_audio_codec include paths
    audio_codec_inc = os.path.join(esp_audio_codec_dir, "include")
    if os.path.exists(audio_codec_inc):
        env.Append(CPPPATH=[audio_codec_inc])
        print(f"[Simple Video Player] Added esp_audio_codec include path: {audio_codec_inc}")
        # Also add decoder subdirectory for esp_audio_dec_reg.h
        audio_codec_dec_inc = os.path.join(audio_codec_inc, "decoder")
        if os.path.exists(audio_codec_dec_inc):
            env.Append(CPPPATH=[audio_codec_dec_inc])
            print(f"[Simple Video Player] Added esp_audio_codec decoder include path: {audio_codec_dec_inc}")

    # Compile C source files (codec registration)
    audio_codec_src_dir = os.path.join(esp_audio_codec_dir, "src")
    audio_codec_objects = []
    if os.path.exists(audio_codec_src_dir):
        src_files = ["audio_decoder_reg.c", "audio_encoder_reg.c", "simple_decoder_reg.c"]
        for src_file in src_files:
            src_path = os.path.join(audio_codec_src_dir, src_file)
            if os.path.exists(src_path):
                obj = env.Object(src_path)
                audio_obj = obj[0] if isinstance(obj, list) else obj
                audio_codec_objects.append(audio_obj)
                print(f"[Simple Video Player] Compiling {src_file}")

    # Create library containing audio codec registration objects
    if audio_codec_objects:
        audio_codec_reg_lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libaudio_codec_reg"),
            audio_codec_objects
        )
        env.Prepend(LIBS=[audio_codec_reg_lib])
        print(f"[Simple Video Player] Created libaudio_codec_reg.a with {len(audio_codec_objects)} registration objects")

    # Link esp_audio_codec library for ESP32-P4
    audio_codec_lib_dir = os.path.join(esp_audio_codec_dir, "lib", "esp32p4")
    if os.path.exists(audio_codec_lib_dir):
        env.Append(LIBPATH=[audio_codec_lib_dir])

        # Link AAC decoder library
        aac_lib = os.path.join(audio_codec_lib_dir, "libesp_audio_codec.a")
        if os.path.exists(aac_lib):
            lib_size_kb = os.path.getsize(aac_lib) / 1024
            print(f"[Simple Video Player] ========================================")
            print(f"[Simple Video Player] Using esp_audio_codec library")
            print(f"[Simple Video Player]   Path: {aac_lib}")
            print(f"[Simple Video Player]   Size: {lib_size_kb:.1f} KB")
            print(f"[Simple Video Player]   Codecs: AAC-LC, AAC-HE, AAC-HEv2")
            print(f"[Simple Video Player] ========================================")

            # Force linker to include all symbols from the library
            env.Prepend(LINKFLAGS=[
                "-Wl,--whole-archive",
                aac_lib,
                "-Wl,--no-whole-archive"
            ])
            print(f"[Simple Video Player] Linked esp_audio_codec library (--whole-archive)")
        else:
            print(f"[Simple Video Player]  WARNING: libesp_audio_codec.a not found in {audio_codec_lib_dir}")
    else:
        print(f"[Simple Video Player]  WARNING: Library directory not found: {audio_codec_lib_dir}")
else:
    print("[Simple Video Player]  WARNING: esp_audio_codec component not found - AAC audio disabled")

print("[Simple Video Player] Build script completed")
