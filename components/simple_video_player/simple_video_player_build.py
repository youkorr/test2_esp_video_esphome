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
# H.264 decoder library linking - ONLY openh264
# ========================================================================
esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
if os.path.exists(esp_h264_dir):
    h264_lib_dir = os.path.join(esp_h264_dir, "sw", "libs", "esp32p4")
    openh264_lib = os.path.join(h264_lib_dir, "libopenh264.a")

    if os.path.exists(openh264_lib):
        print(f"[Simple Video Player] Found openh264 library: {openh264_lib}")

        # Add include paths for openh264
        h264_inc = os.path.join(esp_h264_dir, "sw", "libs", "openh264_inc")
        if os.path.exists(h264_inc):
            env.Append(CPPPATH=[h264_inc])

        # Also add tinyh264_inc for h264bsd API headers
        h264_inc_bsd = os.path.join(esp_h264_dir, "sw", "libs", "tinyh264_inc")
        if os.path.exists(h264_inc_bsd):
            env.Append(CPPPATH=[h264_inc_bsd])

        # Link ONLY openh264 with --whole-archive (NOT tinyh264!)
        env.Append(LINKFLAGS=[
            "-Wl,--whole-archive",
            openh264_lib,
            "-Wl,--no-whole-archive"
        ])
        print(f"[Simple Video Player] ✓ Linked openh264 (Baseline/Main/High profiles, --whole-archive)")
    else:
        print(f"[Simple Video Player] ⚠️  openh264 not found at {openh264_lib}")
else:
    print(f"[Simple Video Player] ⚠️  esp_h264 component not found")

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
