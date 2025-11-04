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

# Définition du schéma de configuration
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ESPVideoComponent),
    cv.Optional(CONF_ENABLE_H264, default=True): cv.boolean,
    cv.Optional(CONF_ENABLE_JPEG, default=True): cv.boolean,
    cv.Optional(CONF_ENABLE_ISP, default=True): cv.boolean,
    cv.Optional(CONF_USE_HEAP_ALLOCATOR, default=True): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

# Validation personnalisée pour s'assurer qu'au moins un encodeur est activé
def validate_esp_video_config(config):
    """Valide la configuration ESP-Video"""
    # Au moins un encodeur doit être activé
    if not config[CONF_ENABLE_H264] and not config[CONF_ENABLE_JPEG]:
        raise cv.Invalid("Au moins un encodeur (H264 ou JPEG) doit être activé")
    
    return config

# Appliquer la validation avant de traiter le schéma
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ESPVideoComponent),
    cv.Optional(CONF_ENABLE_H264, default=True): cv.boolean,
    cv.Optional(CONF_ENABLE_JPEG, default=True): cv.boolean,
    cv.Optional(CONF_ENABLE_ISP, default=True): cv.boolean,
    cv.Optional(CONF_USE_HEAP_ALLOCATOR, default=True): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

# Appliquer directement la validation du schéma
CONFIG_SCHEMA = CONFIG_SCHEMA.extend(validate_esp_video_config)

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
    # Détection du chemin du composant
    # -----------------------------------------------------------------------
    component_dir = os.path.dirname(os.path.abspath(__file__))
    cg.add(cg.RawExpression(f'// [ESP-Video] Component dir: {component_dir}'))
    
    # Log pour debug
    import logging
    logging.info(f"[ESP-Video] Répertoire du composant: {component_dir}")

    # -----------------------------------------------------------------------
    # Ajout des includes (ordre prioritaire)
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
            import logging
            logging.info(f"[ESP-Video] 📁 Include ajouté: {abs_path}")
        else:
            import logging
            logging.debug(f"[ESP-Video] ⚠️ Répertoire non trouvé (ignoré): {abs_path}")

    if not includes_found:
        import logging
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

    # Encodeur H.264
    if config[CONF_ENABLE_H264]:
        flags.append("-DCONFIG_ESP_VIDEO_ENABLE_HW_H264_VIDEO_DEVICE=1")
        cg.add_define("ESP_VIDEO_H264_ENABLED", "1")

    # Encodeur JPEG
    if config[CONF_ENABLE_JPEG]:
        flags.append("-DCONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE=1")
        cg.add_define("ESP_VIDEO_JPEG_ENABLED", "1")

    # Appliquer tous les flags
    for flag in flags:
        cg.add_build_flag(flag)

    import logging
    logging.info(f"[ESP-Video] {len(flags)} flags de compilation ajoutés")

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
    # Script post-compilation (optionnel)
    # -----------------------------------------------------------------------
    build_script_path = os.path.join(component_dir, "esp_video_build.py")
    if os.path.exists(build_script_path):
        cg.add_platformio_option("extra_scripts", [f"post:{build_script_path}"])
        cg.add(cg.RawExpression('// [ESP-Video] Script de build personnalisé activé'))
        import logging
        logging.info(f"[ESP-Video] Script de build trouvé: {build_script_path}")
    else:
        import logging
        logging.debug(
            f"[ESP-Video] Aucun esp_video_build.py trouvé dans {component_dir} "
            "(optionnel, pas d'erreur)"
        )

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
    
    import logging
    logging.info(f"[ESP-Video] ✅ Configuration terminée: {config_summary}")
















