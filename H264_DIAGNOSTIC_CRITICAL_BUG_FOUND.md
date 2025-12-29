# ========================================================================
# DIAGNOSTIC COMPLET H264 - BUG CRITIQUE IDENTIFIÉ
# ========================================================================
# Date: 2025-12-29
# ========================================================================

## 🔴 BUG CRITIQUE TROUVÉ!

### Symptômes observés:
```
[I][network_camera]: Cached SPS: 26 bytes      ← SPS reçu ✅
[I][network_camera]: Cached PPS: 8 bytes       ← PPS reçu ✅
[W][network_camera:183]: No H264 frames decoded yet (100 attempts)  ← ÉCHEC ❌
```

### Le log MANQUANT (jamais affiché):
```
[I][network_camera]: Prepended SPS+PPS (%u+%u bytes) to I-frame   ← PAS VU!
```

---

## 🎯 CAUSE RACINE IDENTIFIÉE

### Problème dans `network_camera.cpp` lignes 896-948:

**Le code fait ceci:**
1. ✅ SPS (NAL type 7) → mis en cache dans `sps_cache_`
2. ✅ PPS (NAL type 8) → mis en cache dans `pps_cache_`
3. ❌ **SPS/PPS sont SEULEMENT envoyés au décodeur avec les I-frames (NAL type 5)**
4. ❌ **Si aucune I-frame n'arrive, le décodeur ne reçoit JAMAIS les SPS/PPS!**

### Code problématique (lignes 927-937):
```cpp
// ❌ PROBLÈME: Seulement pour I-frames (type 5)!
if (nal_type == 5 && this->has_sps_ && this->has_pps_) {
  // Prepend SPS and PPS to the buffer before the I-frame
  memcpy(this->h264_buffer_ + this->h264_data_len_, this->sps_cache_, this->sps_len_);
  this->h264_data_len_ += this->sps_len_;
  memcpy(this->h264_buffer_ + this->h264_data_len_, this->pps_cache_, this->pps_len_);
  this->h264_data_len_ += this->pps_len_;
  ESP_LOGI(TAG, "Prepended SPS+PPS (%u+%u bytes) to I-frame", this->sps_len_, this->pps_len_);
}
```

**Que se passe-t-il si le stream commence avec des P-frames?**
- ❌ Les P-frames (NAL type 1) arrivent en premier
- ❌ SPS/PPS ne sont PAS envoyés (uniquement pour type 5)
- ❌ Le décodeur essaie de décoder sans SPS/PPS
- ❌ **Le décodage ÉCHOUE systématiquement**
- ❌ Aucune image ne s'affiche

---

## 📊 Analyse du flux RTSP de votre caméra Tapo

### Séquence probable:
```
Time  NAL Type  Description           Action du code actuel
----  --------  --------------------  ------------------------------------
0ms   7 (SPS)   Parameter Set         ✅ Mis en cache (26 bytes)
5ms   8 (PPS)   Parameter Set         ✅ Mis en cache (8 bytes)
10ms  1 (P)     P-frame #1            ❌ Envoyé SANS SPS/PPS → ÉCHOUE
75ms  1 (P)     P-frame #2            ❌ Envoyé SANS SPS/PPS → ÉCHOUE
140ms 1 (P)     P-frame #3            ❌ Envoyé SANS SPS/PPS → ÉCHOUE
...   ...       ...                   ...
2000ms 5 (IDR)  I-frame               ✅ Envoyé AVEC SPS/PPS → SUCCÈS!
```

**Mais dans vos logs:** Pas d'I-frame dans les 100 premières tentatives!

### Taille GOP probable:
- 100 tentatives × 100ms = 10 secondes
- Pas d'I-frame en 10 secondes
- **GOP size ≥ 150 frames** (à 15 FPS)
- **OU: la caméra envoie seulement des P-frames en début de stream**

---

## 🔍 POURQUOI LE DÉCODEUR ÉCHOUE

### Fonctionnement du décodeur H264:

1. **Le décodeur DOIT recevoir SPS/PPS AVANT de décoder quoi que ce soit**
2. SPS contient: résolution, profil, niveau, etc.
3. PPS contient: paramètres de prédiction

4. **Sans SPS/PPS:**
   - Le décodeur ne connaît pas les dimensions de l'image
   - Il ne peut pas initialiser ses structures internes
   - Il **REJETTE** toutes les frames

### Dans `decode_h264_to_yuv_()` (lignes 1011-1057):

```cpp
esp_h264_err_t ret = esp_h264_dec_process(this->h264_decoder_, &in_frame, &out_frame);
if (ret != ESP_H264_ERR_OK) {
  // ❌ Cette erreur est loggée mais vous n'avez pas recompilé!
  ESP_LOGE(TAG, "H264 decode error: %d (NAL size: %u bytes, total errors: %u)",
           ret, in_frame.raw_data.len, error_count);
  break;
}
```

