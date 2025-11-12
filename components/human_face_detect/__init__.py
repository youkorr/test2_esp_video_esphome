"""
ESPHome component for Human Face Detection using ESP-DL
Based on Waveshare ESP32-P4 implementation

Optional component - requires esp-dl library from Espressif
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_MODEL,
    UNIT_EMPTY,
    ICON_ACCOUNT,
)
from esphome.core import CORE
import os

CODEOWNERS = ["@youkorr"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = []

# Nécessite ESP32-P4 pour esp-dl
CONF_CAMERA = "camera"
CONF_ENABLE_DETECTION = "enable_detection"
CONF_CONFIDENCE_THRESHOLD = "confidence_threshold"
CONF_MODEL_TYPE = "model_type"
CONF_MODEL_DIR = "model_dir"
CONF_MSR_MODEL_FILE = "msr_model_file"
CONF_MNP_MODEL_FILE = "mnp_model_file"

human_face_detect_ns = cg.esphome_ns.namespace("human_face_detect")
HumanFaceDetectComponent = human_face_detect_ns.class_("HumanFaceDetectComponent", cg.Component)

# Types de modèles disponibles
MODEL_TYPES = {
    "MSRMNP_S8_V1": 0,  # Model MSR+MNP version 1 (quantized s8)
}

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HumanFaceDetectComponent),
    cv.Required(CONF_CAMERA): cv.use_id(cg.Component),  # Référence vers mipi_dsi_cam
    cv.Optional(CONF_ENABLE_DETECTION, default=False): cv.boolean,
    cv.Optional(CONF_CONFIDENCE_THRESHOLD, default=0.5): cv.float_range(min=0.0, max=1.0),
    cv.Optional(CONF_MODEL_TYPE, default="MSRMNP_S8_V1"): cv.enum(MODEL_TYPES),
    cv.Optional(CONF_MODEL_DIR, default="/sdcard"): cv.string,
    cv.Optional(CONF_MSR_MODEL_FILE, default="human_face_detect_msr_s8_v1.espdl"): cv.string,
    cv.Optional(CONF_MNP_MODEL_FILE, default="human_face_detect_mnp_s8_v1.espdl"): cv.string,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Lier avec la caméra
    cam = await cg.get_variable(config[CONF_CAMERA])
    cg.add(var.set_camera(cam))

    # Configuration
    cg.add(var.set_enable_detection(config[CONF_ENABLE_DETECTION]))
    cg.add(var.set_confidence_threshold(config[CONF_CONFIDENCE_THRESHOLD]))
    cg.add(var.set_model_type(config[CONF_MODEL_TYPE]))

    # Model paths configuration
    cg.add(var.set_model_dir(config[CONF_MODEL_DIR]))
    cg.add(var.set_msr_model_filename(config[CONF_MSR_MODEL_FILE]))
    cg.add(var.set_mnp_model_filename(config[CONF_MNP_MODEL_FILE]))

    # -----------------------------------------------------------------------
    # Vérification du framework
    # -----------------------------------------------------------------------
    if not CORE.using_esp_idf:
        raise cv.Invalid(
            "human_face_detect nécessite le framework esp-idf. "
            "Ajoutez 'framework: type: esp-idf' dans votre configuration."
        )

    # -----------------------------------------------------------------------
    # Chemins des composants
    # -----------------------------------------------------------------------
    component_dir = os.path.dirname(__file__)

    # Ajouter les includes
    cg.add_build_flag(f"-I{component_dir}")

    # -----------------------------------------------------------------------
    # FLAGS pour ESP-DL
    # -----------------------------------------------------------------------
    flags = [
        "-DCONFIG_ESP_DL_ENABLE=1",
        "-DCONFIG_ESP_FACE_DETECT_ENABLED=1",
    ]

    # Appliquer tous les flags
    for flag in flags:
        cg.add_build_flag(flag)

    # Note importante pour l'utilisateur
    if config[CONF_ENABLE_DETECTION]:
        cg.add_library("esp-dl", None)  # Ajouter dépendance esp-dl
