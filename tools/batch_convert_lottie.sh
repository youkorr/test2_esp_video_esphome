#!/bin/bash

# ============================================================================
# Script: Conversion batch de fichiers Lottie vers PNG sequences
# ============================================================================
# Convertit tous les fichiers .json Lottie d'un dossier en séquences PNG
#
# Usage:
#   ./batch_convert_lottie.sh [input_dir] [output_dir] [frames] [size]
#
# Exemple:
#   ./batch_convert_lottie.sh ./lottie_files ./animations 12 100
# ============================================================================

set -e  # Arrêter en cas d'erreur

# Couleurs pour output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Paramètres par défaut
INPUT_DIR="${1:-.}"
OUTPUT_DIR="${2:-./animations}"
FRAMES="${3:-8}"
SIZE="${4:-100}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONVERTER="$SCRIPT_DIR/lottie_to_png_sequence.py"

# Banner
echo -e "${BLUE}============================================================================${NC}"
echo -e "${BLUE}  Conversion Batch: Lottie JSON → PNG Sequences${NC}"
echo -e "${BLUE}============================================================================${NC}"
echo ""

# Vérifier que le script de conversion existe
if [ ! -f "$CONVERTER" ]; then
    echo -e "${RED}❌ Erreur: Script de conversion non trouvé:${NC}"
    echo -e "   $CONVERTER"
    exit 1
fi

# Vérifier que Python et dépendances sont installés
if ! python3 -c "import lottie" 2>/dev/null; then
    echo -e "${RED}❌ Erreur: Module Python 'lottie' non installé${NC}"
    echo -e "${YELLOW}   Installation:${NC}"
    echo -e "   pip install lottie cairosvg pillow"
    exit 1
fi

# Vérifier le dossier d'entrée
if [ ! -d "$INPUT_DIR" ]; then
    echo -e "${RED}❌ Erreur: Dossier d'entrée non trouvé: $INPUT_DIR${NC}"
    exit 1
fi

# Créer dossier de sortie
mkdir -p "$OUTPUT_DIR"

# Configuration
echo -e "${GREEN}📋 Configuration:${NC}"
echo -e "   Dossier entrée:  $INPUT_DIR"
echo -e "   Dossier sortie:  $OUTPUT_DIR"
echo -e "   Frames/animation: $FRAMES"
echo -e "   Taille:          ${SIZE}x${SIZE}px"
echo ""

# Trouver tous les fichiers JSON
mapfile -t JSON_FILES < <(find "$INPUT_DIR" -maxdepth 1 -name "*.json" -type f)

if [ ${#JSON_FILES[@]} -eq 0 ]; then
    echo -e "${YELLOW}⚠️  Aucun fichier .json trouvé dans $INPUT_DIR${NC}"
    exit 0
fi

echo -e "${GREEN}📂 Fichiers trouvés: ${#JSON_FILES[@]}${NC}"
echo ""

# Compteurs
SUCCESS_COUNT=0
FAIL_COUNT=0

# Convertir chaque fichier
for JSON_FILE in "${JSON_FILES[@]}"; do
    BASENAME=$(basename "$JSON_FILE" .json)
    # Remplacer espaces par underscores pour nom de dossier
    SAFE_NAME=$(echo "$BASENAME" | tr ' ' '_' | tr '[:upper:]' '[:lower:]')
    OUTPUT_SUBDIR="$OUTPUT_DIR/$SAFE_NAME"

    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${YELLOW}🎨 Conversion: $BASENAME${NC}"
    echo -e "   Fichier: $JSON_FILE"
    echo -e "   Sortie:  $OUTPUT_SUBDIR"
    echo ""

    # Exécuter conversion
    if python3 "$CONVERTER" "$JSON_FILE" "$OUTPUT_SUBDIR" --frames "$FRAMES" --size "$SIZE"; then
        echo -e "${GREEN}✅ Succès: $BASENAME${NC}"
        ((SUCCESS_COUNT++))
    else
        echo -e "${RED}❌ Échec: $BASENAME${NC}"
        ((FAIL_COUNT++))
    fi

    echo ""
done

# Résumé
echo -e "${BLUE}============================================================================${NC}"
echo -e "${GREEN}📊 Résumé de Conversion${NC}"
echo -e "${BLUE}============================================================================${NC}"
echo -e "${GREEN}✅ Réussies: $SUCCESS_COUNT${NC}"
if [ $FAIL_COUNT -gt 0 ]; then
    echo -e "${RED}❌ Échouées: $FAIL_COUNT${NC}"
fi
echo -e "${BLUE}   Total:    ${#JSON_FILES[@]}${NC}"
echo ""

# Instructions suivantes
if [ $SUCCESS_COUNT -gt 0 ]; then
    echo -e "${YELLOW}📋 Prochaines étapes:${NC}"
    echo -e "   1. Vérifier les frames générées dans: $OUTPUT_DIR"
    echo -e "   2. Copier vers carte SD: cp -r $OUTPUT_DIR /sdcard/"
    echo -e "   3. Configurer dans storage: (voir page_home_avec_animations.yaml)"
    echo -e "   4. Utiliser avec animimg widget dans LVGL"
    echo ""

    # Générer snippet de configuration
    CONFIG_FILE="$OUTPUT_DIR/storage_config_snippet.yaml"
    echo "# Configuration Storage générée automatiquement" > "$CONFIG_FILE"
    echo "# Date: $(date)" >> "$CONFIG_FILE"
    echo "" >> "$CONFIG_FILE"
    echo "storage:" >> "$CONFIG_FILE"
    echo "  sd_images:" >> "$CONFIG_FILE"

    for JSON_FILE in "${JSON_FILES[@]}"; do
        BASENAME=$(basename "$JSON_FILE" .json)
        SAFE_NAME=$(echo "$BASENAME" | tr ' ' '_' | tr '[:upper:]' '[:lower:]')

        echo "" >> "$CONFIG_FILE"
        echo "    # Animation: $BASENAME ($FRAMES frames)" >> "$CONFIG_FILE"

        for ((i=1; i<=FRAMES; i++)); do
            FRAME_NUM=$(printf "%02d" $i)
            echo "    - id: ${SAFE_NAME}_f${FRAME_NUM}" >> "$CONFIG_FILE"
            echo "      file_path: \"/animations/${SAFE_NAME}/frame_${FRAME_NUM}.png\"" >> "$CONFIG_FILE"
            echo "      resize: ${SIZE}x${SIZE}" >> "$CONFIG_FILE"
            echo "      format: rgb565" >> "$CONFIG_FILE"
            echo "      auto_load: true" >> "$CONFIG_FILE"
        done
    done

    echo -e "${GREEN}✅ Configuration YAML générée: $CONFIG_FILE${NC}"
    echo ""
fi

echo -e "${GREEN}✨ Terminé!${NC}"
