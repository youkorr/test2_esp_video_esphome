"""
esp_video_build.py — Vérifie la config (sera appelé par PlatformIO, pas ESPHome)
"""
import os
import sys

# ===============================================================
# Ce script est appelé par PlatformIO, pas par ESPHome
# Il ne doit PAS être importé dans __init__.py
# ===============================================================

def main():
    """Fonction principale appelée par PlatformIO"""
    try:
        from SCons.Script import Import
        Import("env")
    except:
        # Si appelé hors contexte PlatformIO, ne rien faire
        print("[ESP-Video] Script appelé hors contexte PlatformIO, ignoré")
        return

    print("\n[ESP-Video] ⚙ Vérification configuration (PlatformIO)")

    # Vérifier framework
    framework = env.get("PIOFRAMEWORK", [])
    if "espidf" not in framework:
        print("[ESP-Video] ❌ ESP-IDF requis")
        sys.exit(1)

    # Recherche composant
    def find_esp_video_dir():
        search_roots = [
            os.getcwd(),
            "/data/external_components",
            "/data/data/external_components",
        ]
        for base in search_roots:
            if not os.path.exists(base):
                continue
            for root, dirs, _ in os.walk(base):
                if "esp_video" in dirs:
                    candidate = os.path.join(root, "esp_video")
                    if os.path.exists(os.path.join(candidate, "CMakeLists.txt")):
                        return candidate
        return None

    component_dir = find_esp_video_dir()
    if not component_dir:
        print("[ESP-Video] ⚠️ Composant introuvable, mais CMake le trouvera")
        return
    
    print(f"[ESP-Video] 📂 Composant: {component_dir}")

    # Vérifier CMakeLists.txt
    cmake_file = os.path.join(component_dir, "CMakeLists.txt")
    if os.path.exists(cmake_file):
        print("[ESP-Video] ✓ CMakeLists.txt trouvé")
    else:
        print("[ESP-Video] ❌ CMakeLists.txt manquant!")
        print("[ESP-Video] ESP-IDF ne compilera PAS les sources sans CMakeLists.txt")
        sys.exit(1)

    # Vérifier stubs
    deps_dir = os.path.join(component_dir, "deps", "include")
    if os.path.exists(deps_dir):
        print(f"[ESP-Video] 🔧 Stubs: {deps_dir}")
        
        required_stubs = [
            "esp_cam_sensor.h",
            "esp_cam_sensor_xclk.h",
            "esp_sccb_i2c.h",
            "esp_cam_sensor_types.h",
        ]
        
        for stub in required_stubs:
            path = os.path.join(deps_dir, stub)
            if os.path.exists(path):
                print(f"[ESP-Video]   ✓ {stub}")
            else:
                print(f"[ESP-Video]   ⚠️ {stub} manquant")

    # Vérifier sources
    src_dir = os.path.join(component_dir, "src")
    if os.path.exists(src_dir):
        critical_sources = ["esp_video.c", "esp_video_init.c", "esp_video_vfs.c"]
        all_found = True
        for src in critical_sources:
            if os.path.exists(os.path.join(src_dir, src)):
                print(f"[ESP-Video]   ✓ {src}")
            else:
                print(f"[ESP-Video]   ❌ {src} manquant")
                all_found = False
        
        if all_found:
            print("[ESP-Video] ✅ Configuration OK")
            print("[ESP-Video] ℹ️  CMake compilera automatiquement via CMakeLists.txt")
        else:
            print("[ESP-Video] ❌ Sources manquantes")
    else:
        print("[ESP-Video] ⚠️ src/ introuvable")

    print()

# ===============================================================
# Exécution seulement si appelé par PlatformIO
# ===============================================================
if __name__ == "__main__" or "env" in dir():
    main()










