import { useProfileStore } from "@/stores/profileStore"
import { useDeviceSession } from "./session"
import {
  type FirmwareLedSnapshot,
  type FirmwareProfileSnapshot,
  isFirmwareProfileSnapshot,
} from "./profile-sync"
import type {
  FilterParams,
  GamepadSettings,
  KeyGamepadMap,
  KeySettings,
  LedIdleOptions,
  RotaryEncoderSettings,
  TriggerChatterGuard,
} from "./device"

function cloneSnapshot(snapshot: FirmwareProfileSnapshot): FirmwareProfileSnapshot {
  return structuredClone(snapshot)
}

function shouldPatchActiveAppProfile(): boolean {
  const profileStore = useProfileStore.getState()
  const session = useDeviceSession.getState()

  return (
    profileStore.runtimeSource === "app" &&
    Boolean(profileStore.activeAppProfileName) &&
    (profileStore.ramOnlyActive || Boolean(session.ramOnlyMode))
  )
}

export function patchActiveAppProfileSnapshot(
  updater: (snapshot: FirmwareProfileSnapshot) => void,
): void {
  if (!shouldPatchActiveAppProfile()) {
    return
  }

  const profileStore = useProfileStore.getState()
  const profileName = profileStore.activeAppProfileName
  if (!profileName) {
    return
  }

  const profile = profileStore.getAppProfileByName(profileName)
  const snapshot = profile?.data.firmwareSnapshot
  if (!profile || !isFirmwareProfileSnapshot(snapshot)) {
    return
  }

  const nextSnapshot = cloneSnapshot(snapshot)
  updater(nextSnapshot)
  nextSnapshot.capturedAt = Date.now()

  profileStore.upsertAppProfileData(profileName, profile.data, {
    firmwareSnapshot: nextSnapshot,
    activate: false,
  })
}

function cloneKeySettings(settings: KeySettings): KeySettings {
  return {
    ...settings,
    dynamic_zones: settings.dynamic_zones.map((zone) => ({ ...zone })),
  }
}

export function patchActiveAppProfileKeySettings(
  settings: KeySettings | KeySettings[],
): void {
  const updates = Array.isArray(settings) ? settings : [settings]
  if (updates.length === 0) {
    return
  }

  patchActiveAppProfileSnapshot((snapshot) => {
    const byKey = new Map<string, KeySettings>()
    for (const entry of snapshot.keySettings) {
      byKey.set(`${entry.layer_index}:${entry.key_index}`, entry)
    }

    for (const update of updates) {
      const key = `${update.layer_index}:${update.key_index}`
      byKey.set(key, cloneKeySettings(update))
    }

    snapshot.keySettings = Array.from(byKey.values()).sort((a, b) => {
      if (a.layer_index !== b.layer_index) {
        return a.layer_index - b.layer_index
      }
      return a.key_index - b.key_index
    })
  })
}

export function patchActiveAppProfileKeyGamepadMap(map: KeyGamepadMap): void {
  patchActiveAppProfileSnapshot((snapshot) => {
    const maps = snapshot.keyGamepadMaps ? snapshot.keyGamepadMaps.map((entry) => ({ ...entry })) : []
    const existingIndex = maps.findIndex((entry) => entry.key_index === map.key_index)
    if (existingIndex >= 0) {
      maps[existingIndex] = { ...map }
    } else {
      maps.push({ ...map })
    }
    maps.sort((a, b) => a.key_index - b.key_index)
    snapshot.keyGamepadMaps = maps
  })
}

export function patchActiveAppProfileGamepadSettings(settings: GamepadSettings): void {
  patchActiveAppProfileSnapshot((snapshot) => {
    snapshot.gamepadSettings = structuredClone(settings)
  })
}

export function patchActiveAppProfileRotarySettings(settings: RotaryEncoderSettings): void {
  patchActiveAppProfileSnapshot((snapshot) => {
    snapshot.rotarySettings = structuredClone(settings)
  })
}

export function patchActiveAppProfileFilterEnabled(enabled: boolean): void {
  patchActiveAppProfileSnapshot((snapshot) => {
    snapshot.filterEnabled = enabled
  })
}

export function patchActiveAppProfileFilterParams(params: FilterParams): void {
  patchActiveAppProfileSnapshot((snapshot) => {
    snapshot.filterParams = { ...params }
  })
}

export function patchActiveAppProfileAdvancedTickRate(tickRate: number): void {
  patchActiveAppProfileSnapshot((snapshot) => {
    snapshot.advancedTickRate = tickRate
  })
}

export function patchActiveAppProfileNkroEnabled(enabled: boolean): void {
  patchActiveAppProfileSnapshot((snapshot) => {
    snapshot.nkroEnabled = enabled
  })
}

export function patchActiveAppProfileOptions(
  options: NonNullable<FirmwareProfileSnapshot["options"]>,
): void {
  patchActiveAppProfileSnapshot((snapshot) => {
    snapshot.options = { ...options }
  })
}

function ensureLedSnapshot(snapshot: FirmwareProfileSnapshot): FirmwareLedSnapshot {
  snapshot.led ??= {
    enabled: null,
    brightness: null,
    pixels: null,
    effectMode: null,
    fpsLimit: null,
    effectParams: null,
    idleOptions: null,
    triggerChatterGuard: null,
  }
  return snapshot.led
}

export function patchActiveAppProfileLedSnapshot(
  patch: Partial<FirmwareLedSnapshot>,
): void {
  patchActiveAppProfileSnapshot((snapshot) => {
    const led = ensureLedSnapshot(snapshot)
    Object.assign(led, structuredClone(patch))
  })
}

export function patchActiveAppProfileLedEffectParams(
  effectMode: number,
  params: ArrayLike<number>,
): void {
  patchActiveAppProfileSnapshot((snapshot) => {
    const led = ensureLedSnapshot(snapshot)
    const values = Array.from(params, (value) => value & 0xff)
    const current = led.effectParams ? led.effectParams.map((entry) => [...entry]) : []
    while (current.length <= effectMode) {
      current.push([])
    }
    current[effectMode] = values
    led.effectParams = current
  })
}

export function patchActiveAppProfileLedPixels(
  pixels: ArrayLike<number>,
): void {
  patchActiveAppProfileLedSnapshot({
    pixels: Array.from(pixels, (value) => value & 0xff),
  })
}

export function patchActiveAppProfileLedPixel(
  index: number,
  color: { r: number; g: number; b: number },
): void {
  patchActiveAppProfileSnapshot((snapshot) => {
    const led = ensureLedSnapshot(snapshot)
    const pixels = led.pixels ? [...led.pixels] : []
    const offset = index * 3
    while (pixels.length <= offset + 2) {
      pixels.push(0)
    }
    pixels[offset] = color.r & 0xff
    pixels[offset + 1] = color.g & 0xff
    pixels[offset + 2] = color.b & 0xff
    led.pixels = pixels
  })
}

export function patchActiveAppProfileLedIdleOptions(options: LedIdleOptions): void {
  patchActiveAppProfileLedSnapshot({ idleOptions: { ...options } })
}

export function patchActiveAppProfileTriggerChatterGuard(
  triggerChatterGuard: TriggerChatterGuard,
): void {
  patchActiveAppProfileLedSnapshot({
    triggerChatterGuard: { ...triggerChatterGuard },
  })
}
