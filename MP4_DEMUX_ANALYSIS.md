# Analyse : Ce Dont Linux/ESP32-P4 A Besoin pour Lire un MP4

## 🎯 La Question Clé

**Question** : "De quoi Linux a besoin pour faire fonctionner un MP4 ?"

**Réponse** : Plusieurs couches sont nécessaires pour lire un fichier MP4.

## 📦 Architecture de Lecture MP4

### Sur Linux (FFmpeg/VLC)

```
┌─────────────────────────────────────────────┐
│  Fichier MP4 sur Disque                     │
│  /home/user/video.mp4                       │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Système de Fichiers (ext4, NTFS, etc.)    │
│  fopen(), fread(), fseek()                  │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Démultiplexeur MP4 (libavformat)          │
│  - Parser container MP4                     │
│  - Lire boxes: ftyp, moov, mdat            │
│  - Extraire metadata (résolution, FPS)     │
│  - Construire sample tables (stco, stsz)   │
│  - Extraire NAL units H.264                │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Décodeur H.264 (libavcodec)               │
│  - Décoder NAL units → YUV frames          │
│  - Hardware (VAAPI, VDPAU) ou Software     │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Conversion Colorspace                      │
│  - YUV420 → RGB                             │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Affichage (X11, Wayland, framebuffer)     │
│  - Rendu à l'écran                          │
└─────────────────────────────────────────────┘
```

### Sur ESP32-P4 avec `simple_video_player` ✅

```
┌─────────────────────────────────────────────┐
│  Fichier MP4 sur SD Card                    │
│  /sdcard/video.mp4                          │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Système de Fichiers FAT32                  │
│  fopen(), fread(), fseek()                  │
│  ✅ ESP32-P4 supporte                       │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Démultiplexeur MP4 (simple_video_player)  │
│  ✅ PRÉSENT !                               │
│  - parse_mp4_()                             │
│  - read_mp4_box_()                          │
│  - parse_moov_(), parse_stbl_()            │
│  - parse_stco_(), parse_stsz_()            │
│  - Extraction complète NAL units            │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Décodeur H.264 (tinyh264 ou OpenH264)     │
│  ✅ PRÉSENT !                               │
│  - Software decode: 80-150ms par frame      │
│  - Baseline: tinyh264                       │
│  - High Profile: OpenH264 (mon commit)      │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Conversion YUV420 → RGB565                 │
│  ✅ PRÉSENT !                               │
│  - Software: 25-35ms                        │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Affichage LVGL                             │
│  ✅ PRÉSENT !                               │
│  - lv_canvas_set_buffer()                   │
└─────────────────────────────────────────────┘
```

### Sur ESP32-P4 avec `network_camera` ❌

```
┌─────────────────────────────────────────────┐
│  Stream Réseau RTSP                         │
│  rtsp://192.168.1.100:8554/stream           │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Client RTSP/RTP                            │
│  ✅ PRÉSENT dans network_camera             │
│  - Reçoit H.264 NAL units via RTP           │
│  - PAS de démultiplexeur MP4 !              │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Décodeur H.264                             │
│  ✅ PRÉSENT !                               │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  Affichage LVGL                             │
│  ✅ PRÉSENT !                               │
└─────────────────────────────────────────────┘
```

## 🔍 La Différence Clé

### Fichier MP4 Local vs Stream RTSP

| Aspect | MP4 File | RTSP Stream |
|--------|----------|-------------|
| **Container** | MP4 (MPEG-4 Part 14) | RTP (packetisation directe) |
| **Démultiplexeur** | ✅ **REQUIS** | ❌ Pas besoin (déjà extrait) |
| **Metadata** | Dans moov box | Envoyé via SDP |
| **Seek** | Possible (stco table) | Non (stream continu) |
| **Composant ESPHome** | `simple_video_player` | `network_camera` |

## 📂 Structure d'un Fichier MP4

