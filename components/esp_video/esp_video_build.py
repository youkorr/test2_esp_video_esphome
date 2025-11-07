"""
Script de build PlatformIO pour ESP-Video
Ajoute tous les fichiers sources C/C++ des composants ESP-IDF
"""

import os
Import("env")

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

for src in esp_video_sources:
    src_path = os.path.join(component_dir, src)
    if os.path.exists(src_path):
        sources_to_add.append(src_path)
        print(f"[ESP-Video Build] + {src}")

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
    "sensor/sc202cs/private_include",
    "sensor/ov02c10/private_include",
]

if os.path.exists(esp_cam_sensor_dir):
    # Ajouter les chemins d'include
    for inc in esp_cam_sensor_includes:
        inc_path = os.path.join(esp_cam_sensor_dir, inc)
        if os.path.exists(inc_path):
            env.Append(CPPPATH=[inc_path])
            print(f"[ESP-Video Build] 📁 Include sensor ajouté: {inc_path}")

    # Compiler les sources
    for src in esp_cam_sensor_sources:
        src_path = os.path.join(esp_cam_sensor_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[ESP-Video Build] + esp_cam_sensor/{src}")

# ========================================================================
# Sources esp_h264
# ========================================================================
# NOTE: Les sources software (sw/src/*_sw*.c) nécessitent des bibliothèques
# externes (OpenH264, tinyh264) fournies dans sw/libs/openh264_inc et sw/libs/tinyh264_inc
# NOTE: esp_h264_alloc_less_than_5_3.c est exclu car nous utilisons ESP-IDF >= 5.3
esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
esp_h264_sources = [
    "port/src/esp_h264_alloc.c",
    # "port/src/esp_h264_alloc_less_than_5_3.c",  # Exclu: pour ESP-IDF < 5.3 seulement
    "port/src/esp_h264_cache.c",
    "sw/src/h264_color_convert.c",
    # Sources logicielles (nécessitent OpenH264 et h264bsd dans sw/libs/):
    "sw/src/esp_h264_enc_sw_param.c",      # Nécessite codec_api.h (OpenH264)
    "sw/src/esp_h264_dec_sw.c",            # Nécessite h264bsd_decoder.h
    "sw/src/esp_h264_enc_single_sw.c",     # Nécessite codec_api.h (OpenH264)
    "interface/include/src/esp_h264_enc_param.c",
    "interface/include/src/esp_h264_enc_param_hw.c",
    "interface/include/src/esp_h264_enc_dual.c",
    "interface/include/src/esp_h264_dec_param.c",
    "interface/include/src/esp_h264_version.c",
    "interface/include/src/esp_h264_dec.c",
    "interface/include/src/esp_h264_enc_single.c",
]

if os.path.exists(esp_h264_dir):
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
]

print("")
print("[ESP-Video Build] ========================================")
print("[ESP-Video Build] === COMPILATION ESP_IPA (CONFIG CUSTOM) ===")
print("[ESP-Video Build] ========================================")

if os.path.exists(esp_ipa_dir):
    for src in esp_ipa_sources:
        src_path = os.path.join(esp_ipa_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[ESP-Video Build] ✓ esp_ipa/{src} -> libesp_video_full.a")
    print("[ESP-Video Build]")
    print("[ESP-Video Build] Ces sources seront dans libesp_video_full.a")
    print("[ESP-Video Build] Le linker utilisera version.o custom (pas celui de libesp_ipa.a)")
    print("[ESP-Video Build] ========================================")
else:
    print("[ESP-Video Build] ⚠️  Répertoire esp_ipa introuvable!")

print("")

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
# Ajouter toutes les sources à la compilation
# ========================================================================
if sources_to_add:
    # Compiler chaque fichier source en objet
    objects = []
    for src_file in sources_to_add:
        # Compiler le fichier source en .o
        obj = env.Object(src_file)
        objects.extend(obj)

    # Créer une bibliothèque statique avec les objets compilés
    lib = env.StaticLibrary(
        os.path.join("$BUILD_DIR", "libesp_video_full"),
        objects
    )

    # Ajouter la bibliothèque au linkage (PREPEND = avant les autres libs)
    env.Prepend(LIBS=[lib])

    print(f"[ESP-Video Build] ✓ {len(sources_to_add)} fichiers sources ajoutés à la compilation")
    print(f"[ESP-Video Build] ✓ libesp_video_full.a créée avec tous les .o (y compris version.o custom)")

    # Maintenant linker avec libesp_ipa.a pour les fonctions IPA internes
    # Le linker utilisera notre version.o de libesp_video_full.a (déjà Prepend ci-dessus)
    # avant de chercher dans libesp_ipa.a
    esp_ipa_lib_dir = os.path.join(parent_components_dir, "esp_ipa", "lib/esp32p4")
    if os.path.exists(esp_ipa_lib_dir):
        env.Append(LIBPATH=[esp_ipa_lib_dir])
        env.Append(LIBS=["esp_ipa"])
        print("")
        print("[ESP-Video Build] ========================================")
        print("[ESP-Video Build] ✓ Linking avec libesp_ipa.a (fonctions IPA internes)")
        print("[ESP-Video Build]   Ordre de linking:")
        print("[ESP-Video Build]   1. libesp_video_full.a (version.o custom)")
        print("[ESP-Video Build]   2. libesp_ipa.a (fonctions internes seulement)")
        print("[ESP-Video Build] ========================================")
    else:
        print("[ESP-Video Build] ⚠️  libesp_ipa.a introuvable!")
else:
    print("[ESP-Video Build] ⚠️ Aucune source trouvée!")
