# MP4 Player — Mode plein écran animé

Cette version du `mp4_player` ajoute deux options pour pouvoir intégrer un
lecteur vidéo dans une carte du dashboard tout en permettant d'agrandir
l'animation en plein écran lorsqu'on tape dessus.

## Options YAML

| Option | Type | Défaut | Description |
| --- | --- | --- | --- |
| `fullscreen_on_touch` | bool | `false` | Si `true`, un tap sur la vidéo bascule entre la taille intégrée et le plein écran (au lieu d'afficher les contrôles). |
| `fullscreen_anim_ms` | int (50-2000) | `350` | Durée de l'animation en ms. |

## Actions disponibles

```yaml
# Bascule plein écran ↔ taille embarquée
- mp4_player.toggle_fullscreen: my_player

# Aller en plein écran (no-op si déjà plein écran)
- mp4_player.enter_fullscreen: my_player

# Revenir à la position embarquée (no-op si déjà embarqué)
- mp4_player.exit_fullscreen: my_player
```

## Exemple minimal

```yaml
lvgl:
  pages:
    - id: home
      widgets:
        - obj:
            id: anim_card
            x: 330
            y: 150
            width: 360
            height: 270
            radius: 18
            clip_corner: true

mp4_player:
  - id: central_video_player
    file_path: "/sdcard/videos/animation.mp4"
    parent_id: anim_card
    auto_play: true
    loop: true
    show_controls: false
    fullscreen_on_touch: true
    fullscreen_anim_ms: 400
```

## Notes d'implémentation

- Le canvas et le touch layer sont **reparentés sur la screen active** au moment
  d'entrer en plein écran, puis remis sur le parent d'origine à la sortie.
- Les contrôles ne s'affichent qu'en plein écran si `show_controls: false`.
- L'animation utilise `lv_anim_path_ease_in_out` (350 ms par défaut) pour les
  coordonnées x/y/width/height ; le `ready_cb` n'est attaché que sur l'animation
  de la hauteur (la dernière) pour finaliser la transition.
- Si `parent_id` n'est pas défini, `fullscreen_on_touch` est ignoré (un tap
  affiche/masque les contrôles comme avant).
