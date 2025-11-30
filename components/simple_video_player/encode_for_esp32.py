import subprocess
import sys
import os
import json

# ======================================
# Configuration pour ESP32-P4
# ======================================
MAX_WIDTH = 800
MAX_HEIGHT = 600
MAX_BITRATE = 500  # kbps
TARGET_FPS = 25

# ======================================
# Fonctions utilitaires
# ======================================
def get_video_info(input_file):
    """Récupère les informations vidéo via ffprobe"""
    cmd = [
        "ffprobe", "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "stream=width,height,r_frame_rate,bit_rate",
        "-of", "json",
        input_file
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    info = json.loads(result.stdout)
    stream = info['streams'][0]
    width = int(stream['width'])
    height = int(stream['height'])
    # r_frame_rate est sous forme "30/1"
    fr_parts = stream['r_frame_rate'].split('/')
    fps = int(fr_parts[0]) / int(fr_parts[1])
    bitrate = int(stream.get('bit_rate', 0)) // 1000  # kbps
    return width, height, fps, bitrate

def reencode_for_esp32(input_file, output_file):
    """Réencode la vidéo pour ESP32-P4"""
    width, height, fps, bitrate = get_video_info(input_file)
    print(f"Video info: {width}x{height}, {fps:.2f} fps, {bitrate} kbps")

    # Déterminer résolution cible
    scale_width = min(width, MAX_WIDTH)
    scale_height = min(height, MAX_HEIGHT)

    # Déterminer framerate cible
    target_fps = min(fps, TARGET_FPS)

    # Déterminer bitrate cible
    target_bitrate = min(bitrate, MAX_BITRATE)

    # FFmpeg command
    cmd = [
        "ffmpeg", "-y", "-i", input_file,
        "-vf", f"scale={scale_width}:{scale_height}",
        "-c:v", "libx264",
        "-profile:v", "baseline",
        "-level", "3.0",
        "-preset", "veryfast",
        "-x264-params", "bframes=0:keyint=30:min-keyint=30:no-scenecut=1",
        "-b:v", f"{target_bitrate}k",
        "-r", f"{target_fps}",
        "-an",  # désactiver l'audio pour simplifier
        "-movflags", "faststart",
        output_file
    ]

    print("Running FFmpeg command:")
    print(" ".join(cmd))
    subprocess.run(cmd)

# ======================================
# Main
# ======================================
if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python encode_for_esp32.py input.mp4 output.mp4")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    if not os.path.exists(input_file):
        print(f"Error: {input_file} does not exist.")
        sys.exit(1)

    reencode_for_esp32(input_file, output_file)
    print(f"Done! Output saved to {output_file}")
