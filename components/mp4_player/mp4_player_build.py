"""
Build script for mp4_player component
Links esp_extractor prebuilt library (no BSP dependencies)
"""

import os
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[MP4 Player] Build script running...")

extractor_dir = os.path.join(component_dir, "components", "esp_extractor")

# ========================================================================
# esp_extractor include paths
# ========================================================================
extractor_inc = os.path.join(extractor_dir, "include")
if os.path.exists(extractor_inc):
    env.Append(CPPPATH=[extractor_inc])
    print(f"[MP4 Player] + esp_extractor includes")

# ========================================================================
# Compile esp_extractor_reg.c (registers MP4/AVI extractors)
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

print("[MP4 Player] Build script completed")
