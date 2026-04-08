# KBHE Firmware

## Premier flash avec le bootloader custom

Cette procédure sert pour une carte neuve ou une carte entièrement effacée.

### 1. Construire les binaires Release

Le build `Release` génère les fichiers utiles :

- `build/Release/kbhe_bootloader.hex`
- `build/Release/kbhe_bootloader.bin`
- `build/Release/kbhe.hex`
- `build/Release/kbhe.bin`

### 2. Mettre la carte en DFU ROM ST

1. Basculer le switch physique sur le mode `DFU / FS`.
2. Brancher le clavier en USB.
3. Ouvrir `STM32CubeProgrammer`.
4. Se connecter en `USB`.

### 3. Effacer complètement la puce

Dans `STM32CubeProgrammer` :

1. Faire un `Full chip erase` / `Mass erase`.

### 4. Flasher le bootloader puis l’application

Toujours dans `STM32CubeProgrammer` :

1. Ouvrir `build/Release/kbhe_bootloader.hex`
2. Cliquer sur `Download`
3. Ouvrir `build/Release/kbhe.hex`
4. Cliquer sur `Download`
5. Déconnecter la carte

Important :

- utiliser les `.hex` pour éviter les erreurs d’adresse
- le bootloader va à `0x08000000`
- l’application va à `0x08010000`
- avec les `.hex`, CubeProgrammer applique déjà les bonnes adresses

### 5. Premier boot en mode normal

1. Repasser le switch physique en mode normal
2. Rebrancher le clavier en USB

Après ce premier flash ST, le clavier peut démarrer sur l’updater logiciel tant que le trailer d’application n’a pas encore été écrit.

### 6. Reflasher une dernière fois via le CLI

Installer la dépendance Python si besoin :

```powershell
pip install hidapi
```

Puis lancer :

```powershell
python raw_hid.py --flash build/Release/kbhe.bin
```

Cette étape :

- parle au bootloader custom via RAW HID
- réécrit l’application
- écrit le trailer de validation
- redémarre ensuite sur le firmware normal

Une fois cette étape terminée, le clavier peut être mis à jour normalement via le logiciel Python.

## Mises à jour suivantes

Après l’installation initiale :

- ne pas refaire toute la procédure CubeProgrammer
- utiliser le logiciel Python ou le CLI :

```powershell
python raw_hid.py --flash build/Release/kbhe.bin
```

## Variante sans bootloader custom

Pour un flash simple sans updater custom, utiliser la build `Release-apponly` :

- `build/Release-apponly/kbhe.hex`

Cette image se flashe directement à `0x08000000`.

## Note CubeMX / DMA

Si CubeMX régénère le code et casse ensuite le DMA, vérifier que `MPU_Config()` remet bien les buffers DMA critiques en non-cacheable, notamment :

- `adc_buffer`
- `ws2812_dma_buffer`

Le symptôme typique est un DMA qui semble configuré correctement mais qui ne produit plus le comportement attendu après régénération. Dans ce cas, comparer `MPU_Config()` avec la version fonctionnelle du repo avant de continuer le debug firmware.
