"""
Script PlatformIO pour compiler ThorVG avec LVGL
Inspiré de esp_video_build.py
"""

Import("env")
import os

def create_thorvg_config_h(thorvg_src_dir):
    """
    Create config.h for ThorVG with minimal configuration for LVGL
    ThorVG normally generates this with meson, but we create it manually
    """
    config_h_path = os.path.join(thorvg_src_dir, "config.h")

    # Check if config.h exists and has correct content
    if os.path.exists(config_h_path):
        with open(config_h_path, 'r') as f:
            existing_content = f.read()
            # Check if it has the #undef fix (sign of correct version)
            if '#undef THORVG_GL_RASTER_SUPPORT' in existing_content:
                return
            else:
                os.remove(config_h_path)

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

/* Disabled features - UNDEF them so #ifdef fails (ThorVG uses #ifdef not #if) */
/* DO NOT define GL/WG to 0, leave them undefined! */
#undef THORVG_GL_RASTER_SUPPORT
#undef THORVG_WG_RASTER_SUPPORT
#undef THORVG_PNG_LOADER_SUPPORT
#undef THORVG_JPG_LOADER_SUPPORT
#undef THORVG_WEBP_LOADER_SUPPORT
#undef THORVG_TTF_LOADER_SUPPORT
#undef THORVG_LOTTIE_EXPRESSIONS_SUPPORT

/* Log configuration */
#define THORVG_LOG_ENABLED 0

/* SIMD support - disabled for compatibility */
#undef THORVG_AVX_VECTOR_SUPPORT
#undef THORVG_NEON_VECTOR_SUPPORT

#endif /* _TVG_CONFIG_H_ */
"""

    try:
        with open(config_h_path, 'w') as f:
            f.write(config_h_content)
    except Exception as e:
        print(f"[ThorVG Build] ERROR: Failed to create config.h: {e}")

# Obtenir le répertoire du composant (ce script est dans components/lvgl/)
# Dans SCons, __file__ n'existe pas, on utilise Dir('.').srcnode().abspath
script_dir = Dir('.').srcnode().abspath
component_dir = script_dir
parent_components_dir = os.path.dirname(component_dir)

# ThorVG est dans components/thorvg/
thorvg_dir = os.path.join(parent_components_dir, "thorvg")

if not os.path.exists(thorvg_dir):
    print(f"[ThorVG Build] ERROR: ThorVG directory not found at {thorvg_dir}")
else:

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
                # Skip WebGPU, OpenGL, and external image loaders
                skip_dirs = ['wg_engine', 'gl_engine', 'external_jpg', 'external_png',
                            'external_webp', 'ttf']
                if any(skip_dir in root for skip_dir in skip_dirs):
                    continue

                for file in files:
                    if file.endswith(".cpp") or file.endswith(".c"):
                        # Skip excluded files
                        if file in exclude_files:
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

        print(f"[ThorVG Build] {len(thorvg_sources)} source files compiled")
    else:
        print("[ThorVG Build] Warning: No ThorVG source files found")
