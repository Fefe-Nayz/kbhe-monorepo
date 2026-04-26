import {
  type FilterParams,
  type GamepadSettings,
  type KeyGamepadMap,
  type KeySettings,
  kbheDevice,
  type LedIdleOptions,
  type RotaryEncoderSettings,
  type TriggerChatterGuard,
} from "./device"
import { KEY_COUNT, LAYER_COUNT, LED_EFFECT_COUNT, LED_EFFECT_PARAM_COUNT } from "./protocol"

export interface FirmwareLedSnapshot {
  enabled: boolean | null
  brightness: number | null
  pixels: number[] | null
  effectMode: number | null
  fpsLimit: number | null
  effectParams: number[][] | null
  idleOptions: LedIdleOptions | null
  triggerChatterGuard: TriggerChatterGuard | null
}

export interface FirmwareProfileSnapshot {
  schemaVersion: 1
  capturedAt: number
  sourceProfileIndex: number
  keySettings: KeySettings[]
  keyGamepadMaps?: KeyGamepadMap[] | null
  gamepadSettings: GamepadSettings | null
  rotarySettings: RotaryEncoderSettings | null
  filterEnabled: boolean | null
  filterParams: FilterParams | null
  options: {
    keyboard_enabled: boolean
    gamepad_enabled: boolean
    raw_hid_echo: boolean
    led_thermal_protection_enabled?: boolean
  } | null
  nkroEnabled: boolean | null
  advancedTickRate: number | null
  led?: FirmwareLedSnapshot
}

interface ApplyFirmwareProfileSnapshotOptions {
  persistToFlash?: boolean
  restoreActiveProfile?: boolean
}

function isObject(value: unknown): value is Record<string, unknown> {
  return Boolean(value) && typeof value === "object"
}

export function isFirmwareProfileSnapshot(value: unknown): value is FirmwareProfileSnapshot {
  if (!isObject(value)) {
    return false
  }

  if (value.schemaVersion !== 1) {
    return false
  }

  if (!Array.isArray(value.keySettings)) {
    return false
  }

  return true
}

async function captureAllKeySettings(profileIndex: number): Promise<KeySettings[] | null> {
  const keySettings: KeySettings[] = []
  const batchSize = 8

  for (let layerIndex = 0; layerIndex < LAYER_COUNT; layerIndex += 1) {
    for (let start = 0; start < KEY_COUNT; start += batchSize) {
      const end = Math.min(start + batchSize, KEY_COUNT)
      const requests = Array.from({ length: end - start }, (_, offset) =>
        kbheDevice.getKeySettings(start + offset, profileIndex, layerIndex),
      )

      const results = await Promise.all(requests)
      for (const result of results) {
        if (!result) {
          return null
        }
        keySettings.push(result)
      }
    }
  }

  return keySettings
}

async function captureAllKeyGamepadMaps(): Promise<KeyGamepadMap[] | null> {
  const maps: KeyGamepadMap[] = []
  const batchSize = 8

  for (let start = 0; start < KEY_COUNT; start += batchSize) {
    const end = Math.min(start + batchSize, KEY_COUNT)
    const results = await Promise.all(
      Array.from({ length: end - start }, (_, offset) =>
        kbheDevice.getKeyGamepadMap(start + offset),
      ),
    )

    for (const result of results) {
      if (!result) {
        return null
      }
      maps.push(result)
    }
  }

  return maps
}

async function captureLedSnapshot(): Promise<FirmwareLedSnapshot> {
  const [
    enabled,
    brightness,
    pixels,
    effectMode,
    fpsLimit,
    idleOptions,
    triggerChatterGuard,
  ] = await Promise.all([
    kbheDevice.ledGetEnabled(),
    kbheDevice.ledGetBrightness(),
    kbheDevice.ledDownloadAll(),
    kbheDevice.getLedEffect(),
    kbheDevice.getLedFpsLimit(),
    kbheDevice.getLedIdleOptions(),
    kbheDevice.getTriggerChatterGuard(),
  ])

  const effectParamResults = await Promise.all(
    Array.from({ length: LED_EFFECT_COUNT }, (_, effect) =>
      kbheDevice.getLedEffectParams(effect),
    ),
  )

  const effectParams = effectParamResults.every((params) => params != null)
    ? effectParamResults.map((params) => {
        const values = Array.from(params ?? [], (value) => value & 0xff)
          .slice(0, LED_EFFECT_PARAM_COUNT)
        while (values.length < LED_EFFECT_PARAM_COUNT) {
          values.push(0)
        }
        return values
      })
    : null

  return {
    enabled,
    brightness,
    pixels,
    effectMode,
    fpsLimit,
    effectParams,
    idleOptions,
    triggerChatterGuard,
  }
}

