"""
Composant ESPHome pour ESP-Video d'Espressif (v1.3.1)
Support complet H264 + JPEG avec dépendances ESP-IDF
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE
import os

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = []

esp_video_ns = cg.esphome_ns.namespace("esp_video")
ESPVideoComponent = esp_video_ns.class_("ESPVideoComponent", cg.Component)

# Configuration optionnelle pour personnalisation
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

    # -----------------------------------------------------------------------
    # Vérification du framework
    # -----------------------------------------------------------------------
    if CORE.using_arduino:
        raise cv.Invalid(
            "esp_video nécessite 'framework: type: esp-idf'\n"
            "Ajoutez dans votre YAML:\n"
            "esp32:\n"
            "  framework:\n"
            "    type: esp-idf\n"
            "    version: 5.4.0"
        )

    # Vérifier la plateforme ESP32
    if CORE.data.get("esp32", {}).get("variant") not in [None, "esp32p4"]:
        import logging
        logging.warning(
            "[ESP-Video] Ce composant est optimisé pour ESP32-P4. "
            "Vérifiez la compatibilité avec votre variante ESP32."
        )

    # -----------------------------------------------------------------------
    # Détection du chemin du composant esp_video
    # -----------------------------------------------------------------------
    component_dir = os.path.dirname(os.path.abspath(__file__))

    import logging
    logging.info(f"[ESP-Video] Répertoire du composant: {component_dir}")

    # -----------------------------------------------------------------------
    # Détection des composants dépendants
    # -----------------------------------------------------------------------
    parent_components_dir = os.path.dirname(component_dir)

    # -----------------------------------------------------------------------
    # Ajout des includes ESP-Video
    # -----------------------------------------------------------------------
    include_dirs = [
        "include",
        "include/linux",
        "include/sys",
        "src",
        "src/device",
        "private_include",
    ]

    includes_found = []
    for subdir in include_dirs:
        abs_path = os.path.join(component_dir, subdir)
        if os.path.exists(abs_path) and os.path.isdir(abs_path):
            cg.add_build_flag(f"-I{abs_path}")
            includes_found.append(abs_path)
            logging.info(f"[ESP-Video] 📁 Include ajouté: {abs_path}")

    # -----------------------------------------------------------------------
    # Ajout des includes des composants ESP-IDF dépendants
    # -----------------------------------------------------------------------
    # esp_cam_sensor
    esp_cam_sensor_dir = os.path.join(parent_components_dir, "esp_cam_sensor")
    if os.path.exists(esp_cam_sensor_dir):
        for inc in ["include", "sensor/ov5647/include", "sensor/sc202cs/include",
                    "src", "src/driver_spi", "src/driver_cam"]:
            inc_path = os.path.join(esp_cam_sensor_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")
                logging.info(f"[ESP-Video] 📁 Include esp_cam_sensor ajouté: {inc_path}")

    # esp_h264
    esp_h264_dir = os.path.join(parent_components_dir, "esp_h264")
    if os.path.exists(esp_h264_dir):
        for inc in ["interface/include", "port/include", "sw/include"]:
            inc_path = os.path.join(esp_h264_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")
                logging.info(f"[ESP-Video] 📁 Include esp_h264 ajouté: {inc_path}")

    # esp_ipa
    esp_ipa_dir = os.path.join(parent_components_dir, "esp_ipa")
    if os.path.exists(esp_ipa_dir):
        inc_path = os.path.join(esp_ipa_dir, "include")
        if os.path.exists(inc_path):
            cg.add_build_flag(f"-I{inc_path}")
            logging.info(f"[ESP-Video] 📁 Include esp_ipa ajouté: {inc_path}")

    # esp_sccb_intf
    esp_sccb_intf_dir = os.path.join(parent_components_dir, "esp_sccb_intf")
    if os.path.exists(esp_sccb_intf_dir):
        for inc in ["include", "interface", "sccb_i2c/include"]:
            inc_path = os.path.join(esp_sccb_intf_dir, inc)
            if os.path.exists(inc_path):
                cg.add_build_flag(f"-I{inc_path}")
                logging.info(f"[ESP-Video] 📁 Include esp_sccb_intf ajouté: {inc_path}")

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

    # Capteur de caméra SC202CS (activé par défaut pour mipi_dsi_cam)
    flags.extend([
        "-DCONFIG_CAMERA_SC202CS=1",
        "-DCONFIG_CAMERA_SC202CS_AUTO_DETECT=1",
    ])

    # ISP (Image Signal Processor)
    if config[CONF_ENABLE_ISP]:
        flags.extend([
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1",
        ])
        cg.add_define("ESP_VIDEO_ISP_ENABLED", "1")

    # Allocateur mémoire
    if config[CONF_USE_HEAP_ALLOCATOR]:
        flags.append("-DCONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1")

    # Encodeur H.264 (CORRIGÉ: utilise H264_VIDEO_DEVICE, pas HW_H264_VIDEO_DEVICE)
    if config[CONF_ENABLE_H264]:
        flags.append("-DCONFIG_ESP_VIDEO_ENABLE_H264_VIDEO_DEVICE=1")
        cg.add_define("ESP_VIDEO_H264_ENABLED", "1")

    # Encodeur JPEG (CORRIGÉ: utilise JPEG_VIDEO_DEVICE, pas HW_JPEG)
    if config[CONF_ENABLE_JPEG]:
        flags.extend([
            "-DCONFIG_ESP_VIDEO_ENABLE_JPEG_VIDEO_DEVICE=1",
            "-DCONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE=1",  # Pour esp_driver_jpeg
        ])
        cg.add_define("ESP_VIDEO_JPEG_ENABLED", "1")

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

    # Ajouter la bibliothèque précompilée esp_ipa
    variant = CORE.data.get("esp32", {}).get("variant", "esp32p4")
    if not variant:
        variant = "esp32p4"

    lib_path = os.path.join(esp_ipa_dir, f"lib/{variant}")
    if os.path.exists(lib_path):
        cg.add_build_flag(f"-L{lib_path}")
        cg.add_build_flag("-lesp_ipa")
        logging.info(f"[ESP-Video] Bibliothèque précompilée esp_ipa ajoutée pour {variant}")

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
        cg.add_platformio_option("extra_scripts", ["pre:" + build_script_path])
        logging.info(f"[ESP-Video] ✓ Script de build activé: {build_script_path}")
    else:
        logging.error(f"[ESP-Video] ❌ Script de build manquant: {build_script_path}")

    # -----------------------------------------------------------------------
    # Définitions globales
    # -----------------------------------------------------------------------
    cg.add_define("ESP_VIDEO_VERSION", '"1.3.1"')
    
    # Résumé de la configuration
    encoders = []
    if config[CONF_ENABLE_H264]:
        encoders.append("H264")
    if config[CONF_ENABLE_JPEG]:
        encoders.append("JPEG")
    
    config_summary = f"ESP-Video v1.3.1 (Encodeurs: {', '.join(encoders)})"
    if config[CONF_ENABLE_ISP]:
        config_summary += " + ISP"
    
    cg.add(cg.RawExpression(f'// [ESP-Video] {config_summary}'))
    
    logging.info(f"[ESP-Video] ✅ Configuration terminée: {config_summary}")

















