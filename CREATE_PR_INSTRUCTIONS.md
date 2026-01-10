# Instructions pour Créer la Pull Request

## 📋 Résumé

La branche `claude/check-lvgl-camera-isp-eZrQR` est prête et contient :
- ✅ Fix du tearing (image coupée en deux)
- ✅ Fix de 2 bugs critiques (corruption PPA, frames gelées)
- ✅ Documentation complète de vérification

## 🔗 Étape 1 : Ouvrir le Lien GitHub

Cliquez sur ce lien pour créer la PR :

**https://github.com/youkorr/test2_esp_video_esphome/pull/new/claude/check-lvgl-camera-isp-eZrQR**

## 📝 Étape 2 : Remplir le Formulaire PR

### Titre de la PR
```
Fix tearing issue in LVGL camera display + critical buffer bugs
```

### Description de la PR
Copiez-collez le contenu du fichier `PR_DESCRIPTION.md` (généré automatiquement)

Ou utilisez cette version courte :

```markdown
## Summary
Fixes tearing (image split in half) + 2 critical buffer bugs in LVGL camera display.

## Problems Fixed
1. **Tearing**: Buffer re-queued to V4L2 before LVGL finished displaying → Added pending queue
2. **PPA Corruption**: Reading wrong pointer (data instead of v4l2_data) → Fixed
3. **Frozen Frames**: data not restored when PPA disabled → Fixed

## Architecture Verified ✅
- ISP Processing (/dev/video20): AWB, CCM, AE, RAW→RGB565
- DMA Transfer (/dev/video0): V4L2 USERPTR zero-copy
- LVGL Rendering: lv_canvas_set_buffer() zero-copy

## Changes
- Added v4l2_data field + pending_release_buffers_[] queue
- Fixed capture_frame(): requeue pending BEFORE DQBUF, use v4l2_data, restore data
- Fixed release_buffer(): add to pending queue, validate index

## Testing
1. Without PPA: No tearing at 30 FPS
2. With PPA: Rotation works, no corruption
3. Toggle PPA: Image doesn't freeze
4. High FPS: No tearing at 50 FPS

See `VERIFICATION_TEARING_FIX.md` for complete details.

## Commits
- bd8f28e: Fix tearing (pending queue)
- 6be90f2: Fix PPA bugs (v4l2_data usage)
- 7ce2baa: Add verification doc
```

### Paramètres
- **Base branch**: `main` (ou votre branche principale)
- **Compare branch**: `claude/check-lvgl-camera-isp-eZrQR` (déjà sélectionné)
- **Reviewers**: (optionnel) Ajouter des reviewers si nécessaire
- **Labels**: (optionnel) `bug`, `enhancement`

## ✅ Étape 3 : Créer la PR

Cliquez sur **"Create Pull Request"**

## 📊 Commits Inclus

La PR contiendra ces 3 commits :

```
7ce2baa - Add comprehensive verification document for tearing fix
6be90f2 - Fix critical bugs in buffer pointer management (PPA compatibility)
bd8f28e - Fix tearing issue in LVGL camera display (image split in half)
```

## 📄 Fichiers Modifiés

```
components/esp_cam_sensor/esp_cam_sensor_camera.h     (+8 lines)
components/esp_cam_sensor/esp_cam_sensor_camera.cpp   (+89/-32 lines)
VERIFICATION_TEARING_FIX.md                           (+335 lines, new file)
```

## 🎯 Résultats Attendus

Après merge, le système devrait :
- ✅ Afficher la caméra sans tearing (image complète, pas coupée)
- ✅ Supporter PPA (rotation/mirror) sans corruption
- ✅ Permettre toggle PPA sans freeze
- ✅ Fonctionner à 30-50 FPS sans problème
- ✅ Supporter les overlays de détection

## 🔍 Review Points

Points à vérifier lors de la review :
1. Pending queue correctement implémentée (pas de double-free)
2. v4l2_data utilisé pour lecture DMA
3. data restauré avant override PPA
4. Thread-safety (spinlock) correcte
5. Pas de memory leaks
6. Validation index bounds

## 📞 Contact

En cas de question, se référer à `VERIFICATION_TEARING_FIX.md` pour :
- Détails techniques complets
- Scénarios de test (5 tests)
- Logs à surveiller
- Architecture ISP/DMA/LVGL

---

**Status**: ✅ Prêt pour création de PR
**Branch**: claude/check-lvgl-camera-isp-eZrQR
**Pushed**: ✅ Oui (remote tracking configuré)
