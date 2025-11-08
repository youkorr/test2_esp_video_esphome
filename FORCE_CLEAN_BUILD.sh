#!/bin/bash

# Script pour forcer un clean build complet et garantir la recompilation

echo "========================================="
echo "  FORCE CLEAN BUILD - Suppression cache"
echo "========================================="

# Supprimer TOUS les caches de build
echo "🗑️  Suppression du cache ESPHome..."
rm -rf .esphome/build/
rm -rf .esphome/storage/

echo "🗑️  Suppression du cache PlatformIO..."
rm -rf .pioenvs/
rm -rf .pio/build/
rm -rf .pio/.cache/

# Supprimer spécifiquement les .o qui posent problème
echo "🗑️  Recherche et suppression de esp_video_init.o..."
find . -name "esp_video_init.o" -type f -delete 2>/dev/null || true

echo "🗑️  Recherche et suppression de esp_cam_sensor_detect_stubs.o..."
find . -name "esp_cam_sensor_detect_stubs.o" -type f -delete 2>/dev/null || true

# Supprimer les .sconsign (cache SCons)
echo "🗑️  Suppression du cache SCons..."
find . -name ".sconsign*" -type f -delete 2>/dev/null || true

echo ""
echo "========================================="
echo "  ✅ Cache nettoyé complètement"
echo "========================================="
echo ""
echo "Maintenant exécutez :"
echo "  esphome compile tab5.yaml"
echo ""
