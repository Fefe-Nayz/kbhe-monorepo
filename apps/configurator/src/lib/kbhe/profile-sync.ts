import {
  type FilterParams,
  type GamepadSettings,
  type KeySettings,
  kbheDevice,
  type RotaryEncoderSettings,
} from "./device"
import { KEY_COUNT, LAYER_COUNT } from "./protocol"

export interface FirmwareProfileSnapshot {
  schemaVersion: 1
  capturedAt: number
  sourceProfileIndex: number
  keySettings: KeySettings[]
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
}

interface ApplyFirmwareProfileSnapshotOptions {
  persistToFlash?: boolean
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

export async function captureFirmwareProfileSnapshot(profileIndex: number): Promise<FirmwareProfileSnapshot | null> {
  try {
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
    ] = await Promise.all([
      kbheDevice.getGamepadSettings(),
      kbheDevice.getRotaryEncoderSettings(),
      kbheDevice.getFilterEnabled(),
      kbheDevice.getFilterParams(),
      kbheDevice.getOptions(),
      kbheDevice.getNkroEnabled(),
      kbheDevice.getAdvancedTickRate(),
    ])

    return {
      schemaVersion: 1,
      capturedAt: Date.now(),
      sourceProfileIndex: profileIndex,
      keySettings,
      gamepadSettings,
      rotarySettings,
      filterEnabled,
      filterParams,
      options,
      nkroEnabled,
      advancedTickRate,
    }
  } catch {
    return null
  }
}

export async function applyFirmwareProfileSnapshot(
  snapshot: FirmwareProfileSnapshot,
  targetProfileIndex: number,
  options: ApplyFirmwareProfileSnapshotOptions = {},
): Promise<boolean> {
  try {
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

    if (options.persistToFlash) {
      const saved = await kbheDevice.saveSettings()
      if (!saved) {
        return false
      }
    }

    return true
  } catch {
    return false
  }
}
