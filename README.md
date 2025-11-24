# Horloge 4 digits ESP8266

Projet PlatformIO pour une horloge 4 digits (Wemos D1 mini / ESP-12E) pilotant 30 LED NeoPixel (14 pour les heures, 2 pour les deux points centraux, 14 pour les minutes). La configuration est conservée dans `config.json` sur LittleFS et peut être ajustée à chaud via une API HTTP protégée par WiFiManager.

## Matériel supporté
- ESP8266 (profil PlatformIO `esp12e`).
- Bandeau de 30 LED adressables (NEO_GRB, 800 kHz) connecté sur la broche D6 / GPIO12.
- Deux digits d'heures et deux digits de minutes, câblés en segments consécutifs (7 LED par digit) avec deux LED centrales dédiées aux points.
- Le câblage attendu suit l'ordre **heures → heures → deux points → minutes → minutes** sur la strip continue, avec un routage de segments non standard (LED1=e, LED2=d, LED3=c, LED4=g, LED5=f, LED6=a, LED7=b). Si votre montage diffère, ajustez `HOUR_TENS_DIGIT_INDEX` et `SEGMENT_LED_OFFSET` dans `src/main.cpp`.

## Fonctionnement général
1. **LittleFS** est monté au démarrage pour charger `config.json` (un fichier d'exemple est fourni dans `data/config.json`). S'il est absent ou illisible, une configuration par défaut est générée et sauvée.
2. **WiFiManager** lance un portail de configuration « Clock-Setup » s'il ne retrouve pas de réseau connu. Dès que le WiFi est disponible, le serveur HTTP embarqué (port 80) expose l'API.
3. **Interface LED** : un Adafruit_NeoPixel gère les 30 LED. Chaque digit comporte 7 segments (ordre A–G) et les deux points centraux occupent les indices 14 (gauche) et 15 (droite).
4. **Modes** : `clock` est pleinement implémenté. Les modes `timer`, `weather`, `custom` et `alarm` réutilisent actuellement l'affichage principal (avec clignotement des points pour `timer`/`alarm`) et servent de base pour des comportements plus évolués. Le mode `off` coupe simplement toutes les LED.
5. **Synchronisation NTP** : à chaque démarrage (et lors des modifications via l'API), l'horloge synchronise l'heure sur le serveur configuré (`pool.ntp.org` par défaut) et applique un décalage UTC paramétrable.
6. **Plage nocturne** : une fenêtre horaire optionnelle peut réduire automatiquement la luminosité (jusqu'à éteindre totalement) pour préserver l'obscurité.

## Configuration (`config.json`)
Structure principale :
```json
{
  "power": {
    "power_on": true,
    "mode": "clock",
    "startup_mode": "clock",
    "exit_special_mode": false
  },
  "time": { "hour": 12, "minute": 0, "second": 0 },
  "display": {
    "brightness": 80,
    "general_color": "#FF5500",
    "per_digit_color": {
      "enabled": false,
      "values": ["#FF5500", "#FF5500", "#FF5500", "#FF5500"]
    },
    "quiet_hours": {
      "enabled": false,
      "start_hour": 23,
      "start_minute": 0,
      "end_hour": 7,
      "end_minute": 0,
      "dim_brightness": 0
    }
  },
  "dots": {
    "enabled": true,
    "left_color": "#FFFFFF",
    "right_color": "#FFFFFF",
    "force_override": false,
    "forced_color": "#FF0000"
  },
  "network": { "ntp_server": "pool.ntp.org", "utc_offset_minutes": 0 }
}
```
- Les couleurs sont exprimées en hexadécimal `#RRGGBB`.
- `per_digit_color.values` contient 4 entrées (digits 0→3). Activer `enabled` applique ces couleurs à la place de `general_color`.
- `display.quiet_hours` réduit automatiquement la luminosité (jusqu'à 0) entre `start_*` et `end_*`. Lorsque la plage chevauche minuit, la réduction s'applique sur deux jours.
- `dots.force_override` applique temporairement `forced_color` sur les deux points (sinon chaque point utilise sa couleur dédiée).
- `network.ntp_server` définit le serveur NTP utilisé à chaque synchronisation (modifiable via l'API `/api/time` ou en éditant le fichier).
- `network.utc_offset_minutes` applique un décalage horaire (en minutes, plage -720 ↔ 840) par rapport à UTC lors de la synchronisation.

Le fichier peut être téléversé vers le système de fichiers avec `pio run -t uploadfs`. Pendant l'exécution, toute modification via l'API est persistée immédiatement.

## API HTTP locale
Toutes les routes répondent et acceptent du JSON, avec CORS activé. Méthodes disponibles : `GET` (lecture), `POST` (mise à jour), `OPTIONS` (préflight).

### `/api/power`
- Champs gérés :
  - `power_on` (`bool`)
  - `mode` (`clock`, `timer`, `weather`, `custom`, `alarm`, `off`).
  - `startup_mode` : mode utilisé après reboot ou lorsqu'on demande une sortie de mode spécial.
  - `exit_special_mode` (`true` force un retour au `startup_mode`).
- Exemple :
```bash
curl -X POST http://clock.local/api/power \
  -H 'Content-Type: application/json' \
  -d '{"power_on":true,"mode":"clock"}'
```

### `/api/time`
- Détermine l'heure de départ utilisée par `ModeClock` lorsque aucun autre minuteur n'est actif.
- Champs : `hour` (0-23), `minute` (0-59), `second` (0-59), `ntp_server` (chaîne), `utc_offset_minutes` (entier entre -720 et 840). Envoyer un `ntp_server` ou un `utc_offset_minutes` déclenche une resynchronisation immédiate tant que le WiFi est connecté.

### `/api/display`
- `brightness` (1-255).
- `general_color`: couleur par défaut.
- `per_digit_color`: objet `{ enabled: bool, values: ["#RRGGBB", ...] }` ou directement un tableau pour activer la coloration par digit.
- `quiet_hours`: `{ enabled, start_hour, start_minute, end_hour, end_minute, dim_brightness }` pour réduire (ou éteindre) l'affichage sur une plage horaire (dim_brightness accepte 0→255).

### `/api/dots`
- `enabled`: active les deux points.
- `left_color`, `right_color`: couleurs individuelles.
- `force_override`, `forced_color`: force simultanément les deux points avec une même couleur.

### Interface `/`
- Accéder à `http://<IP>/` ouvre une interface web légère (HTML/JS) qui consomme les endpoints REST pour éditer `config.json` (alimentation, heure/NTP, affichage, points, offset UTC). Aucune dépendance externe, tout est embarqué dans le firmware.

### `/api/info`
- Retourne un petit JSON de statut (nom du projet et liste des endpoints exposés).

## Déploiement et test
1. Installer les dépendances définies dans `platformio.ini` (ArduinoJson, Adafruit NeoPixel, WiFiManager).
2. Construire le firmware : `pio run`. (Selon votre environnement, PlatformIO peut nécessiter les droits d'écriture sur `~/.platformio`.)
3. Téléverser le firmware : `pio run -t upload`.
4. Charger `data/config.json` sur LittleFS : `pio run -t uploadfs`.
5. À la première mise sous tension, rejoignez le portail WiFi `Clock-Setup` pour configurer le réseau.

## Personnalisation des modes
- `clock` : affiche HH:MM avec masquage du zéro initial et rafraîchissement toutes les 250 ms.
- `timer` et `alarm` : identiques à `clock` mais les points clignotent pour indiquer un état particulier. L'implémentation peut facilement évoluer vers un vrai compte à rebours.
- `weather` : remplit les 30 LED avec `general_color`. Peut être remplacé par un rendu météo (température, icône, etc.).
- `custom` : si `per_digit_color` est activé, l'affichage HH:MM est utilisé, sinon toutes les LED sont remplies avec `general_color`.

## Fichiers clés
- `src/main.cpp` : firmware complet (WiFiManager, LittleFS, API HTTP, gestion NeoPixel).
- `platformio.ini` : configuration PlatformIO (LittleFS + dépendances).
- `data/config.json` : configuration par défaut téléversable sur LittleFS.
