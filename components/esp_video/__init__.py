"""
Composant ESPHome pour ESP-Video d'Espressif (v1.3.1)
Support complet H264 + JPEG avec dépendances ESP-IDF

Ce composant initialise ESP-Video en utilisant le bus I2C d'ESPHome.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID, CONF_I2C_ID
from esphome.core import CORE
import os
import logging

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32", "i2c"]  # i2c ajouté car nous utilisons le bus I2C ESPHome
AUTO_LOAD = []

esp_video_ns = cg.esphome_ns.namespace("esp_video")
ESPVideoComponent = esp_video_ns.class_("ESPVideoComponent", cg.Component)

# Configuration
CONF_ENABLE_H264 = "enable_h264"
CONF_ENABLE_JPEG = "enable_jpeg"
CONF_ENABLE_ISP = "enable_isp"
CONF_USE_HEAP_ALLOCATOR = "use_heap_allocator"

def validate_esp_video_config(config):
    """Valide la configuration ESP-Video"""
    # Au moins un encodeur doit être activé
    if not config[CONF_ENABLE_H264] and not config[CONF_ENABLE_JPEG]:
        raise cv.Invalid("Au moins un encodeur (H264 ou JPEG) doit être activé")

    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(ESPVideoComponent),
        cv.Required(CONF_I2C_ID): cv.use_id(i2c.I2CBus),
        cv.Optional(CONF_ENABLE_H264, default=True): cv.boolean,
        cv.Optional(CONF_ENABLE_JPEG, default=True): cv.boolean,
        cv.Optional(CONF_ENABLE_ISP, default=True): cv.boolean,
        cv.Optional(CONF_USE_HEAP_ALLOCATOR, default=True): cv.boolean,
    }).extend(cv.COMPONENT_SCHEMA),
    validate_esp_video_config
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Récupérer le bus I2C d'ESPHome
    i2c_bus = await cg.get_variable(config[CONF_I2C_ID])
    cg.add(var.set_i2c_bus(i2c_bus))

    logging.info(f"[ESP-Video] Configuration I2C: Utilise le bus ESPHome '{config[CONF_I2C_ID]}'")
    logging.info("[ESP-Video] esp_video utilisera le bus I2C ESPHome (init_sccb=false)")
    logging.info("[ESP-Video] Avantage: Partage propre du bus I2C, pas de conflit")

    # -----------------------------------------------------------------------
    # Vérification du framework
    # -----------------------------------------------------------------------
    if not CORE.using_esp_idf:
        raise cv.Invalid(
            "ESP-Video nécessite le framework esp-idf. "
            "Ajoutez 'framework: type: esp-idf' dans votre configuration."
        )

    # -----------------------------------------------------------------------
    # Chemins des composants ESP-IDF
    # -----------------------------------------------------------------------
    # Chemin du composant esp_video
    component_dir = os.path.dirname(__file__)
    parent_components_dir = os.path.dirname(component_dir)

    logging.info(f"[ESP-Video] Répertoire composant: {component_dir}")
    logging.info(f"[ESP-Video] Répertoire parent: {parent_components_dir}")

    # -----------------------------------------------------------------------
    # Ajout des répertoires include
    # -----------------------------------------------------------------------
    includes_found = False

    # esp_video
    esp_video_includes = ["include", "private_include", "src"]
    for inc in esp_video_includes:
        inc_path = os.path.join(component_dir, inc)
        if os.path.exists(inc_path):
            cg.add_build_flag(f"-I{inc_path}")
            logging.info(f"[ESP-Video] 📁 Include ajouté: {inc_path}")
            includes_found = True

    # esp_cam_sensor
    esp_cam_sensor_dir = os.path.join(parent_components_dir, "esp_cam_sensor")
    if os.path.exists(esp_cam_sensor_dir):
        for inc in ["include", "sensor/ov5647/include", "sensor/sc202cs/include", "sensor/ov02c10/include", "src", "src/driver_spi", "src/driver_cam"]:
            inc_path = os.path.join(esp_cam_sensor_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")
                logging.info(f"[ESP-Video] 📁 Include esp_cam_sensor ajouté: {inc_path}")
                includes_found = True

    # esp_h264
    esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
    if os.path.exists(esp_h264_dir):
        for inc in ["interface/include", "port/include", "port/inc", "sw/include", "hw/include", "sw/libs/openh264_inc", "sw/libs/tinyh264_inc"]:
            inc_path = os.path.join(esp_h264_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")
                logging.info(f"[ESP-Video] 📁 Include esp_h264 ajouté: {inc_path}")
                includes_found = True

    # esp_ipa
    esp_ipa_dir = os.path.join(parent_components_dir, "esp_ipa")
    if os.path.exists(esp_ipa_dir):
        for inc in ["include", "src"]:
            inc_path = os.path.join(esp_ipa_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")
                logging.info(f"[ESP-Video] 📁 Include esp_ipa ajouté: {inc_path}")
                includes_found = True

    # esp_sccb_intf
    esp_sccb_intf_dir = os.path.join(parent_components_dir, "esp_sccb_intf")
    if os.path.exists(esp_sccb_intf_dir):
        for inc in ["include", "interface", "sccb_i2c/include"]:
            inc_path = os.path.join(esp_sccb_intf_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")
                logging.info(f"[ESP-Video] 📁 Include esp_sccb_intf ajouté: {inc_path}")
                includes_found = True

    if not includes_found:
        logging.warning(
            "[ESP-Video] ⚠️ Aucun répertoire d'include trouvé! "
            "Vérifiez la structure du composant ESP-Video."
        )

    # -----------------------------------------------------------------------
    # FLAGS ESP-Video selon la configuration
    # -----------------------------------------------------------------------
    flags = []

    # Flags de base (toujours activés)
    flags.extend([
        "-DCONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1",
        "-DCONFIG_IDF_TARGET_ESP32P4=1",
        "-DCONFIG_SOC_I2C_SUPPORTED=1",
    ])

    # Capteurs de caméra - configurations pour TOUS les capteurs supportés
    # L'auto-détection essaiera tous les capteurs et utilisera celui détecté

    # SC202CS
    flags.extend([
        "-DCONFIG_CAMERA_SC202CS=1",
        "-DCONFIG_CAMERA_SC202CS_AUTO_DETECT=1",
        "-DCONFIG_CAMERA_SC202CS_ABSOLUTE_GAIN_LIMIT=16000",  # 16x max
        "-DCONFIG_CAMERA_SC202CS_ANA_GAIN_PRIORITY=1",
        "-DCONFIG_CAMERA_SC202CS_DIG_GAIN_PRIORITY=0",
    ])

    # OV5647
    flags.extend([
        "-DCONFIG_CAMERA_OV5647=1",
        "-DCONFIG_CAMERA_OV5647_AUTO_DETECT=1",
        "-DCONFIG_CAMERA_OV5647_CSI_LINESYNC_ENABLE=0",
        "-DCONFIG_CAMERA_OV5647_MIPI_IF_FORMAT_INDEX_DEFAULT=0",
    ])

    # OV02C10
    flags.extend([
        "-DCONFIG_CAMERA_OV02C10=1",
        "-DCONFIG_CAMERA_OV02C10_AUTO_DETECT=1",
        "-DCONFIG_CAMERA_OV02C10_ABSOLUTE_GAIN_LIMIT=16000",  # 16x max
        "-DCONFIG_CAMERA_OV02C10_ANA_GAIN_PRIORITY=1",         # Analog gain priority
        "-DCONFIG_CAMERA_OV02C10_DIG_GAIN_PRIORITY=0",
        "-DCONFIG_CAMERA_OV02C10_CSI_LINESYNC_ENABLE=0",
        "-DCONFIG_CAMERA_OV02C10_MIPI_IF_FORMAT_INDEX_DEFAULT=0",
        "-DCONFIG_CAMERA_OV02C10_MAX_SUPPORT=1",
    ])

    # ISP (Image Signal Processor)
    if config[CONF_ENABLE_ISP]:
        flags.extend([
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1",
            "-DESP_VIDEO_ISP_ENABLED=1",  # Pour esp_video_component.cpp
        ])

    # Allocateur mémoire
    if config[CONF_USE_HEAP_ALLOCATOR]:
        flags.append("-DCONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1")

    # Encodeur H.264
    if config[CONF_ENABLE_H264]:
        flags.extend([
            "-DCONFIG_ESP_VIDEO_ENABLE_H264_VIDEO_DEVICE=1",
            "-DESP_VIDEO_H264_ENABLED=1",  # Pour esp_video_component.cpp
        ])

    # Encodeur JPEG
    if config[CONF_ENABLE_JPEG]:
        flags.extend([
            "-DCONFIG_ESP_VIDEO_ENABLE_JPEG_VIDEO_DEVICE=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE=1",
            "-DESP_VIDEO_JPEG_ENABLED=1",  # Pour esp_video_component.cpp
        ])

    # Appliquer tous les flags
    for flag in flags:
        cg.add_build_flag(flag)

    logging.info(f"[ESP-Video] {len(flags)} flags de compilation ajoutés")

    # -----------------------------------------------------------------------
    # Compilation des sources via script PlatformIO
    # -----------------------------------------------------------------------
    # Les sources C/C++ de tous les composants (esp_video, esp_cam_sensor,
    # esp_h264, esp_ipa, esp_sccb_intf) sont compilées via le script
    # esp_video_build.py qui est exécuté pendant la phase de build PlatformIO.
    #
    # NOTE: esp_ipa est compilé dans esp_video_build.py avec ordre garanti:
    # 1. Compile version.c (config custom: 5 IPAs, no AGC)
    # 2. Compile esp_ipa_detect_stubs.c
    # 3. Ajoute tout à libesp_video_full.a
    # 4. Link avec libesp_ipa.a (fonctions IPA internes)
    # Le linker utilise version.o de libesp_video_full.a (pas celui de libesp_ipa.a)

    # -----------------------------------------------------------------------
    # Flags de compilation supplémentaires pour la compatibilité
    # -----------------------------------------------------------------------
    extra_flags = [
        "-Wno-unused-function",
        "-Wno-unused-variable",
        "-Wno-missing-field-initializers",
    ]

    for flag in extra_flags:
        cg.add_build_flag(flag)

    # -----------------------------------------------------------------------
    # Script de build PlatformIO (obligatoire)
    # -----------------------------------------------------------------------
    build_script_path = os.path.join(component_dir, "esp_video_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
        logging.info(f"[ESP-Video] Script de build ajouté: {build_script_path}")
    else:
        raise cv.Invalid(f"Script de build introuvable: {build_script_path}")

    logging.info("[ESP-Video] ✅ Configuration terminée")
