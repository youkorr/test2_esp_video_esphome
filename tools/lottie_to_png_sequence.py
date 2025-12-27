#!/usr/bin/env python3
"""
Convertir un fichier Lottie JSON en séquence PNG
Nécessite: pip install lottie cairosvg pillow

Usage:
    python lottie_to_png_sequence.py input.json output_dir/ --frames 8 --size 100
"""

import argparse
import json
import os
from pathlib import Path

try:
    from lottie.exporters import export_png
    from lottie.objects import Animation
except ImportError:
    print("❌ Erreur: Installez les dépendances:")
    print("   pip install lottie cairosvg pillow")
    exit(1)


def convert_lottie_to_png_sequence(
    lottie_file: str,
    output_dir: str,
    num_frames: int = 8,
    size: int = 100
):
    """
    Convertit un fichier Lottie en séquence PNG

    Args:
        lottie_file: Chemin vers le fichier .json Lottie
        output_dir: Dossier de sortie pour les PNG
        num_frames: Nombre de frames à générer (défaut: 8)
        size: Taille des images en pixels (défaut: 100x100)
    """
    # Créer dossier de sortie
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    # Charger le fichier Lottie
    print(f"📂 Chargement: {lottie_file}")
    with open(lottie_file, 'r') as f:
        lottie_data = json.load(f)

    animation = Animation.load(lottie_data)

    # Calculer la durée totale
    fps = animation.frame_rate
    total_frames = animation.out_point - animation.in_point
    frame_step = max(1, total_frames // num_frames)

    print(f"⚙️  Configuration:")
    print(f"   FPS: {fps}")
    print(f"   Frames total: {total_frames}")
    print(f"   Frames à générer: {num_frames}")
    print(f"   Taille: {size}x{size}px")
    print()

    # Générer les frames
    for i in range(num_frames):
        frame_number = animation.in_point + (i * frame_step)
        output_file = output_path / f"frame_{i+1:02d}.png"

        print(f"🎨 Génération frame {i+1}/{num_frames} → {output_file.name}")

        # Export PNG
        export_png(
            animation,
            output_file,
            width=size,
            height=size,
            frame=frame_number
        )

    print()
    print(f"✅ Terminé! {num_frames} frames créées dans {output_dir}")
    print()
    print("📋 Prochaines étapes:")
    print(f"   1. Copier {output_dir} vers /sdcard/animations/")
    print("   2. Ajouter les frames dans storage: sd_images:")
    print("   3. Utiliser avec animimg: dans LVGL")


def main():
    parser = argparse.ArgumentParser(
        description="Convertir Lottie JSON vers séquence PNG"
    )
    parser.add_argument(
        "input",
        help="Fichier Lottie (.json)"
    )
    parser.add_argument(
        "output",
        help="Dossier de sortie"
    )
    parser.add_argument(
        "--frames",
        type=int,
        default=8,
        help="Nombre de frames (défaut: 8)"
    )
    parser.add_argument(
        "--size",
        type=int,
        default=100,
        help="Taille en pixels (défaut: 100)"
    )

    args = parser.parse_args()

    convert_lottie_to_png_sequence(
        args.input,
        args.output,
        args.frames,
        args.size
    )


if __name__ == "__main__":
    main()
