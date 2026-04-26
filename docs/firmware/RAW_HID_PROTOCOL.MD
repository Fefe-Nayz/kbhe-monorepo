# Protocole RAW HID KBHE - Documentation globale

Cette documentation couvre les deux canaux RAW HID du projet KBHE.

- Protocole application (PID 0x0002): configuration, debug, calibration, LED, gamepad
- Protocole updater (PID 0x0003): flash firmware du slot applicatif

Toutes les trames sont sur 64 octets.

## 1. Transport et identification USB

- VID: 0x9172
- PID application: 0x0002
- PID updater: 0x0003
- RAW HID usage page: 0xFF00
- Interface RAW HID application: interface 1

## 2. Format de paquet

### 2.1 Application RAW HID (PID 0x0002)

- Octet 0: command_id
- Octet 1: status (reponse) ou champ reserve (requete)
- Octets 2..63: payload

Codes status:

- 0x00: OK
- 0x01: ERROR
- 0x02: INVALID_CMD
- 0x03: INVALID_PARAM

Convention host importante:

- Pour les commandes dont le handler lit req->payload[n], il faut conserver un octet placeholder en tete du data host.
- Exemple host Python: data = [0, valeur] pour ecrire payload[0] = valeur cote firmware.

### 2.2 Updater RAW HID (PID 0x0003)

Structure updater_packet_t:

- Octet 0: command
- Octet 1: sequence
- Octet 2: status
- Octet 3: length
- Octets 4..7: offset (uint32 LE)
- Octets 8..63: payload (56 octets max)

Codes status updater:

- 0x00: OK
- 0x01: ERROR
- 0x02: INVALID_COMMAND
- 0x03: INVALID_PARAMETER
- 0x04: INVALID_STATE
- 0x05: VERIFY_FAILED
- 0x06: INVALID_IMAGE

## 3. Commandes application RAW HID (PID 0x0002)

### 3.1 Commandes systeme (0x00..0x1F)

| Commande | ID | Parametres requete | Reponse |
|---|---:|---|---|
| GET_FIRMWARE_VERSION | 0x00 | aucun | status + version u16 LE en octets 2..3 |
| REBOOT | 0x01 | aucun | status |
| ENTER_BOOTLOADER | 0x02 | aucun | status |
| FACTORY_RESET | 0x03 | aucun | status |
| USB_REENUMERATE | 0x04 | aucun | status |

`FACTORY_RESET` retourne une erreur en mode RAM-only: le reset usine est une operation persistante.

### 3.2 Options globales (0x20..0x2F)

| Commande | ID | Parametres requete | Reponse |
|---|---:|---|---|
| GET_OPTIONS | 0x20 | aucun | status, keyboard_enabled, gamepad_enabled, raw_hid_echo, led_thermal_protection_enabled |
| SET_OPTIONS | 0x21 | keyboard_enabled, gamepad_enabled, raw_hid_echo, led_thermal_protection_enabled | status + echo des valeurs appliquees |
| GET_KEYBOARD_ENABLED | 0x22 | aucun | status + value (0/1) |
| SET_KEYBOARD_ENABLED | 0x23 | value (0/1) | status + value courant |
| GET_GAMEPAD_ENABLED | 0x24 | aucun | status + value (0/1) |
| SET_GAMEPAD_ENABLED | 0x25 | value (0/1) | status + value courant |
| SAVE_SETTINGS | 0x26 | aucun | status |
| GET_NKRO_ENABLED | 0x27 | aucun | status + value (0/1) |
| SET_NKRO_ENABLED | 0x28 | value (0/1) | status + value courant |
| GET_ADVANCED_TICK_RATE | 0x29 | aucun | status + tick_rate |
| SET_ADVANCED_TICK_RATE | 0x2A | tick_rate (1..100) | status + tick_rate applique |
| GET_DEVICE_INFO | 0x2B | aucun | status + version + serial + keyboard_name |
| GET_KEYBOARD_NAME | 0x2C | aucun | status + keyboard_name |
| SET_KEYBOARD_NAME | 0x2D | keyboard_name[32] | status + keyboard_name applique |
| COPY_PROFILE_SLOT | 0x2E | source_profile_index, target_profile_index | status + source + target + profile_used_mask |
| RESET_PROFILE_SLOT | 0x2F | profile_index | status + profile_index + profile_used_mask |

