#!/bin/bash
#
# Download and Pack ESP-DL Face Detection Models
# Downloads models from esp-dl repository and optionally packs them into single file
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODELS_DIR="$SCRIPT_DIR"

ESPDL_VERSION="v3.1.0"
ESPDL_BASE_URL="https://github.com/espressif/esp-dl/raw/${ESPDL_VERSION}/models/human_face_detect"

MSR_MODEL="human_face_detect_msr_s8_v1.espdl"
MNP_MODEL="human_face_detect_mnp_s8_v1.espdl"

echo "============================================================"
echo "ESP-DL Face Detection Model Downloader"
echo "============================================================"
echo "Target directory: $MODELS_DIR"
echo "ESP-DL version: $ESPDL_VERSION"
echo ""

# Download MSR model
echo "[1/2] Downloading MSR model..."
if [ -f "$MODELS_DIR/$MSR_MODEL" ]; then
    echo "  ⚠️  $MSR_MODEL already exists, skipping"
else
    wget -q --show-progress "$ESPDL_BASE_URL/$MSR_MODEL" -O "$MODELS_DIR/$MSR_MODEL"
    echo "  ✅ Downloaded: $MSR_MODEL"
fi

# Download MNP model
echo "[2/2] Downloading MNP model..."
if [ -f "$MODELS_DIR/$MNP_MODEL" ]; then
    echo "  ⚠️  $MNP_MODEL already exists, skipping"
else
    wget -q --show-progress "$ESPDL_BASE_URL/$MNP_MODEL" -O "$MODELS_DIR/$MNP_MODEL"
    echo "  ✅ Downloaded: $MNP_MODEL"
fi

echo ""
echo "✅ Models downloaded successfully!"
echo ""

# Ask if user wants to pack models
read -p "Do you want to pack models into a single file? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "Packing models..."
    python3 "$SCRIPT_DIR/pack_model.py" \
        -m "$MODELS_DIR/$MSR_MODEL" "$MODELS_DIR/$MNP_MODEL" \
        -o "$MODELS_DIR/human_face_detect.espdl"
    echo ""
    echo "============================================================"
    echo "📦 PACKED MODEL CREATED"
    echo "============================================================"
    echo "Single file: human_face_detect.espdl"
    echo ""
    echo "Copy to SD card root:"
    echo "  cp $MODELS_DIR/human_face_detect.espdl /path/to/sdcard/"
    echo ""
else
    echo ""
    echo "============================================================"
    echo "📁 SEPARATE MODELS READY"
    echo "============================================================"
    echo "Two files:"
    echo "  - $MSR_MODEL"
    echo "  - $MNP_MODEL"
    echo ""
    echo "Copy both to SD card root:"
    echo "  cp $MODELS_DIR/*.espdl /path/to/sdcard/"
    echo ""
fi

echo "Models are ready for use with human_face_detect component!"
echo "============================================================"
