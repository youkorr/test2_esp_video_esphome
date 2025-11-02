"""
esp_video_build.py — Script avec deps/include en PRIORITÉ
"""
import os
import sys
from SCons.Script import Import

Import("env")

print("\n[ESP-Video] ⚙ Initialisation du build script")

framework = env.get("PIOFRAMEWORK", [])
if "espidf" not in framework:
    print("[ESP-Video] ❌ ESP-IDF requis")
    sys.exit(1)

def find_component_root():
    try:
        current_dir = os.path.dirname(os.path.abspath(__file__))
        return current_dir
    except NameError:
        pass
    
    for root, dirs, _ in os.walk("/data/external_components"):
        if "esp_video" in dirs:
            path = os.path.join(root, "esp_video")
            if os.path.exists(os.path.join(path, "include")):
                return path
    return os.getcwd()

component_dir = find_component_root()
print(f"[ESP-Video] 📂 Composant: {component_dir}")

# ===============================================================
# CRÉER ET AJOUTER deps/include EN PREMIER (PRIORITÉ MAXIMALE)
# ===============================================================
deps_dir = os.path.join(component_dir, "deps", "include")

print(f"[ESP-Video] 🔧 Vérification des stubs dans: {deps_dir}")

# Créer le dossier si nécessaire
os.makedirs(deps_dir, exist_ok=True)

# Vérifier que les 3 fichiers existent
required_stubs = [
    "esp_cam_sensor.h",
    "esp_cam_sensor_xclk.h", 
    "esp_sccb_i2c.h",
    "esp_cam_motor_types.h",
    "esp_cam_sensor_types.h",
]

all_present = True
for stub in required_stubs:
    stub_path = os.path.join(deps_dir, stub)
    if os.path.exists(stub_path):
        print(f"[ESP-Video]   ✓ {stub}")
    else:
        print(f"[ESP-Video]   ❌ {stub} MANQUANT!")
        all_present = False

if not all_present:
    print("[ESP-Video] ⚠️ ERREUR: Stubs manquants! Vérifiez votre push GitHub.")
    sys.exit(1)

# AJOUTER deps/include EN PREMIER (avec Prepend au lieu de Append)
env.Prepend(CPPPATH=[deps_dir])
print(f"[ESP-Video] ➕ Include deps ajouté EN PRIORITÉ: {deps_dir}")

# ===============================================================
# Includes esp_video (après deps)
# ===============================================================
def add_include(path):
    if os.path.exists(path):
        env.Append(CPPPATH=[path])
        print(f"[ESP-Video] ➕ Include: {os.path.basename(path)}")

add_include(os.path.join(component_dir, "include"))
add_include(os.path.join(component_dir, "include", "linux"))
add_include(os.path.join(component_dir, "include", "sys"))
add_include(os.path.join(component_dir, "src"))
add_include(os.path.join(component_dir, "src", "device"))
add_include(os.path.join(component_dir, "private_include"))

# ===============================================================
# Tab5 camera
# ===============================================================
project_dir = env.subst("$PROJECT_DIR")
for path in [
    os.path.join(project_dir, "src/esphome/components/tab5_camera"),
    "/data/build/tab5/src/esphome/components/tab5_camera"
]:
    if os.path.exists(path):
        env.Append(CPPPATH=[path])
        print(f"[ESP-Video] 🎯 tab5_camera: {path}")
        break

# ===============================================================
# Flags de compilation
# ===============================================================
for flag in [
    "CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1",
    "CONFIG_ESP_VIDEO_ENABLE_ISP=1",
    "CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1",
    "CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1",
    "CONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1",
]:
    env.Append(CPPDEFINES=[flag.replace("-D", "")])

print("[ESP-Video] ✅ Configuration terminée")
print(f"[ESP-Video] 📋 CPPPATH priorité: {env['CPPPATH'][:3]}\n")







