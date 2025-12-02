import subprocess
import sys
import os

def get_video_info(input_file):
    """Récupère la résolution et le framerate de la vidéo"""
    cmd = [
        "ffprobe",
        "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "stream=width,height,r_frame_rate",
        "-of", "default=noprint_wrappers=1:nokey=1",
        input_file
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"ffprobe error: {result.stderr}")
    lines = result.stdout.strip().split("\n")
    width = int(lines[0])
    height = int(lines[1])
    framerate = eval(lines[2])  # convertit '30/1' en 30.0
    return width, height, framerate

def reencode_for_esp32(input_file, output_file):
    """Réencode la vidéo pour l’ESP32-P4"""
    width, height, framerate = get_video_info(input_file)

    # Limite la résolution à 320x240 si plus grande
    target_width = min(width, 320)
    target_height = min(height, 240)

    print(f"Réencodage {input_file} ({width}x{height}, {framerate:.2f}fps) -> {target_width}x{target_height}, 15fps")

    # Commande FFmpeg pour réencoder H.264 + AAC audio
    cmd = [
        "ffmpeg",
        "-i", input_file,
        "-vf", f"scale={target_width}:{target_height}",
        "-r", "15",                  # framerate cible
        "-c:v", "libx264",           # codec vidéo H.264
        "-preset", "fast",           # rapide pour ESP32
        "-crf", "28",                # qualité/poids
        "-c:a", "aac",               # codec audio
        "-b:a", "64k",               # bitrate audio faible
        "-y",                        # overwrite output
        output_file
    ]

    subprocess.run(cmd, check=True)
    print(f"Réencodage terminé : {output_file}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python reencode_for_esp32.py <input_video.mp4>")
        sys.exit(1)

    input_video = sys.argv[1]
    base, ext = os.path.splitext(input_video)
    output_video = f"{base}_esp32.mp4"

    reencode_for_esp32(input_video, output_video)

