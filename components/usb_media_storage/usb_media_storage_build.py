"""
Build script for usb_media_storage component
Compiles and links bundled usb_host_msc library (from espressif/esp-usb)
"""

import os
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir

print("[USB Media Storage] Build script running...")

# ========================================================================
# usb_host_msc include paths
# ========================================================================
msc_dir = os.path.join(component_dir, "components", "usb_host_msc")

msc_public_inc = os.path.join(msc_dir, "include")
msc_private_inc = os.path.join(msc_dir, "private_include")

if os.path.exists(msc_public_inc):
    env.Append(CPPPATH=[msc_public_inc])
    print(f"[USB Media Storage] + usb_host_msc public includes")

if os.path.exists(msc_private_inc):
    env.Append(CPPPATH=[msc_private_inc])
    print(f"[USB Media Storage] + usb_host_msc private includes")

# ========================================================================
# Compile usb_host_msc source files
# ========================================================================
msc_src_dir = os.path.join(msc_dir, "src")
if os.path.exists(msc_src_dir):
    msc_sources = []
    for f in sorted(os.listdir(msc_src_dir)):
        if f.endswith(".c"):
            src_path = os.path.join(msc_src_dir, f)
            msc_sources.append(src_path)

    if msc_sources:
        objs = [env.Object(src) for src in msc_sources]
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libusb_host_msc"),
            objs
        )
        env.Prepend(LIBS=[lib])
        print(f"[USB Media Storage] Compiled and linked {len(msc_sources)} usb_host_msc source files")
    else:
        print(f"[USB Media Storage] WARNING: No source files found in {msc_src_dir}")
else:
    print(f"[USB Media Storage] WARNING: usb_host_msc source directory not found")

print("[USB Media Storage] Build script completed")
