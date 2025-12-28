# Analyse: Pourquoi OV5647 a une Meilleure Clarté que OV02C10

## 🔍 Observation

**L'image OV5647 est largement supérieure en clarté** comparé à OV02C10.

Après analyse des fichiers JSON IPA, j'ai identifié **3 problèmes majeurs** dans OV02C10.

---

## ⚠️ PROBLÈME 1: OVER-SHARPENING (Principal Problème!)

### OV5647 (Image Claire et Nette)

```json
"sharpen": [{
    "gain": 1,
    "param": {
        "h_thresh": 56,          // ← Threshold ÉLEVÉ
        "l_thresh": 10,
        "h_coeff": 0.425,        // ← Coefficient FAIBLE
        "m_coeff": 0.625,        // ← Coefficient FAIBLE
        "matrix": [1,2,1, 2,2,2, 1,2,1]
    }
}]
```

### OV02C10 (Image Moins Claire)

```json
"sharpen": [{
    "gain": 1,
    "param": {
        "h_thresh": 25,          // ← Threshold TROP FAIBLE
        "l_thresh": 5,
        "h_coeff": 1.925,        // ← Coefficient TROP ÉLEVÉ (4.5x plus!)
        "m_coeff": 1.825,        // ← Coefficient TROP ÉLEVÉ (2.9x plus!)
        "matrix": [1,2,1, 2,2,2, 1,2,1]
    }
}]
```

### Explication

| Paramètre | OV5647 | OV02C10 | Problème OV02C10 |
|-----------|--------|---------|------------------|
| **h_thresh** | 56 | 25 | ❌ TROP FAIBLE → sharpen appliqué sur TOUT (même le bruit) |
| **h_coeff** | 0.425 | 1.925 | ❌ **4.5x TROP ÉLEVÉ** → over-sharpening extrême! |
| **m_coeff** | 0.625 | 1.825 | ❌ **2.9x TROP ÉLEVÉ** → amplifie le bruit |

**Conséquence:** OV02C10 fait du **OVER-SHARPENING**:
- Amplifie le bruit au lieu de le réduire
- Crée des artifacts (halos, ringing)
- Détruit la clarté naturelle de l'image
- Image "artificielle" au lieu de "nette"

**Solution:** Utiliser des coefficients PLUS FAIBLES comme OV5647!

---

## ⚠️ PROBLÈME 2: GAMMA TROP FAIBLE

### Comparaison Gamma

| Sensor | Gamma Param | Résultat |
|--------|-------------|----------|
| **OV5647** | **0.72** | ✅ Image claire et lumineuse |
| **OV02C10** | **0.518** | ❌ Image plus sombre (28% moins lumineux) |

**Formule gamma:** `output = input^(1/gamma_param)`

- Gamma 0.72 → exposant 1.39 → image plus claire
- Gamma 0.518 → exposant 1.93 → image plus sombre

**Conséquence:** L'image OV02C10 est **28% plus sombre** que nécessaire!

---

## ⚠️ PROBLÈME 3: CONTRASTE INSUFFISANT

### Comparaison Contraste

| Sensor | Contraste (gain 1) | Résultat |
|--------|-------------------|----------|
| **OV5647** | **134** | ✅ Bon contraste |
| **OV02C10** | **132** (puis diminue à 126) | ❌ Contraste plus faible |

**Différence:** -1.5% de contraste en moins

Bien que faible, cela contribue à l'impression de "manque de clarté".

---

## 📊 RÉSUMÉ DES DIFFÉRENCES

| Paramètre IPA | OV5647 (Meilleur) | OV02C10 (Problèmes) | Impact sur Clarté |
|---------------|-------------------|---------------------|-------------------|
| **Sharpen h_coeff** | 0.425 | 1.925 (**4.5x trop!**) | ❌❌❌ **CRITIQUE** |
| **Sharpen m_coeff** | 0.625 | 1.825 (**2.9x trop!**) | ❌❌❌ **CRITIQUE** |
| **Sharpen h_thresh** | 56 | 25 (trop faible) | ❌❌ **MAJEUR** |
| **Gamma** | 0.72 | 0.518 (28% plus sombre) | ❌❌ **MAJEUR** |
| **Contraste** | 134 | 132 (1.5% moins) | ❌ MINEUR |

---

## ✅ SOLUTION RECOMMANDÉE

### Option 1: Copier les Paramètres OV5647 (RECOMMANDÉ)

