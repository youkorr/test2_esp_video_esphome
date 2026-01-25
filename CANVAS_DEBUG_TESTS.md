# Tests de Debug Canvas LVGL 9.4 - ESP32-P4

## Problème Identifié

Le crash `Guru Meditation Error 0xf003e007` se produit avec le canvas 640x480 mais pas sans canvas. Cela suggère que le problème vient du **canvas lui-même** et non du timing de configuration.

## Hypothèses à Tester

1. **Taille du buffer** - Le buffer 640x480 (600KB) est trop grand
2. **Alignement mémoire** - Le buffer n'est pas correctement aligné (64 bytes requis)
3. **Allocation PSRAM** - L'allocation en PSRAM échoue ou retourne une adresse invalide
4. **Rendering LVGL** - LVGL essaie de rendre le canvas et crash sur accès mémoire

## Calculs de Taille Buffer

| Canvas | Résolution | Format | Taille Buffer | Emplacement |
|--------|-----------|--------|---------------|-------------|
| Small  | 100x100   | RGB565 | 20 KB         | SRAM/PSRAM  |
| Medium | 320x240   | RGB565 | 150 KB        | PSRAM       |
| **Large** | **640x480** | **RGB565** | **600 KB** | **PSRAM** |

### Formule de calcul:
```
Buffer Size = Width × Height × 2 bytes (RGB565)
640 × 480 × 2 = 614,400 bytes = 600 KB
```

## Tests à Exécuter (Dans l'ordre)

### Test 1: Canvas Petit (100x100)
```bash
esphome compile test_canvas_small.yaml
esphome upload test_canvas_small.yaml
```

**Résultat attendu:** ✅ Devrait fonctionner
**Si ça crash:** Le problème est dans le code du widget canvas lui-même

---

### Test 2: Canvas Moyen (320x240)
```bash
esphome compile test_canvas_medium.yaml
esphome upload test_canvas_medium.yaml
```

**Résultat attendu:** ✅ Devrait fonctionner
**Si ça crash:** Le problème est probablement l'allocation PSRAM

---

### Test 3: Canvas Large (640x480)
```bash
esphome compile test_canvas_large.yaml
esphome upload test_canvas_large.yaml
```

**Résultat attendu:** ❌ Devrait crasher (reproduire le problème)
**Si ça NE crash PAS:** Le problème est l'interaction avec lvgl_camera_display

---

## Ce qu'il faut observer dans les logs

### 1. Allocation du Buffer

Cherchez dans les logs de compilation:
```
[lvgl] allocate XXXXX bytes (64-byte aligned) -> 0xXXXXXXXX
```

**Vérifications:**
- ✓ L'adresse retournée est-elle valide? (pas 0x00000000)
- ✓ L'adresse est-elle alignée sur 64 bytes? (les 6 derniers bits = 0)
- ✓ L'adresse est-elle dans PSRAM? (commence par 0x48...)

### 2. Au Runtime

Cherchez:
```
[lvgl:xxx]: LVGL loop started - system is now fully ready
```

**Si crash APRÈS cette ligne:**
- Le problème est dans le premier `lv_timer_handler()`
- LVGL essaie de rendre le canvas
- Accès à une adresse mémoire invalide

### 3. Adresse du Crash

```
MTVAL : 0xf003e007
```

Cette adresse `0xf003e007` est suspecte:
- Commence par `0xf0` = adresse très haute (probablement invalide)
- Se termine par `007` = pas alignée sur 64 bytes
- **Hypothèse:** C'est un offset calculé incorrectement

---

## Analyse de l'Adresse 0xf003e007

### Décomposition:
```
0xf003e007 = 11110000 00000011 11100000 00000111
             ^^^^^^^^ ^^^^^^^^ ^^^^^^^^ ^^^^^^^^
             Très     Suspect  Suspect  Non-aligné
             haut
```

### Possibilités:

1. **Offset mal calculé**
   - Base: `0x48000000` (PSRAM)
   - + Offset: `0xA803e007` (incorrect)
   - = Overflow: `0xf003e007`

2. **Pointeur non initialisé**
   - Buffer allocation a échoué
   - Pointeur = garbage
   - LVGL utilise pointeur invalide

3. **Corruption mémoire**
   - Un autre composant a écrasé le pointeur
   - Canvas buffer pointer corrompu

---

## Code Suspect à Examiner

### Dans `canvas.py` (lignes 106-118):

```python
draw_buf = cg.new_Pvariable(config[CONF_DRAW_BUF_ID])
buf_size = literal(f"LV_DRAW_BUF_SIZE({width}, {height}, {color_format})")
lv.draw_buf_init(
    draw_buf,
    width,
    height,
    literal(color_format),
    0,
    lv_expr.malloc_core(buf_size),  # ← ALLOCATION ICI
    literal(buf_size),
)
```

**Questions:**
- Est-ce que `lv_malloc_core(buf_size)` retourne une adresse valide?
- Est-ce que l'adresse est alignée sur 64 bytes?
- Est-ce que PSRAM a assez de mémoire contiguë?

---

## Solutions Potentielles

### Si Test 1 échoue:
→ Bug dans le widget canvas LVGL 9.4
→ Vérifier la version de LVGL et le code de canvas.py

### Si Test 2 échoue mais Test 1 réussit:
→ Problème d'allocation PSRAM pour buffers moyens/grands
→ Vérifier sdkconfig_options PSRAM

### Si Test 3 échoue mais Test 2 réussit:
→ Limite de taille pour les buffers canvas
→ Options:
   1. Réduire la taille du canvas (ex: 320x240)
   2. Utiliser plusieurs petits canvas
   3. Ne pas utiliser de canvas, utiliser `lv_image` avec buffer externe

---

## Commandes de Debug Utiles

### Vérifier la mémoire disponible

Ajoutez dans votre YAML:
```yaml
sensor:
  - platform: template
    name: "Free PSRAM"
    lambda: |-
      return heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024.0;
    unit_of_measurement: "KB"
    update_interval: 5s
```

### Logs verbeux LVGL

```yaml
logger:
  logs:
    lvgl: VERBOSE
    lvgl.component: VERBOSE
```

---

## Prochaines Étapes

1. **Tester les 3 configurations** dans l'ordre
2. **Noter les résultats** (réussite/crash pour chacune)
3. **Partager les logs** du test qui crash
4. **Identifier la taille limite** où le crash commence

---

## Résultats des Tests

| Test | Canvas | Status | Notes |
|------|--------|--------|-------|
| 1 | 100x100 (20KB) | ⬜ À tester | |
| 2 | 320x240 (150KB) | ⬜ À tester | |
| 3 | 640x480 (600KB) | ⬜ À tester | |

**Légende:**
- ✅ = Fonctionne (pas de crash)
- ❌ = Crash
- ⬜ = Pas encore testé

---

## Contact

Si vous trouvez la cause du problème, mettez à jour ce document avec:
- La configuration qui crash
- Les logs complets
- La solution trouvée
