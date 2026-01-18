"""
Script de build PlatformIO pour ESP-Video
Ajoute tous les fichiers sources C/C++ des composants ESP-IDF
"""

import os
Import("env")

# Force enable ISP Pipeline Controller
# This define ensures ISP pipeline code is included in the build
print("[ESP-Video Build] ========================================")
print("[ESP-Video Build] Adding ISP Pipeline Controller define")
env.Append(CPPDEFINES=[
    ("CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER", "1"),
])
print("[ESP-Video Build]   - CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1")
print("[ESP-Video Build] ========================================")

# Obtenir le répertoire du composant (ce script est dans components/esp_video/)
# Dans SCons, __file__ n'existe pas, on utilise Dir('.').srcnode().abspath
script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print(f"[ESP-Video Build] Répertoire composant: {component_dir}")
print(f"[ESP-Video Build] Répertoire parent: {parent_components_dir}")

# Liste de tous les fichiers sources à compiler
sources_to_add = []

# ========================================================================
# Sources ESP-Video
# ========================================================================
esp_video_sources = [
    "src/esp_video_buffer.c",
    "src/esp_video_init.c",
    "src/esp_video_ioctl.c",
    "src/esp_video_mman.c",
    "src/esp_video_vfs.c",
    "src/esp_video.c",
    "src/esp_video_cam.c",
    "src/device/esp_video_csi_device.c",
    "src/device/esp_video_h264_device.c",
    "src/device/esp_video_jpeg_device.c",
    "src/device/esp_video_isp_device.c",
    "src/esp_video_isp_pipeline.c",
    "src/esp_video_isp_stubs.c",
]

# Ajouter le chemin d'include private_include de esp_video
esp_video_private_include = os.path.join(component_dir, "private_include")
if os.path.exists(esp_video_private_include):
    env.Append(CPPPATH=[esp_video_private_include])
    print(f"[ESP-Video Build] Include privé ajouté: {esp_video_private_include}")

for src in esp_video_sources:
    src_path = os.path.join(component_dir, src)
    if os.path.exists(src_path):
        sources_to_add.append(src_path)
        # print(f"[ESP-Video Build] + {src}")

# ========================================================================
# Sources esp_cam_sensor
# ========================================================================
esp_cam_sensor_dir = os.path.join(parent_components_dir, "esp_cam_sensor")
esp_cam_sensor_sources = [
    "src/esp_cam_sensor.c",
    "src/esp_cam_motor.c",
    "src/esp_cam_sensor_xclk.c",
    "src/esp_cam_sensor_detect_stubs.c",  # Linker symbols for sensor auto-detection
    "src/driver_spi/spi_slave.c",
    "src/driver_cam/esp_cam_ctlr_spi_cam.c",
    "sensor/ov5647/ov5647.c",
    "sensor/sc202cs/sc202cs.c",
    "sensor/ov02c10/ov02c10.c",
]

# Ajouter les chemins d'include pour les sensors (private_include)
esp_cam_sensor_includes = [
    "sensor/ov5647/private_include",
    "sensor/sc202cs/include/private_include",
    "sensor/ov02c10/private_include",
]