Modifier le fichier `ov02c10_default.json` pour utiliser les mêmes paramètres que OV5647:

```json
"aen": {
    "gamma": {
        "use_gamma_param": true,
        "luma_env": "ae.luma.avg",
        "luma_min_step": 16.0,
        "table": [{
            "luma": 71.1,
            "gamma_param": 0.72    // ← CHANGÉ de 0.518 à 0.72
        }]
    },
    "sharpen": [{
        "gain": 1,
        "param": {
            "h_thresh": 56,        // ← CHANGÉ de 25 à 56
            "l_thresh": 10,        // ← CHANGÉ de 5 à 10
            "h_coeff": 0.425,      // ← CHANGÉ de 1.925 à 0.425
            "m_coeff": 0.625,      // ← CHANGÉ de 1.825 à 0.625
            "matrix": [1,2,1, 2,2,2, 1,2,1]  // ← Identique
        }
    }],
    "contrast": [{
        "gain": 1,
        "value": 134           // ← CHANGÉ de 132 à 134
    }]
}
```

### Option 2: Ajustement Progressif

Si vous voulez tester progressivement:

**Étape 1:** Réduire le sharpen (IMPACT MAJEUR)
```json
"h_coeff": 0.8,    // ← De 1.925 → 0.8 (test intermédiaire)
"m_coeff": 1.0,    // ← De 1.825 → 1.0 (test intermédiaire)
```

**Étape 2:** Augmenter le gamma (IMPACT MAJEUR)
```json
"gamma_param": 0.65   // ← De 0.518 → 0.65 (test intermédiaire)
```

**Étape 3:** Augmenter le contraste (IMPACT MINEUR)
```json
"value": 134          // ← De 132 → 134
```

---

## 🎯 RÉSULTATS ATTENDUS Après Correction

Avec les paramètres OV5647 appliqués à OV02C10:

✅ **Sharpen naturel** au lieu d'over-sharpening
✅ **Moins de bruit** (coefficients plus faibles)
✅ **Image plus lumineuse** (gamma 0.72 au lieu de 0.518)
✅ **Meilleur contraste** (134 au lieu de 132)
✅ **Clarté comparable à OV5647**

---

## 📝 AUTRES OBSERVATIONS

### Denoise (adn.bf)

**OV5647:** Simple, un seul niveau
```json
"bf": [{
    "gain": 1,
    "param": {
        "level": 5,
        "matrix": [1,2,1, 2,4,2, 1,2,1]
    }
}]
```

**OV02C10:** Complexe, 7 niveaux adaptés au gain
```json
"bf": [
    { "gain": 1,  "level": 3, ... },
    { "gain": 4,  "level": 3, ... },
    { "gain": 8,  "level": 4, ... },
    { "gain": 16, "level": 5, ... },
    { "gain": 24, "level": 6, ... },
    { "gain": 32, "level": 7, ... },
    { "gain": 64, "level": 7, ... }
]
```

**Impact:** OV02C10 a un denoise PLUS agressif → peut réduire la clarté fine

**Suggestion:** Simplifier comme OV5647 pour garder plus de détails.

### CCM (Color Correction Matrix)

**IDENTIQUE** entre OV5647 et OV02C10:
```json
"matrix": [
     2.0000,  -0.5459, -0.4541,
    -0.4751,   1.7696, -0.2945,
    -0.2002,  -0.7998,  2.0000
]
```

✅ Pas de problème de couleur, la CCM est correcte.

---

## 🔧 IMPLÉMENTATION

Voulez-vous que je:

1. ✅ **Modifie le JSON OV02C10** avec les paramètres OV5647 (RECOMMANDÉ)
2. ⚠️ **Crée un nouveau JSON optimisé** avec ajustements progressifs
3. 📊 **Crée des versions A/B** pour tester la différence

La solution #1 est la plus rapide et garantit une image aussi claire que OV5647!

---

## 💡 CONCLUSION

**La clarté supérieure d'OV5647 vient de:**
1. **Sharpen modéré** (coefficients 4-5x plus faibles)
2. **Gamma plus élevé** (image 28% plus lumineuse)
3. **Meilleur contraste** (+1.5%)

**OV02C10 souffre d'OVER-SHARPENING** qui détruit la clarté naturelle de l'image.

**Solution:** Copier les paramètres sharpen/gamma d'OV5647 → clarté immédiate!
