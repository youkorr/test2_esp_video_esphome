"""
Build script for ESP-DL shared component
Compiles ESP-DL core sources once for all detection components
"""

import os
import glob
Import("env")

script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

# Check if ESP-DL is already built (avoid duplicate compilation)
build_marker = os.path.join(env.subst("$BUILD_DIR"), ".esp_dl_built")
if os.path.exists(build_marker):
    print("[ESP-DL] Already compiled (skip)")
else:
    print("[ESP-DL] Build script running...")

# ========================================================================
# Add CONFIG defines
# ========================================================================
env.Append(CPPDEFINES=[
    ("CONFIG_IDF_TARGET_ESP32P4", "1"),
])

sources_to_add = []

# ========================================================================
# ESP-DL Sources - Only essential files for detection/recognition
# ========================================================================
esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
if os.path.exists(esp_dl_dir):
    # Add all include directories
    esp_dl_include_dirs = [
        "dl", "dl/tool/include", "dl/tool/isa/esp32p4", "dl/tool/isa/tie728",
        "dl/tool/isa/xtensa", "dl/tool/src", "dl/tensor/include", "dl/tensor/src",
        "dl/base", "dl/base/isa", "dl/base/isa/esp32p4", "dl/base/isa/tie728",
        "dl/base/isa/xtensa", "dl/math/include", "dl/math/src", "dl/model/include",
        "dl/model/src", "dl/module/include", "dl/module/src", "fbs_loader/include",
        "fbs_loader/lib/esp32p4", "fbs_loader/src", "vision/detect", "vision/image",
        "vision/image/isa", "vision/image/isa/esp32p4", "vision/recognition",
        "vision/classification",
    ]

    for inc_dir in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc_dir)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    print(f"[ESP-DL] Include paths added")

    # Explicit list of essential ESP-DL source files
    # This is much faster than wildcard scanning
    esp_dl_essential_sources = [
        # Core tensor operations
        "dl/tensor/src/dl_tensor_base.cpp",

        # Model loading
        "dl/model/src/dl_model_base.cpp",
        "dl/model/src/dl_model_context.cpp",
        "dl/model/src/dl_memory_manager.cpp",
        "dl/model/src/dl_memory_manager_greedy.cpp",

        # Module base
        "dl/module/src/dl_module_base.cpp",

        # Tools
        "dl/tool/src/dl_tool.cpp",
        "dl/tool/src/dl_tool_cache.cpp",

        # Math operations
        "dl/math/src/dl_math.cpp",
        "dl/math/src/dl_math_matrix.cpp",

        # FBS loader
        "fbs_loader/src/fbs_loader.cpp",

        # Vision - Image processing
        "vision/image/dl_image_preprocessor.cpp",
        "vision/image/dl_image_draw.cpp",
        "vision/image/dl_image_process.cpp",
        "vision/image/dl_image_pixel_cvt_dispatch_rgb565.cpp",
        "vision/image/dl_image_pixel_cvt_dispatch_rgb888.cpp",
        "vision/image/dl_image_pixel_cvt_dispatch_gray.cpp",
        "vision/image/dl_image_ppa.cpp",

        # Vision - Detection
        "vision/detect/dl_detect_base.cpp",
        "vision/detect/dl_detect_postprocessor.cpp",
        "vision/detect/dl_detect_msr_postprocessor.cpp",
        "vision/detect/dl_detect_mnp_postprocessor.cpp",
        "vision/detect/dl_detect_pico_postprocessor.cpp",

        # Vision - Recognition
        "vision/recognition/dl_feat_base.cpp",
        "vision/recognition/dl_feat_image_preprocessor.cpp",
        "vision/recognition/dl_feat_postprocessor.cpp",
        "vision/recognition/dl_recognition_database.cpp",

        # Neural network layers (dl/base) - essential operations
        "dl/base/dl_base_conv2d.cpp",
        "dl/base/dl_base_depthwise_conv2d.cpp",
        "dl/base/dl_base_relu.cpp",
        "dl/base/dl_base_leakyrelu.cpp",
        "dl/base/dl_base_prelu.cpp",
        "dl/base/dl_base_add.cpp",
        "dl/base/dl_base_add2d.cpp",
        "dl/base/dl_base_sub.cpp",
        "dl/base/dl_base_sub2d.cpp",
        "dl/base/dl_base_mul.cpp",
        "dl/base/dl_base_mul2d.cpp",
        "dl/base/dl_base_div.cpp",
        "dl/base/dl_base_max.cpp",
        "dl/base/dl_base_max2d.cpp",
        "dl/base/dl_base_min.cpp",
        "dl/base/dl_base_min2d.cpp",
        "dl/base/dl_base_max_pool2d.cpp",
        "dl/base/dl_base_avg_pool2d.cpp",
        "dl/base/dl_base_pad.cpp",
        "dl/base/dl_base_resize.cpp",
        "dl/base/dl_base_shape.cpp",
        "dl/base/dl_base_elemwise.cpp",
        "dl/base/dl_base_requantize_linear.cpp",
    ]

    # Files to explicitly exclude
    esp_dl_exclude = [
        "dl_base_dotprod.cpp",  # Use custom implementation
        "dl_image_jpeg.cpp",     # Not needed
        "dl_image_bmp.cpp",      # Not needed
    ]

    for src in esp_dl_essential_sources:
        src_path = os.path.join(esp_dl_dir, src)
        basename = os.path.basename(src)
        if os.path.exists(src_path) and basename not in esp_dl_exclude:
            sources_to_add.append(src_path)

    print(f"[ESP-DL] {len(sources_to_add)} essential source files selected")

    # Add prebuilt FBS library
    fbs_lib_dir = os.path.join(esp_dl_dir, "fbs_loader", "lib", "esp32p4")
    fbs_lib = os.path.join(fbs_lib_dir, "libfbs_model.a")
    if os.path.exists(fbs_lib):
        env.Append(LIBPATH=[fbs_lib_dir])
        env.Prepend(LIBS=["fbs_model"])
        print("[ESP-DL] Added libfbs_model.a")