`SAVE_SETTINGS` retourne une erreur en mode RAM-only: ce mode interdit toute planification d'ecriture flash.

Note option `raw_hid_echo`:

- Le champ est bien persiste/expose via GET/SET_OPTIONS.
- Dans l'implementation actuelle, il n'active/desactive pas un comportement runtime distinct (commande ECHO 0xFE toujours disponible).

Note option `led_thermal_protection_enabled`:

- Si activee, le firmware limite la luminosite LED a 96/255 quand la temperature MCU depasse 70 deg C.
- Une hysteresis de 3 deg C est appliquee (retour a la luminosite normale en dessous de 67 deg C) pour eviter les oscillations.

GET_DEVICE_INFO (0x2B) payload reponse:

- version u16
- serial[26] (chaine C NUL-terminee)
- keyboard_name[32] (chaine C NUL-terminee)

GET/SET_KEYBOARD_NAME:

- keyboard_name ASCII printable, longueur max 32
- les caracteres non imprimables sont ignores/sanitizes cote firmware
- si nom vide, fallback sur le nom par defaut firmware

COPY_PROFILE_SLOT (0x2E):

- Requete:
  - source_profile_index (slot present)
  - target_profile_index (slot 0..3, peut etre utilise ou libre)
- Reponse:
  - status
  - source_profile_index
  - target_profile_index
  - profile_used_mask

RESET_PROFILE_SLOT (0x2F):

- Requete:
  - profile_index (slot present)
- Reponse:
  - status
  - profile_index
  - profile_used_mask

Notes:

- `COPY_PROFILE_SLOT` clone integralement le snapshot source vers le slot cible.
- Si le slot cible etait libre, il devient `used` et recoit un nom par defaut de slot.
- `RESET_PROFILE_SLOT` remet le snapshot du slot aux defaults firmware en conservant son nom et son statut `used`.

Semantique NKRO (0x27/0x28):