if os.path.exists(esp_cam_sensor_dir):
    # Ajouter les chemins d'include
    for inc in esp_cam_sensor_includes:
        inc_path = os.path.join(esp_cam_sensor_dir, inc)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])
            print(f"[ESP-Video Build] Include sensor ajouté: {inc_path}")

    # Compiler les sources
    for src in esp_cam_sensor_sources:
        src_path = os.path.join(esp_cam_sensor_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[ESP-Video Build] + esp_cam_sensor/{src}")

# ========================================================================
# Sources esp_h264
# ========================================================================
# NOTE: Le décodeur utilise tinyh264/h264bsd (Baseline profile uniquement)
# NOTE: L'encodeur utilise OpenH264 (Baseline, Main, High profiles)
# NOTE: esp_h264_alloc_less_than_5_3.c est exclu car nous utilisons ESP-IDF >= 5.3
esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
esp_h264_sources = [
    "port/src/esp_h264_alloc.c",
    # "port/src/esp_h264_alloc_less_than_5_3.c",  # Exclu: pour ESP-IDF < 5.3 seulement
    "port/src/esp_h264_cache.c",
    "sw/src/h264_color_convert.c",
    # Sources logicielles
    "sw/src/esp_h264_enc_sw_param.c",      # Nécessite codec_api.h (OpenH264 encoder)
    # NOTE: esp_h264_dec_sw.c is compiled SEPARATELY below with explicit DUAL_TASK flags
    "sw/src/esp_h264_enc_single_sw.c",     # Nécessite codec_api.h (OpenH264 encoder)
    # Sources matérielles (encodeur H.264 hardware ESP32-P4)
    "hw/src/esp_h264_enc_single_hw.c",     # Encodeur hardware single-stream
    "hw/src/esp_h264_enc_dual_hw.c",       # Encodeur hardware dual-stream
    "hw/src/esp_h264_enc_hw_param.c",      # Paramètres encodeur hardware
    "hw/src/h264_nal.c",                   # Gestion NAL units
    "hw/src/h264_rc.c",                    # Rate control
    "hw/hal/esp32p4/h264_hal.c",           # HAL H.264 pour ESP32-P4
    "hw/hal/esp32p4/h264_dma_hal.c",       # HAL DMA H.264
    "interface/src/esp_h264_enc_param.c",
    "interface/src/esp_h264_enc_param_hw.c",
    "interface/src/esp_h264_enc_dual.c",
    "interface/src/esp_h264_dec_param.c",
    "interface/src/esp_h264_version.c",
    "interface/src/esp_h264_dec.c",
    "interface/src/esp_h264_enc_single.c",
]

if os.path.exists(esp_h264_dir):
    # ========================================================================
    # CRITICAL: Compile esp_h264_dec_sw.c with explicit DUAL_TASK flags
    # ========================================================================
    # This must be done BEFORE adding other sources to ensure the flags are applied
    esp_h264_dec_sw_path = os.path.join(esp_h264_dir, "sw/src/esp_h264_dec_sw.c")
    if os.path.exists(esp_h264_dec_sw_path):
        # Clone environment to avoid affecting other files
        dec_env = env.Clone()

        # Add DUAL_TASK flags explicitly for esp_h264_dec_sw.c
        # These MUST be defined for this file to compile correctly
        dec_env.Append(CPPDEFINES=[
            ("CONFIG_ESP_H264_DUAL_TASK", "1"),
            ("CONFIG_ESP_H264_DUAL_TASK_CORE", "1"),
            ("CONFIG_ESP_H264_DUAL_TASK_PRIORITY", "17"),
        ])

        # Add required include paths for decoder
        dec_includes = [
            os.path.join(esp_h264_dir, "sw", "libs", "tinyh264_inc"),
            os.path.join(esp_h264_dir, "sw", "libs", "openh264_inc"),
            os.path.join(esp_h264_dir, "port", "inc"),
            os.path.join(esp_h264_dir, "sw", "inc"),
        ]
        for inc in dec_includes:
            if os.path.exists(inc):
                dec_env.Append(CPPPATH=[inc])

        # Create a special marker to compile this file separately later
        # We'll handle it in the compilation loop with dec_env
        # Store the path and environment for later use
        if not hasattr(env, 'h264_dec_custom_compile'):
            env.h264_dec_custom_compile = []
        env.h264_dec_custom_compile.append({
            'path': esp_h264_dec_sw_path,
            'env': dec_env
        })
        print("[ESP-Video Build] Will compile esp_h264_dec_sw.c with dual-task flags from esp_h264 component")
    else:
        print("[ESP-Video Build]  esp_h264_dec_sw.c not found!")

    # Dual-task H.264 flags are defined in esp_h264/__init__.py to avoid redefinition warnings

    # Ajouter les chemins d'include pour les bibliothèques H.264
    h264_lib_includes = [
        "sw/libs/openh264_inc",   # codec_api.h (OpenH264 encoder + decoder)
        "sw/libs/tinyh264_inc",   # h264bsd_decoder.h (fallback, not used with OpenH264)
    ]

    # Ajouter les chemins d'include pour le hardware encoder
    h264_hw_includes = [
        "hw/src",                  # Headers privés HW
        "hw/hal/esp32p4",         # HAL et LL headers
        "hw/soc/esp32p4",         # SOC structures (h264_struct.h, h264_dma_struct.h)
    ]

    for inc in h264_lib_includes:
        inc_path = os.path.join(esp_h264_dir, inc)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])
            print(f"[ESP-Video Build] Include H.264 lib ajouté: {inc}")

    for inc in h264_hw_includes:
        inc_path = os.path.join(esp_h264_dir, inc)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])
            print(f"[ESP-Video Build] Include H.264 HW ajouté: {inc}")

    # Note: H.264 decoder uses tinyh264 (h264bsd), encoder uses OpenH264
    # tinyh264 supports H.264 Baseline profile
    print(f"[ESP-Video Build] tinyh264 decoder enabled (H.264 Baseline profile)")
    print(f"[ESP-Video Build]   Up to 8K UHD, 8-bit 4:2:0 planar YUV")

    # Link H.264 libraries: tinyh264 (decoder) + OpenH264 (encoder)
    h264_static_libs_dir = os.path.join(esp_h264_dir, "sw/libs/esp32p4")
    if os.path.exists(h264_static_libs_dir):
        tinyh264_lib = os.path.join(h264_static_libs_dir, "libtinyh264.a")
        openh264_lib = os.path.join(h264_static_libs_dir, "libopenh264.a")

        if os.path.exists(tinyh264_lib) and os.path.exists(openh264_lib):
            # Add library path
            env.Append(LIBPATH=[h264_static_libs_dir])

            # CRITICAL FIX: Link OpenH264 with --whole-archive, TinyH264 WITHOUT
            # Problem: libtinyh264.a contains esp_h264_dec_sw.o (precompiled without DUAL_TASK)
            # Solution: Our custom esp_h264_dec_sw.o (in libesp_video_full.a) must take precedence!
            #           Link TinyH264 normally - it will provide h264bsd* symbols only
            env.Append(LINKFLAGS=[
                "-Wl,--allow-multiple-definition",
                "-Wl,--whole-archive",
                openh264_lib,   # Encoder library (needs all symbols)
                "-Wl,--no-whole-archive",
            ])

            # Link TinyH264 NORMALLY (not --whole-archive) to allow our custom wrapper to override
            env.Append(LIBS=["tinyh264"])

            print(f"[ESP-Video Build] Linked libopenh264.a (--whole-archive)")
            print(f"[ESP-Video Build] Linked libtinyh264.a (NORMAL mode)")
            print(f"[ESP-Video Build]  Custom esp_h264_dec_sw.o (with DUAL_TASK) will override library version")
        else:
            if not os.path.exists(tinyh264_lib):
                print(f"[ESP-Video Build]  libtinyh264.a not found in {h264_static_libs_dir}")
            if not os.path.exists(openh264_lib):
                print(f"[ESP-Video Build]  libopenh264.a not found in {h264_static_libs_dir}")

    for src in esp_h264_sources:
        src_path = os.path.join(esp_h264_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[ESP-Video Build] + esp_h264/{src}")

# ========================================================================
# Sources esp_ipa (IMPORTANT: compiler AVANT de linker avec libesp_ipa.a)
# ========================================================================
esp_ipa_dir = os.path.join(parent_components_dir, "esp_ipa")
esp_ipa_sources = [
    "src/version.c",              # Config IPA custom (5 IPAs: AWB, denoise, sharpen, gamma, CC - PAS AGC)
    "src/esp_ipa_detect_stubs.c", # Detection array
    "src/esp_ipa_json_loader.c",  # JSON IPA parser pour charger configs OV02C10/OV5647
]

# print("")
# print("[ESP-Video Build] ========================================")
# print("[ESP-Video Build] === COMPILATION ESP_IPA (CONFIG CUSTOM) ===")
# print("[ESP-Video Build] ========================================")

if os.path.exists(esp_ipa_dir):
    for src in esp_ipa_sources:
        src_path = os.path.join(esp_ipa_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[ESP-Video Build] esp_ipa/{src} -> libesp_video_full.a")
    # print("[ESP-Video Build]")
    # print("[ESP-Video Build] Ces sources seront dans libesp_video_full.a")
    # print("[ESP-Video Build] Le linker utilisera version.o custom (pas celui de libesp_ipa.a)")
    # print("[ESP-Video Build] ========================================")
else:
    pass
    # print("[ESP-Video Build]  Répertoire esp_ipa introuvable!")

# print("")

# ========================================================================
# Sources esp_sccb_intf
# ========================================================================
esp_sccb_intf_dir = os.path.join(parent_components_dir, "esp_sccb_intf")
esp_sccb_intf_sources = [
    "src/sccb.c",
    "sccb_i2c/src/sccb_i2c.c",
]

if os.path.exists(esp_sccb_intf_dir):
    for src in esp_sccb_intf_sources:
        src_path = os.path.join(esp_sccb_intf_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[ESP-Video Build] + esp_sccb_intf/{src}")

# ========================================================================
# Sources esp_audio_codec - REMOVED (not working)
# ========================================================================
# Audio codec has been removed because it does not work properly

# ========================================================================
# Embarquer les fichiers JSON IPA des capteurs comme binary data
# ========================================================================
# print("")
# print("[ESP-Video Build] ========================================")
# print("[ESP-Video Build] === EMBEDDING SENSOR JSON CONFIGS ===")
# print("[ESP-Video Build] ========================================")

# Liste des fichiers JSON à embarquer
json_files_to_embed = [
    {
        "path": os.path.join(esp_cam_sensor_dir, "sensor/ov5647/cfg/ov5647_default.json"),
        "symbol": "ov5647_ipa_config_json",
    },
    {
        "path": os.path.join(esp_cam_sensor_dir, "sensor/ov02c10/cfg/ov02c10_default.json"),
        "symbol": "ov02c10_ipa_config_json",
    },
]

# Embarquer chaque fichier JSON comme binary data
embedded_json_objects = []
for json_info in json_files_to_embed:
    json_path = json_info["path"]
    symbol_name = json_info["symbol"]

    if os.path.exists(json_path):
        # Créer un nom de fichier objet pour ce JSON
        json_basename = os.path.basename(json_path).replace(".", "_")
        obj_filename = f"embedded_{json_basename}.o"
        obj_path = os.path.join("$BUILD_DIR", obj_filename)

        # Utiliser objcopy pour créer un fichier objet depuis le JSON
        # Les symbols générés seront: _binary_<name>_start, _binary_<name>_end, _binary_<name>_size
        objcopy_cmd = f"xtensa-esp32s3-elf-objcopy --input-target binary --output-target elf32-xtensa-le --binary-architecture xtensa {json_path} {obj_path}"

        # Note: PlatformIO/SCons n'a pas objcopy par défaut, donc on va utiliser une approche différente
        # On va créer un fichier C qui contient le JSON comme string
        c_wrapper_content = f'''/* Auto-generated wrapper for {os.path.basename(json_path)} */
#include <stddef.h>

const char {symbol_name}_start[] __attribute__((aligned(4))) =
'''

        # Lire le contenu du JSON et le convertir en string C
        try:
            with open(json_path, 'r') as f:
                json_content = f.read()
                # Échapper les caractères spéciaux pour le string C
                json_content_escaped = json_content.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')
                c_wrapper_content += f'    "{json_content_escaped}";\n\n'
                c_wrapper_content += f'const char *{symbol_name}_end = {symbol_name}_start + sizeof({symbol_name}_start);\n'
                c_wrapper_content += f'const size_t {symbol_name}_size = sizeof({symbol_name}_start);\n'

            # Créer le fichier C wrapper
            wrapper_filename = f"embedded_{symbol_name}.c"
            wrapper_path = os.path.join(component_dir, "src", wrapper_filename)

            # Écrire le fichier wrapper
            with open(wrapper_path, 'w') as f:
                f.write(c_wrapper_content)

            # Ajouter ce wrapper aux sources à compiler
            sources_to_add.append(wrapper_path)
            print(f"[ESP-Video Build] JSON embarqué: {os.path.basename(json_path)} -> {symbol_name}")

        except Exception as e:
            print(f"[ESP-Video Build]  Erreur lors de l'embedding de {json_path}: {e}")
    else:
        pass
        # print(f"[ESP-Video Build]  Fichier JSON introuvable: {json_path}")

# print("[ESP-Video Build] ========================================")
# print("")

# ========================================================================
# Forcer la recompilation en modifiant le timestamp ET supprimant les .o
# ========================================================================
import time as time_module
import glob

# Fichiers critiques qui doivent être recompilés (problème de cache SCons)
force_rebuild_files = [
    os.path.join(component_dir, "src/esp_video_init.c"),
    os.path.join(esp_cam_sensor_dir, "src/esp_cam_sensor_detect_stubs.c"),
]

print("[ESP-Video Build] ========================================")
print("[ESP-Video Build] === FORCED REBUILD OF CRITICAL FILES ===")

# Étape 1: Supprimer tous les .o correspondants PARTOUT
build_root = env.subst("$PROJECT_BUILD_DIR")
for src_file in force_rebuild_files:
    basename = os.path.basename(src_file).replace('.c', '.o')
    # Chercher récursivement dans tout le projet
    obj_pattern = os.path.join(build_root, "**", basename)
    found_objs = glob.glob(obj_pattern, recursive=True)
    for obj_file in found_objs:
        try:
            os.remove(obj_file)
            print(f"[ESP-Video Build] DELETED: {obj_file}")
        except Exception as e:
            print(f"[ESP-Video Build] Could not delete {obj_file}: {e}")

# Étape 2: Modifier les timestamps des sources
for src_file in force_rebuild_files:
    if os.path.exists(src_file):
        # Modifier le timestamp du fichier pour forcer SCons à le recompiler
        current_time = time_module.time()
        os.utime(src_file, (current_time, current_time))
        print(f"[ESP-Video Build] FORCED REBUILD: {os.path.basename(src_file)}")
        print(f"[ESP-Video Build]    Updated timestamp to force recompilation")
    else:
        print(f"[ESP-Video Build] File not found: {src_file}")

print("[ESP-Video Build] ========================================")

# ========================================================================
# Ajouter toutes les sources à la compilation
# ========================================================================
if sources_to_add:
    # Compiler chaque fichier source en objet
    objects = []
    h264_dec_obj = None  # Will hold the custom decoder object to link separately

    # First, compile esp_h264_dec_sw.c with custom environment if it exists
    if hasattr(env, 'h264_dec_custom_compile'):
        for custom in env.h264_dec_custom_compile:
            src_file = custom['path']
            custom_env = custom['env']
            obj = custom_env.Object(src_file)
            # CRITICAL: Don't add to objects[] - we'll link it directly later
            h264_dec_obj = obj[0] if isinstance(obj, list) else obj
            print(f"[ESP-Video Build] Compiled {os.path.basename(src_file)} with DUAL_TASK environment")
            print(f"[ESP-Video Build]  This object will be linked DIRECTLY (not in archive) to force precedence")

    for src_file in sources_to_add:
        # Skip if this file was already compiled with custom environment
        if hasattr(env, 'h264_dec_custom_compile'):
            if any(src_file == custom['path'] for custom in env.h264_dec_custom_compile):
                continue

        # Vérifier si c'est un fichier critique qui doit être forcé à recompiler
        is_critical = any(
            src_file.endswith(os.path.basename(critical_file))
            for critical_file in force_rebuild_files
        )

        # Compiler le fichier source en .o
        obj = env.Object(src_file)

        # Pour les fichiers critiques, forcer SCons à toujours les recompiler
        if is_critical:
            # AlwaysBuild: Force SCons to rebuild this file every time
            env.AlwaysBuild(obj)
            # NoCache: Don't use cached version of this object file
            env.NoCache(obj)
            print(f"[ESP-Video Build] ========================================")
            print(f"[ESP-Video Build] ALWAYS BUILD (NO CACHE): {os.path.basename(src_file)}")
            print(f"[ESP-Video Build] This file will be recompiled with ISP defines")
            print(f"[ESP-Video Build] ========================================")

        objects.extend(obj)

    # Créer une bibliothèque statique avec les objets compilés
    lib = env.StaticLibrary(
        os.path.join("$BUILD_DIR", "libesp_video_full"),
        objects
    )

    # Ajouter la bibliothèque au linkage (PREPEND = avant les autres libs)
    env.Prepend(LIBS=[lib])

    # CRITICAL: Create separate library for custom H.264 decoder to ensure precedence
    # This ensures our DUAL_TASK version overrides any precompiled version
    if h264_dec_obj is not None:
        # Create a tiny library containing ONLY our custom decoder
        h264_custom_lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libh264_decoder_custom"),
            [h264_dec_obj]
        )
        # Prepend this library to be linked FIRST
        env.Prepend(LIBS=[h264_custom_lib])
        print(f"[ESP-Video Build] Created libh264_decoder_custom.a with DUAL_TASK decoder")
        print(f"[ESP-Video Build] This library will be linked FIRST (highest precedence)")

    # print(f"[ESP-Video Build] {len(sources_to_add)} fichiers sources ajoutés à la compilation")
    # print(f"[ESP-Video Build] libesp_video_full.a créée avec tous les .o (y compris version.o custom)")

    # Maintenant linker avec libesp_ipa.a pour les fonctions IPA internes
    # Le linker utilisera notre version.o de libesp_video_full.a (déjà Prepend ci-dessus)
    # avant de chercher dans libesp_ipa.a
    esp_ipa_lib_dir = os.path.join(parent_components_dir, "esp_ipa", "lib/esp32p4")
    if os.path.exists(esp_ipa_lib_dir):
        env.Append(LIBPATH=[esp_ipa_lib_dir])
        env.Append(LIBS=["esp_ipa"])
        # print("")
        # print("[ESP-Video Build] ========================================")
        # print("[ESP-Video Build] Linking avec libesp_ipa.a (fonctions IPA internes)")
        # print("[ESP-Video Build]   Ordre de linking:")
        # print("[ESP-Video Build]   1. libesp_video_full.a (version.o custom)")
        # print("[ESP-Video Build]   2. libesp_ipa.a (fonctions internes seulement)")
        # print("[ESP-Video Build] ========================================")
    else:
        # print("[ESP-Video Build]  libesp_ipa.a introuvable!")
        pass
else:
    # print("[ESP-Video Build] Aucune source trouvée!")
    pass

# Message simple final
if sources_to_add:
    print("[ESP-Video Build] ========================================")
    print(f"[ESP-Video Build] {len(sources_to_add)} source files compiled")
    print("[ESP-Video Build] ISP Pipeline Controller: ENABLED")
    print("[ESP-Video Build] esp-video: ok")
    print("[ESP-Video Build] ========================================")