# ========================================================================
# Copy stub files from lvgl_camera_display
# ========================================================================
lvgl_cam_dir = os.path.join(parent_components_dir, "lvgl_camera_display")

# Custom dotprod implementation
dotprod_src = os.path.join(lvgl_cam_dir, "dl_base_dotprod_no_dsp.cpp")
dotprod_dst = os.path.join(component_dir, "dl_base_dotprod_no_dsp.cpp")
if os.path.exists(dotprod_src) and not os.path.exists(dotprod_dst):
    import shutil
    shutil.copy(dotprod_src, dotprod_dst)
if os.path.exists(dotprod_dst):
    sources_to_add.append(dotprod_dst)
    print("[ESP-DL] + dl_base_dotprod_no_dsp.cpp")

# mbedTLS stub
mbedtls_stub_src = os.path.join(lvgl_cam_dir, "mbedtls_aes_stub.c")
mbedtls_stub_dst = os.path.join(component_dir, "mbedtls_aes_stub.c")
if os.path.exists(mbedtls_stub_src) and not os.path.exists(mbedtls_stub_dst):
    import shutil
    shutil.copy(mbedtls_stub_src, mbedtls_stub_dst)
if os.path.exists(mbedtls_stub_dst):
    sources_to_add.append(mbedtls_stub_dst)
    print("[ESP-DL] + mbedtls_aes_stub.c")

# mbedTLS header directory
mbedtls_header_src = os.path.join(lvgl_cam_dir, "mbedtls")
mbedtls_header_dst = os.path.join(component_dir, "mbedtls")
if os.path.exists(mbedtls_header_src) and not os.path.exists(mbedtls_header_dst):
    import shutil
    shutil.copytree(mbedtls_header_src, mbedtls_header_dst)

env.Append(CPPPATH=[component_dir])

# ========================================================================
# Compile sources
# ========================================================================
if sources_to_add and not os.path.exists(build_marker):
    objects = []
    for src_file in sources_to_add:
        try:
            obj = env.Object(src_file)
            objects.extend(obj)
        except Exception as e:
            print(f"[ESP-DL] Failed to compile {os.path.basename(src_file)}: {e}")

    if objects:
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libesp_dl"),
            objects
        )
        env.Prepend(LIBS=[lib])
        env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
        print(f"[ESP-DL] {len(sources_to_add)} source files compiled")
        print("[ESP-DL] libesp_dl.a created")

        # Create build marker
        os.makedirs(os.path.dirname(build_marker), exist_ok=True)
        with open(build_marker, 'w') as f:
            f.write("built")

print("[ESP-DL] Build script completed")