export async function captureFirmwareProfileSnapshot(profileIndex: number): Promise<FirmwareProfileSnapshot | null> {
  let restoreProfileIndex: number | null = null
  let switchedProfile = false

  try {
    const [activeProfile, ramOnlyMode] = await Promise.all([
      kbheDevice.getActiveProfile(),
      kbheDevice.getRamOnlyMode(),
    ])

    if (activeProfile?.profile_index != null && activeProfile.profile_index !== profileIndex) {
      if (ramOnlyMode) {
        return null
      }

      restoreProfileIndex = activeProfile.profile_index
      const switched = await kbheDevice.setActiveProfile(profileIndex)
      if (!switched) {
        return null
      }
      switchedProfile = true
    }

    const keySettings = await captureAllKeySettings(profileIndex)
    if (!keySettings) {
      return null
    }

    const [
      gamepadSettings,
      rotarySettings,
      filterEnabled,
      filterParams,
      options,
      nkroEnabled,
      advancedTickRate,
      led,
      keyGamepadMaps,
    ] = await Promise.all([
      kbheDevice.getGamepadSettings(),
      kbheDevice.getRotaryEncoderSettings(),
      kbheDevice.getFilterEnabled(),
      kbheDevice.getFilterParams(),
      kbheDevice.getOptions(),
      kbheDevice.getNkroEnabled(),
      kbheDevice.getAdvancedTickRate(),
      captureLedSnapshot(),
      captureAllKeyGamepadMaps(),
    ])

    return {
      schemaVersion: 1,
      capturedAt: Date.now(),
      sourceProfileIndex: profileIndex,
      keySettings,
      keyGamepadMaps,
      gamepadSettings,
      rotarySettings,
      filterEnabled,
      filterParams,
      options,
      nkroEnabled,
      advancedTickRate,
      led,
    }
  } catch {
    return null
  } finally {
    if (switchedProfile && restoreProfileIndex != null) {
      await kbheDevice.setActiveProfile(restoreProfileIndex)
    }
  }
}

async function applyLedSnapshot(led: FirmwareLedSnapshot): Promise<boolean> {
  if (led.effectParams) {
    for (let effect = 0; effect < led.effectParams.length; effect += 1) {
      const params = led.effectParams[effect]
      if (!params) {
        continue
      }
      const ok = await kbheDevice.setLedEffectParams(effect, params)
      if (!ok) {
        return false
      }
    }
  }

  if (led.pixels) {
    const ok = await kbheDevice.ledUploadAll(led.pixels)
    if (!ok) {
      return false
    }
  }

  if (led.brightness != null) {
    const ok = await kbheDevice.ledSetBrightness(led.brightness)
    if (!ok) {
      return false
    }
  }

  if (led.fpsLimit != null) {
    const ok = await kbheDevice.setLedFpsLimit(led.fpsLimit)
    if (!ok) {
      return false
    }
  }

  if (led.idleOptions) {
    const ok = await kbheDevice.setLedIdleOptions(
      led.idleOptions.idle_timeout_seconds,
      led.idleOptions.allow_system_when_disabled,
      led.idleOptions.third_party_stream_counts_as_activity,
      led.idleOptions.usb_suspend_rgb_off,
    )
    if (!ok) {
      return false
    }
  }

  if (led.triggerChatterGuard) {
    const ok = await kbheDevice.setTriggerChatterGuard(
      led.triggerChatterGuard.enabled,
      led.triggerChatterGuard.duration_ms,
    )
    if (!ok) {
      return false
    }
  }

  if (led.enabled != null) {
    const ok = await kbheDevice.ledSetEnabled(led.enabled)
    if (!ok) {
      return false
    }
  }

  if (led.effectMode != null) {
    const ok = await kbheDevice.setLedEffect(led.effectMode)
    if (!ok) {
      return false
    }
  }

  return true
}

