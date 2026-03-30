"""
Shared ESP-DL source compilation.
Called by detection component build scripts (face_detection, yolov11, yolo11_detection).
Compiles ESP-DL sources ONCE into a single shared libespdl.a.
Subsequent calls detect the existing library and skip recompilation.
"""

import os
import glob

# Marker to prevent double compilation within the same SCons run
_ESPDL_BUILT_KEY = "_ESPDL_SHARED_LIB_BUILT"


def build_espdl(env, esp_dl_dir, isa_target="esp32p4"):
    """Compile ESP-DL sources into a shared static library.

    Args:
        env: PlatformIO SCons environment
        esp_dl_dir: Path to esp-dl root (containing dl/, vision/, fbs_loader/)
        isa_target: ISA target (esp32p4, tie728, xtensa)

    Returns:
        True if library was built or already exists, False on error.
    """
    # Skip if already built in this SCons run
    if env.get(_ESPDL_BUILT_KEY, False):
        print("[ESP-DL Shared] Already compiled in this build, skipping")
        return True

    if not os.path.exists(esp_dl_dir):
        print(f"[ESP-DL Shared] ERROR: esp-dl not found at {esp_dl_dir}")
        return False

    # ====================================================================
    # Include paths
    # ====================================================================
    esp_dl_include_dirs = [
        "dl", "dl/tool/include", f"dl/tool/isa/{isa_target}",
        "dl/tool/src", "dl/tensor/include", "dl/tensor/src",
        "dl/base", "dl/base/isa", f"dl/base/isa/{isa_target}",
        "dl/math/include", "dl/math/src", "dl/model/include",
        "dl/model/src", "dl/module/include", "dl/module/src",
        "fbs_loader/include", f"fbs_loader/lib/{isa_target}", "fbs_loader/src",
        "vision/detect", "vision/image", "vision/image/isa",
        f"vision/image/isa/{isa_target}", "vision/recognition",
        "vision/classification",
    ]

    inc_count = 0
    for inc_dir in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc_dir)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])
            inc_count += 1
    print(f"[ESP-DL Shared] {inc_count} include paths added")

    # ====================================================================
    # Source directories (only dl/, fbs_loader/, vision/)
    # Excludes: audio/, examples/, docs/, test/, speech/
    # ====================================================================
    esp_dl_source_dirs = [
        "dl/tensor/src",
        "dl/model/src",
        "dl/module/src",
        "dl/tool/src",
        "dl/math/src",
        "fbs_loader/src",
        "vision/image",
        "vision/detect",
        "vision/recognition",
    ]

    exclude_dirs = {"audio", "examples", "docs", "test", "speech"}
    exclude_files = {
        "dl_image_jpeg.cpp",
        "dl_image_bmp.cpp",
    }

    sources = []

    # Core + vision sources
    for src_dir in esp_dl_source_dirs:
        src_dir_path = os.path.join(esp_dl_dir, src_dir)
        if not os.path.exists(src_dir_path):
            continue
        if src_dir.startswith("vision/"):
            for src_file in glob.glob(os.path.join(src_dir_path, "**", "*.cpp"), recursive=True):
                fname = os.path.basename(src_file)
                rel = os.path.relpath(src_file, esp_dl_dir)
                parts = set(rel.split(os.sep))
                if parts & exclude_dirs:
                    continue
                if fname not in exclude_files:
                    sources.append(src_file)
        else:
            for src_file in glob.glob(os.path.join(src_dir_path, "*.cpp")):
                fname = os.path.basename(src_file)
                if fname not in exclude_files:
                    sources.append(src_file)

    # dl/base/*.cpp
    dl_base_dir = os.path.join(esp_dl_dir, "dl", "base")
    if os.path.exists(dl_base_dir):
        for src_file in glob.glob(os.path.join(dl_base_dir, "*.cpp")):
            sources.append(src_file)

    # ISA-specific assembly/source
    for isa_dir in [f"dl/base/isa/{isa_target}", f"dl/tool/isa/{isa_target}",
                     f"vision/image/isa/{isa_target}"]:
        isa_path = os.path.join(esp_dl_dir, isa_dir)
        if os.path.exists(isa_path):
            for ext in ["*.cpp", "*.S", "*.c"]:
                for src_file in glob.glob(os.path.join(isa_path, ext)):
                    sources.append(src_file)

    # Pre-compiled static library (fbs_loader)
    fbs_lib = os.path.join(esp_dl_dir, "fbs_loader", "lib", isa_target, "libfbs_model.a")
    if os.path.exists(fbs_lib):
        env.Append(LIBPATH=[os.path.dirname(fbs_lib)])
        env.Append(LIBS=["fbs_model"])
        print("[ESP-DL Shared] + libfbs_model.a")

    print(f"[ESP-DL Shared] {len(sources)} source files to compile")

    # ====================================================================
    # Compile into shared static library
    # ====================================================================
    if sources:
        objects = []
        for src_file in sources:
            try:
                obj = env.Object(src_file)
                objects.extend(obj)
            except Exception as e:
                print(f"[ESP-DL Shared] Failed: {os.path.basename(src_file)}: {e}")

        if objects:
            lib = env.StaticLibrary(
                os.path.join("$BUILD_DIR", "libespdl_shared"),
                objects
            )
            env.Prepend(LIBS=[lib])
            if '-Wl,--start-group' not in env.get('_LIBFLAGS', ''):
                env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
            print(f"[ESP-DL Shared] libespdl_shared.a created ({len(objects)} objects)")

    # Mark as built
    env[_ESPDL_BUILT_KEY] = True
    return True
