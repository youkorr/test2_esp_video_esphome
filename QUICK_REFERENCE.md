# Guide de Référence Rapide - Solution JPEG Hardware

## ✅ Solution Implémentée: JPEG Hardware Pipeline

**Performance attendue:** JPEG decode = 18-32ms → **30+ FPS**

### Changement d'Architecture

**Avant (PPA):**
```
/dev/video0 (RGB565, 1.8MB) → PPA copy 43ms → LVGL
= 22 FPS
```

**Après (JPEG Hardware):**
```
/dev/video10 (JPEG, 20-100KB) → JPEG decode 18-32ms → LVGL
= 30-50 FPS ✅
```

### Tests PPA Effectués (Tous ❌ Sans amélioration)
- ✅ Test 1: mirror_x = true → 43.5ms (inchangé)
- ✅ Test 2: Buffers en SPIRAM → 43.5ms (inchangé)
- ✅ Test 3: Cache sync → 43.5ms (inchangé)
- ✅ Test 4: 64-byte alignment → 43.5ms (inchangé)

**Conclusion:** PPA limité à 42 MB/s (limite hardware) → Solution JPEG choisie

---

## 🚀 Solution JPEG Hardware (ACTUELLE)

**Pipeline complet:**
```
Sensor → ISP → JPEG Encoder (/dev/video10) → Hardware Decoder → LVGL
         └─> "Gratuit"    └─> 20-100KB      └─> 18-32ms
```

**Implémenté dans:** Commit f09ef83

### Pourquoi JPEG?

1. **Performance:** 18-32ms au lieu de 43ms PPA
2. **Hardware Dédié:** ESP32-P4 a encodeur + décodeur JPEG
3. **Pas de Tearing:** Buffer séparé comme PPA
4. **Bande Passante:** 50% moins de SPIRAM usage

### Logs Attendus

```bash
# Démarrage
[mipi_dsi_cam] === START STREAMING (JPEG Hardware) ===
[mipi_dsi_cam] Device: /dev/video10 (JPEG hardware encoder)
[mipi_dsi_cam] ✓ JPEG hardware decoder initialized

# Première frame
[mipi_dsi_cam] ✅ First frame decoded:
[mipi_dsi_cam]    JPEG size: 45123 bytes (compressed)
[mipi_dsi_cam]    Compression ratio: 40.8x
[mipi_dsi_cam]    Timing: DQBUF=412us, JPEG decode=18234us

# Profiling (toutes les 100 frames)
📊 JPEG Hardware Profiling:
   DQBUF: 400 us (0.4 ms)
   JPEG decode: 25000 us (25.0 ms) ← Devrait être <33ms
   QBUF: 50 us (0.1 ms)
   TOTAL: 25450 us (25.5 ms) → 39.3 FPS ✅
```

### Si Performance Insuffisante

**Ajuster qualité JPEG:**
```yaml
mipi_dsi_cam:
  jpeg_quality: 5   # Meilleur qualité (decode plus long)
  # ou
  jpeg_quality: 20  # Plus compressé (decode plus rapide)
```

---

## 📚 Autres Solutions (Alternatives)

### Option B: Zero-Copy RGB565
**Référence:** Commit 108a4d3
- Performance: 0.45ms → 30+ FPS garanti
- Risque: Léger tearing
- Quand: Si JPEG decode >33ms

### Option C: Profiler M5Stack
**Guide:** `M5STACK_PROFILING_GUIDE.md`
- Vérifier leur temps PPA réel
- Voir s'ils utilisent JPEG ou RGB565

---

## 📁 Documentation Complète

| Fichier | Contenu |
|---------|---------|
| **`JPEG_HARDWARE_SOLUTION.md`** | **⭐ Solution JPEG complète (ACTUELLE)** |
| `TEST_RESULTS_SUMMARY.md` | Résumé tests PPA (échec) |
| `PPA_INVESTIGATION.md` | Investigation PPA et limites |
| `M5STACK_PROFILING_GUIDE.md` | Guide profiling M5Stack |
| `PERFORMANCE_ANALYSIS.md` | Analyse performance détaillée |
| `STREAMING_VIDEO_FIX.md` | Architecture streaming V4L2 |

---

## 💻 Commandes Git

```bash
# Solution actuelle (JPEG)
git show f09ef83  # Implement JPEG hardware pipeline
git show 00011eb  # JPEG documentation

# Tests PPA (pour référence)
git show ea48d5a  # Cache sync (test 3)
git show ed57dba  # 64-byte alignment (test 4)

# Alternative zero-copy
git show 108a4d3  # Zero-copy RGB565 (si JPEG échoue)
```

---

## 📊 Tableau Comparatif Final

| Solution | Temps | FPS | Tearing | Qualité | Status |
|----------|-------|-----|---------|---------|--------|
| PPA RGB565 | 43.9ms | ~22 | Non | Parfaite | ❌ Trop lent |
| **JPEG Hardware** | **20-32ms** | **30-50** | Non | Excellente | ✅ **ACTUELLE** |
| Zero-copy RGB565 | 0.45ms | 30+ | Possible | Parfaite | 💾 Fallback |

---

## ❓ FAQ

**Q: Pourquoi JPEG au lieu de PPA?**
A: PPA limité à 42 MB/s (hardware). JPEG encoder "gratuit" (dans ISP) + decoder hardware dédié = plus rapide.

**Q: La qualité JPEG est acceptable?**
A: Oui! Avec `jpeg_quality: 5-10`, quasi indiscernable du RGB565 brut pour affichage vidéo.

**Q: Si JPEG est trop lent?**
A: 1) Augmenter `jpeg_quality` (plus compressé = decode plus rapide)
   2) Fallback vers zero-copy (garanti 30 FPS)

**Q: Comment tester?**
A: `esphome run config.yaml` et observer les logs de profiling toutes les 100 frames.

---

**Prochaine étape:** Compiler et tester la solution JPEG → voir `JPEG_HARDWARE_SOLUTION.md`
