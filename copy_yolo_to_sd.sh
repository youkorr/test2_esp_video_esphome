#!/bin/bash
# ============================================================================
# Script: Copier le modèle YOLO11 sur la carte SD
# ============================================================================
# Usage:
#   ./copy_yolo_to_sd.sh [chemin_carte_sd]
#
# Exemples:
#   ./copy_yolo_to_sd.sh /media/sdcard
#   ./copy_yolo_to_sd.sh /Volumes/SDCARD
#   ./copy_yolo_to_sd.sh /run/media/user/SDCARD
# ============================================================================

set -e  # Arrêter en cas d'erreur

# Couleurs pour output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Fonction pour afficher les messages
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[⚠]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# Chemin du modèle source
MODEL_SOURCE="components/yolo11_detect/models/p4/yolo11_detect_s8_v1.espdl"
MODEL_NAME="yolo11_detect_s8_v1.espdl"

# Vérifier que le modèle source existe
if [ ! -f "$MODEL_SOURCE" ]; then
    print_error "Modèle source introuvable: $MODEL_SOURCE"
    print_info "Assurez-vous d'être dans le répertoire racine du projet."
    exit 1
fi

# Afficher les informations du modèle
MODEL_SIZE=$(ls -lh "$MODEL_SOURCE" | awk '{print $5}')
print_info "Modèle trouvé: $MODEL_SOURCE ($MODEL_SIZE)"

# Détecter le chemin de la carte SD
if [ -n "$1" ]; then
    # Chemin fourni en argument
    SD_PATH="$1"
else
    # Tenter de détecter automatiquement
    print_info "Détection automatique de la carte SD..."

    # Linux - Essayer plusieurs emplacements communs
    if [ -d "/media/$USER" ]; then
        SD_CANDIDATES=$(find /media/$USER -maxdepth 1 -type d 2>/dev/null | tail -n +2)
    elif [ -d "/run/media/$USER" ]; then
        SD_CANDIDATES=$(find /run/media/$USER -maxdepth 1 -type d 2>/dev/null | tail -n +2)
    elif [ -d "/Volumes" ]; then
        # macOS
        SD_CANDIDATES=$(find /Volumes -maxdepth 1 -type d 2>/dev/null | grep -v "Macintosh HD")
    else
        SD_CANDIDATES=""
    fi

    if [ -z "$SD_CANDIDATES" ]; then
        print_error "Carte SD non détectée automatiquement."
        echo ""
        print_info "Usage: $0 <chemin_carte_sd>"
        echo ""
        echo "Exemples:"
        echo "  $0 /media/sdcard"
        echo "  $0 /Volumes/SDCARD"
        echo "  $0 E:/ (sur Windows avec Git Bash)"
        exit 1
    fi

    # Afficher les candidats trouvés
    echo ""
    print_info "Cartes SD détectées:"
    echo "$SD_CANDIDATES" | nl
    echo ""

    # Si une seule carte, l'utiliser automatiquement
    COUNT=$(echo "$SD_CANDIDATES" | wc -l)
    if [ "$COUNT" -eq 1 ]; then
        SD_PATH=$(echo "$SD_CANDIDATES" | head -n 1)
        print_info "Utilisation automatique: $SD_PATH"
    else
        # Demander à l'utilisateur de choisir
        read -p "Numéro de la carte SD à utiliser: " CHOICE
        SD_PATH=$(echo "$SD_CANDIDATES" | sed -n "${CHOICE}p")

        if [ -z "$SD_PATH" ]; then
            print_error "Choix invalide."
            exit 1
        fi
    fi
fi

# Vérifier que le chemin existe
if [ ! -d "$SD_PATH" ]; then
    print_error "Chemin invalide: $SD_PATH"
    print_info "Assurez-vous que la carte SD est bien insérée et montée."
    exit 1
fi

# Afficher les informations
echo ""
print_info "Configuration:"
print_info "  Source: $MODEL_SOURCE"
print_info "  Destination: $SD_PATH/$MODEL_NAME"
print_info "  Taille: $MODEL_SIZE"
echo ""

# Vérifier l'espace disponible sur la SD
AVAILABLE_SPACE=$(df -h "$SD_PATH" | tail -n 1 | awk '{print $4}')
print_info "Espace disponible sur la carte SD: $AVAILABLE_SPACE"

# Vérifier si le fichier existe déjà
if [ -f "$SD_PATH/$MODEL_NAME" ]; then
    print_warning "Le fichier existe déjà sur la carte SD."

    # Comparer les checksums
    CHECKSUM_SOURCE=$(md5sum "$MODEL_SOURCE" | awk '{print $1}')
    CHECKSUM_DEST=$(md5sum "$SD_PATH/$MODEL_NAME" | awk '{print $1}')

    if [ "$CHECKSUM_SOURCE" = "$CHECKSUM_DEST" ]; then
        print_success "Le fichier sur la SD est identique au modèle source."
        echo ""
        print_info "Rien à faire! Le modèle est déjà à jour sur la carte SD."
        exit 0
    else
        print_warning "Le fichier sur la SD est différent du modèle source."
        read -p "Voulez-vous le remplacer? (y/N) " -n 1 -r
        echo ""

        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            print_info "Copie annulée."
            exit 0
        fi
    fi
fi

# Copier le fichier
print_info "Copie en cours..."
cp -v "$MODEL_SOURCE" "$SD_PATH/$MODEL_NAME"

# Vérifier que la copie a réussi
if [ -f "$SD_PATH/$MODEL_NAME" ]; then
    print_success "Modèle copié avec succès!"

    # Vérifier le checksum
    print_info "Vérification de l'intégrité..."
    CHECKSUM_SOURCE=$(md5sum "$MODEL_SOURCE" | awk '{print $1}')
    CHECKSUM_DEST=$(md5sum "$SD_PATH/$MODEL_NAME" | awk '{print $1}')

    if [ "$CHECKSUM_SOURCE" = "$CHECKSUM_DEST" ]; then
        print_success "Checksum vérifié: intégrité OK"
        print_success "MD5: $CHECKSUM_SOURCE"
    else
        print_error "Checksums différents! La copie peut être corrompue."
        print_error "Source: $CHECKSUM_SOURCE"
        print_error "Destination: $CHECKSUM_DEST"
        exit 1
    fi

    # Afficher la structure finale
    echo ""
    print_success "Structure de la carte SD:"
    echo "$SD_PATH/"
    echo "└── $MODEL_NAME ($(ls -lh "$SD_PATH/$MODEL_NAME" | awk '{print $5}'))"
    echo ""

    print_info "Prochaines étapes:"
    echo "  1. Éjectez la carte SD en toute sécurité"
    echo "  2. Insérez-la dans votre ESP32-P4"
    echo "  3. Modifiez votre fichier YAML (voir PATCH_YOLO_TO_SD.yaml)"
    echo "  4. Compilez avec: esphome compile votre_config.yaml"
    echo ""
    print_success "✅ Configuration SD Card prête!"
else
    print_error "La copie a échoué."
    exit 1
fi
