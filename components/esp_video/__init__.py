"""
esp_video_build.py — Compile VRAIMENT les sources ESP-Video
"""
import os
import sys
from SCons.Script import Import

Import("env")

print("\n[ESP-Video] ⚙ Build script avec compilation des sources")

framework = env.get("PIOFRAMEWORK", [])
if "espidf" not in framework:
    print("[ESP-Video] ❌ ESP-IDF requis")
    sys.exit(1)

# ===============================================================
# Trouver esp_video
# ===============================================================
def find_esp_video_dir():
    for base in ["/data/external_components", "/data/data/external_components"]:
        for root, dirs, _ in os.walk(base):
            if "esp_video" in dirs:
                candidate = os.path.join(root, "esp_video")
                if os.path.exists(os.path.join(candidate, "src")):
                    return candidate
    print("[ESP-Video] ❌ Composant introuvable")
    sys.exit(1)

component_dir = find_esp_video_dir()
print(f"[ESP-Video] 📂 Composant: {component_dir}")

# ===============================================================
# Vérifier deps/include
# ===============================================================
deps_dir = os.path.join(component_dir, "deps", "include")
print(f"[ESP-Video] 🔧 Deps: {deps_dir}")

if not os.path.exists(deps_dir):
    print("[ESP-Video] ❌ deps/include manquant")
    sys.exit(1)

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
        print(f"[ESP-Video]   ❌ {stub} MANQUANT")
        sys.exit(1)

# ===============================================================
# Includes
# ===============================================================
env.Prepend(CPPPATH=[deps_dir])
print(f"[ESP-Video] ➕ Include deps (priorité)")

include_paths = [
    os.path.join(component_dir, "include"),
    os.path.join(component_dir, "include", "linux"),
    os.path.join(component_dir, "include", "sys"),
    os.path.join(component_dir, "private_include"),
    os.path.join(component_dir, "src"),
    os.path.join(component_dir, "src", "device"),
]

for p in include_paths:
    if os.path.exists(p):
        env.Append(CPPPATH=[p])
        print(f"[ESP-Video] ➕ {os.path.basename(p)}")

# ===============================================================
# COMPILER LES SOURCES ESP-VIDEO
# ===============================================================
print("[ESP-Video] 🔨 Compilation des sources...")

src_dir = os.path.join(component_dir, "src")

# Liste complète des sources ESP-Video
base_sources = [
    "esp_video.c",
    "esp_video_buffer.c",
    "esp_video_init.c",
    "esp_video_ioctl.c",
    "esp_video_mman.c",
    "esp_video_vfs.c",
    "esp_video_cam.c",
    "esp_video_isp_pipeline.c",
]

device_sources = [
    "device/esp_video_csi_device.c",
    "device/esp_video_isp_device.c",
]

# Sources conditionnelles (si H264/JPEG activés)
optional_sources = [
    "device/esp_video_jpeg_device.c",   # Si JPEG
    "device/esp_video_h264_device.c",   # Si H264
]

all_sources = []

# Ajouter sources de base
for src in base_sources:
    src_path = os.path.join(src_dir, src)
    if os.path.exists(src_path):
        all_sources.append(src_path)
        print(f"[ESP-Video]   + {src}")

# Ajouter sources device
for src in device_sources:
    src_path = os.path.join(src_dir, src)
    if os.path.exists(src_path):
        all_sources.append(src_path)
        print(f"[ESP-Video]   + {src}")

# Ajouter sources optionnelles si présentes
for src in optional_sources:
    src_path = os.path.join(src_dir, src)
    if os.path.exists(src_path):
        all_sources.append(src_path)
        print(f"[ESP-Video]   + {src}")

if not all_sources:
    print("[ESP-Video] ❌ Aucune source trouvée!")
    sys.exit(1)

# Compiler les sources en bibliothèque
print(f"[ESP-Video] 🔨 Compilation de {len(all_sources)} fichiers...")

esp_video_lib = env.Library(
    target=os.path.join("$BUILD_DIR", "libesp_video"),
    source=all_sources
)

# Ajouter la lib au link
env.Append(LIBS=[esp_video_lib])

print(f"[ESP-Video] ✅ Bibliothèque créée: libesp_video.a")

# ===============================================================
# Flags
# ===============================================================
flags = [
    "CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=1",
    "CONFIG_ESP_VIDEO_ENABLE_ISP=1",
    "CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE=1",
    "CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=1",
    "CONFIG_ESP_VIDEO_USE_HEAP_ALLOCATOR=1",
    "CONFIG_ESP_VIDEO_ENABLE_HW_H264_VIDEO_DEVICE=1",
    "CONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE=1",
]

env.Append(CPPDEFINES=flags)

# ===============================================================
# Fin
# ===============================================================
print("[ESP-Video] ✅ Configuration terminée")
print(f"[ESP-Video] 📦 Flash devrait augmenter de ~25-30%\n")



