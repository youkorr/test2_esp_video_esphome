"""PlatformIO build script for MP4 Player component.

Compiles app_extractor.c and app_stream_adapter.c source files
and links esp_extractor and esp_audio_codec prebuilt libraries.
"""

Import("env")

import os

# Get component directories
component_dir = os.path.dirname(os.path.abspath(env.subst("$PROJECT_DIR")))
# Try relative to this script
script_dir = os.path.dirname(os.path.abspath(__file__))
main_dir = os.path.join(script_dir, "main")
extractor_dir = os.path.join(script_dir, "components", "esp_extractor")

# Add source files from main/
if os.path.exists(main_dir):
    # Add C source files
    for src_file in ["app_extractor.c", "app_stream_adapter.c"]:
        src_path = os.path.join(main_dir, src_file)
        if os.path.exists(src_path):
            env.BuildSources(
                os.path.join("$BUILD_DIR", "mp4_player_main", src_file.replace(".c", "")),
                main_dir,
                src_filter=f"+<{src_file}>"
            )

# Add include paths
if os.path.exists(main_dir):
    env.Append(CPPPATH=[main_dir])

extractor_inc = os.path.join(extractor_dir, "include")
if os.path.exists(extractor_inc):
    env.Append(CPPPATH=[extractor_inc])

# Link prebuilt libraries
extractor_lib = os.path.join(extractor_dir, "lib", "esp32p4")
if os.path.exists(extractor_lib):
    env.Append(LIBPATH=[extractor_lib])
    env.Append(LIBS=["esp_extractor"])

# Also compile esp_extractor_reg.c
reg_src = os.path.join(extractor_dir, "esp_extractor_reg.c")
if os.path.exists(reg_src):
    env.BuildSources(
        os.path.join("$BUILD_DIR", "mp4_player_extractor"),
        extractor_dir,
        src_filter="+<esp_extractor_reg.c>"
    )
