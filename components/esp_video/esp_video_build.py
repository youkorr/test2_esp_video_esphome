"""
esp_video_build.py — build script pour ESPHome (ESP-IDF)
Gère automatiquement :
 - les includes de esp_video et deps/include
 - la priorité deps/include
 - la détection de tab5_camera dans n’importe quel external_components/
"""

import os
import sys
from SCons.Script import Import

Import("env")

print("\n[ESP-Video] ⚙ Initialisation du build script")

# ===============================================================
# Vérification du framework
# ===============================================================
framework = env.get("PIOFRAMEWORK", [])
if "espidf" not in framework:
    print("[ESP-Video] ❌ ESP-IDF requis (Arduino non supporté)")
    sys.exit(1)

# ===============================================================
# Localiser le composant esp_video
# ===============================================================
def find_component_root():
    try:
        return os.path.dirname(os.path.abspath(__file__))
    except NameError:
        pass

    for root, dirs, _ in os.walk("/data/external_components"):
        if "esp_video" in dirs:
            path = os.path.join(root, "esp_video")
            if os.path.exists(os.path.join(path, "include")):
                return path
    return os.getcwd()

component_dir = find_component_root()
print(f"[ESP-Video] 📂 Composant détecté : {component_dir}")

# ===============================================================
# Vérifier deps/include et les stubs requis
# ===============================================================
deps_dir = os.path.join(component_dir, "deps", "include")
os.makedirs(deps_dir, exist_ok=True)

print(f"[ESP-Video] 🔧 Vérification des stubs dans : {deps_dir}")

required_stubs = [
    "esp_cam_sensor.h",
    "esp_cam_sensor_xclk.h",
    "esp_sccb_i2c.h",
    "esp_cam_motor_types.h",
    "esp_cam_sensor_types.h",
]

missing = []
for stub in required_stubs:
    path = os.path.join(deps_dir, stub)
    if os.path.exists(path):
        print(f"[ESP-Video]   ✓ {stub}")
    else:
        print(f"[ESP-Video]   ❌ {stub} MANQUANT")
        missing.append(stub)

if missing:
    print(f"[ESP-Video] ⚠️ Erreur : stubs manquants ({', '.join(missing)})")
    sys.exit(1)

# ===============================================================
# Ajouter deps/include en priorité
# ===============================================================
env.Prepend(CPPPATH=[deps_dir])
print(f"[ESP-Video] ➕ Include deps ajouté EN PRIORITÉ : {deps_dir}")

# ===============================================================
# Ajouter includes du composant esp_video
# ===============================================================
def add_include(path):
    if os.path.exists(path):
        env.Append(CPPPATH=[path])
        print(f"[ESP-Video] ➕ Include : {path}")

include_paths = [
    os.path.join(component_dir, "include"),
    os.path.join(component_dir, "include", "linux"),
    os.path.join(component_dir, "include", "sys"),
    os.path.join(component_dir, "private_include"),
    os.path.join(component_dir, "src"),
    os.path.join(component_dir, "src", "device"),
]

for p in include_paths:
    add_include(p)

# ===============================================================
# Recherche automatique de tab5_camera dans external_components
# ===============================================================
def find_tab5_camera_dir():
    # 1️⃣ chemin standard du projet
    project_dir = env.subst("$PROJECT_DIR")
    default_path = os.path.join(project_dir, "src/esphome/components/tab5_camera")
    if os.path.exists(default_path):
        return default_path

    # 2️⃣ scanner tous les external_components
    for root, dirs, _ in os.walk("/data/external_components"):
        if "tab5_camera" in dirs:
            return os.path.join(root, "tab5_camera")

    # 3️⃣ fallback build dir (rare)
    build_path = "/data/build/tab5/src/esphome/components/tab5_camera"
    if os.path.exists(build_path):
        return build_path

    return None

tab5_camera_dir = find_tab5_camera_dir()
if tab5_camera_dir:
    env.Append(CPPPATH=[tab5_camera_dir])
    print(f"[ESP-Video] 🎯 tab5_camera trouvé : {tab5_camera_dir}")
else:
    print("[ESP-Video] ⚠️ Aucun dossier tab5_camera trouvé dans les externals")

# ===============================================================
# Flags de compilation
# ===============================================================
flags = [
    "CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1",
    "CONFIG_ESP_VIDEO_ENABLE_ISP=1",
    "CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1",
    "CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1",
    "CONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1",
]
env.Append(CPPDEFINES=flags)

# ===============================================================
# Fin du script
# ===============================================================
print("[ESP-Video] ✅ Configuration terminée")
print(f"[ESP-Video] 📋 Priorité CPPPATH : {env['CPPPATH'][:3]}\n")








