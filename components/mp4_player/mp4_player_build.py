"""
Build script for mp4_player component
Compiles app_extractor.c sources and links esp_extractor + audio codec libraries
"""

import os
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[MP4 Player] Build script running...")

main_dir = os.path.join(component_dir, "main")
extractor_dir = os.path.join(component_dir, "components", "esp_extractor")

# ========================================================================
# Include paths
# ========================================================================
if os.path.exists(main_dir):
    env.Append(CPPPATH=[main_dir])
    print(f"[MP4 Player] + main/ includes (app_extractor.h, app_stream_adapter.h)")

extractor_inc = os.path.join(extractor_dir, "include")
if os.path.exists(extractor_inc):
    env.Append(CPPPATH=[extractor_inc])
    print(f"[MP4 Player] + esp_extractor includes")

# ========================================================================
# Compile app_extractor.c and app_stream_adapter.c from main/
# ========================================================================
if os.path.exists(main_dir):
    c_sources = ["app_extractor.c", "app_stream_adapter.c"]
    main_objects = []
    for src_file in c_sources:
        src_path = os.path.join(main_dir, src_file)
        if os.path.exists(src_path):
            obj = env.Object(src_path)
            main_objects.extend(obj)
            print(f"[MP4 Player] + main/{src_file}")

    if main_objects:
        main_lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libmp4_player_main"),
            main_objects
        )
        env.Prepend(LIBS=[main_lib])
        print(f"[MP4 Player] Created libmp4_player_main.a")

# ========================================================================
# Compile esp_extractor_reg.c
# ========================================================================
reg_src = os.path.join(extractor_dir, "esp_extractor_reg.c")
if os.path.exists(reg_src):
    reg_obj = env.Object(reg_src)
    env.Prepend(LIBS=[env.StaticLibrary(
        os.path.join("$BUILD_DIR", "libmp4_extractor_reg"),
        reg_obj
    )])
    print(f"[MP4 Player] + esp_extractor_reg.c compiled")

# ========================================================================
# Link prebuilt libesp_extractor.a
# ========================================================================
extractor_lib_dir = os.path.join(extractor_dir, "lib", "esp32p4")
extractor_lib = os.path.join(extractor_lib_dir, "libesp_extractor.a")
if os.path.exists(extractor_lib):
    env.Append(LIBPATH=[extractor_lib_dir])
    env.Append(LINKFLAGS=[
        "-Wl,--whole-archive",
        extractor_lib,
        "-Wl,--no-whole-archive",
    ])
    print(f"[MP4 Player] Linked libesp_extractor.a (MP4/AVI parser)")
else:
    print(f"[MP4 Player] WARNING: libesp_extractor.a not found")

# ========================================================================
# Audio codec library (AAC/MP3 decoder)
# ========================================================================
esp_audio_codec_dir = os.path.join(parent_components_dir, "esp_audio_codec")
if os.path.exists(esp_audio_codec_dir):
    audio_codec_includes = [
        os.path.join(esp_audio_codec_dir, "include"),
        os.path.join(esp_audio_codec_dir, "include", "decoder"),
        os.path.join(esp_audio_codec_dir, "include", "decoder", "impl"),
        os.path.join(esp_audio_codec_dir, "include", "encoder"),
        os.path.join(esp_audio_codec_dir, "include", "encoder", "impl"),
        os.path.join(esp_audio_codec_dir, "include", "simple_dec"),
    ]
    for inc_path in audio_codec_includes:
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    # Compile registration source files
    audio_codec_sources = [
        os.path.join(esp_audio_codec_dir, "src", "audio_decoder_reg.c"),
        os.path.join(esp_audio_codec_dir, "src", "audio_encoder_reg.c"),
        os.path.join(esp_audio_codec_dir, "src", "simple_decoder_reg.c"),
    ]
    audio_codec_objects = []
    for src_path in audio_codec_sources:
        if os.path.exists(src_path):
            obj = env.Object(src_path)
            audio_codec_objects.extend(obj)
            print(f"[MP4 Player] + esp_audio_codec/{os.path.basename(src_path)}")

    if audio_codec_objects:
        audio_codec_lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libmp4_audio_codec_reg"),
            audio_codec_objects
        )
        env.Prepend(LIBS=[audio_codec_lib])

    # Link prebuilt audio codec libraries
    audio_lib_dir = os.path.join(esp_audio_codec_dir, "lib", "esp32p4")
    if os.path.exists(audio_lib_dir):
        env.Append(LIBPATH=[audio_lib_dir])
        audio_codec_a = os.path.join(audio_lib_dir, "libesp_audio_codec.a")
        audio_simple_a = os.path.join(audio_lib_dir, "libesp_audio_simple_dec.a")
        if os.path.exists(audio_codec_a):
            env.Append(LIBS=["esp_audio_codec"])
            print(f"[MP4 Player] Linked esp_audio_codec (AAC/MP3 decoder)")
        if os.path.exists(audio_simple_a):
            env.Append(LIBS=["esp_audio_simple_dec"])
            print(f"[MP4 Player] Linked esp_audio_simple_dec")
    else:
        print(f"[MP4 Player] WARNING: esp_audio_codec lib dir not found")
else:
    print(f"[MP4 Player] WARNING: esp_audio_codec component not found")

print("[MP4 Player] Build script completed")
