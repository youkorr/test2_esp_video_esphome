import os
import glob
import sys
Import("env")

try:
    component_dir = os.path.dirname(os.path.abspath(__file__))
except NameError:
    component_dir = Dir('.').srcnode().abspath
parent_components_dir = os.path.dirname(component_dir)

env.Append(CPPDEFINES=[("CONFIG_IDF_TARGET_ESP32P4", "1")])

sources_to_add = []

esp_dl_dir = os.path.join(parent_components_dir, "esp-dl")
if os.path.exists(esp_dl_dir):
    esp_dl_include_dirs = [
        "dl", "dl/tool/include", "dl/tool/isa/esp32p4", "dl/tool/isa/tie728",
        "dl/tool/isa/xtensa", "dl/tool/src", "dl/tensor/include", "dl/tensor/src",
        "dl/base", "dl/base/isa", "dl/base/isa/esp32p4", "dl/base/isa/tie728",
        "dl/base/isa/xtensa", "dl/math/include", "dl/math/src", "dl/model/include",
        "dl/model/src", "dl/module/include", "dl/module/src", "fbs_loader/include",
        "fbs_loader/lib/esp32p4", "fbs_loader/src", "vision/detect", "vision/image",
        "vision/image/isa", "vision/image/isa/esp32p4",
    ]

    for inc_dir in esp_dl_include_dirs:
        inc_path = os.path.join(esp_dl_dir, inc_dir)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])

    esp_dl_source_dirs = [
        "dl/tensor/src",
        "dl/model/src",
        "dl/module/src",
        "dl/tool/src",
        "dl/math/src",
        "fbs_loader/src",
        "vision/image",
        "vision/detect",
    ]

    esp_dl_exclude = [
        "dl_base_dotprod.cpp",
        "dl_image_jpeg.cpp",
        "dl_image_bmp.cpp",
        "dl_detect_msr_postprocessor.cpp",
        "dl_detect_mnp_postprocessor.cpp",
        "dl_pose_yolo11_postprocessor.cpp",
        "dl_detect_espdet_postprocessor.cpp",
        "dl_detect_pico_postprocessor.cpp",
    ]

    for src_dir in esp_dl_source_dirs:
        src_dir_path = os.path.join(esp_dl_dir, src_dir)
        if os.path.exists(src_dir_path):
            if src_dir.startswith("vision/"):
                pattern = os.path.join(src_dir_path, "**", "*.cpp")
                for src_file in glob.glob(pattern, recursive=True):
                    if os.path.basename(src_file) not in esp_dl_exclude:
                        sources_to_add.append(src_file)
            else:
                for src_file in glob.glob(os.path.join(src_dir_path, "*.cpp")):
                    if os.path.basename(src_file) not in esp_dl_exclude:
                        sources_to_add.append(src_file)

    dl_base_dir = os.path.join(esp_dl_dir, "dl", "base")
    if os.path.exists(dl_base_dir):
        for src_file in glob.glob(os.path.join(dl_base_dir, "*.cpp")):
            if os.path.basename(src_file) not in esp_dl_exclude:
                sources_to_add.append(src_file)

    isa_dirs = [
        ("dl/base/isa/esp32p4", "*.S"),
        ("dl/base/isa/esp32p4", "*.cpp"),
        ("dl/tool/isa/esp32p4", "*.S"),
        ("vision/image/isa/esp32p4", "*.S"),
    ]

    for isa_dir, pattern in isa_dirs:
        isa_path = os.path.join(esp_dl_dir, isa_dir)
        if os.path.exists(isa_path):
            for asm_file in glob.glob(os.path.join(isa_path, pattern)):
                sources_to_add.append(asm_file)

    fbs_lib_dir = os.path.join(esp_dl_dir, "fbs_loader", "lib", "esp32p4")
    fbs_lib = os.path.join(fbs_lib_dir, "libfbs_model.a")
    if os.path.exists(fbs_lib):
        env.Append(LIBPATH=[fbs_lib_dir])
        env.Prepend(LIBS=["fbs_model"])

def _flag_value_truthy(value):
    if value is None:
        return False
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return value != 0
    s = str(value).strip().strip('"').strip("'")
    return s not in ("", "0", "false", "False", "FALSE")

sdcard_mode = False
sdcard_flag_seen = False

for define in env.get("CPPDEFINES", []):
    if isinstance(define, (tuple, list)) and len(define) >= 1 \
            and define[0] == "CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD":
        sdcard_flag_seen = True
        sdcard_mode = _flag_value_truthy(define[1] if len(define) >= 2 else None)
        break
    if isinstance(define, str) and define.split("=", 1)[0] == "CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD":
        sdcard_flag_seen = True
        if "=" in define:
            sdcard_mode = _flag_value_truthy(define.split("=", 1)[1])
        else:
            sdcard_mode = True
        break

if not sdcard_flag_seen:
    for flag in env.get("BUILD_FLAGS", []):
        s = str(flag).strip()
        if s.startswith("-D"):
            s = s[2:]
        name, _, value = s.partition("=")
        if name == "CONFIG_YOLO11_DETECT_MODEL_IN_SDCARD":
            sdcard_flag_seen = True
            sdcard_mode = _flag_value_truthy(value if value else "1")
            break

yolo11_detect_dir = os.path.join(parent_components_dir, "yolo11_detect")

if not sdcard_mode:
    if not os.path.exists(yolo11_detect_dir):
        sys.exit(f"[YOLO11 Detection] FATAL: yolo11_detect dir not found at {yolo11_detect_dir}")

    yolo11_model = os.path.join(yolo11_detect_dir, "models", "p4", "yolo11_detect_s8_v1.espdl")

    embed_c_file = os.path.join(component_dir, "yolo11_detect_espdl_embed.c")

    with open(yolo11_model, "rb") as f:
        model_data = f.read()

    c_lines = [
        "// Auto-generated - embedded yolo11_detect model",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "__attribute__((aligned(16)))",
        "const uint8_t _binary_yolo11_detect_espdl_start[] = {",
    ]
    for i in range(0, len(model_data), 16):
        chunk = model_data[i:i + 16]
        c_lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    c_lines.append("};")
    c_lines.append(f"const uint8_t *const _binary_yolo11_detect_espdl_end = _binary_yolo11_detect_espdl_start + {len(model_data)};")
    c_lines.append(f"const size_t _binary_yolo11_detect_espdl_size = {len(model_data)};")

    with open(embed_c_file, "w") as f:
        f.write("\n".join(c_lines))

    sources_to_add.append(embed_c_file)

if os.path.exists(yolo11_detect_dir):
    env.Append(CPPPATH=[yolo11_detect_dir])
    yolo11_sources = ["yolo11_detect.cpp"]
    for src in yolo11_sources:
        src_path = os.path.join(yolo11_detect_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)

dotprod_file = os.path.join(component_dir, "dl_base_dotprod_no_dsp.cpp")
if os.path.exists(dotprod_file):
    sources_to_add.append(dotprod_file)

mbedtls_stub = os.path.join(component_dir, "mbedtls_aes_stub.c")
if os.path.exists(mbedtls_stub):
    sources_to_add.append(mbedtls_stub)

env.Append(CPPPATH=[component_dir])

if sources_to_add:
    objects = []
    for src_file in sources_to_add:
        try:
            obj = env.Object(src_file)
            objects.extend(obj)
        except Exception as e:
            pass

    if objects:
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libyolo11_detection"),
            objects
        )
        env.Prepend(LIBS=[lib])
        env['_LIBFLAGS'] = '-Wl,--start-group ' + env['_LIBFLAGS'] + ' -Wl,--end-group'
