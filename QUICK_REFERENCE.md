# Guide de Référence Rapide - Investigation PPA

## 📊 Résultats des Tests

**Performance actuelle:** PPA = 43.5ms → ~22 FPS
**Cible:** 30 FPS (nécessite <33ms par frame)

### Tests Effectués (Tous ❌ Aucune amélioration)
- ✅ Test 1: mirror_x = true
- ✅ Test 2: Buffers en SPIRAM (optimal)
- ✅ Test 3: Cache sync (esp_cache_msync)
- ✅ Test 4: 64-byte alignment

**Conclusion:** 43ms semble être la limite hardware du PPA pour 1.8MB copy.

---

## 🎯 Options pour Atteindre 30 FPS

### Option A: Profiler M5Stack (Recommandé d'abord)
**Fichier:** `M5STACK_PROFILING_GUIDE.md`

**Résumé:**
```bash
# 1. Cloner M5Stack
git clone https://github.com/m5stack/M5Tab5-UserDemo.git

# 2. Modifier platforms/tab5/main/hal/components/hal_camera.cpp
#    Ajouter profiling autour de ppa_do_scale_rotate_mirror()

# 3. Compiler et tester
idf.py build flash monitor

# 4. Observer les logs:
#    Si PPA ~43ms → Pareil que nous → Zero-copy est la solution
#    Si PPA <20ms → Chercher la différence de config
```

---

### Option B: Zero-Copy (Solution garantie 30 FPS)
**Référence:** Commit 108a4d3

**Principe:**
```
LVGL lit directement les buffers V4L2 MMAP
→ Pas de copie PPA (0ms au lieu de 43ms)
→ 30+ FPS garanti
```

**Performance:**
```
Actuel:  DQBUF(0.4ms) + PPA(43.5ms) + QBUF(0.05ms) = 43.95ms → 22 FPS
Zero-copy: DQBUF(0.4ms) + QBUF(0.05ms) = 0.45ms → 30+ FPS ✓
```

**Risque:** Léger tearing possible (acceptable pour vidéo live)

---

### Option C: Réduire Résolution

**480P (640×480):**
- PPA estimé: ~14ms → ~28 FPS

**QVGA (320×240):**
- PPA estimé: ~3.6ms → 30+ FPS

---

## 📁 Documentation Complète

| Fichier | Contenu |
|---------|---------|
| `TEST_RESULTS_SUMMARY.md` | **📊 Résumé complet des tests et recommandations** |
| `M5STACK_PROFILING_GUIDE.md` | Guide pour profiler M5Stack Tab5 |
| `PPA_INVESTIGATION.md` | Hypothèses et tests détaillés |
| `PPA_OPTIMIZATION_TESTS.md` | Guide de test des optimisations |
| `PERFORMANCE_ANALYSIS.md` | Analyse de performance |
| `STREAMING_VIDEO_FIX.md` | Architecture du streaming vidéo |

---

## 🚀 Chemin Recommandé

```
1. Profiler M5Stack (1-2h)
   └─ Guide: M5STACK_PROFILING_GUIDE.md

2. Si M5Stack PPA = ~43ms:
   └─ Implémenter zero-copy
      └─ Performance garantie: 30+ FPS

3. Si M5Stack PPA = <20ms:
   └─ Analyser leur config et reproduire
```

---

## 💡 Commandes Git

```bash
# État actuel
git log --oneline -10

# Voir les changements
git show 561508b  # Test results summary
git show afb571b  # M5Stack profiling guide
git show ea48d5a  # Cache sync (test 3)
git show ed57dba  # 64-byte alignment (test 4)

# Revenir à zero-copy (si besoin)
git show 108a4d3  # Version zero-copy précédente
```

---

## 📞 Questions Fréquentes

**Q: Pourquoi le PPA est-il lent?**
A: SPIRAM bandwidth limité (~80-120 MB/s théorique). PPA fait READ+WRITE simultanés → ~40-50 MB/s max. Pour 1.8MB: minimum 36-45ms.

**Q: M5Stack est vraiment à 30 FPS?**
A: À vérifier! Profiler leur code pour confirmer. Peut-être qu'ils sont aussi à ~20 FPS.

**Q: Zero-copy a beaucoup de tearing?**
A: En pratique, minimal pour vidéo live. Buffer ping-pong réduit le risque.

**Q: Peut-on combiner PPA + zero-copy?**
A: Non, c'est l'un ou l'autre. PPA copie vers buffer séparé, zero-copy utilise buffers V4L2 directement.

---

**Prochaine étape recommandée:** Profiler M5Stack → voir `M5STACK_PROFILING_GUIDE.md`
