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

# ========================================================================
# Compile additional source files into a static library
# ESPHome only auto-compiles the main component .cpp file
# ========================================================================
yuv_convert_cpp = os.path.join(component_dir, "yuv_rgb_convert.cpp")
yuv_convert_simd_cpp = os.path.join(component_dir, "yuv_rgb_convert_simd.cpp")

sources_to_compile = []
if os.path.exists(yuv_convert_cpp):
    sources_to_compile.append(yuv_convert_cpp)
    print("[Simple Video Player] + yuv_rgb_convert.cpp")

if os.path.exists(yuv_convert_simd_cpp):
    sources_to_compile.append(yuv_convert_simd_cpp)
    print("[Simple Video Player] + yuv_rgb_convert_simd.cpp")

# Compile into a static library and link it
if sources_to_compile:
    lib = env.StaticLibrary(
        target=os.path.join(env['PROJECT_BUILD_DIR'], "libyuv_convert"),
        source=sources_to_compile
    )
    env.Prepend(LIBS=[lib])
    print("[Simple Video Player] ✓ Created libyuv_convert.a")

# ========================================================================
# Link optimized H.264 decoder library (tinyh264)
# ========================================================================
esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
if os.path.exists(esp_h264_dir):
    # Configure H.264 decoder for dual-core ESP32-P4 processing
    # Use core 1 for decoding (core 0 for main app)
    env.Append(CPPDEFINES=[
        ("CONFIG_ESP_H264_DUAL_TASK", "1"),           # Enable dual-task mode
        ("CONFIG_ESP_H264_DUAL_TASK_CORE", "1"),      # Use CPU core 1 for decode task
    ])
    print("[Simple Video Player] ✓ Enabled dual-core H.264 decoding (core 1)")
    # Add esp_h264 library path for ESP32-P4
    h264_lib_dir = os.path.join(esp_h264_dir, "sw", "libs", "esp32p4")
    # Try openh264 first (more optimized but larger)
    h264_lib = os.path.join(h264_lib_dir, "libopenh264.a")
    h264_lib_name = "openh264"

    if not os.path.exists(h264_lib):
        # Fallback to tinyh264
        h264_lib = os.path.join(h264_lib_dir, "libtinyh264.a")
        h264_lib_name = "tinyh264"

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

        # Force linking with --whole-archive to override compiled esp_h264_dec_sw.o
        # This ensures library symbols take precedence over any duplicate symbols
        env.Append(LINKFLAGS=[
            "-Wl,--whole-archive",
            h264_lib,
            "-Wl,--no-whole-archive"
        ])

        print(f"[Simple Video Player] ✓ Linked optimized {h264_lib_name} decoder library (with --whole-archive)")
        print("[Simple Video Player]   This should reduce H.264 decode time from ~60ms to ~10-20ms")
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
