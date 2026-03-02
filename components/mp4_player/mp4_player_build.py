"""
Build script for mp4_player component
Links esp_extractor prebuilt library (no BSP dependencies)
Applies SDMMC DMA alignment patch for ESP32-P4 video performance
"""

import os
import subprocess
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print("[MP4 Player] Build script running...")

# ========================================================================
# Apply SDMMC aligned buffer patch for ESP32-P4
# Without this patch, SD card reads fall back to slow single-block DMA
# instead of fast multi-block DMA reads, causing slow video playback
# ========================================================================
patch_file = os.path.join(component_dir, "0004-fix-sdmmc-aligned-write-buffer.patch")
if os.path.exists(patch_file):
    # Find the ESP-IDF framework directory
    idf_path = env.get("IDF_PATH") or os.environ.get("IDF_PATH", "")
    if not idf_path:
        # PlatformIO stores framework path in PIOINCLUDES or we can find it from the toolchain
        for p in env.get("CPPPATH", []):
            p_str = str(p)
            if "framework-espidf" in p_str:
                # Walk up to find the framework root
                idx = p_str.find("framework-espidf")
                idf_path = p_str[:idx + len("framework-espidf")]
                break
    if not idf_path:
        # Try common PlatformIO paths
        platform = env.PioPlatform()
        if hasattr(platform, 'get_package_dir'):
            try:
                idf_path = platform.get_package_dir("framework-espidf")
            except Exception:
                pass

    sdmmc_cmd_file = ""
    if idf_path:
        sdmmc_cmd_file = os.path.join(idf_path, "components", "sdmmc", "sdmmc_cmd.c")

    if sdmmc_cmd_file and os.path.exists(sdmmc_cmd_file):
        # Check if patch is already applied by looking for the cache buffer marker
        with open(sdmmc_cmd_file, "r") as f:
            content = f.read()

        if "s_write_cache_buffer" not in content:
            print(f"[MP4 Player] Applying SDMMC DMA alignment patch...")
            try:
                result = subprocess.run(
                    ["git", "apply", "--directory", idf_path, patch_file],
                    capture_output=True, text=True, cwd=idf_path
                )
                if result.returncode != 0:
                    # git apply may fail if not a git repo, try patch command
                    result = subprocess.run(
                        ["patch", "-p1", "-N", "-i", patch_file],
                        capture_output=True, text=True, cwd=idf_path
                    )
                if result.returncode == 0:
                    print(f"[MP4 Player] SDMMC patch applied successfully")
                else:
                    print(f"[MP4 Player] WARNING: SDMMC patch failed: {result.stderr}")
            except Exception as e:
                print(f"[MP4 Player] WARNING: Could not apply SDMMC patch: {e}")
        else:
            print(f"[MP4 Player] SDMMC patch already applied")
    else:
        print(f"[MP4 Player] WARNING: sdmmc_cmd.c not found (IDF_PATH={idf_path})")
else:
    print(f"[MP4 Player] WARNING: SDMMC patch file not found")

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
