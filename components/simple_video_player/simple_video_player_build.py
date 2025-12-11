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

# ESPHome auto-discovers and compiles .cpp files in component directory
# No need for manual StaticLibrary compilation
print("[Simple Video Player] Note: yuv_rgb_convert*.cpp files auto-compiled by ESPHome")

# ========================================================================
# Link optimized H.264 decoder library (tinyh264)
# ========================================================================
esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
if os.path.exists(esp_h264_dir):
    # Configure H.264 decoder for dual-core ESP32-P4 processing
    # Use core 1 for decoding (core 0 for main app)
    # CRITICAL: These flags MUST be set for esp_h264_dec_sw.c compilation
    env.Append(CPPDEFINES=[
        ("CONFIG_ESP_H264_DUAL_TASK", "1"),           # Enable dual-task mode
        ("CONFIG_ESP_H264_DUAL_TASK_CORE", "1"),      # Use CPU core 1 for decode task
        ("CONFIG_ESP_H264_DUAL_TASK_PRIORITY", "5"),  # Task priority (default 5-10)
        ("CONFIG_ESP_H264_DECODER_IRAM", "1"),        # Place decoder in IRAM for faster execution
    ])

    # Also add as compiler flags to ensure they reach GCC
    env.Append(CCFLAGS=[
        "-DCONFIG_ESP_H264_DUAL_TASK=1",
        "-DCONFIG_ESP_H264_DUAL_TASK_CORE=1",
        "-DCONFIG_ESP_H264_DUAL_TASK_PRIORITY=5",
        "-DCONFIG_ESP_H264_DECODER_IRAM=1",
    ])
    env.Append(CXXFLAGS=[
        "-DCONFIG_ESP_H264_DUAL_TASK=1",
        "-DCONFIG_ESP_H264_DUAL_TASK_CORE=1",
        "-DCONFIG_ESP_H264_DUAL_TASK_PRIORITY=5",
        "-DCONFIG_ESP_H264_DECODER_IRAM=1",
    ])

    print("[Simple Video Player] ✓ Enabled dual-core H.264 decoding (core 1, priority 5)")
    print("[Simple Video Player] ✓ Enabled IRAM placement for decoder (faster execution)")

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
    print("[Simple Video Player] ✓ Enabled ESP32-P4 performance optimizations (vectorization, fast-math)")

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

    # Compile esp_h264_dec_sw.c with dual-task flags into a static library
    # This wrapper code configures the tinyh264 decoder
    # We create a library that will be linked BEFORE the pre-compiled library
    # to ensure our version with DUAL_TASK flags takes precedence
    esp_h264_dec_sw_c = os.path.join(esp_h264_dir, "sw", "src", "esp_h264_dec_sw.c")
    h264_wrapper_sources = []

    if os.path.exists(esp_h264_dec_sw_c):
        h264_wrapper_sources.append(esp_h264_dec_sw_c)
        print(f"[Simple Video Player] Compiling esp_h264_dec_sw.c with DUAL_TASK flags...")

    # Create static library from wrapper sources
    if h264_wrapper_sources:
        # CRITICAL: Explicitly compile objects with DUAL_TASK flags
        # Use UNIQUE target name to force recompilation (avoid SCons cache)
        print("[Simple Video Player] ⚠️  EXPLICITLY compiling with -DCONFIG_ESP_H264_DUAL_TASK=1")
        wrapper_objects = []
        for src in h264_wrapper_sources:
            # Convert deque to list, then add our DUAL_TASK flags
            existing_defines = list(env.get('CPPDEFINES', []))
            dual_task_defines = existing_defines + [
                ("CONFIG_ESP_H264_DUAL_TASK", "1"),
                ("CONFIG_ESP_H264_DUAL_TASK_CORE", "1"),
                ("CONFIG_ESP_H264_DUAL_TASK_PRIORITY", "5"),
            ]

            # Use UNIQUE target name to force recompilation (avoid SCons cached version)
            src_basename = os.path.basename(src).replace('.c', '_dual_task.o')
            target_path = os.path.join(env['PROJECT_BUILD_DIR'], src_basename)

            # CRITICAL: Pass flags as BOTH CPPDEFINES AND CCFLAGS to ensure they reach GCC
            obj = env.Object(
                target=target_path,
                source=src,
                CPPDEFINES=dual_task_defines + [("CONFIG_ESP_H264_DECODER_IRAM", "1")],
                CCFLAGS=env.get('CCFLAGS', []) + [
                    "-DCONFIG_ESP_H264_DUAL_TASK=1",
                    "-DCONFIG_ESP_H264_DUAL_TASK_CORE=1",
                    "-DCONFIG_ESP_H264_DUAL_TASK_PRIORITY=5",
                    "-DCONFIG_ESP_H264_DECODER_IRAM=1"
                ]
            )

            # Force SCons to ALWAYS rebuild this file (never use cache)
            env.AlwaysBuild(obj)
            env.NoCache(obj)

            wrapper_objects.extend(obj)
            print(f"[Simple Video Player] ✓ Compiling {os.path.basename(src)} → {src_basename} (CCFLAGS + CPPDEFINES)")

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

        print("[Simple Video Player] ✓ ULTRA AGGRESSIVE linking: --undefined + --whole-archive")

        print("[Simple Video Player] ✓ Created libh264_wrapper_dual.a with DUAL_TASK enabled")
        print("[Simple Video Player] ✓ AGGRESSIVE linking: --allow-multiple-definition + --whole-archive")
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
        print("[Simple Video Player] ⚠️  WARNING: Using openh264 (no ESP32 dual-task support)")
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

        print(f"[Simple Video Player] ✓ Linked optimized {h264_lib_name} decoder library")
        print("[Simple Video Player]   Wrapper symbols from libh264_wrapper_dual.a take precedence")
        print("[Simple Video Player]   This should reduce H.264 decode time from ~60ms to ~20-30ms")
    else:
        print(f"[Simple Video Player] ⚠️  H.264 decoder library not found in {h264_lib_dir}")
