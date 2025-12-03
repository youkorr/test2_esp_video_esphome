#!/bin/bash

################################################################################
# Script de conversion vidéo optimisé pour ESP32-P4
# Utilise H.264 Baseline profile + normalisation audio
################################################################################

# Couleurs pour output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Vérification des paramètres
if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ]; then
    echo -e "${RED}Usage: $0 <input_file> <output_file> <frame_size (e.g. 640x480)>${NC}"
    echo ""
    echo "Exemples:"
    echo "  $0 input.mp4 output.mp4 640x480"
    echo "  $0 input.mp4 output.mp4 480x320"
    echo "  $0 input.mp4 output.mp4 320x240"
    exit 1
fi

# Vérification des dépendances
if ! command -v ffmpeg &> /dev/null; then
    echo -e "${RED}Erreur: ffmpeg n'est pas installé${NC}"
    exit 1
fi

if ! command -v jq &> /dev/null; then
    echo -e "${YELLOW}Avertissement: jq n'est pas installé (normalisation audio désactivée)${NC}"
    NORMALIZE_AUDIO=false
else
    NORMALIZE_AUDIO=true
fi

# Paramètres
input_file="$1"
output_file="$2"
frame_size="$3"

# Vérifier si le fichier d'entrée existe
if [ ! -f "$input_file" ]; then
    echo -e "${RED}Erreur: Le fichier '$input_file' n'existe pas${NC}"
    exit 1
fi

echo -e "${GREEN}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  Conversion vidéo optimisée pour ESP32-P4             ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "Entrée    : $input_file"
echo "Sortie    : $output_file"
echo "Résolution: $frame_size"
echo "Codec     : H.264 Baseline profile"
echo ""

################################################################################
# PASS 1: Analyse audio (si normalisation activée)
################################################################################
if [ "$NORMALIZE_AUDIO" = true ]; then
    echo -e "${YELLOW}[PASS 1/2] Analyse de l'audio pour normalisation...${NC}"

    loudness_data=$(ffmpeg -i "$input_file" \
        -af "loudnorm=print_format=json" \
        -vn -sn -dn -f null -y /dev/null 2>&1 | \
        awk '/{/,/}/' | jq -c '.')

    if [ -z "$loudness_data" ]; then
        echo -e "${YELLOW}Avertissement: Impossible d'analyser l'audio, normalisation désactivée${NC}"
        NORMALIZE_AUDIO=false
    else
        # Extraire les valeurs
        input_i=$(echo "$loudness_data" | jq -r '.input_i')
        input_tp=$(echo "$loudness_data" | jq -r '.input_tp')
        input_lra=$(echo "$loudness_data" | jq -r '.input_lra')
        input_thresh=$(echo "$loudness_data" | jq -r '.input_thresh')

        echo "  ✓ Loudness mesuré: ${input_i} LUFS"
        echo ""
    fi
fi

################################################################################
# PASS 2: Encodage H.264 Baseline optimisé ESP32-P4
################################################################################
echo -e "${YELLOW}[PASS 2/2] Encodage vidéo H.264 Baseline...${NC}"

# Construire le filtre audio
if [ "$NORMALIZE_AUDIO" = true ]; then
    audio_filter="loudnorm=measured_i=$input_i:measured_tp=$input_tp:measured_lra=$input_lra:measured_thresh=$input_thresh:offset=0.0:linear=true:print_format=summary"
else
    audio_filter="volume=1.0"
fi

# Encodage avec paramètres optimisés ESP32-P4
ffmpeg -i "$input_file" \
    -vf "scale=$frame_size:force_original_aspect_ratio=increase,crop=$frame_size,format=yuv420p" \
    -af "$audio_filter" \
    -c:v libx264 \
    -profile:v baseline \
    -level:v 3.1 \
    -preset veryslow \
    -tune fastdecode \
    -crf 23 \
    -maxrate 500k \
    -bufsize 1000k \
    -g 15 \
    -bf 0 \
    -r 15 \
    -pix_fmt yuv420p \
    -colorspace:v bt709 \
    -color_primaries:v bt709 \
    -color_trc:v bt709 \
    -color_range:v tv \
    -x264opts slices=1 \
    -movflags +faststart \
    -acodec pcm_u8 \
    -ar 16000 \
    -ac 1 \
    -y "$output_file"

# Vérifier le succès
if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✓ Conversion réussie !${NC}"
    echo ""

    # Afficher les informations du fichier de sortie
    if command -v ffprobe &> /dev/null; then
        echo -e "${GREEN}Informations du fichier de sortie:${NC}"
        ffprobe -v error -show_entries format=duration,size,bit_rate \
                -show_entries stream=codec_name,profile,width,height,r_frame_rate \
                -of default=noprint_wrappers=1 "$output_file" 2>&1 | grep -v "^$"
        echo ""

        # Taille des fichiers
        input_size=$(du -h "$input_file" | cut -f1)
        output_size=$(du -h "$output_file" | cut -f1)
        echo "Taille entrée : $input_size"
        echo "Taille sortie : $output_size"
    fi

    echo ""
    echo -e "${GREEN}Fichier prêt pour ESP32-P4: $output_file${NC}"
else
    echo -e "${RED}✗ Erreur lors de la conversion${NC}"
    exit 1
fi

################################################################################
# Paramètres expliqués:
################################################################################
# -profile:v baseline    : Profil H.264 compatible ESP32-P4 (CRUCIAL)
# -level:v 3.1           : Niveau H.264 pour résolutions jusqu'à 720p
# -preset veryslow       : Meilleure compression (OK pour conversion offline)
# -tune fastdecode       : Optimisé pour décodage rapide sur ESP32
# -crf 23                : Qualité constante (18-28, 23 = bon équilibre)
# -maxrate 500k          : Limite bitrate pour ESP32 (300-500k recommandé)
# -bufsize 1000k         : Buffer pour contrôle bitrate
# -g 15                  : GOP size = 15 frames (1 keyframe/seconde à 15fps)
# -bf 0                  : Pas de B-frames (Baseline profile)
# -r 15                  : 15 FPS (optimal pour ESP32-P4)
# -pix_fmt yuv420p       : Format pixel standard
# -movflags +faststart   : Métadonnées au début (streaming plus rapide)
################################################################################
