"""
ESP-GMF Build Script for ESPHome/PlatformIO
Compiles gmf_core and gmf_video elements
"""

import os
Import("env")

script_dir = Dir('.').srcnode().abspath
gmf_root = script_dir

print("[ESP-GMF] Building ESP-GMF framework...")

# ========================================================================
# GMF Core - Framework foundation
# ========================================================================
gmf_core_dir = os.path.join(gmf_root, "gmf_core")

if os.path.exists(gmf_core_dir):
    # Add include paths for gmf_core
    gmf_core_includes = [
        os.path.join(gmf_core_dir, "include"),
        os.path.join(gmf_core_dir, "oal", "include"),
        os.path.join(gmf_core_dir, "data_bus", "include"),
        os.path.join(gmf_core_dir, "helpers", "include"),
    ]

    for inc_path in gmf_core_includes:
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])
            print(f"[ESP-GMF] Added gmf_core include: {os.path.basename(inc_path)}")

    # Compile gmf_core sources
    gmf_core_sources = []

    # src/*.c files
    src_dir = os.path.join(gmf_core_dir, "src")
    if os.path.exists(src_dir):
        for f in os.listdir(src_dir):
            if f.endswith('.c'):
                gmf_core_sources.append(os.path.join(src_dir, f))

    # oal/*.c files
    oal_dir = os.path.join(gmf_core_dir, "oal")
    if os.path.exists(oal_dir):
        for f in os.listdir(oal_dir):
            if f.endswith('.c'):
                gmf_core_sources.append(os.path.join(oal_dir, f))

    # data_bus/*.c files
    data_bus_dir = os.path.join(gmf_core_dir, "data_bus")
    if os.path.exists(data_bus_dir):
        for f in os.listdir(data_bus_dir):
            if f.endswith('.c'):
                gmf_core_sources.append(os.path.join(data_bus_dir, f))

    # helpers/*.c files
    helpers_dir = os.path.join(gmf_core_dir, "helpers")
    if os.path.exists(helpers_dir):
        for f in os.listdir(helpers_dir):
            if f.endswith('.c'):
                gmf_core_sources.append(os.path.join(helpers_dir, f))

    if gmf_core_sources:
        print(f"[ESP-GMF] Compiling {len(gmf_core_sources)} gmf_core source files...")
        for src in gmf_core_sources:
            env.StaticObject(src)
    else:
        print("[ESP-GMF] ⚠️  No gmf_core sources found")
else:
    print("[ESP-GMF] ⚠️  gmf_core directory not found")

# ========================================================================
# GMF Video - Video processing elements
# ========================================================================
gmf_video_dir = os.path.join(gmf_root, "element", "gmf_video")

if os.path.exists(gmf_video_dir):
    # Add include paths for gmf_video
    gmf_video_includes = [
        os.path.join(gmf_video_dir, "include"),
        os.path.join(gmf_video_dir, "private_include"),
    ]

    for inc_path in gmf_video_includes:
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])
            print(f"[ESP-GMF] Added gmf_video include: {os.path.basename(inc_path)}")

    # Compile gmf_video sources
    gmf_video_sources = []
    for f in os.listdir(gmf_video_dir):
        if f.endswith('.c') and not f.startswith('test'):
            gmf_video_sources.append(os.path.join(gmf_video_dir, f))

    if gmf_video_sources:
        print(f"[ESP-GMF] Compiling {len(gmf_video_sources)} gmf_video source files...")
        # Only compile esp_gmf_video_ppa.c for now (what we need)
        ppa_src = os.path.join(gmf_video_dir, "esp_gmf_video_ppa.c")
        common_src = os.path.join(gmf_video_dir, "gmf_video_common.c")

        if os.path.exists(ppa_src):
            env.StaticObject(ppa_src)
            print(f"[ESP-GMF]   ✓ esp_gmf_video_ppa.c")

        if os.path.exists(common_src):
            env.StaticObject(common_src)
            print(f"[ESP-GMF]   ✓ gmf_video_common.c")
    else:
        print("[ESP-GMF] ⚠️  No gmf_video sources found")
else:
    print("[ESP-GMF] ⚠️  gmf_video directory not found")

print("[ESP-GMF] Build script completed")
