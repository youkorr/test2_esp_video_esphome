"""
esp_video_build.py — version multi-external stable
Gère l’exécution depuis /data/build/... et détecte automatiquement
le vrai composant esp_video dans /data/external_components.
"""

import os
import sys
from SCons.Script import Import

Import("env")

print("\n[ESP-Video] ⚙ Initialisation du build script")

# ===============================================================
# Vérification framework
# ===============================================================
framework = env.get("PIOFRAMEWORK", [])
if "espidf" not in framework:
    print("[ESP-Video] ❌ Ce composant nécessite ESP-IDF (pas Arduino)")
    sys.exit(1)

# ===============================================================
# Recherche robuste du vrai dossier du composant esp_video
# ===============================================================
def find_esp_video_dir():
    search_roots = [
        os.getcwd(),
        "/data/external_components",
        "/data/data/external_components",
    ]
    for base in search_roots:
        for root, dirs, _ in os.walk(base):
            if "esp_video" in dirs:
                candidate = os.path.join(root, "esp_video")
                if os.path.exists(os.path.join(candidate, "deps", "include")):
                    print(f"[ESP-Video] 🔍 Composant trouvé : {candidate}")
                    return candidate
    print("[ESP-Video] ❌ Composant esp_video introuvable !")
    sys.exit(1)

component_dir = find_esp_video_dir()

# ===============================================================
# Vérification deps/include
# ===============================================================
deps_dir = os.path.join(component_dir, "deps", "include")
print(f"[ESP-Video] 🔧 Vérification des stubs dans : {deps_dir}")
os.makedirs(deps_dir, exist_ok=True)

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
    print(f"[ESP-Video] ⚠️ Erreur : fichiers manquants ({', '.join(missing)})")
    sys.exit(1)

# ===============================================================
# Ajout des includes
# ===============================================================
env.Prepend(CPPPATH=[deps_dir])
print(f"[ESP-Video] ➕ Include deps ajouté EN PRIORITÉ : {deps_dir}")

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
# Recherche du composant tab5_camera
# ===============================================================
def find_tab5_camera_dir():
    for base in ["/data/external_components", "/data/data/external_components"]:
        for root, dirs, _ in os.walk(base):
            if "tab5_camera" in dirs:
                return os.path.join(root, "tab5_camera")
    return None

tab5_dir = find_tab5_camera_dir()
if tab5_dir:
    env.Append(CPPPATH=[tab5_dir])
    print(f"[ESP-Video] 🎯 tab5_camera trouvé : {tab5_dir}")
else:
    print("[ESP-Video] ⚠️ Aucun tab5_camera trouvé")

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
# Fin
# ===============================================================
print("[ESP-Video] ✅ Configuration terminée")
print(f"[ESP-Video] 📋 CPPPATH (top3): {env['CPPPATH'][:3]}\n")










