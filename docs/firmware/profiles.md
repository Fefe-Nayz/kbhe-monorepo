Oui. Voici le modèle complet de la feature, en séparant bien **stockage**, **runtime**, **sync**, **boot** et **sécurité RAM-only**.

```mermaid
flowchart TD
  User["Utilisateur"] --> UI["kbhe-configurator UI"]

  UI --> ProfileStore["Profile Store Zustand"]

  ProfileStore --> AppProfiles["App profiles<br/>localStorage: keyboard-profile:*<br/>illimites"]
  ProfileStore --> DeviceMirror["Miroir device profiles<br/>localStorage: keyboard-device-profile:*<br/>copie locale seulement"]
  ProfileStore --> RuntimeState["Etat runtime app<br/>runtimeSource<br/>activeAppProfileName<br/>activeDeviceSlot<br/>defaultDeviceSlot<br/>ramOnlyActive"]

  UI --> Protocol["RAW HID protocol"]

  Protocol --> FirmwareRAM["Firmware RAM<br/>current_settings<br/>etat actuellement applique"]
  FirmwareRAM --> Flash["Flash MCU<br/>settings_t persistant<br/>device profiles slots 0..3"]

  Flash --> Boot["Boot clavier"]
  Boot --> DefaultProfile{"Default device profile configure ?"}
  DefaultProfile -->|oui| LoadDefault["Charger defaultDeviceSlot"]
  DefaultProfile -->|non| LoadLast["Charger last active device slot"]

  AppProfiles --> TemporaryApply["Apply app profile"]
  TemporaryApply --> RamOnly["RAM-only mode"]
  RamOnly --> FirmwareRAM

  DeviceMirror -. "cache / miroir, jamais source de verite firmware" .-> UI
  Flash -. "source de verite des device profiles" .-> DeviceMirror
```

**Modèle mental**

Il y a 3 concepts distincts:

1. **Device profile**
Stocké dans la flash du clavier. Limité aux slots MCU, actuellement `0..3`. C’est persistant, bootable, et le clavier est la source de vérité.

2. **App profile**
Stocké uniquement dans l’app, dans `localStorage` via `keyboard-profile:*`. Illimité. Le clavier ne peut jamais booter directement dessus.

3. **Temporary RAM session**
Quand on applique un app profile au clavier, l’app envoie son snapshot au firmware en RAM-only. Le clavier utilise ces réglages en RAM, mais ne les écrit jamais en flash.

```mermaid
stateDiagram-v2
  [*] --> DeviceRuntime: boot clavier

  DeviceRuntime: Runtime device profile
  DeviceRuntime: Flash = source de verite
  DeviceRuntime: writes peuvent persister

  TemporaryRuntime: Runtime app profile temporaire
  TemporaryRuntime: App storage = source de verite
  TemporaryRuntime: Firmware RAM = copie active
  TemporaryRuntime: Flash interdite

  DeviceRuntime --> TemporaryRuntime: Apply app profile
  TemporaryRuntime --> DeviceRuntime: Use device profile
  TemporaryRuntime --> DeviceRuntime: create/rename/delete device profile
  TemporaryRuntime --> [*]: unplug/reboot perte RAM

  DeviceRuntime --> DeviceRuntime: edit device settings
  TemporaryRuntime --> TemporaryRuntime: edit temp settings
```

**Règles de vérité**

```mermaid
flowchart LR
  DP["Device profile"] --> DPSOT["Source de verite:<br/>Flash clavier"]
  DP --> DPMirror["Copie app:<br/>keyboard-device-profile:*"]

  AP["App profile"] --> APSOT["Source de verite:<br/>App localStorage"]
  AP --> Temp["Copie runtime:<br/>Firmware RAM-only"]

  Temp --> Lost["Perdu au reboot/unplug"]
  APSOT --> Reapply["Peut etre reapplique par l'app"]
```

Pour un **device profile**, l’app ne doit pas inventer l’état sauvegardé: elle lit/capture le clavier et garde un miroir local.

Pour un **app profile**, le clavier ne dicte pas le stockage: l’app garde le snapshot, puis le pousse au clavier temporairement.

**Flux: appliquer un app profile temporaire**

