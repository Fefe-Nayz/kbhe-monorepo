# Protocole RAW HID – Documentation Technique

## Généralités

Le protocole RAW HID permet la communication entre un hôte (PC, logiciel) et le firmware du clavier via une interface HID personnalisée. Les paquets échangés font 64 octets.

### Structure d’un paquet

- **Octet 0** : `command_id` (identifiant de la commande)
- **Octet 1** : `status` (pour les réponses) ou `status_or_len` (pour les requêtes)
- **Octets 2-63** : Payload (paramètres ou données)

## Codes de statut

- `0x00` : OK
- `0x01` : ERREUR
- `0x02` : COMMANDE INVALIDE
- `0x03` : PARAMÈTRE INVALIDE

---

## Liste des commandes principales

| Commande                        | ID (hex) | Description                                 |
|---------------------------------|----------|---------------------------------------------|
| CMD_GET_FIRMWARE_VERSION        | 0x00     | Version du firmware                         |
| CMD_REBOOT                      | 0x01     | Redémarrage du microcontrôleur              |
| CMD_ENTER_BOOTLOADER            | 0x02     | Mode bootloader                             |
| CMD_FACTORY_RESET               | 0x03     | Réinitialisation usine                      |
| CMD_GET_OPTIONS                 | 0x20     | Lire les options                            |
| CMD_SET_OPTIONS                 | 0x21     | Écrire les options                          |
| CMD_GET_KEYBOARD_ENABLED        | 0x22     | Statut clavier activé                       |
| CMD_SET_KEYBOARD_ENABLED        | 0x23     | Activer/désactiver le clavier               |
| CMD_GET_GAMEPAD_ENABLED         | 0x24     | Statut gamepad activé                       |
| CMD_SET_GAMEPAD_ENABLED         | 0x25     | Activer/désactiver le gamepad               |
| CMD_SAVE_SETTINGS               | 0x26     | Sauvegarder la configuration                |
| CMD_GET_NKRO_ENABLED            | 0x27     | Statut NKRO                                 |
| CMD_SET_NKRO_ENABLED            | 0x28     | Activer/désactiver NKRO                     |
| CMD_GET_KEY_SETTINGS            | 0x40     | Lire la config d’une touche                 |
| CMD_SET_KEY_SETTINGS            | 0x41     | Écrire la config d’une touche               |
| CMD_GET_ALL_KEY_SETTINGS        | 0x42     | Lire la config de toutes les touches        |
| CMD_SET_ALL_KEY_SETTINGS        | 0x43     | Écrire la config de toutes les touches      |
| CMD_GET_LED_ENABLED             | 0x60     | Statut LEDs                                 |
| CMD_SET_LED_ENABLED             | 0x61     | Activer/désactiver LEDs                     |
| CMD_GET_LED_BRIGHTNESS          | 0x62     | Lire la luminosité LED                      |
| CMD_SET_LED_BRIGHTNESS          | 0x63     | Régler la luminosité LED                    |
| CMD_GET_ADC_VALUES              | 0xE0     | Lire valeurs ADC (debug)                    |
| CMD_GET_KEY_STATES              | 0xE1     | Lire l’état des touches (debug)             |
| CMD_ECHO                        | 0xFE     | Test d’écho                                 |

*Voir le fichier d’en-tête pour la liste exhaustive.*

---

## Exemples de paquets

### 1. Lire la version du firmware

**Requête :**
```
[0x00, 0x00, ... 62x 0x00]
```
**Réponse :**
```
[0x00, 0x00, <version LSB>, <version MSB>, ... 60x 0x00]
```

### 2. Activer le clavier

**Requête :**
```
[0x23, 0x00, 0x01, ... 61x 0x00]   // 0x01 = activer, 0x00 = désactiver
```
**Réponse :**
```
[0x23, 0x00, 0x01, ... 61x 0x00]   // 0x01 = activé, 0x00 = désactivé
```

### 3. Lire les options

**Requête :**
```
[0x20, 0x00, ... 62x 0x00]
```
**Réponse :**
```
[0x20, 0x00, <keyboard_enabled>, <gamepad_enabled>, <raw_hid_echo>, ... 59x 0x00]
```

---

## Structures de données

### Paquet générique

```c
typedef struct {
  uint8_t command_id;
  uint8_t status_or_len;
  uint8_t payload[62];
} hid_packet_t;
```

### Options

```c
typedef struct {
  uint8_t command_id;
  uint8_t status;
  uint8_t keyboard_enabled;
  uint8_t gamepad_enabled;
  uint8_t raw_hid_echo;
  uint8_t reserved[59];
} hid_packet_options_t;
```

### Réponse version firmware

```c
typedef struct {
  uint8_t command_id;
  uint8_t status;
  uint16_t version;
  uint8_t reserved[60];
} hid_resp_firmware_version_t;
```

*Voir le fichier `hid_protocol.h` pour toutes les structures.*

---

## Utilisation

- Envoyer un paquet de 64 octets via l’interface RAW HID.
- Lire la réponse de 64 octets.
- Toujours vérifier le champ `status` dans la réponse.

---

## Exemple Python (pyhidapi)

```python
import hid

dev = hid.device()
dev.open(0x1234, 0x5678)  # Remplacer par VID/PID

# Lire la version firmware
packet = bytearray(64)
packet[0] = 0x00  # CMD_GET_FIRMWARE_VERSION
dev.write(packet)
response = dev.read(64)
print("Firmware version:", response[2] | (response[3] << 8))
```

---

Pour plus de détails sur chaque commande, se référer à la documentation du firmware ou au fichier `hid_protocol.h`. Si tu veux la description détaillée d’une commande ou structure, précise-la !
