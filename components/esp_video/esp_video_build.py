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
    "src/driver_spi/spi_slave.c",
    "src/driver_cam/esp_cam_ctlr_spi_cam.c",
    "sensor/ov5647/ov5647.c",
    "sensor/sc202cs/sc202cs.c",
]

if os.path.exists(esp_cam_sensor_dir):
    for src in esp_cam_sensor_sources:
        src_path = os.path.join(esp_cam_sensor_dir, src)
        if os.path.exists(src_path):
            sources_to_add.append(src_path)
            print(f"[ESP-Video Build] + esp_cam_sensor/{src}")

# ========================================================================
# Sources esp_h264
# ========================================================================
# NOTE: Les sources software (sw/src/*_sw*.c) nécessitent des bibliothèques
# externes (OpenH264, h264bsd) qui ne sont pas incluses. Ces sources sont
# donc exclues de la compilation. Seul le support matériel (hardware) est activé.
esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
esp_h264_sources = [
    "port/src/esp_h264_alloc.c",
    "port/src/esp_h264_alloc_less_than_5_3.c",
    "port/src/esp_h264_cache.c",
    "sw/src/h264_color_convert.c",
    # Sources logicielles exclues (dépendances externes manquantes):
    # "sw/src/esp_h264_enc_sw_param.c",      # Nécessite codec_api.h (OpenH264)
    # "sw/src/esp_h264_dec_sw.c",            # Nécessite h264bsd_decoder.h
    # "sw/src/esp_h264_enc_single_sw.c",     # Nécessite codec_api.h (OpenH264)
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
# Sources esp_ipa
# ========================================================================
esp_ipa_dir = os.path.join(parent_components_dir, "esp_ipa")
esp_ipa_src = os.path.join(esp_ipa_dir, "src/version.c")
if os.path.exists(esp_ipa_src):
    sources_to_add.append(esp_ipa_src)
    print(f"[ESP-Video Build] + esp_ipa/src/version.c")

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

    # Ajouter les objets compilés au projet
    env.Append(PIOBUILDFILES=objects)

    print(f"[ESP-Video Build] ✓ {len(sources_to_add)} fichiers sources ajoutés à la compilation")
else:
    print("[ESP-Video Build] ⚠️ Aucune source trouvée!")