export async function applyFirmwareProfileSnapshot(
  snapshot: FirmwareProfileSnapshot,
  targetProfileIndex: number,
  options: ApplyFirmwareProfileSnapshotOptions = {},
): Promise<boolean> {
  let restoreProfileIndex: number | null = null
  let switchedProfile = false
  const shouldRestore = options.restoreActiveProfile ?? true
  let restoreNeedsSave = false

  try {
    if (options.persistToFlash) {
      const ramOnly = await kbheDevice.getRamOnlyMode()
      if (ramOnly) {
        const exited = await kbheDevice.exitRamOnlyMode()
        if (!exited) {
          return false
        }
      }
    }

    const activeProfile = await kbheDevice.getActiveProfile()
    if (activeProfile?.profile_index != null && activeProfile.profile_index !== targetProfileIndex) {
      restoreProfileIndex = activeProfile.profile_index
      const switched = await kbheDevice.setActiveProfile(targetProfileIndex)
      if (!switched) {
        return false
      }
      switchedProfile = true
      restoreNeedsSave = Boolean(options.persistToFlash)
    }

    const orderedKeySettings = [...snapshot.keySettings].sort((a, b) => {
      if (a.layer_index !== b.layer_index) {
        return a.layer_index - b.layer_index
      }
      return a.key_index - b.key_index
    })

    for (const settings of orderedKeySettings) {
      const ok = await kbheDevice.setKeySettingsExtended(settings.key_index, {
        ...settings,
        profile_index: targetProfileIndex,
        layer_index: settings.layer_index,
      })
      if (!ok) {
        return false
      }
    }

    if (snapshot.keyGamepadMaps) {
      for (const map of snapshot.keyGamepadMaps) {
        const ok = await kbheDevice.setKeyGamepadMap(
          map.key_index,
          map.axis,
          map.direction,
          map.button,
          map.layer_mask,
        )
        if (!ok) {
          return false
        }
      }
    }

    if (snapshot.gamepadSettings) {
      const ok = await kbheDevice.setGamepadSettings(snapshot.gamepadSettings)
      if (!ok) {
        return false
      }
    }

    if (snapshot.rotarySettings) {
      const ok = await kbheDevice.setRotaryEncoderSettings(snapshot.rotarySettings)
      if (!ok) {
        return false
      }
    }

    if (snapshot.filterEnabled != null) {
      const ok = await kbheDevice.setFilterEnabled(snapshot.filterEnabled)
      if (!ok) {
        return false
      }
    }

    if (snapshot.filterParams) {
      const ok = await kbheDevice.setFilterParams(
        snapshot.filterParams.noise_band,
        snapshot.filterParams.alpha_min_denom,
        snapshot.filterParams.alpha_max_denom,
      )
      if (!ok) {
        return false
      }
    }

    if (snapshot.options) {
      const optionsOk = await kbheDevice.setOptions({
        keyboard_enabled: snapshot.options.keyboard_enabled,
        gamepad_enabled: snapshot.options.gamepad_enabled,
        raw_hid_echo: snapshot.options.raw_hid_echo,
        led_thermal_protection_enabled:
          snapshot.options.led_thermal_protection_enabled ?? true,
      })
      if (!optionsOk) {
        return false
      }
    }

    if (snapshot.nkroEnabled != null) {
      const ok = await kbheDevice.setNkroEnabled(snapshot.nkroEnabled)
      if (!ok) {
        return false
      }
    }

    if (snapshot.advancedTickRate != null) {
      const ok = await kbheDevice.setAdvancedTickRate(snapshot.advancedTickRate)
      if (!ok) {
        return false
      }
    }

    if (snapshot.led) {
      const ok = await applyLedSnapshot(snapshot.led)
      if (!ok) {
        return false
      }
    }

    if (options.persistToFlash) {
      const saved = await kbheDevice.saveSettings()
      if (!saved) {
        return false
      }
    }

    return true
  } catch {
    return false
  } finally {
    if (switchedProfile && shouldRestore && restoreProfileIndex != null) {
      const restored = await kbheDevice.setActiveProfile(restoreProfileIndex)
      if (restored && restoreNeedsSave) {
        await kbheDevice.saveSettings()
      }
    }
  }
}