```
video.mp4
├─ ftyp (File Type Box)
│  └─ Brand: isom, mp41, mp42
├─ free (Free Space)
├─ mdat (Media Data) ← Données vidéo/audio RAW
│  ├─ Video sample 1 (NAL units H.264)
│  ├─ Video sample 2
│  ├─ ...
│  └─ Audio samples (AAC)
└─ moov (Movie Metadata) ← Tables d'index
   ├─ mvhd (Movie Header)
   ├─ trak (Video Track)
   │  ├─ tkhd (Track Header)
   │  └─ mdia (Media)
   │     ├─ mdhd (Media Header - timescale)
   │     └─ minf (Media Info)
   │        └─ stbl (Sample Table) ← INDEX CRITIQUE
   │           ├─ stsd (Sample Description - codec)
   │           │  └─ avc1 (H.264)
   │           │     └─ avcC (H.264 config - SPS/PPS)
   │           ├─ stts (Time-to-Sample - durées)
   │           ├─ stsc (Sample-to-Chunk - mapping)
   │           ├─ stsz (Sample Sizes - tailles)
   │           ├─ stco (Chunk Offsets - positions mdat)
   │           └─ stss (Sync Samples - I-frames)
   └─ trak (Audio Track)
      └─ ...
```

## ✅ Ce Que `simple_video_player` Fait

### Parsing Complet du Container MP4

Voici les fonctions de démultiplexage présentes dans `simple_video_player.cpp` :

```cpp
// Ligne 1902-1945 : Parsing principal
bool SimpleVideoPlayer::parse_mp4_() {
  // 1. Chercher la box 'moov'
  while (!feof()) {
    uint32_t size, type;
    read_mp4_box_(size, type);

    if (type == 'moov') {
      parse_moov_(size - 8);  // ← Parse metadata
      found_moov = true;
    }
  }

  // 2. Vérifier qu'on a des samples vidéo
  if (video_samples_.empty()) {
    ESP_LOGE("No video samples found!");
    return false;
  }

  return true;
}

// Ligne 1947-1958 : Lecture d'une box MP4
bool SimpleVideoPlayer::read_mp4_box_(uint32_t &size, uint32_t &type) {
  size = read_be32();  // 4 bytes big-endian
  type = read_be32();  // 4 bytes FourCC
  return (size > 0);
}

// Ligne 1960-1980 : Parse moov (metadata)
bool SimpleVideoPlayer::parse_moov_(uint32_t size) {
  // Chercher boxes 'trak' (tracks vidéo/audio)
  while (not at end) {
    read_mp4_box_(box_size, box_type);

    if (box_type == 'trak') {
      parse_trak_(box_size - 8, true);  // Parse track
    }
  }
}

// Ligne 2050-2120 : Parse stbl (sample table)
bool SimpleVideoPlayer::parse_stbl_(uint32_t size, bool is_video) {
  // Parse toutes les tables d'index:

  if (box_type == 'stsd') parse_stsd_();  // Sample description (codec)
  if (box_type == 'stts') parse_stts_();  // Time-to-sample
  if (box_type == 'stsc') parse_stsc_();  // Sample-to-chunk
  if (box_type == 'stsz') parse_stsz_();  // Sample sizes
  if (box_type == 'stco') parse_stco_();  // Chunk offsets
  if (box_type == 'stss') parse_stss_();  // Sync samples (I-frames)
}

// Ligne 2250-2290 : Parse stco (chunk offsets)
bool SimpleVideoPlayer::parse_stco_(uint32_t size, bool is_video) {
  uint32_t entry_count = read_be32();

  for (uint32_t i = 0; i < entry_count; i++) {
    uint32_t chunk_offset = read_be32();  // Position dans mdat

    if (is_video) {
      video_chunk_offsets_.push_back(chunk_offset);
    } else {
      audio_chunk_offsets_.push_back(chunk_offset);
    }
  }
}

// Ligne 2220-2245 : Parse stsz (sample sizes)
bool SimpleVideoPlayer::parse_stsz_(uint32_t size, bool is_video) {
  uint32_t sample_size = read_be32();  // Taille fixe (0 = variable)
  uint32_t sample_count = read_be32();

  for (uint32_t i = 0; i < sample_count; i++) {
    uint32_t size = (sample_size == 0) ? read_be32() : sample_size;

    if (is_video) {
      video_sample_sizes_.push_back(size);
    } else {
      audio_sample_sizes_.push_back(size);
    }
  }
}

// Ligne 2300-2350 : Lecture d'un sample vidéo
bool SimpleVideoPlayer::read_next_mp4_sample_() {
  // 1. Récupérer position et taille depuis tables
  uint32_t chunk_offset = video_chunk_offsets_[chunk_index];
  uint32_t sample_size = video_sample_sizes_[sample_index];

  // 2. Seek vers position dans fichier
  fseek(file_, chunk_offset, SEEK_SET);

  // 3. Lire les NAL units H.264
  fread(h264_buffer_, 1, sample_size, file_);

  // 4. Préparer pour décodage
  h264_data_len_ = sample_size;

  return true;
}
```