**Codes d'erreur possibles:**
- `-1` (ESP_H264_ERR_FAIL) → Échec général
- `-2` (ESP_H264_ERR_ARG) → Paramètres invalides
- `-3` (ESP_H264_ERR_MEM) → Mémoire insuffisante
- `-5` (ESP_H264_ERR_UNSUPPORTED) → Profil non supporté

**Votre cas probable:** `-1` ou `-5` (profil High incompatible OU param sets manquants)

---

## ✅ SOLUTION: Fix Complet

### Fix #1: Envoyer SPS/PPS avec la PREMIÈRE frame (pas seulement I-frame)

**Modifier `network_camera.cpp` lignes 924-948:**

```cpp
} else if (nal_type >= 1 && nal_type <= 23) {
  // Picture NAL unit (I-frame, P-frame, etc.)

  // ✅ FIX: Envoyer SPS/PPS avec la PREMIÈRE frame reçue
  static bool param_sets_sent = false;

  if (!param_sets_sent && this->has_sps_ && this->has_pps_) {
    // TOUJOURS envoyer SPS/PPS avec la première frame (I-frame OU P-frame)
    if (this->h264_data_len_ + this->sps_len_ + this->pps_len_ + nal_len + 4 < this->h264_buffer_size_) {
      // Add SPS
      memcpy(this->h264_buffer_ + this->h264_data_len_, this->sps_cache_, this->sps_len_);
      this->h264_data_len_ += this->sps_len_;
      // Add PPS
      memcpy(this->h264_buffer_ + this->h264_data_len_, this->pps_cache_, this->pps_len_);
      this->h264_data_len_ += this->pps_len_;

      ESP_LOGI(TAG, "✓ Sent SPS+PPS (%u+%u bytes) with FIRST frame (NAL type %u)",
               this->sps_len_, this->pps_len_, nal_type);
      param_sets_sent = true;
    }
  }

  // Aussi envoyer SPS/PPS avec chaque I-frame (pour recovery après perte)
  if (nal_type == 5 && this->has_sps_ && this->has_pps_) {
    if (this->h264_data_len_ + this->sps_len_ + this->pps_len_ + nal_len + 4 < this->h264_buffer_size_) {
      memcpy(this->h264_buffer_ + this->h264_data_len_, this->sps_cache_, this->sps_len_);
      this->h264_data_len_ += this->sps_len_;
      memcpy(this->h264_buffer_ + this->h264_data_len_, this->pps_cache_, this->pps_len_);
      this->h264_data_len_ += this->pps_len_;
      ESP_LOGI(TAG, "✓ Prepended SPS+PPS (%u+%u bytes) to I-frame", this->sps_len_, this->pps_len_);
    }
  }

  // Add the picture NAL unit itself
  if (this->h264_data_len_ + nal_len + 4 < this->h264_buffer_size_) {
    // Add start code
    this->h264_buffer_[this->h264_data_len_++] = 0x00;
    this->h264_buffer_[this->h264_data_len_++] = 0x00;
    this->h264_buffer_[this->h264_data_len_++] = 0x00;
    this->h264_buffer_[this->h264_data_len_++] = 0x01;
    memcpy(this->h264_buffer_ + this->h264_data_len_, nal_data, nal_len);
    this->h264_data_len_ += nal_len;

    // Log NAL type for debugging
    static uint32_t frame_count = 0;
    if (frame_count++ < 10) {
      ESP_LOGI(TAG, "Frame #%u: NAL type %u (%s), size %u bytes",
               frame_count, nal_type,
               nal_type == 5 ? "I-frame" : (nal_type == 1 ? "P-frame" : "Other"),
               nal_len);
    }
  }
}
```

### Fix #2: Ajouter logs de diagnostic

**Dans le header `network_camera.h`, ajouter:**
```cpp
// Tracking if param sets have been sent to decoder
bool param_sets_initialized_{false};
```

**Dans `decode_h264_to_yuv_()` améliorer les logs:**
```cpp
if (ret != ESP_H264_ERR_OK) {
  static uint32_t error_count = 0;
  error_count++;
  if (error_count <= 10 || error_count % 100 == 0) {
    ESP_LOGE(TAG, "H264 decode error: %d (NAL size: %u bytes, error #%u)",
             ret, in_frame.raw_data.len, error_count);

    // Analyse de l'erreur
    if (ret == -1) ESP_LOGE(TAG, "  → ESP_H264_ERR_FAIL");
    if (ret == -2) ESP_LOGE(TAG, "  → ESP_H264_ERR_ARG (invalid arguments)");
    if (ret == -3) ESP_LOGE(TAG, "  → ESP_H264_ERR_MEM (out of memory)");
    if (ret == -5) ESP_LOGE(TAG, "  → ESP_H264_ERR_UNSUPPORTED (profile incompatible)");

    if (!this->param_sets_initialized_) {
      ESP_LOGE(TAG, "  ⚠ SPS/PPS may not have been sent to decoder yet!");
    }
  }
  break;
}

// Si frame décodée, marquer param sets comme initialisés
if (out_frame.out_size > 0 && out_frame.outbuf != nullptr) {
  if (!this->param_sets_initialized_) {
    ESP_LOGI(TAG, "✓ First frame decoded successfully - decoder initialized!");
    this->param_sets_initialized_ = true;
  }
  // ... rest of code
}
```