- value=1: mode Auto NKRO (essaie NKRO apres enumeration USB, puis fallback runtime 6KRO si NKRO n'est pas pret sous ~1000 ms)
- value=0: mode 6KRO only (n'essaie jamais NKRO)
- le fallback runtime est re-evalue a chaque nouvelle enumeration USB
- garde-fou runtime: si NKRO devient actif mais reste bloque sans progression (endpoint NKRO non pret) pendant ~100 ms, fallback automatique vers 6KRO

Matrice de mode clavier effectif:

| keyboard_enabled (0x22/0x23) | nkro_enabled (0x27/0x28) | Boot protocol USB | Resultat effectif |
|---:|---:|---:|---|
| 0 | 0/1 | 0/1 | Aucun envoi clavier |
| 1 | 0 | 0/1 | 6KRO uniquement |
| 1 | 1 | 1 | 6KRO force (mode Boot) |
| 1 | 1 | 0 + NKRO pret < 1000 ms | NKRO |
| 1 | 1 | 0 + NKRO non pret >= 1000 ms | Fallback runtime 6KRO jusqu'a deconnexion USB |

Notes importantes:

- Le Boot protocol est pilote par la requete USB standard SET_PROTOCOL cote host (pas par une commande RAW HID dediee).
- La bascule de route clavier 6KRO/NKRO est protegee: si une touche est tenue, la transition est differee jusqu'a ce que plus aucune touche ne soit active, pour eviter les artefacts.
- Lors d'une transition de route autorisee, le firmware flush les etats 6KRO+NKRO avant d'activer la nouvelle route.

Valeurs par defaut usine (profil courant):

- Options globales:
  - keyboard_enabled=1
  - gamepad_enabled=0
  - raw_hid_echo=0
  - nkro_enabled=1 (Auto NKRO avec fallback runtime 6KRO)
- Clavier par touche (defaults):
  - actuation_point_mm=12 (1.2 mm)
  - release_point_mm=12 (1.2 mm)
- Gamepad global:
  - keyboard_routing=2 (Unmapped only)
  - api_mode=1 (XInput)
- LED:
  - led_effect_mode=1 (RAINBOW)
  - led_brightness=50
  - led_effect_color=(255,0,0)
  - led_fps_limit=60
  - led_usb_suspend_rgb_off=1 (RGB coupe pendant USB suspend)
- Filtre ADC:
  - filter_enabled=1
  - noise_band=8
  - alpha_min_denom=1
  - alpha_max_denom=8
- Keyboard name runtime:
  - "75HE Keyboard"

Serial number details:

- calcule une seule fois au demarrage (cache RAM)
- source: UID STM32 (96 bits)
- encodage: base62 sur 12 octets UID
- format: prefixe `75HE-NF-` + base62 UID (17 chars)

### 3.3 Configuration touches, layers, calibration, gamepad (0x40..0x5F)

#### 3.3.1 Key settings detaillees

GET_KEY_SETTINGS (0x40)

- Requete:
  - key_index
  - profile_index
  - layer_index
- Reponse:
  - key_index
  - profile_index
  - layer_index
  - hid_keycode (u16)
  - actuation_point_mm (0.1 mm)
  - release_point_mm (0.1 mm)
  - rapid_trigger_press (0.01 mm)
  - rapid_trigger_release (0.01 mm)
  - socd_pair (0xFF = none)
  - socd_resolution
  - rapid_trigger_enabled
  - disable_kb_on_gamepad
  - continuous_rapid_trigger
  - behavior_mode
  - hold_threshold_10ms
  - secondary_hid_keycode (u16)
  - dynamic_zones[4]:
    - end_mm_tenths = bitmap DKS (2 bits par phase)
      - bit0..1 key press
      - bit2..3 key fully pressed
      - bit4..5 key release from fully pressed
      - bit6..7 key release
    - hid_keycode(u16) = binding DKS
  - tap_hold_options
    - bit0 hold_on_other_key_press
    - bit1 uppercase_hold
  - dks_bottom_out_point (0.1 mm)
  - socd_fully_pressed_enabled (0/1)
  - socd_fully_pressed_point (0.1 mm)

Notes runtime:

- Les champs avances sont maintenant resolus par triplet `profile_index + layer_index + key_index`.
- Le firmware applique toujours les memes seuils de base:
  - press si distance >= actuation_point_mm
  - release si distance < release_point_mm
- Rapid Trigger est une surcouche de sensibilite (press/release), il ne remplace pas ces seuils.

SET_KEY_SETTINGS (0x41)

- Requete: meme structure que ci-dessus (sans status)
- Validation:
  - key_index < NUM_KEYS
  - profile_index < SETTINGS_PROFILE_COUNT et slot present
  - layer_index < SETTINGS_LAYER_COUNT
  - socd_pair valide (0xFF ou autre key)
  - socd_resolution < SETTINGS_SOCD_RESOLUTION_MAX
  - behavior_mode < KEY_BEHAVIOR_MAX
- Reponse: status + echo de la config appliquee

Valeurs enum associees:

- behavior_mode:
  - 0 Normal
  - 1 Tap-Hold
  - 2 Toggle
  - 3 Dynamic Keystroke (DKS)
- socd_resolution:
  - 0 Last input wins
  - 1 Most pressed wins
  - 2 Absolute priority (Key 1)
  - 3 Absolute priority (Key 2)
  - 4 Neutral

#### 3.3.2 Key settings en chunks

GET_ALL_KEY_SETTINGS (0x42)

- Requete: start_index
- Reponse:
  - start_index
  - key_count (<= 6)
  - keys[6] compact:
    - hid_keycode u16
    - actuation/release/rapid fields
    - socd_pair
    - flags
- flags:
  - bit0 rapid_trigger_enabled
  - bit1 disable_kb_on_gamepad
  - bit2..4 socd_resolution

SET_ALL_KEY_SETTINGS (0x43)

- Requete:
  - start_index
  - key_count (1..6)
  - keys[] compact
- Reponse:
  - status global
  - start_index
  - key_count
  - keys[] effectivement appliques

#### 3.3.3 Gamepad global

GET_GAMEPAD_SETTINGS (0x44)

- Reponse:
  - keyboard_routing
  - square_mode
  - reactive_stick
  - api_mode
  - curve[4] = x_01mm(u16) + y(u8)

SET_GAMEPAD_SETTINGS (0x45)

- Requete:
  - keyboard_routing
  - square_mode
  - reactive_stick
  - api_mode
  - curve[4]
- Validation:
  - keyboard_routing < GAMEPAD_KEYBOARD_ROUTING_MAX
  - api_mode < GAMEPAD_API_MAX
  - curve monotone en x et x <= 400 (4.00 mm)
- Reponse: status + valeurs appliquees

Note: Le deadzone est configure via `curve[0].x_01mm` (point de debut de la courbe gamepad).

Enums utiles:

- keyboard_routing:
  - 0 Disabled
  - 1 All keys
  - 2 Unmapped only
- api_mode:
  - 0 HID
  - 1 XInput

#### 3.3.4 Calibration

GET_CALIBRATION (0x46)

- Requete: start_index
- Reponse:
  - start_index
  - value_count (<= 29)
  - lut_zero_value (i16)
  - key_zero_values[value_count] (i16)

SET_CALIBRATION (0x47)

- Requete: start_index, value_count, lut_zero_value, key_zero_values chunk
- Validation:
  - range index/size valide
  - toutes les valeurs dans 0..4095
- Reponse: status + echo chunk

AUTO_CALIBRATE (0x48)

- Requete: key_index (0..NUM_KEYS-1, ou 0xFF pour toutes)
- Reponse:
  - status
  - calibration partielle (meme format que GET_CALIBRATION, depuis index 0)

GET_CALIBRATION_MAX (0x4F) / SET_CALIBRATION_MAX (0x50)

- Meme logique que zero calibration, mais pour key_max_values

GUIDED_CALIBRATION_START (0x51)
GUIDED_CALIBRATION_STATUS (0x52)
GUIDED_CALIBRATION_ABORT (0x53)

- Reponse commune:
  - active
  - phase
  - current_key
  - progress_percent
  - sample_count u16
  - phase_elapsed_ms u16

#### 3.3.5 Courbe analogique par touche

GET_KEY_CURVE (0x49)

- Requete: key_index
- Reponse: status, key_index, curve_enabled, p1_x, p1_y, p2_x, p2_y

SET_KEY_CURVE (0x4A)

- Requete: key_index, curve_enabled, p1_x, p1_y, p2_x, p2_y
- Reponse: status + echo

Note runtime:

- Ces champs sont persistes et relus via 0x49/0x4A.
- Dans le firmware actuel, la sortie runtime active n'applique pas encore cette courbe par touche.

#### 3.3.6 Mapping gamepad par touche

GET_KEY_GAMEPAD_MAP (0x4B)

- Requete: key_index
- Reponse: status, key_index, axis, direction, button

SET_KEY_GAMEPAD_MAP (0x4C)

- Requete: key_index, axis, direction, button
- Reponse: status + echo

Enums utiles:

- axis: 0 none, 1 LX, 2 LY, 3 RX, 4 RY, 5 LT, 6 RT
- direction: 0 positive, 1 negative
- button: 0..17 (A/B/X/Y/LB/RB/LT/RT/Select/Start/L3/R3/DPad/Home)

Note: 0x4D et 0x4E sont reserves (anciens `GET_GAMEPAD_WITH_KB` / `SET_GAMEPAD_WITH_KB`, supprimes). Ces IDs ne doivent pas etre reutilises.

#### 3.3.7 Rotary encoder

GET_ROTARY_ENCODER_SETTINGS (0x54)

- Reponse:
  - rotation_action
  - button_action
  - sensitivity
  - step_size
  - invert_direction
  - rgb_behavior
  - rgb_effect_mode
  - progress_style
  - progress_effect_mode
  - progress_color_r/g/b
  - cw_binding:
    - mode (0 internal, 1 keycode)
    - keycode(u16)
    - modifier_mask_exact (HID mods bitmask)
    - fallback_no_mod_keycode(u16)
    - layer_mode (0 active, 1 fixed)
    - layer_index
  - ccw_binding (meme structure)
  - click_binding (meme structure)

SET_ROTARY_ENCODER_SETTINGS (0x55)

- Requete: meme champs
- Validation:
  - enums dans bornes
  - sensitivity 1..16
  - step_size 1..64
  - effect_mode != THIRD_PARTY pour rgb/progress
  - binding.mode/layer_mode/layer_index valides
- Reponse: status + echo applique

GET_ROTARY_STATE (0x59)

- Reponse:
  - button_pressed (0/1)
  - last_direction (-1, 0, +1)
  - step_counter (u32, increment a chaque cran)

Notes:

- Commande runtime (polling), non persistante.
- `step_counter` est remis a zero au redemarrage.
- En mode keycode, les bindings rotary appliquent `modifier_mask_exact`; si non-match, le firmware tente `fallback_no_mod_keycode`.

#### 3.3.8 Profils persistants (metadata)

GET_ACTIVE_PROFILE (0x5A)

- Reponse:
  - profile_index (0..3)
  - profile_used_mask (bit N = slot N present)

SET_ACTIVE_PROFILE (0x5B)

- Requete:
  - profile_index (slot present)
- Reponse:
  - status
  - profile_index applique
  - profile_used_mask

GET_PROFILE_NAME (0x5C)

- Requete:
  - profile_index (slot present)
- Reponse:
  - status
  - profile_index
  - profile_used_mask
  - profile_name (ASCII court, taille fixe)

SET_PROFILE_NAME (0x5D)

- Requete:
  - profile_index (slot present)
  - profile_name (ASCII court, taille fixe)
- Reponse:
  - status
  - profile_index
  - profile_used_mask
  - profile_name applique

CREATE_PROFILE (0x5E)

- Requete:
  - profile_name (ASCII court, optionnel)
- Reponse:
  - status
  - profile_index cree
  - profile_used_mask
  - profile_name applique

DELETE_PROFILE (0x5F)

- Requete:
  - profile_index (slot present)
- Reponse:
  - status
  - profile_index actif apres suppression
  - profile_used_mask

Notes:

- Limite MCU: 4 slots max.
- Etat par defaut firmware/factory reset: seul le slot 0 est initialise avec les reglages par defaut (`profile_used_mask = 0x01`).
- La suppression du dernier slot restant est refusee.
- `CREATE_PROFILE` clone l'integralite des reglages du profil actif (mapping, layers, gamepad, LED, filtres, rotary, etc.).
- `SET_ACTIVE_PROFILE` recharge l'integralite des reglages du slot cible.
- Les commandes qui modifient les slots device (`SET_ACTIVE_PROFILE`, `SET_DEFAULT_PROFILE`, `SET_PROFILE_NAME`, `CREATE_PROFILE`, `DELETE_PROFILE`, `COPY_PROFILE_SLOT`, `RESET_PROFILE_SLOT`) retournent une erreur en mode RAM-only. L'hote doit d'abord sortir du mode RAM-only pour faire une operation persistante.

#### 3.3.9 Keycode de layer

GET_LAYER_KEYCODE (0x56)

- Requete: layer_index, key_index
- Reponse: status, layer_index, key_index, hid_keycode(u16)

SET_LAYER_KEYCODE (0x57)

- Requete: layer_index, key_index, hid_keycode(u16)
- Reponse: status + valeur courante

Notes:

- SETTINGS_LAYER_COUNT = 4
- Les layer keycodes sont utilises pour les overlays (hors base)

#### 3.3.10 Reset actuation + rapid trigger d'une touche

RESET_KEY_TRIGGER_SETTINGS (0x58)

- Requete:
  - key_index
- Effet:
  - remet uniquement les champs trigger de la touche aux valeurs par defaut firmware:
    - actuation_point_mm
    - release_point_mm
    - rapid_trigger_press
    - rapid_trigger_release
    - rapid_trigger_enabled
    - continuous_rapid_trigger
  - conserve les autres champs de la touche (hid_keycode, SOCD, behavior_mode, zones dynamiques, mappings gamepad, etc.)
- Reponse:
  - status
  - echo complet du key settings (meme format que GET_KEY_SETTINGS)

### 3.4 LED matrix et effets (0x60..0x7E)

| Commande | ID | Parametres requete | Reponse |
|---|---:|---|---|
| GET_LED_ENABLED | 0x60 | aucun | status + value |
| SET_LED_ENABLED | 0x61 | value (0/1) | status + value |
| GET_LED_BRIGHTNESS | 0x62 | aucun | status + brightness |
| SET_LED_BRIGHTNESS | 0x63 | brightness | status + brightness applique |
| GET_LED_PIXEL | 0x64 | index | status + index + r g b |
| SET_LED_PIXEL | 0x65 | index + r g b | status + echo |
| GET_LED_ROW | 0x66 | row | status + row + 24 bytes RGB |
| SET_LED_ROW | 0x67 | row + 24 bytes RGB | status + echo |
| GET_LED_ALL | 0x68 | chunk_index | status + chunk_index + chunk_size + data |
| SET_LED_ALL | 0x69 | declaree mais non traitee par le dispatcher | utiliser 0x6A |
| SET_LED_ALL_CHUNK | 0x6A | chunk_index + chunk_size + data | status + chunk_size |
| LED_CLEAR | 0x6B | aucun | status |
| LED_FILL | 0x6C | r g b | status + r g b |
| LED_TEST_RAINBOW | 0x6D | aucun | status |
| GET_LED_EFFECT | 0x6E | aucun | status + effect_mode |
| SET_LED_EFFECT | 0x6F | effect_mode | status (INVALID_PARAM si mode hors borne) |
| GET_LED_FPS_LIMIT | 0x70 | aucun | status + fps_limit |
| SET_LED_FPS_LIMIT | 0x71 | fps_limit | status + fps_limit |
| GET_LED_EFFECT_PARAMS | 0x72 | effect_mode | status + effect_mode + param_count + params |
| SET_LED_EFFECT_PARAMS | 0x73 | effect_mode + params | status + lecture params appliques |
| SET_LED_VOLUME_OVERLAY | 0x74 | level (0..255) | status + level |
| CLEAR_LED_VOLUME_OVERLAY | 0x75 | aucun | status |
| RESTORE_LED_EFFECT_BEFORE_THIRD_PARTY | 0x76 | aucun | status + effect_mode courant |
| GET_LED_EFFECT_SCHEMA | 0x77 | effect_mode + chunk_index | status + effect_mode + chunk_index + total_chunks + total_active + descriptors |
| SET_LED_AUDIO_SPECTRUM | 0x78 | band_count + bands + impact_level | status + echo payload |
| CLEAR_LED_AUDIO_SPECTRUM | 0x79 | aucun | status |
| SET_LED_ALPHA_MASK | 0x7A | mask_len + mask_bytes (len=0 pour clear) | status + mask_len applique |
| GET_LED_IDLE_OPTIONS | 0x7B | aucun | status + idle_timeout_seconds + allow_system_when_disabled + third_party_stream_counts_as_activity |
| SET_LED_IDLE_OPTIONS | 0x7C | idle_timeout_seconds + allow_system_when_disabled + third_party_stream_counts_as_activity | status + echo des valeurs appliquees |
| GET_LED_USB_SUSPEND_RGB_OFF | 0x7D | aucun | status + value (0/1) |
| SET_LED_USB_SUSPEND_RGB_OFF | 0x7E | value (0/1) | status + value courant |

Notes generales LED:
- La vitesse et les couleurs des effets sont uniquement transportees via
  `GET_LED_EFFECT_PARAMS` / `SET_LED_EFFECT_PARAMS`.
- `LED_EFFECT_PARAM_SPEED` (index 14) porte la vitesse par effet.
- `LED_EFFECT_PARAM_COLOR_R/G/B` (index 8/9/10) portent la couleur de base (RGB).
- Tous les effets exposent PCOLOR (couleur de base). Par defaut rouge (255,0,0) = QMK default.
- `GET_LED_EFFECT_SCHEMA` retourne des descriptors chunks de 6 octets:
  `id`, `type`, `min`, `max`, `default`, `step`.
- Si `LED_USB_SUSPEND_RGB_OFF` est active, la matrice RGB est forcee a noir
  pendant `USB suspend` puis restauree au `USB resume`.

Params effets par categorie:

Gradient up-down:
- Param 0 v_scale: amplitude de la rotation de teinte verticale (160=original)
- Param 1 sat: saturation (144=original)
- Param 2 val: luminosite (255=original)
- PCOLOR: teinte de base

Hue breathing / Hue pendulum:
- Param 0 swing: plage de rotation de teinte (12=original, 64=large)
- PCOLOR: teinte de base

Hue wave:
- Param 0 wave_scale: amplitude de la vague de teinte (24=original, 128=grande)
- PCOLOR: teinte de base

Alpha mods:
- Param 0 hue_offset: decalage de teinte entre alphas et modificateurs
  (0=dynamique pilote par la vitesse, >0=offset fixe, ex: 128=complementaire)
- PCOLOR: teinte de base

Starlight dual sat:
- Param 0 sat_spread: ecart de saturation entre etoiles (31=original)
- PCOLOR: teinte de base

Starlight dual hue:
- Param 0 hue_spread: ecart de teinte entre etoiles (31=original)
- PCOLOR: teinte de base

Pixel fractal:
- Param 0 sat: saturation (255=original)
- Param 1 val: luminosite (255=original)
- PCOLOR: teinte de base

Splash / Multi-splash:
- Param 0 decay: vitesse de disparition (72=taux QMK / duree physique)
- Param 1 spread: rayon de vague (128=original)
- Param 2 base_glow: eclat ambient (8=original)
- Param 3 white_core: burst lumineux au front de vague (bool, 1=defaut)
- Param 4 gain: echelle de luminosite (224=original)
- Param 5 palette: 0=couleur custom, 1=arc-en-ciel radial
- Param 6 mode: 0=QMK (degrade HSV simple, defaut), 1=physique (anneau+trace+noyau)
- PCOLOR: couleur de base (defaut vert #00FF60)

### 3.5 Filtre ADC (0x80..0x83)

| Commande | ID | Parametres requete | Reponse |
|---|---:|---|---|
| GET_FILTER_ENABLED | 0x80 | aucun | status + enabled |
| SET_FILTER_ENABLED | 0x81 | enabled (0/1) | status + enabled courant |
| GET_FILTER_PARAMS | 0x82 | aucun | status + noise_band + alpha_min_denom + alpha_max_denom |
| SET_FILTER_PARAMS | 0x83 | noise_band + alpha_min_denom + alpha_max_denom | status + valeurs courantes |

### 3.6 Debug et telemetrie (0xE0..0xE9)

GET_ADC_VALUES (0xE0)

- Reponse etendue (64 octets):
  - adc_raw[6] u16
  - adc_filtered[6] u16
  - scan_time_us u16
  - scan_rate_hz u16
  - task timings u16: analog, trigger, socd, keyboard, keyboard_nkro, gamepad, led, total
  - analog monitor u16: raw, filter, calibration, lut, store, key_min, key_max, key_avg, nonzero_keys

GET_KEY_STATES (0xE1)

- Requete: start_index
- Reponse:
  - start_index
  - key_count <= 15
  - par key: state, distance_norm, distance_01mm(u16)

GET_LOCK_STATES (0xE2)

- Reponse payload[0] bitfield:
  - bit0 Num Lock
  - bit1 Caps Lock
  - bit2 Scroll Lock

ADC_CAPTURE_START (0xE3)

- Requete:
  - key_index
  - duration_ms (u32)
- Reponse:
  - active
  - key_index
  - duration_ms
  - sample_count
  - overflow_count

ADC_CAPTURE_STATUS (0xE4)

- Reponse: meme format status capture

ADC_CAPTURE_READ (0xE5)

- Requete:
  - start_index (u32)
  - max_samples (1..12)
- Reponse:
  - active
  - key_index
  - total_samples u32
  - start_index u32
  - sample_count
  - raw_samples[12] u16
  - filtered_samples[12] u16

GET_RAW_ADC_CHUNK (0xE6)
GET_FILTERED_ADC_CHUNK (0xE7)
GET_CALIBRATED_ADC_CHUNK (0xE8)

- Requete: start_index
- Reponse:
  - start_index
  - value_count <= 30
  - adc_values[value_count] u16

GET_MCU_METRICS (0xE9)

- Reponse:
  - temperature_c i16
  - vref_mv u16
  - core_clock_hz u32
  - scan_cycle_us u16
  - scan_rate_hz u16
  - work_us u16
  - load_permille u16
  - temp_valid u8

### 3.7 Test et erreurs

ECHO (0xFE)

- Requete: toute trame
- Reponse: copie de la trame avec status force a OK

Commande inconnue

- Reponse:
  - command_id recopie
  - status = INVALID_CMD

## 4. Commandes updater RAW HID (PID 0x0003)

Constantes updater:

- protocol_version: 0x0001
- payload max: 56 octets
- align write flash: 4 octets
- app_base: 0x08010000
- app_max_size: 0x00050000 - 0x00000100

### 4.1 HELLO (0x01)

- Requete:
  - payload vide
- Reponse payload updater_hello_payload_t:
  - protocol_version u16
  - flags u16
  - app_base u32
  - app_max_size u32
  - write_align u32
  - installed_fw_version u16
  - reserved u16
- flags:
  - bit0 app valide
  - bit1 session active

### 4.2 BEGIN (0x02)

- Requete payload updater_begin_request_t:
  - image_size u32
  - image_crc32 u32
  - fw_version u16
  - reserved u16
- Validation:
  - image_size > 0
  - image_size <= app_max_size
- Effet:
  - erase slot app
  - ouverture session
- Reponse:
  - status
  - payload progress (next_offset, accepted_length, flags)

### 4.3 DATA (0x03)

- Requete:
  - offset aligne 4
  - length aligne 4, 1..56
  - payload data chunk
- Validation:
  - session active
  - offset == next_offset attendu
  - offset + length <= aligned_size
- Reponse:
  - status
  - payload progress:
    - next_offset u32
    - accepted_length u32
    - flags u32
    - reserved u32

### 4.4 FINISH (0x04)

- Requete: payload vide
- Validation:
  - session active
  - next_offset == aligned_size
  - vecteur app valide
  - crc32 image == crc attendu
- Effet:
  - ecriture trailer (magic, size, crc, fw_version)
  - fermeture session
- Reponse:
  - status
  - progress payload

### 4.5 ABORT (0x05)

- Requete: payload vide
- Effet: reset session updater
- Reponse: status + progress payload

### 4.6 BOOT (0x06)

- Requete: payload vide
- Validation: image application valide
- Reponse:
  - OK puis saut applique apres envoi reponse
  - ou INVALID_IMAGE

### 4.7 Idempotence

Le bootloader memorise la derniere requete complete.

- Si le meme paquet est recu a nouveau (retransmission), il renvoie la meme reponse precedente.

## 5. Notes implementation host

- Toujours verifier status avant d'interpreter la payload.
- Pour les transferts chunkes (keys, LED, calibration, ADC), utiliser start_index/chunk_index de reponse comme reference.
- Pour updater:
  - incrementer sequence a chaque requete
  - respecter alignement 4 octets
  - sur timeout, retransmettre la meme requete (dedup cote bootloader)

## 6. Resume rapide IDs

- Systeme: 0x00..0x04
- Settings: 0x20..0x2F
- Keys/Calibration/Gamepad/Rotary/Layer/Profile: 0x40..0x5F
  - 0x4D, 0x4E: reserves (anciens GET/SET_GAMEPAD_WITH_KB, supprimes)
- LED: 0x60..0x79
- Filter: 0x80..0x83
- Debug: 0xE0..0xE9
- Echo: 0xFE
- Updater: 0x01..0x06 sur PID 0x0003
