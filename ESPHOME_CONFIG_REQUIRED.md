# Configuration ESPHome Requise pour Éviter le Crash VFS

## Problème

Le système crash avec `ESP_ERR_NO_MEM` dans `esp_vfs_lwip_sockets_register` car la limite VFS par défaut (8 devices) est atteinte.

## Solution

Ajouter les paramètres ESP-IDF suivants dans votre fichier ESPHome YAML principal (ex: `/config/esphome/tab5.yaml`):

```yaml
esp32:
  board: esp32-p4-function-ev-board
  variant: esp32p4
  framework:
    type: esp-idf
    version: 5.4.2
    platform_version: https://github.com/pioarduino/platform-espressif32/releases/download/51.03.07/platform-espressif32.zip

    # CONFIGURATION CRITIQUE: Augmenter les limites VFS
    sdkconfig_options:
      # Augmenter de 8 (défaut) à 16 pour supporter:
      # - 4 video devices (/dev/video0, /dev/video10, /dev/video11, /dev/video20)
      # - Devices système (UART, console, etc.)
      # - Sockets réseau (lwip)
      CONFIG_VFS_MAX_COUNT: "16"

      # Augmenter aussi les sockets lwip
      CONFIG_LWIP_MAX_SOCKETS: "16"

      # S'assurer que les sockets peuvent s'enregistrer
      CONFIG_VFS_SUPPORT_IO: "y"
```

## Vérification

Après avoir ajouté ces paramètres:

1. Clean build:
   ```bash
   esphome clean tab5.yaml
   ```

2. Recompile:
   ```bash
   esphome compile tab5.yaml
   ```

3. Le système devrait démarrer sans crash et les logs devraient montrer:
   ```
   ✅ /dev/video0 existe et accessible
   ✅ ISP Pipeline initialized
   ✅ Network initialized  ← Plus de crash ici!
   ```

## Fichiers de Support

- `sdkconfig.defaults` : Configuration de base (peut ne pas être utilisé par ESPHome)
- `esp-idf/sdkconfig.defaults` : Alternative pour certaines versions d'ESPHome
- **IMPORTANT**: La méthode recommandée est d'utiliser `sdkconfig_options:` dans le YAML

## Diagnostic

Si le crash persiste:

1. Vérifiez que la section `sdkconfig_options:` est bien dans votre YAML
2. Vérifiez les logs de compilation pour confirmer que `CONFIG_VFS_MAX_COUNT=16` est appliqué
3. Après compilation, cherchez dans les logs: `CONFIG_VFS_MAX_COUNT`

## Contexte Technique

La caméra fonctionne parfaitement (sensor détecté, ISP initialisé). Le crash se produit 12 secondes après l'init de la caméra, quand le composant réseau (WiFi/Ethernet) essaie d'enregistrer ses sockets dans le VFS et trouve qu'il n'y a plus d'espace disponible.

Ce n'est **pas** un problème de RAM (30MB libres), mais un problème de limite de configuration ESP-IDF.
