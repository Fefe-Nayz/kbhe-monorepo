# KBHE - Firmware, Bootloader et Outils de Configuration

KBHE est un projet de clavier analogique (Hall Effect) base sur STM32F723.
Le depot contient :

- le firmware principal (gestion des touches, USB HID, LED, gamepad, calibration)
- un bootloader custom RAW HID pour la mise a jour firmware sans outil ST
- des outils Python CLI/GUI pour configurer, monitorer, calibrer et flasher
- des utilitaires d'analyse de donnees ADC et de regression/LUT

## Vision d'ensemble

Le systeme est pense pour separer clairement :

- la logique temps reel embarquee (scan analogique, trigger, HID)
- la configuration utilisateur (RAW HID depuis PC)
- le cycle de mise a jour firmware robuste

Le clavier expose une interface RAW HID qui sert de canal de controle entre le firmware et les outils Python.

## Architecture du depot

- `Core/`: firmware principal (application)
- `Bootloader/`: bootloader custom de mise a jour
- `kbhe_tool/`: librairie Python de communication + GUI Qt
- `raw_hid.py`: point d'entree CLI principal
- `firmware_updater.py`: protocole de flash updater (PID dedie)
- `adc_capture_cli.py`, `value_extractor.py`, `regression.py`, `lut_extractor.py`: outils d'analyse/calibration
- `docs/RAW_HID_PROTOCOL.MD`: reference de protocole RAW HID
- `CMakeLists.txt`, `CMakePresets.json`: build embedded (Debug/Release/AppOnly)

## Fonctionnalites principales (detaillees)

### 1. Moteur clavier analogique

- Scan analogique multi-touches avec pipeline de traitement dedie.
- Parametrage par touche des seuils d'activation/release.
- Rapid Trigger configurable : sensibilite appui, sensibilite relache (en surcouche des memes seuils actuation/release).
- Modes de comportement avances par touche :
	- Normal
	- Tap-Hold (hold on other key press + option uppercase en appui long)
	- Toggle
	- Dynamic Keystroke (4 phases: press, fully pressed, release from fully pressed, release)
- Scope avancé profile+layer+touche : chaque profil et chaque layer peuvent porter leur propre comportement avance.
- Gestion SOCD (liaison de paires + strategies Last / Most Pressed / Absolute Priority Key 1 / Absolute Priority Key 2 / Neutral + bypass fully-pressed configurable).
- Support de couches (layers) : base + overlays avec keycodes par couche.
- Profils persistants sur MCU : 1 profil par defaut, creation/suppression/renommage dynamiques jusqu'a 4 slots, avec snapshot complet des reglages par profil (incluant le rotary encoder).
- Rotary first-class : bindings CW/CCW/click en mode action interne ou keycode arbitraire, avec exact-match modifiers + fallback no-mod.

### 2. Sorties USB HID et mode gamepad

- Deux interfaces clavier HID :
	- 6KRO (boot-compatible)
	- NKRO (mode auto avec fallback runtime vers 6KRO si NKRO indisponible au demarrage USB)
- Consumer/media controls et fonctions systeme.
- Mode gamepad activable, avec options de routage clavier/gamepad.
- Mapping par touche vers axe/direction/bouton gamepad.
- Choix du mode API gamepad :
	- HID (DirectInput)
	- XInput (Xbox compatible)
- Courbe gamepad configurable (4 points), deadzone, modes de reactivite.

### 3. Eclairage LED matrice

- Activation/desactivation de la matrice LED.
- Reglage de luminosite globale.
- Gestion pixel, ligne, frame complete et upload/download chunkes.
- Effets integres (rainbow, breathing, plasma, fire, reactive, etc.).
- Parametres d'effet persistants (couleur, vitesse, params specifiques).
- Limitation FPS LED et modes de diagnostic LED (normal/DMA stress/CPU stress).
- Overlay volume hote (niveau audio Windows pousse vers le clavier).

### 4. Calibration et precision capteur

