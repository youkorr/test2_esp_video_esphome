#!/bin/bash

# Script de conversion MP4 → MJPEG optimisé pour ESP32-P4
# Usage: ./convert_mp4_to_mjpeg.sh input.mp4 [quality] [resolution] [fps]

set -e

# Couleurs pour output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Afficher logo
echo -e "${BLUE}"
echo "╔════════════════════════════════════════════════════╗"
echo "║   MP4 → MJPEG Converter for ESP32-P4             ║"
echo "║   Optimized for LVGL v8 Hardware Decoder          ║"
echo "╚════════════════════════════════════════════════════╝"
echo -e "${NC}"

# Vérifier FFmpeg
if ! command -v ffmpeg &> /dev/null; then
    echo -e "${RED}❌ FFmpeg n'est pas installé !${NC}"
    echo ""
    echo "Installation:"
    echo "  Ubuntu/Debian: sudo apt install ffmpeg"
    echo "  macOS:         brew install ffmpeg"
    echo "  Windows:       https://ffmpeg.org/download.html"
    exit 1
fi

# Paramètres
INPUT_FILE="$1"
QUALITY="${2:-10}"        # Default: 10 (bonne qualité)
RESOLUTION="${3:-640x480}" # Default: 640x480
FPS="${4:-25}"            # Default: 25 FPS

# Vérifier fichier d'entrée
if [ -z "$INPUT_FILE" ]; then
    echo -e "${RED}❌ Usage: $0 <input.mp4> [quality] [resolution] [fps]${NC}"
    echo ""
    echo "Exemples:"
    echo "  $0 video.mp4                    # Qualité moyenne (q=10), 640x480, 25fps"
    echo "  $0 video.mp4 5                  # Haute qualité"
    echo "  $0 video.mp4 15 320x240         # Petite résolution"
    echo "  $0 video.mp4 10 640x480 30      # 30 FPS"
    echo ""
    echo "Presets qualité:"
    echo "  q=3-5   : Excellente qualité (80-120KB/frame) - Pour écrans haute qualité"
    echo "  q=8-12  : Bonne qualité (40-60KB/frame) - RECOMMANDÉ pour ESP32-P4"
    echo "  q=15-20 : Qualité moyenne (25-40KB/frame) - Pour économiser mémoire"
    echo "  q=25-30 : Qualité basse (15-25KB/frame) - Pour petits écrans"
    exit 1
fi

if [ ! -f "$INPUT_FILE" ]; then
    echo -e "${RED}❌ Fichier non trouvé: $INPUT_FILE${NC}"
    exit 1
fi

# Générer nom de fichier de sortie
BASENAME=$(basename "$INPUT_FILE" .mp4)
OUTPUT_FILE="${BASENAME}_mjpeg_q${QUALITY}_${RESOLUTION}_${FPS}fps.avi"

# Afficher infos
echo -e "${BLUE}📋 Configuration:${NC}"
echo "  Entrée:      $INPUT_FILE"
echo "  Sortie:      $OUTPUT_FILE"
echo "  Qualité:     $QUALITY (1=meilleur, 31=pire)"
echo "  Résolution:  $RESOLUTION"
echo "  FPS:         $FPS"
echo ""

# Analyser fichier d'entrée
echo -e "${BLUE}🔍 Analyse du fichier d'entrée...${NC}"
ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,r_frame_rate,duration -of default=noprint_wrappers=1 "$INPUT_FILE"
echo ""

# Demander confirmation
read -p "Continuer la conversion? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo -e "${YELLOW}⚠️  Conversion annulée${NC}"
    exit 0
fi

# Conversion
echo ""
echo -e "${GREEN}🎬 Conversion en cours...${NC}"
echo ""

ffmpeg -i "$INPUT_FILE" \
  -c:v mjpeg \
  -q:v "$QUALITY" \
  -vf "scale=$RESOLUTION" \
  -r "$FPS" \
  -an \
  -y \
  "$OUTPUT_FILE"

# Vérifier succès
if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✅ Conversion réussie !${NC}"
    echo ""

    # Statistiques
    INPUT_SIZE=$(stat -f%z "$INPUT_FILE" 2>/dev/null || stat -c%s "$INPUT_FILE" 2>/dev/null)
    OUTPUT_SIZE=$(stat -f%z "$OUTPUT_FILE" 2>/dev/null || stat -c%s "$OUTPUT_FILE" 2>/dev/null)

    INPUT_SIZE_MB=$(echo "scale=2; $INPUT_SIZE / 1048576" | bc)
    OUTPUT_SIZE_MB=$(echo "scale=2; $OUTPUT_SIZE / 1048576" | bc)

    echo -e "${BLUE}📊 Statistiques:${NC}"
    echo "  Fichier original:  ${INPUT_SIZE_MB} MB"
    echo "  Fichier converti:  ${OUTPUT_SIZE_MB} MB"

    if [ "$OUTPUT_SIZE" -lt "$INPUT_SIZE" ]; then
        RATIO=$(echo "scale=1; 100 - ($OUTPUT_SIZE * 100 / $INPUT_SIZE)" | bc)
        echo -e "  ${GREEN}Réduction: ${RATIO}%${NC}"
    else
        RATIO=$(echo "scale=1; ($OUTPUT_SIZE * 100 / $INPUT_SIZE) - 100" | bc)
        echo -e "  ${YELLOW}Augmentation: ${RATIO}%${NC} (normal pour MJPEG basse qualité)"
    fi

    echo ""
    echo -e "${BLUE}📝 Fichier de sortie:${NC}"
    echo "  $OUTPUT_FILE"
    echo ""

    # Conseils ESP32-P4
    echo -e "${BLUE}🔧 Configuration ESPHome recommandée:${NC}"
    echo ""
    echo "video_player:"
    echo "  - id: my_video"
    echo "    file: \"/sdcard/$OUTPUT_FILE\""
    echo "    canvas_id: my_canvas"
    echo "    loop: true"
    echo "    autoplay: true"
    echo ""
    echo -e "${GREEN}Performance attendue: 25-30 FPS avec décodeur JPEG hardware${NC}"
    echo ""

else
    echo ""
    echo -e "${RED}❌ Erreur lors de la conversion${NC}"
    exit 1
fi