### Résultat

`simple_video_player` construit des **tables d'index complètes** :

```cpp
std::vector<uint32_t> video_chunk_offsets_;   // Positions mdat
std::vector<uint32_t> video_sample_sizes_;    // Tailles NAL units
std::vector<uint32_t> video_sample_to_chunk_; // Mapping samples→chunks
std::vector<uint32_t> video_time_to_sample_;  // Durées frames

// Exemple après parsing d'un MP4 de 300 frames:
// video_chunk_offsets_  = {4096, 8192, 12288, ...}  ← 300 positions
// video_sample_sizes_   = {8502, 245, 312, 189, ...} ← 300 tailles
// video_time_to_sample_ = {1001, 1001, 1001, ...}   ← 300 durées
```

## ❌ Ce Que `network_camera` NE Fait PAS

`network_camera` est conçu pour des **streams réseau**, pas des fichiers locaux :

### Protocoles Supportés

```cpp
// network_camera.h ligne 23-27
enum class Protocol {
  MJPEG,  // HTTP MJPEG stream (multipart/x-mixed-replace)
  RTSP    // RTSP/RTP stream (H.264 NAL units)
};
```

### Pas de Démultiplexeur MP4

```cpp
// network_camera.cpp - fetch_rtp_frame_()
// Cette fonction reçoit des PAQUETS RTP contenant directement
// des NAL units H.264, PAS un container MP4 !

bool NetworkCamera::fetch_rtp_frame_() {
  // 1. Recevoir paquet RTP depuis socket
  int len = recv(rtsp_socket_, rtp_buffer_, RTP_BUFFER_SIZE, 0);

  // 2. Parser header RTP (12 bytes)
  uint8_t payload_type = rtp_buffer_[1] & 0x7F;

  // 3. Extraire NAL unit H.264 DIRECTEMENT (pas de MP4!)
  uint8_t *nal_data = rtp_buffer_ + 12;  // Skip RTP header

  // 4. Décoder NAL unit
  decode_h264_to_yuv_(nal_data, nal_size);

  // PAS de parsing MP4 - les NAL units arrivent déjà extraites!
}
```

### Différence RTSP vs MP4

| Aspect | RTSP Stream | MP4 File |
|--------|-------------|----------|
| **Transport** | RTP packets (UDP/TCP) | Fichier sur disque |
| **Format** | NAL units directes | Container MP4 |
| **Metadata** | SDP (Session Description) | moov box |
| **Index** | Pas d'index (stream) | stco, stsz, stsc tables |
| **SPS/PPS** | Envoyé au début | Dans avcC box |
| **Seek** | ❌ Impossible | ✅ Possible (random access) |

## 🎯 Pourquoi C'est Important

### Votre Situation

Vous mentionnez que :
1. ESP-IDF fonctionne bien avec MP4 Baseline
2. ESPHome est "coincé quelque part" à 7-8 FPS

**Hypothèse** : Vous utilisez peut-être les **mauvais composants** :

- ✅ **Pour fichiers MP4 locaux** (SD card) : Utilisez `simple_video_player`
  - A un démultiplexeur MP4 complet
  - Lit depuis SD card via `fopen()`
  - Peut faire seek, pause, etc.

- ✅ **Pour streams réseau** (caméra IP) : Utilisez `network_camera`
  - Reçoit H.264 via RTSP/RTP
  - Pas de démultiplexeur MP4 (pas nécessaire)
  - Optimisé pour streaming