- Calibration manuelle des valeurs zero/max par touche.
- Auto-calibration globale ou par touche.
- Calibration guidee (sequence pilotee, suivi d'etat, progression, abort).
- Support des courbes analogiques par touche (modele de reponse).
- Outils PC pour construire et valider les LUT a partir de mesures reelles.

### 5. Debug, telemetrie et acquisition

- Lecture ADC raw + filtre (formats legacy et etendu pris en charge).
- Lecture etat des touches + distances normalisees et en 0.01 mm.
- Telemetrie MCU : temperature, Vref, charge de scan, temps des taches.
- Capture ADC en RAM MCU avec export CSV (chunks, statut, overflow).
- Outils de visualisation/graphes pour analyse de bruit et regressions.

### 6. Mise a jour firmware robuste

- Bootloader custom avec protocole de flash (HELLO/BEGIN/DATA/FINISH/BOOT).
- Detection et resolution de version firmware depuis le binaire.
- Verification CRC image et echanges controles par sequence/ack.
- Gestion des timeouts et retries au niveau paquet.
- Script d'auto-retry global pour scenarios de recuperation.

## Outils Python disponibles

### CLI principal

Commande d'entree :

```powershell
python raw_hid.py
```

Options principales :

- `--gui` : lance le configurateur Qt
- `--demo` : GUI en mode simulation (sans clavier)
- `--flash <firmware.bin>` : flash via updater
- `--fw-version`, `--timeout`, `--retries` : options avancees de flash

### GUI configurateur (PySide6)

Le configurateur offre des pages dediees :

- Overview / etat global
- Keyboard (mapping, comportements avances, layers, profils, SOCD)
- Calibration (manuel + guide)
- Travel / Graph / Raw ADC / Debug sensors
- Gamepad
- Rotary encoder
- Lighting + Effects
- Firmware (mise a jour)

### Scripts d'analyse

- `adc_capture_cli.py` : capture ADC brute/filtree et export CSV
- `value_extractor.py` : extraction de points ADC <-> distance
- `regression.py` : ajustements mathematiques et comparaison de modeles
- `lut_extractor.py` : generation/validation LUT depuis modeles et CSV
- `parse_adc_data.py` / `analysis_data.py` : conversion et analyse rapide

## Build firmware

Le projet utilise CMake + presets.

Presets disponibles :

- `Debug`
- `Release`
- `Release-apponly` (sans bootloader custom)

Exemple :

```powershell
cmake --preset Release
cmake --build --preset Release
```

Artifacts attendus en `Release` :

- `build/Release/kbhe_bootloader.hex`
- `build/Release/kbhe_bootloader.bin`
- `build/Release/kbhe.hex`
- `build/Release/kbhe.bin`

## Flash initial d'une carte neuve (bootloader custom)

### 1) Construire les binaires Release

Verifier la presence des 4 artifacts ci-dessus.

### 2) Passer en DFU ROM ST

1. Basculer le switch physique en mode `DFU / FS`
2. Brancher en USB
3. Ouvrir STM32CubeProgrammer
4. Se connecter en USB

### 3) Effacer la puce

Faire un full chip erase (mass erase).

### 4) Flasher bootloader puis application

Dans STM32CubeProgrammer :

1. Flasher `build/Release/kbhe_bootloader.hex`
2. Flasher `build/Release/kbhe.hex`

Notes importantes :

- preferer les `.hex` (adresses embarquees)
- bootloader a `0x08000000`
- application a `0x08010000`

### 5) Premier boot normal

1. Remettre le switch en mode normal
2. Rebrancher le clavier

### 6) Finaliser via updater RAW HID

Installer la dependance HID si necessaire :

```powershell
pip install hidapi
```

Puis lancer :

```powershell
python raw_hid.py --flash build/Release/kbhe.bin
```

Cette etape finalise l'image applicative avec le cycle updater complet.

## Mises a jour firmware suivantes

Apres installation initiale, utiliser directement :

```powershell
python raw_hid.py --flash build/Release/kbhe.bin
```

Pas besoin de repasser par la procedure complete CubeProgrammer.

## Variante sans bootloader custom

Utiliser le preset `Release-apponly`, puis flasher :

- `build/Release-apponly/kbhe.hex`

Cette image se flashe directement a l'adresse `0x08000000`.

## Dependances cote PC

- Obligatoire : `hidapi` (package Python `hid`)
- GUI : `PySide6`
- Analyse scientifique (selon scripts) : `numpy`, `matplotlib`, `pandas`

Exemple rapide :

```powershell
pip install hidapi PySide6 numpy matplotlib pandas
```

## Documentation complementaire

- Protocole RAW HID : `docs/RAW_HID_PROTOCOL.MD`
- Notes d'usage RAW HID : `RAW_HID.md`

## Note CubeMX / DMA

Si une regeneration CubeMX degrade le DMA, verifier `MPU_Config()` et la configuration non-cacheable des buffers DMA critiques (notamment les buffers ADC et WS2812).
