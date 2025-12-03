"""
Build script for network_camera component
Links openh264 decoder library
"""

import os
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[Network Camera] Build script running...")

# ========================================================================
# H.264 decoder library linking - ONLY openh264
# ========================================================================
esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
if os.path.exists(esp_h264_dir):
    h264_lib_dir = os.path.join(esp_h264_dir, "sw", "libs", "esp32p4")
    openh264_lib = os.path.join(h264_lib_dir, "libopenh264.a")

    if os.path.exists(openh264_lib):
        print(f"[Network Camera] Found openh264 library: {openh264_lib}")

        # Add include paths for openh264
        h264_inc = os.path.join(esp_h264_dir, "sw", "libs", "openh264_inc")
        if os.path.exists(h264_inc):
            env.Append(CPPPATH=[h264_inc])

        # Also add tinyh264_inc for h264bsd API headers
        h264_inc_bsd = os.path.join(esp_h264_dir, "sw", "libs", "tinyh264_inc")
        if os.path.exists(h264_inc_bsd):
            env.Append(CPPPATH=[h264_inc_bsd])

        # Link ONLY openh264 with --whole-archive (NOT tinyh264!)
        # Use --allow-multiple-definition in case other components also link it
        env.Append(LINKFLAGS=[
            "-Wl,--allow-multiple-definition",
            "-Wl,--whole-archive",
            openh264_lib,
            "-Wl,--no-whole-archive"
        ])
        print(f"[Network Camera] ✓ Linked openh264 (Baseline/Main/High profiles, --whole-archive, --allow-multiple-definition)")
    else:
        print(f"[Network Camera] ⚠️  openh264 not found at {openh264_lib}")
else:
    print(f"[Network Camera] ⚠️  esp_h264 component not found")

print("[Network Camera] Build script completed")
