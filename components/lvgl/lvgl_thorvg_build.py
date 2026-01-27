"""
Script PlatformIO pour compiler ThorVG avec LVGL
Inspiré de esp_video_build.py
"""

Import("env")
import os

print("=" * 80)
print("[ThorVG Build] ========================================")
print("[ThorVG Build] Script START - lvgl_thorvg_build.py")
print("[ThorVG Build] ========================================")
print("=" * 80)

def create_thorvg_config_h(thorvg_src_dir):
    """
    Create config.h for ThorVG with minimal configuration for LVGL
    ThorVG normally generates this with meson, but we create it manually
    """
    config_h_path = os.path.join(thorvg_src_dir, "config.h")

    # Don't recreate if it already exists
    if os.path.exists(config_h_path):
        print(f"[ThorVG Build] config.h already exists, skipping")
        return

    config_h_content = """/* Auto-generated config.h for ThorVG (LVGL external mode) */
#ifndef _TVG_CONFIG_H_
#define _TVG_CONFIG_H_

/* ThorVG version */
#define THORVG_VERSION_STRING "0.15.16"

/* Enabled features for LVGL */
#define THORVG_SW_RASTER_SUPPORT 1
#define THORVG_SVG_LOADER_SUPPORT 1
#define THORVG_LOTTIE_LOADER_SUPPORT 1
#define THORVG_RAW_LOADER_SUPPORT 1

/* Disabled features (not needed for LVGL) */
#define THORVG_GL_RASTER_SUPPORT 0
#define THORVG_WG_RASTER_SUPPORT 0
#define THORVG_PNG_LOADER_SUPPORT 0
#define THORVG_JPG_LOADER_SUPPORT 0
#define THORVG_WEBP_LOADER_SUPPORT 0
#define THORVG_TTF_LOADER_SUPPORT 0
#define THORVG_LOTTIE_EXPRESSIONS_SUPPORT 0

/* Log configuration */
#define THORVG_LOG_ENABLED 0

/* SIMD support - disabled for compatibility */
#define THORVG_AVX_VECTOR_SUPPORT 0
#define THORVG_NEON_VECTOR_SUPPORT 0

#endif /* _TVG_CONFIG_H_ */
"""

    try:
        with open(config_h_path, 'w') as f:
            f.write(config_h_content)
        print(f"[ThorVG Build] Created config.h at: {config_h_path}")
    except Exception as e:
        print(f"[ThorVG Build] ERROR: Failed to create config.h: {e}")

# Obtenir le répertoire du composant (ce script est dans components/lvgl/)
# Dans SCons, __file__ n'existe pas, on utilise Dir('.').srcnode().abspath
script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

print(f"[ThorVG Build] Script directory: {script_dir}")
print(f"[ThorVG Build] Component directory: {component_dir}")
print(f"[ThorVG Build] Parent components directory: {parent_components_dir}")

# ThorVG est dans components/thorvg/
thorvg_dir = os.path.join(parent_components_dir, "thorvg")
print(f"[ThorVG Build] Looking for ThorVG at: {thorvg_dir}")
print(f"[ThorVG Build] ThorVG exists: {os.path.exists(thorvg_dir)}")

if not os.path.exists(thorvg_dir):
    print(f"[ThorVG Build] ERROR: ThorVG directory not found!")
    print(f"[ThorVG Build] Expected at: {thorvg_dir}")
    print(f"[ThorVG Build] Please ensure ThorVG was downloaded during code generation")
else:
    print(f"[ThorVG Build] ThorVG directory found!")
    print(f"[ThorVG Build] Adding ThorVG sources from {thorvg_dir}")

    # Create config.h if it doesn't exist
    thorvg_src_dir = os.path.join(thorvg_dir, "src")
    create_thorvg_config_h(thorvg_src_dir)

    # Ajouter les includes ThorVG (tous les sous-répertoires nécessaires)
    env.Append(CPPPATH=[
        os.path.join(thorvg_dir, "inc"),
        os.path.join(thorvg_dir, "src"),
        os.path.join(thorvg_dir, "src", "common"),
        os.path.join(thorvg_dir, "src", "renderer"),
        os.path.join(thorvg_dir, "src", "renderer", "sw_engine"),
        os.path.join(thorvg_dir, "src", "loaders"),
        os.path.join(thorvg_dir, "src", "loaders", "svg"),
        os.path.join(thorvg_dir, "src", "loaders", "lottie"),
        os.path.join(thorvg_dir, "src", "loaders", "raw"),
        os.path.join(thorvg_dir, "src", "bindings", "capi"),
    ])

    # Collecter tous les fichiers .cpp ThorVG
    thorvg_sources = []

    # Sources principales ThorVG (SEULEMENT software rasterizer, pas GL/WG)
    src_dirs = [
        os.path.join(thorvg_dir, "src", "common"),
        os.path.join(thorvg_dir, "src", "renderer"),
        os.path.join(thorvg_dir, "src", "renderer", "sw_engine"),  # Software engine only
        os.path.join(thorvg_dir, "src", "loaders"),
        os.path.join(thorvg_dir, "src", "loaders", "svg"),
        os.path.join(thorvg_dir, "src", "loaders", "lottie"),
        os.path.join(thorvg_dir, "src", "loaders", "raw"),
        os.path.join(thorvg_dir, "src", "bindings", "capi"),
    ]

    # Files to exclude (WebGPU, OpenGL, unused loaders)
    exclude_files = [
        "tvgWgCanvas.cpp",      # WebGPU canvas (disabled)
        "tvgGlCanvas.cpp",      # OpenGL canvas (disabled)
    ]

    for src_dir in src_dirs:
        if os.path.exists(src_dir):
            for root, dirs, files in os.walk(src_dir):
                # Skip WebGPU and OpenGL engine directories
                if 'wg_engine' in root or 'gl_engine' in root:
                    continue

                for file in files:
                    if file.endswith(".cpp") or file.endswith(".c"):
                        # Skip excluded files
                        if file in exclude_files:
                            print(f"[ThorVG Build] Skipping {file} (feature disabled)")
                            continue

                        thorvg_sources.append(os.path.join(root, file))

    # Flags de compilation pour ThorVG
    env.Append(CPPDEFINES=[
        "THORVG_SW_RASTER_SUPPORT",  # Software rasterizer
        "THORVG_SVG_LOADER_SUPPORT",  # SVG loader
        "THORVG_LOTTIE_LOADER_SUPPORT",  # Lottie loader
    ])

    # Supprimer les warnings ThorVG
    env.Append(CXXFLAGS=[
        "-Wno-unused-variable",
        "-Wno-unused-function",
        "-Wno-missing-field-initializers",
    ])

    if thorvg_sources:
        print(f"[ThorVG Build] Found {len(thorvg_sources)} source files")

        # Compiler chaque fichier source en objet (comme esp_video_build.py)
        objects = []
        for src_file in thorvg_sources:
            obj = env.Object(src_file)
            objects.extend(obj)

        # Créer une bibliothèque statique avec les objets compilés
        lib = env.StaticLibrary(
            os.path.join("$BUILD_DIR", "libthorvg"),
            objects
        )

        # Ajouter la bibliothèque au linkage (PREPEND = avant les autres libs)
        env.Prepend(LIBS=[lib])

        print(f"[ThorVG Build] Compiled {len(thorvg_sources)} files into libthorvg.a")
        print("[ThorVG Build] ThorVG library added to linking")
    else:
        print("[ThorVG Build] Warning: No ThorVG source files found")