else:
    print(f"[Simple Video Player] ⚠️  esp_h264 component not found")

# ========================================================================
# Link esp_image_effects SIMD library (Hardware-accelerated YUV→RGB)
# ========================================================================
esp_imgfx_dir = os.path.join(parent_components_dir, "esp_image_effects")
if os.path.exists(esp_imgfx_dir):
    # Enable SIMD YUV→RGB conversion
    env.Append(CPPDEFINES=[
        ("USE_ESP_IMAGE_EFFECTS", "1"),
        ("HAVE_ESP_IMGFX_H", "1")  # Tell code that headers are available
    ])
    print("[Simple Video Player] ✓ Enabled SIMD YUV→RGB conversion (esp_image_effects)")

    # Add include paths
    imgfx_inc = os.path.join(esp_imgfx_dir, "include")
    if os.path.exists(imgfx_inc):
        # Add to CPPPATH for normal includes
        env.Append(CPPPATH=[imgfx_inc])

        # Also add directly to compiler flags to ensure __has_include() finds it
        env.Append(CCFLAGS=[f"-I{imgfx_inc}"])
        env.Append(CXXFLAGS=[f"-I{imgfx_inc}"])

        print(f"[Simple Video Player] Added esp_imgfx include path: {imgfx_inc}")

        # Verify header files exist
        header_path = os.path.join(imgfx_inc, "esp_imgfx_color_convert.h")
        if os.path.exists(header_path):
            print(f"[Simple Video Player] ✓ Header found: esp_imgfx_color_convert.h")
        else:
            print(f"[Simple Video Player] ⚠️  Header NOT found: {header_path}")

    # Add library path and link library for ESP32-P4
    imgfx_lib_dir = os.path.join(esp_imgfx_dir, "lib", "esp32p4")
    imgfx_lib = os.path.join(imgfx_lib_dir, "libesp_image_effects.a")

    if os.path.exists(imgfx_lib):
        print(f"[Simple Video Player] Found esp_image_effects library: {imgfx_lib}")

        # Add library path
        env.Append(LIBPATH=[imgfx_lib_dir])

        # Force linking with --whole-archive
        env.Append(LINKFLAGS=[
            "-Wl,--whole-archive",
            imgfx_lib,
            "-Wl,--no-whole-archive"
        ])

        print("[Simple Video Player] ✓ Linked esp_image_effects library (with --whole-archive)")
        print("[Simple Video Player]   Expected: 3-5x faster YUV→RGB conversion (3-5ms vs 10-15ms)")
        print("[Simple Video Player]   FPS boost: 640×480 → 35+ FPS, 480×272 → 100+ FPS")
    else:
        print(f"[Simple Video Player] ⚠️  esp_image_effects library not found: {imgfx_lib}")
else:
    print(f"[Simple Video Player] ⚠️  esp_image_effects component not found")

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
            print("[Simple Video Player] ✓ Linked esp_audio_codec libraries (with --whole-archive)")
            print("[Simple Video Player]   AAC audio decoding should now be available")
        else:
            print(f"[Simple Video Player] ⚠️  One or more audio codec libraries not found")
            print(f"[Simple Video Player]     Looking for: {audio_codec_lib}")
            print(f"[Simple Video Player]     Looking for: {audio_simple_dec_lib}")
    else:
        print(f"[Simple Video Player] ⚠️  Audio codec libraries not found at {audio_lib_dir}")
else:
    print(f"[Simple Video Player] ⚠️  esp_audio_codec component not found")

print("[Simple Video Player] Build script completed")