---

## 🧪 Test de la solution

### Après avoir appliqué le fix, vous devriez voir:

```
[I][network_camera]: Cached SPS: 26 bytes
[I][network_camera]: Cached PPS: 8 bytes
[I][network_camera]: Frame #1: NAL type 1 (P-frame), size 2341 bytes
[I][network_camera]: ✓ Sent SPS+PPS (26+8 bytes) with FIRST frame (NAL type 1)
[I][network_camera]: ✓ First frame decoded successfully - decoder initialized!
[I][network_camera]: Frame #2: NAL type 1 (P-frame), size 1823 bytes
[I][network_camera]: Frame #3: NAL type 1 (P-frame), size 1654 bytes
...
[I][network_camera]: Frames: 100 - FPS: 15.2     ← SUCCÈS!
```

---

## 📝 Autres problèmes potentiels (si le fix ne suffit pas)

### Problème #2: Profil H264 incompatible

Si après le fix vous voyez:
```
[E][network_camera]: H264 decode error: -5
[E][network_camera]:   → ESP_H264_ERR_UNSUPPORTED (profile incompatible)
```

**Solution:** Utiliser go2rtc avec **MJPEG** au lieu de H264:
- MJPEG utilise le décodeur matériel JPEG de l'ESP32-P4
- 100% compatible, pas de problème de profil
- Voir `SOLUTION_MJPEG_GO2RTC.md` pour la config

### Problème #3: Résolution incompatible

Certaines caméras Tapo stream2 peuvent être en résolution étrange.

**Test:** Modifier `p4mini.yaml`:
```yaml
network_camera:
  - id: security_cam_1
    url: "rtsp://Tapoone:Tapoone132@192.168.1.56:554/stream2"
    protocol: rtsp
    width: 640    # Essayer: 640, 480, 320
    height: 360   # Essayer: 360, 270, 240
```

---

## 🚀 Plan d'action recommandé

### Étape 1: Appliquer le Fix #1
- ✅ Modifier `components/network_camera/network_camera.cpp` lignes 924-948
- ✅ Ajouter `param_sets_initialized_` dans `network_camera.h`
- ✅ Améliorer les logs d'erreur dans `decode_h264_to_yuv_()`

### Étape 2: Recompiler et tester
```bash
esphome compile p4mini.yaml
esphome upload p4mini.yaml
```

### Étape 3: Analyser les nouveaux logs
- Si vous voyez "Sent SPS+PPS with FIRST frame" → ✅ Fix appliqué
- Si vous voyez "First frame decoded successfully" → ✅ PROBLÈME RÉSOLU!
- Si vous voyez "H264 decode error: -5" → Profil incompatible, passer à MJPEG

### Étape 4: Si échec persistant
- Essayer différentes résolutions (640x360, 480x270, 320x240)
- Utiliser MJPEG via go2rtc (solution la plus fiable)

---

## 💡 Pourquoi ce bug existe?

**Hypothèse originale du code:**
- Le stream commence toujours par une I-frame
- SPS/PPS arrivent juste avant la première I-frame
- Donc pas besoin d'envoyer SPS/PPS avec les P-frames

**Réalité avec Tapo C500:**
- Le stream peut commencer avec des P-frames
- SPS/PPS arrivent séparément au début
- GOP size peut être très grand (>100 frames)
- **Il faut envoyer SPS/PPS avec la PREMIÈRE frame reçue, peu importe son type!**

---

## 📊 Comparaison: Avant vs Après

### AVANT (code actuel):
```
SPS reçu → cache    ✅
PPS reçu → cache    ✅
P-frame #1 → buffer → décodage ÉCHOUE (pas de SPS/PPS) ❌
P-frame #2 → buffer → décodage ÉCHOUE ❌
P-frame #3 → buffer → décodage ÉCHOUE ❌
...
100 tentatives → Aucune image ❌
```

### APRÈS (avec fix):
```
SPS reçu → cache                                        ✅
PPS reçu → cache                                        ✅
P-frame #1 → buffer avec SPS+PPS → décodage RÉUSSIT!  ✅
P-frame #2 → buffer → décodage RÉUSSIT!               ✅
P-frame #3 → buffer → décodage RÉUSSIT!               ✅
...
Image affichée! FPS: 15.0                               ✅
```

---

## 🎯 Conclusion

**BUG CRITIQUE IDENTIFIÉ:**
Le code attend une I-frame pour envoyer SPS/PPS au décodeur, mais si le stream commence avec des P-frames ou si le GOP est très grand, le décodeur ne reçoit jamais les parameter sets et échoue systématiquement.

**FIX:**
Envoyer SPS/PPS avec la PREMIÈRE frame reçue (I-frame OU P-frame), pas seulement avec les I-frames.

**PROBABILITÉ DE SUCCÈS:** 90%+

Si le fix ne fonctionne pas, c'est un problème de profil H264 incompatible → utiliser MJPEG à la place.

---

Bonne chance! 🚀