```mermaid
sequenceDiagram
  participant U as User
  participant UI as Configurator
  participant Store as ProfileStore
  participant HID as RAW HID
  participant FW as Firmware RAM
  participant Flash as MCU Flash

  U->>UI: Apply Temporary sur un app profile
  UI->>Store: lire firmwareSnapshot
  UI->>HID: SET_RAM_ONLY_MODE = 1
  HID->>FW: entrer RAM-only
  UI->>HID: appliquer snapshot complet
  HID->>FW: key settings, gamepad, rotary, filters, LED, NKRO, tick rate
  FW--xFlash: aucune ecriture flash
  UI->>Store: runtimeSource = app
  UI->>Store: ramOnlyActive = true
```

En RAM-only, les lectures depuis l’app doivent représenter l’état effectif en RAM. Les écritures modifient:
- le firmware RAM,
- le snapshot app local,
- mais jamais la flash.

**Flux: éditer pendant une session temporaire**

```mermaid
sequenceDiagram
  participant Page as Page UI
  participant HID as RAW HID
  participant FW as Firmware RAM
  participant Snapshot as App profile firmwareSnapshot
  participant Flash as MCU Flash

  Page->>HID: SET_KEY_SETTINGS / SET_LED_EFFECT / etc.
  HID->>FW: appliquer en RAM
  FW--xFlash: pas de save
  Page->>Snapshot: patchActiveAppProfile...
  Snapshot->>Snapshot: mettre a jour la copie app
```

C’est le rôle de `profile-snapshot-store.ts`: chaque page qui écrit dans le clavier patch aussi le snapshot local si `runtimeSource === "app"` et `ramOnlyActive === true`.

**Flux: créer un device profile depuis RAM-only**

```mermaid
sequenceDiagram
  participant UI as Configurator
  participant HID as RAW HID
  participant FW as Firmware
  participant Flash as MCU Flash

  UI->>HID: GET_RAM_ONLY_MODE
  HID-->>UI: true
  UI->>HID: RELOAD_SETTINGS_FROM_FLASH
  HID->>FW: sortir RAM-only
  FW->>Flash: relire dernier etat persistant
  UI->>HID: CREATE_PROFILE "game"
  HID->>FW: creer slot device
  UI->>HID: SAVE_SETTINGS
  FW->>Flash: ecrire settings_t
  UI->>HID: capture snapshot du slot
```

Et côté firmware, une sécurité supplémentaire existe maintenant: si un host tente `CREATE_PROFILE`, `DELETE_PROFILE`, `SET_PROFILE_NAME`, `SET_DEFAULT_PROFILE`, etc. en RAM-only, le firmware répond erreur.

**Bug `"game"` devient `"me"`**

```mermaid
flowchart TD
  Old["Ancien firmware CREATE_PROFILE"] --> BadOffset["Lisait profile_name a l'offset 4 du paquet"]
  App["App envoyait le nom dans payload[0]"]
  BadOffset --> Result["game devient me<br/>les 2 premiers caracteres etaient sautes"]

  Fix["Correctif firmware"] --> Payload0["CREATE_PROFILE lit payload[0]"]
  Fix --> Payload1["SET_PROFILE_NAME lit payload[1]<br/>apres profile_index"]
  Payload0 --> Correct["game reste game"]
```

Le bug était donc bien firmware. L’app envoyait le payload selon la logique attendue, mais le handler firmware le parsait comme une struct complète de réponse.

**Boot behavior**

```mermaid
flowchart TD
  PowerOn["Power on / reboot"] --> LoadFlash["Lire settings_t depuis flash"]
  LoadFlash --> HasDefault{"default_profile_index valide ?"}
  HasDefault -->|oui| BootDefault["Charger ce device profile"]
  HasDefault -->|non| BootLast["Charger dernier active_profile_index persistant"]
  BootDefault --> Runtime["Runtime device profile"]
  BootLast --> Runtime

  Temp["Ancienne session app RAM-only"] -->|jamais restauree| Lost["Perdue"]
```

Un app profile ne peut pas être un boot default, car il n’existe pas dans la flash du clavier.

**Résumé des garanties**

- Les app profiles sont illimités et app-only.
- Les device profiles occupent un slot MCU.
- Une session app temporaire ne pollue jamais la flash.
- Une opération device persistante sort toujours du RAM-only avant d’agir.
- Le firmware refuse maintenant les opérations device persistantes si RAM-only est encore actif.
- Le boot ne restaure que des device profiles.
- Le miroir app des device profiles est une copie locale, pas la source de vérité.