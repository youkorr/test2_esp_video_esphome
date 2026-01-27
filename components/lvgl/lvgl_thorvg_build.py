"""
Script PlatformIO pour compiler ThorVG avec LVGL
Inspiré de esp_video_build.py
"""

Import("env")
import os

# Trouver le répertoire ThorVG
project_dir = env.subst("$PROJECT_DIR")
components_dir = os.path.join(project_dir, "components")
thorvg_dir = os.path.join(components_dir, "thorvg")

if not os.path.exists(thorvg_dir):
    print(f"[ThorVG Build] Warning: ThorVG directory not found at {thorvg_dir}")
else:
    print(f"[ThorVG Build] Adding ThorVG sources from {thorvg_dir}")

    # Ajouter les includes ThorVG
    env.Append(CPPPATH=[
        os.path.join(thorvg_dir, "inc"),
        os.path.join(thorvg_dir, "src"),
        os.path.join(thorvg_dir, "src", "bindings", "capi"),
    ])

    # Collecter tous les fichiers .cpp ThorVG
    thorvg_sources = []

    # Sources principales ThorVG
    src_dirs = [
        os.path.join(thorvg_dir, "src", "common"),
        os.path.join(thorvg_dir, "src", "renderer"),
        os.path.join(thorvg_dir, "src", "renderer", "sw_engine"),
        os.path.join(thorvg_dir, "src", "loaders"),
        os.path.join(thorvg_dir, "src", "loaders", "svg"),
        os.path.join(thorvg_dir, "src", "loaders", "lottie"),
        os.path.join(thorvg_dir, "src", "loaders", "raw"),
        os.path.join(thorvg_dir, "src", "loaders", "png"),
        os.path.join(thorvg_dir, "src", "loaders", "jpg"),
        os.path.join(thorvg_dir, "src", "loaders", "webp"),
        os.path.join(thorvg_dir, "src", "bindings", "capi"),
    ]

    for src_dir in src_dirs:
        if os.path.exists(src_dir):
            for root, dirs, files in os.walk(src_dir):
                for file in files:
                    if file.endswith(".cpp") or file.endswith(".c"):
                        thorvg_sources.append(os.path.join(root, file))

    if thorvg_sources:
        print(f"[ThorVG Build] Found {len(thorvg_sources)} source files")
        env.Append(SRC_FILTER=["+<" + src + ">" for src in thorvg_sources])

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
    else:
        print("[ThorVG Build] Warning: No ThorVG source files found")
