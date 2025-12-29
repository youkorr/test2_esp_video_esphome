#!/bin/bash
# Script pour diagnostiquer le flux RTSP de votre caméra Tapo
# Ce script affiche les informations détaillées sur le codec H264

echo "=========================================="
echo "Diagnostic du flux RTSP Tapo Camera"
echo "=========================================="

RTSP_URL="rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream1"

echo ""
echo "Analysing stream1 (haute qualité)..."
ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,profile,level,width,height,bit_rate -of default=noprint_wrappers=1 "$RTSP_URL" 2>&1

echo ""
echo "=========================================="
echo ""

RTSP_URL2="rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream2"

echo "Analysing stream2 (qualité moyenne)..."
ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,profile,level,width,height,bit_rate -of default=noprint_wrappers=1 "$RTSP_URL2" 2>&1

echo ""
echo "=========================================="
echo "Profils H264 supportés par ESP32-P4:"
echo "  ✅ Baseline Profile (66)"
echo "  ✅ Main Profile (77)"
echo "  ✅ High Profile (100) - parfois"
echo "  ❌ High 10 Profile"
echo "=========================================="
echo ""
echo "Recommandation:"
echo "Si stream1 = High Profile → Utilisez stream2"
echo "Si stream2 = Baseline/Main → Utilisez stream2"
echo "=========================================="