- ❌ **NE PAS UTILISER** `network_camera` pour lire des fichiers MP4 locaux
  - Pas de support pour parsing MP4
  - Conçu pour streams, pas fichiers

## 🔧 Vérification

### Question Clé

**Quel est votre cas d'usage exact ?**

**Cas 1** : Lire un fichier MP4 depuis SD card
```yaml
# ESPHome - Utilisez simple_video_player
simple_video_player:
  id: player
  file: "/sdcard/video.mp4"  # ✅ Fichier local
  width: 640
  height: 480
```
→ `simple_video_player` a le démultiplexeur MP4 nécessaire

**Cas 2** : Recevoir stream RTSP depuis caméra IP
```yaml
# ESPHome - Utilisez network_camera
network_camera:
  id: cam
  url: "rtsp://192.168.1.100:8554/stream"  # ✅ Stream réseau
  protocol: rtsp
```
→ `network_camera` reçoit H.264 déjà extrait via RTP

**Cas 3** : Lire MP4 depuis serveur HTTP
```yaml
# ESPHome - Utilisez simple_video_player avec HTTP
simple_video_player:
  id: player
  url: "http://192.168.1.100/video.mp4"  # ✅ HTTP download puis parsing
```
→ `simple_video_player` télécharge puis parse MP4

## 📊 Comparaison Performance

### `simple_video_player` avec MP4 Local (SD Card)

```
Temps par frame:
- SD card read:        5-15ms   (dépend de vitesse SD card)
- MP4 demux:           2-5ms    (lookup tables + fseek)
- H.264 decode:        80-150ms (software - tinyh264/OpenH264)
- YUV→RGB conversion:  25-35ms  (software)
- LVGL display:        10-30ms  (dépend de résolution)
────────────────────────────────
TOTAL:                 122-245ms per frame
FPS:                   4-8 FPS  ← Correspond à vos 7-8 FPS !
```

### `simple_video_player` avec MJPEG Local (SD Card)

```
Temps par frame:
- SD card read:        5-15ms
- MJPEG demux:         1-2ms    (chercher FFD8/FFD9)
- JPEG decode:         18-32ms  (HARDWARE - très rapide!)
- Pas de conversion:   0ms      (direct RGB565)
- LVGL display:        10-30ms
────────────────────────────────
TOTAL:                 34-79ms per frame
FPS:                   12-30 FPS  ← 3-4x plus rapide!
```

### `network_camera` avec RTSP Stream

```
Temps par frame:
- Network receive:     10-50ms  (dépend du WiFi)
- RTP parsing:         <1ms     (juste skip header)
- H.264 decode:        80-150ms (software)
- YUV→RGB conversion:  25-35ms  (software)
- LVGL display:        10-30ms
────────────────────────────────
TOTAL:                 125-265ms per frame
FPS:                   4-8 FPS (avec limites 5/10/15 pour éviter crashes)
```

## ✅ Conclusion

### Ce Dont On A Besoin pour MP4

1. ✅ **Système de fichiers** (FAT32) - ESP32-P4 l'a
2. ✅ **Démultiplexeur MP4** - `simple_video_player` l'a
3. ✅ **Décodeur H.264** - tinyh264/OpenH264 (mon commit)
4. ✅ **Mémoire SPIRAM** - ESP32-P4 l'a (2-8MB)
5. ✅ **Conversion YUV→RGB** - Software dans ESP32-P4
6. ✅ **Display LVGL** - ESPHome l'a

**Tout est présent !** Le problème est probablement l'architecture synchrone qui cause des watchdog timeouts à hauts FPS.

### Recommandation

1. **Pour MP4 locaux** : Utilisez `simple_video_player` (pas `network_camera`)
2. **Pour streams RTSP** : Utilisez `network_camera`
3. **Pour meilleures performances** : Convertir MP4 → MJPEG (utilise JPEG hardware)

### Prochaine Étape

Pouvez-vous clarifier :
- Utilisez-vous `simple_video_player` ou `network_camera` ?
- Lisez-vous des fichiers MP4 locaux (SD card) ou des streams RTSP ?
- Quelle est votre configuration ESPHome exacte ?

Cela m'aidera à mieux comprendre où se trouve le vrai problème de performance.
